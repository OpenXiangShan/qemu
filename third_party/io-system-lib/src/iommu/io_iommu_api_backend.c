#include "io_iommu_internal.h"

#include "io_cmodel_internal.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IO_IOMMU_AXI_ATOP_ATOMIC_STORE_LE_BIT_SET UINT8_C(0x13)

static IoIommuApiBackend *io_iommu_api_backend(IoIommu *iommu)
{
    return (IoIommuApiBackend *)iommu;
}

static uint32_t io_iommu_load_le32(const void *data)
{
    const uint8_t *bytes = data;

    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void io_iommu_store_le32(void *data, uint32_t value)
{
    uint8_t *bytes = data;

    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint64_t io_iommu_load_le(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint64_t value = 0;

    for (size_t i = 0; i < size && i < sizeof(value); i++) {
        value |= (uint64_t)bytes[i] << (i * 8);
    }
    return value;
}

static const char *io_iommu_api_name(const IoIommuApiBackend *backend)
{
    return backend && backend->api_path ? backend->api_path :
           "builtin refmodel";
}

static void io_iommu_attrs_to_context(const IoSystemDmaAttrs *attrs,
                                      iommu_request_context_t *context)
{
    memset(context, 0, sizeof(*context));
    if (!attrs) {
        return;
    }

    context->device_id = attrs->requester_id & 0x00ffffffu;
    context->process_id = attrs->pasid & 0x000fffffu;
    context->process_id_valid = attrs->has_pasid ? 1 : 0;
    context->is_translated = attrs->translated ? 1 : 0;
}

static bool io_iommu_write_is_atomic_bit_set(
    const iommu_ace_lite_attrs_t *attrs)
{
    return attrs &&
           attrs->awatop == IO_IOMMU_AXI_ATOP_ATOMIC_STORE_LE_BIT_SET;
}

static bool io_iommu_size_is_power_of_two(size_t size)
{
    return size && (size & (size - 1)) == 0;
}

static uint64_t io_iommu_callback_beat_base(const IoSystem *system,
                                            uint64_t addr, size_t data_len)
{
    if (system && system->config.iommu_kind == IO_SYSTEM_IOMMU_RTL &&
        io_iommu_size_is_power_of_two(data_len)) {
        return addr & ~(uint64_t)(data_len - 1);
    }
    return addr;
}

static IoSystemStatus io_iommu_guest_masked_write(IoSystem *system,
                                                  uint64_t beat_base,
                                                  const uint8_t *data,
                                                  size_t lane_count,
                                                  uint32_t strobe)
{
    size_t start = 0;

    while (start < lane_count) {
        size_t end;

        while (start < lane_count && !(strobe & (UINT32_C(1) << start))) {
            start++;
        }
        if (start >= lane_count) {
            break;
        }

        end = start + 1;
        while (end < lane_count && (strobe & (UINT32_C(1) << end))) {
            end++;
        }

        IoSystemStatus status =
            io_system_guest_memory_write(system, beat_base + start,
                                         data + start, end - start);
        if (status != IO_SYSTEM_OK) {
            return status;
        }
        start = end;
    }

    return IO_SYSTEM_OK;
}

static IoSystemStatus io_iommu_guest_atomic_or_write(IoSystem *system,
                                                     uint64_t beat_base,
                                                     const uint8_t *data,
                                                     size_t lane_count,
                                                     uint32_t strobe)
{
    for (size_t lane = 0; lane < lane_count; lane++) {
        uint8_t value;
        IoSystemStatus status;

        if (!(strobe & (UINT32_C(1) << lane)) || data[lane] == 0) {
            continue;
        }

        status = io_system_guest_memory_read(system, beat_base + lane,
                                             &value, sizeof(value));
        if (status != IO_SYSTEM_OK) {
            return status;
        }

        value |= data[lane];
        status = io_system_guest_memory_write(system, beat_base + lane,
                                              &value, sizeof(value));
        if (status != IO_SYSTEM_OK) {
            return status;
        }
    }

    return IO_SYSTEM_OK;
}

static int io_iommu_guest_read_cb(uint64_t addr, uint8_t *data,
                                  size_t data_len, uint16_t id,
                                  size_t beat_index, size_t beat_count,
                                  const iommu_ace_lite_attrs_t *attrs,
                                  void *user_data)
{
    IoSystem *system = user_data;
    uint64_t read_base;
    IoSystemStatus status;

    (void)id;
    (void)beat_index;
    (void)beat_count;
    (void)attrs;

    if (!system || (!data && data_len)) {
        return 0;
    }
    if (data_len) {
        memset(data, 0xff, data_len);
    }
    read_base = io_iommu_callback_beat_base(system, addr, data_len);
    status = io_system_guest_memory_read(system, read_base, data, data_len);
    if (io_iommu_trace_enabled()) {
        fprintf(stderr,
                "[io-iommu] guest-read addr=0x%016llx len=%zu id=0x%x "
                "beat=%zu/%zu status=%d\n",
                (unsigned long long)read_base, data_len, id, beat_index,
                beat_count, status);
        io_iommu_trace_bytes("[io-iommu] guest-read", data, data_len);
    }
    return status == IO_SYSTEM_OK;
}

static void io_iommu_guest_write_cb(uint64_t addr, const uint8_t *data,
                                    size_t data_len, uint32_t strobe,
                                    uint16_t id, size_t beat_index,
                                    size_t beat_count,
                                    const iommu_ace_lite_attrs_t *attrs,
                                    void *user_data)
{
    IoSystem *system = user_data;
    bool atomic_bit_set = io_iommu_write_is_atomic_bit_set(attrs);
    size_t lane_count = data_len < 32 ? data_len : 32;
    uint64_t beat_base;
    IoSystemStatus status;

    (void)id;
    (void)beat_index;
    (void)beat_count;

    if (!system || (!data && data_len)) {
        return;
    }
    if (!data_len) {
        return;
    }
    if (strobe == 0) {
        return;
    }
    if (!lane_count) {
        return;
    }

    beat_base = io_iommu_callback_beat_base(system, addr, data_len);
    if (atomic_bit_set) {
        status = io_iommu_guest_atomic_or_write(system, beat_base, data,
                                                lane_count, strobe);
    } else {
        status = io_iommu_guest_masked_write(system, beat_base, data,
                                             lane_count, strobe);
    }
    if (io_iommu_trace_enabled()) {
        fprintf(stderr,
                "[io-iommu] guest-write%s addr=0x%016llx len=%zu "
                "strobe=0x%x id=0x%x beat=%zu/%zu status=%d\n",
                atomic_bit_set ? "-atomic-bit-set" : "",
                (unsigned long long)beat_base, data_len, strobe, id,
                beat_index, beat_count, status);
        io_iommu_trace_bytes("[io-iommu] guest-write", data, data_len);
    }
}

IoSystemStatus io_iommu_api_bind_symbols(IoSystem *system, IoIommuApi *api,
                                          void *api_so,
                                          const char *api_path)
{
    const char *error;

    if (!api || !api_so) {
        return IO_SYSTEM_ERR_INVALID;
    }
    memset(api, 0, sizeof(*api));

#define IO_IOMMU_BIND_REQUIRED(_field, _sym)                                \
    do {                                                                    \
        dlerror();                                                          \
        *(void **)(&api->_field) = dlsym(api_so, _sym);                     \
        error = dlerror();                                                  \
        if (error || !api->_field) {                                        \
            io_iommu_log(system, 0,                                         \
                         "io-iommu: failed to resolve %s from %s: %s\n",   \
                         _sym, api_path ? api_path : "",                   \
                         error ? error : "missing");                      \
            return IO_SYSTEM_ERR_IO;                                        \
        }                                                                   \
    } while (0)

#define IO_IOMMU_BIND_OPTIONAL(_field, _sym)                                \
    do {                                                                    \
        dlerror();                                                          \
        *(void **)(&api->_field) = dlsym(api_so, _sym);                     \
    } while (0)

    IO_IOMMU_BIND_REQUIRED(open, "iommu_open");
    IO_IOMMU_BIND_OPTIONAL(open_with_args, "iommu_open_with_args");
    IO_IOMMU_BIND_REQUIRED(close, "iommu_close");
    IO_IOMMU_BIND_OPTIONAL(set_trace, "iommu_set_trace");
    IO_IOMMU_BIND_OPTIONAL(set_trace_txn_id, "iommu_set_trace_txn_id");
    IO_IOMMU_BIND_REQUIRED(mmio_write, "mmio_write");
    IO_IOMMU_BIND_REQUIRED(mmio_read, "mmio_read");
    IO_IOMMU_BIND_REQUIRED(step, "iommu_step");
    IO_IOMMU_BIND_REQUIRED(memory_write_with_context,
                           "memory_write_with_context");
    IO_IOMMU_BIND_REQUIRED(memory_read_with_context,
                           "memory_read_with_context");
    IO_IOMMU_BIND_REQUIRED(downstream_set_callbacks_v2,
                           "downstream_set_callbacks_v2");
    IO_IOMMU_BIND_REQUIRED(translation_set_callbacks_v2,
                           "translation_set_callbacks_v2");
    return IO_SYSTEM_OK;

#undef IO_IOMMU_BIND_OPTIONAL
#undef IO_IOMMU_BIND_REQUIRED
}

static IoSystemStatus io_iommu_api_backend_reset(IoIommu *iommu)
{
    IoIommuApiBackend *backend = io_iommu_api_backend(iommu);
    IoSystemStatus status;

    if (!backend || !backend->handle || !backend->api.step) {
        return IO_SYSTEM_ERR_INVALID;
    }
    status = backend->api.step(backend->handle, 1) ?
             IO_SYSTEM_OK : IO_SYSTEM_ERR_IO;
    if (status != IO_SYSTEM_OK) {
        return status;
    }
    if (backend->model_ops && backend->model_ops->reset) {
        return backend->model_ops->reset(backend);
    }
    return IO_SYSTEM_OK;
}

static IoSystemStatus io_iommu_api_backend_mmio_read(IoIommu *iommu,
                                                     uint64_t addr,
                                                     void *data, size_t size)
{
    IoIommuApiBackend *backend = io_iommu_api_backend(iommu);
    uint64_t off;
    size_t done = 0;

    if (!backend || !backend->handle || !data || !size || size > 8 ||
        !io_iommu_range_contains(iommu->mmio_base, iommu->mmio_size,
                                 addr, size)) {
        return IO_SYSTEM_ERR_INVALID;
    }

    off = addr - iommu->mmio_base;
    if (off + size > UINT32_MAX) {
        return IO_SYSTEM_ERR_INVALID;
    }

    if (backend->model_ops && backend->model_ops->pre_mmio_read) {
        if (backend->model_ops->pre_mmio_read(backend, (uint32_t)off, size) !=
            IO_SYSTEM_OK) {
            return IO_SYSTEM_ERR_IO;
        }
    }

    while (done < size) {
        uint32_t word = 0;
        uint32_t word_off = (uint32_t)((off + done) & ~UINT64_C(3));
        size_t lane = (size_t)((off + done) & 3u);
        size_t chunk = 4 - lane;

        if (chunk > size - done) {
            chunk = size - done;
        }
        if (!backend->api.mmio_read(backend->handle, word_off, &word)) {
            memset((uint8_t *)data + done, 0xff, chunk);
            return IO_SYSTEM_ERR_IO;
        }
        memcpy((uint8_t *)data + done, ((uint8_t *)&word) + lane, chunk);
        if (io_iommu_trace_enabled()) {
            fprintf(stderr,
                    "[io-iommu] mmio-read off=0x%04x word=0x%08x "
                    "lane=%zu chunk=%zu\n",
                    word_off, word, lane, chunk);
        }
        done += chunk;
    }
    return IO_SYSTEM_OK;
}

static IoSystemStatus io_iommu_api_backend_mmio_write(IoIommu *iommu,
                                                      uint64_t addr,
                                                      const void *data,
                                                      size_t size)
{
    IoIommuApiBackend *backend = io_iommu_api_backend(iommu);
    uint64_t off;
    size_t done = 0;

    if (!backend || !backend->handle || !data || !size || size > 8 ||
        !io_iommu_range_contains(iommu->mmio_base, iommu->mmio_size,
                                 addr, size)) {
        return IO_SYSTEM_ERR_INVALID;
    }

    off = addr - iommu->mmio_base;
    if (off + size > UINT32_MAX) {
        return IO_SYSTEM_ERR_INVALID;
    }

    while (done < size) {
        uint32_t word_off = (uint32_t)((off + done) & ~UINT64_C(3));
        size_t lane = (size_t)((off + done) & 3u);
        size_t chunk = 4 - lane;
        uint8_t word_bytes[4];
        uint32_t word;

        if (chunk > size - done) {
            chunk = size - done;
        }
        if (lane || chunk != 4) {
            if (!backend->api.mmio_read(backend->handle, word_off, &word)) {
                return IO_SYSTEM_ERR_IO;
            }
            io_iommu_store_le32(word_bytes, word);
        } else {
            memset(word_bytes, 0, sizeof(word_bytes));
        }
        memcpy(word_bytes + lane, (const uint8_t *)data + done, chunk);
        word = io_iommu_load_le32(word_bytes);
        if (io_iommu_trace_enabled()) {
            fprintf(stderr,
                    "[io-iommu] mmio-write off=0x%04x word=0x%08x "
                    "lane=%zu chunk=%zu\n",
                    word_off, word, lane, chunk);
        }
        if (!backend->api.mmio_write(backend->handle, word_off, word)) {
            return IO_SYSTEM_ERR_IO;
        }
        done += chunk;
    }

    if (backend->model_ops && backend->model_ops->post_mmio_write) {
        if (backend->model_ops->post_mmio_write(
                backend, (uint32_t)off, io_iommu_load_le(data, size), size) !=
            IO_SYSTEM_OK) {
            return IO_SYSTEM_ERR_IO;
        }
    }
    return IO_SYSTEM_OK;
}

static IoSystemStatus io_iommu_api_backend_mmio_read_cb(void *opaque,
                                                        uint64_t addr,
                                                        void *data,
                                                        size_t size)
{
    return io_iommu_api_backend_mmio_read(opaque, addr, data, size);
}

static IoSystemStatus io_iommu_api_backend_mmio_write_cb(void *opaque,
                                                         uint64_t addr,
                                                         const void *data,
                                                         size_t size)
{
    return io_iommu_api_backend_mmio_write(opaque, addr, data, size);
}

static IoSystemStatus io_iommu_api_backend_dma_read(
    IoIommu *iommu, const IoSystemDmaAttrs *attrs,
    uint64_t iova, void *dst, size_t size)
{
    IoIommuApiBackend *backend = io_iommu_api_backend(iommu);
    iommu_request_context_t context;
    IoSystemStatus status;

    if (!backend || !backend->handle || !dst || !size) {
        return IO_SYSTEM_ERR_INVALID;
    }

    if (backend->model_ops && backend->model_ops->pre_dma) {
        status = backend->model_ops->pre_dma(backend);
        if (status != IO_SYSTEM_OK) {
            return status;
        }
    }

    io_iommu_attrs_to_context(attrs, &context);
    if (io_iommu_trace_enabled()) {
        fprintf(stderr,
                "[io-iommu] dma-read-begin iova=0x%016llx size=%zu "
                "rid=0x%x pasid=0x%x pasid_valid=%u translated=%u\n",
                (unsigned long long)iova, size, context.device_id,
                context.process_id, context.process_id_valid,
                context.is_translated);
    }
    status = backend->api.memory_read_with_context(backend->handle, iova, dst,
                                                   size, &context) ?
        IO_SYSTEM_OK : IO_SYSTEM_ERR_IO;
    if (io_iommu_trace_enabled()) {
        fprintf(stderr,
                "[io-iommu] dma-read-end iova=0x%016llx size=%zu "
                "rid=0x%x status=%d\n",
                (unsigned long long)iova, size, context.device_id, status);
    }
    return status;
}

static IoSystemStatus io_iommu_api_backend_dma_write(
    IoIommu *iommu, const IoSystemDmaAttrs *attrs,
    uint64_t iova, const void *src, size_t size)
{
    IoIommuApiBackend *backend = io_iommu_api_backend(iommu);
    iommu_request_context_t context;
    IoSystemStatus status;

    if (!backend || !backend->handle || !src || !size) {
        return IO_SYSTEM_ERR_INVALID;
    }

    if (backend->model_ops && backend->model_ops->pre_dma) {
        status = backend->model_ops->pre_dma(backend);
        if (status != IO_SYSTEM_OK) {
            return status;
        }
    }

    io_iommu_attrs_to_context(attrs, &context);
    if (io_iommu_trace_enabled()) {
        fprintf(stderr,
                "[io-iommu] dma-write-begin iova=0x%016llx size=%zu "
                "rid=0x%x pasid=0x%x pasid_valid=%u translated=%u\n",
                (unsigned long long)iova, size, context.device_id,
                context.process_id, context.process_id_valid,
                context.is_translated);
    }
    status = backend->api.memory_write_with_context(backend->handle, iova, src,
                                                    size, &context) ?
        IO_SYSTEM_OK : IO_SYSTEM_ERR_IO;
    if (io_iommu_trace_enabled()) {
        fprintf(stderr,
                "[io-iommu] dma-write-end iova=0x%016llx size=%zu "
                "rid=0x%x status=%d\n",
                (unsigned long long)iova, size, context.device_id, status);
    }
    return status;
}

static IoSystemStatus io_iommu_api_backend_service(IoIommu *iommu,
                                                   uint32_t budget)
{
    IoIommuApiBackend *backend = io_iommu_api_backend(iommu);

    if (!backend || !backend->handle || !backend->api.step) {
        return IO_SYSTEM_ERR_INVALID;
    }
    return backend->api.step(backend->handle, budget ? (int)budget : 1) ?
           IO_SYSTEM_OK : IO_SYSTEM_ERR_IO;
}

static bool io_iommu_api_backend_needs_service(IoIommu *iommu)
{
    (void)iommu;

    return false;
}

static void io_iommu_api_backend_destroy(IoIommu *iommu)
{
    IoIommuApiBackend *backend = io_iommu_api_backend(iommu);

    if (!backend) {
        return;
    }
    if (backend->handle && backend->api.close) {
        backend->api.close(backend->handle);
    }
    if (backend->model_ops && backend->model_ops->destroy) {
        backend->model_ops->destroy(backend);
    }
    if (backend->api_so) {
        dlclose(backend->api_so);
    }
    free(backend->api_path);
    memset(&backend->api, 0, sizeof(backend->api));
    free(backend);
}

static const IoIommuOps io_iommu_api_backend_ops = {
    .destroy = io_iommu_api_backend_destroy,
    .reset = io_iommu_api_backend_reset,
    .mmio_read = io_iommu_api_backend_mmio_read,
    .mmio_write = io_iommu_api_backend_mmio_write,
    .dma_read = io_iommu_api_backend_dma_read,
    .dma_write = io_iommu_api_backend_dma_write,
    .service = io_iommu_api_backend_service,
    .needs_service = io_iommu_api_backend_needs_service,
};

static IoSystemStatus io_iommu_api_backend_register_mmio(
    IoIommuApiBackend *backend)
{
    const IoManifestEntry *entry;

    entry = io_manifest_find(backend->base.system->manifest,
                             IO_MANIFEST_DEVICE_IOMMU);
    if (!entry || !entry->base || !entry->size) {
        return IO_SYSTEM_ERR_INVALID;
    }

    backend->base.mmio_base = entry->base;
    backend->base.mmio_size = entry->size;
    return io_cmodel_register_mmio_window(
        backend->base.system,
        entry->name ? entry->name : "iommu",
        entry->base, entry->size,
        io_iommu_api_backend_mmio_read_cb,
        io_iommu_api_backend_mmio_write_cb,
        backend);
}

IoSystemStatus io_iommu_api_backend_create(
    IoSystem *system, const IoIommuApiBackendConfig *config,
    IoIommu **out_iommu)
{
    IoIommuApiBackend *backend;
    IoSystemStatus status;

    if (!system || !config || !config->api || !out_iommu) {
        return IO_SYSTEM_ERR_INVALID;
    }
    *out_iommu = NULL;

    backend = calloc(1, sizeof(*backend));
    if (!backend) {
        if (config->api_so) {
            dlclose(config->api_so);
        }
        free(config->api_path);
        return IO_SYSTEM_ERR_NOMEM;
    }

    backend->base.ops = &io_iommu_api_backend_ops;
    backend->base.system = system;
    backend->base.kind = config->kind;
    backend->base.placement = config->placement;
    backend->api = *config->api;
    backend->api_so = config->api_so;
    backend->api_path = config->api_path;
    backend->model_ops = config->model_ops;
    backend->model_opaque = config->model_opaque;

    if (!backend->api.open || !backend->api.close ||
        !backend->api.mmio_read || !backend->api.mmio_write ||
        !backend->api.step ||
        !backend->api.memory_read_with_context ||
        !backend->api.memory_write_with_context ||
        !backend->api.downstream_set_callbacks_v2 ||
        !backend->api.translation_set_callbacks_v2) {
        io_iommu_api_backend_destroy(&backend->base);
        return IO_SYSTEM_ERR_INVALID;
    }

    backend->handle = backend->api.open();
    if (!backend->handle) {
        io_iommu_log(system, 0, "io-iommu: iommu_open failed for %s\n",
                     io_iommu_api_name(backend));
        io_iommu_api_backend_destroy(&backend->base);
        return IO_SYSTEM_ERR_IO;
    }

    if (backend->api.set_trace && io_iommu_trace_enabled()) {
        backend->api.set_trace(backend->handle, 1);
    }
    if (backend->api.set_trace_txn_id) {
        uint32_t txn_id;

        if (io_iommu_trace_txn_id(&txn_id)) {
            backend->api.set_trace_txn_id(backend->handle, txn_id, 1);
        }
    }
    backend->api.downstream_set_callbacks_v2(backend->handle,
                                             io_iommu_guest_write_cb,
                                             io_iommu_guest_read_cb,
                                             system);
    backend->api.translation_set_callbacks_v2(backend->handle,
                                              io_iommu_guest_write_cb,
                                              io_iommu_guest_read_cb,
                                              system);

    if (backend->model_ops && backend->model_ops->post_open) {
        status = backend->model_ops->post_open(backend);
        if (status != IO_SYSTEM_OK) {
            io_iommu_api_backend_destroy(&backend->base);
            return status;
        }
    }

    status = io_iommu_api_backend_register_mmio(backend);
    if (status != IO_SYSTEM_OK) {
        io_iommu_api_backend_destroy(&backend->base);
        return status;
    }

    *out_iommu = &backend->base;
    return IO_SYSTEM_OK;
}
