#include "io_system_internal.h"

#include <stdlib.h>
#include <string.h>

void io_system_trace(IoSystem *system, const char *port, const char *channel,
                     uint64_t addr, const void *data, size_t size,
                     IoAxiResponse response)
{
    IoAxiBeatTrace *trace;
    size_t copy_size;

    if (!system || !system->trace ||
        system->trace_count >= system->trace_capacity) {
        return;
    }

    trace = &system->trace[system->trace_count++];
    memset(trace, 0, sizeof(*trace));
    trace->port = port;
    trace->channel = channel;
    trace->transaction_id = system->next_transaction_id++;
    trace->address = addr;
    trace->beat_index = 0;
    trace->beat_size = (uint8_t)size;
    trace->response = response;

    copy_size = size < IO_AXI_MAX_BEAT_BYTES ? size : IO_AXI_MAX_BEAT_BYTES;
    if (data && copy_size) {
        memcpy(trace->data, data, copy_size);
    }
    memset(trace->strobe, 0xff, copy_size);
}

uint32_t io_system_outstanding_depth(const IoSystem *system)
{
    if (!system) {
        return 1;
    }
    if (system->config.io2q_outstanding_depth) {
        return system->config.io2q_outstanding_depth;
    }
    return system->config.io2q_initial_outstanding ?
           system->config.io2q_initial_outstanding : 1;
}

static int io_system_scheduler_guest_read(void *opaque, uint64_t gpa,
                                           void *dst, uint32_t len)
{
    IoSystem *system = opaque;
    int rc;

    if (!system || !system->host_ops.guest_memory_read) {
        return -1;
    }
    memset(dst, 0xff, len);
    rc = system->host_ops.guest_memory_read(system->host_ops.opaque, gpa,
                                            dst, len);
    io_system_trace(system, "io2q", "memory-read", gpa, dst, len,
                    rc == (int)len ? IO_AXI_RESPONSE_OKAY :
                    IO_AXI_RESPONSE_SLVERR);
    return rc;
}

static int io_system_scheduler_guest_write(void *opaque, uint64_t gpa,
                                            const void *src, uint32_t len)
{
    IoSystem *system = opaque;
    int rc;

    if (!system || !system->host_ops.guest_memory_write) {
        return -1;
    }
    rc = system->host_ops.guest_memory_write(system->host_ops.opaque, gpa,
                                             src, len);
    io_system_trace(system, "io2q", "memory-write", gpa, src, len,
                    rc == (int)len ? IO_AXI_RESPONSE_OKAY :
                    IO_AXI_RESPONSE_SLVERR);
    return rc;
}

static void io_system_scheduler_completion(void *opaque,
                                           const IoAxiTransaction *txn)
{
    IoSystem *system = opaque;
    IoAxiTransaction completed;
    IoSystemStatus status;

    if (!system || !txn ||
        !(txn->attributes & IO_SYSTEM_TXN_ATTR_BACKEND_COMPLETION)) {
        return;
    }
    completed = *txn;
    completed.attributes &= ~IO_SYSTEM_TXN_ATTR_BACKEND_COMPLETION;
    if (!system->backend || !system->backend->ops ||
        !system->backend->ops->io2q_complete) {
        return;
    }
    status = system->backend->ops->io2q_complete(system->backend, &completed);
    if (status != IO_SYSTEM_OK &&
        system->scheduler_completion_status == IO_SYSTEM_OK) {
        system->scheduler_completion_status = status;
    }
}

IoSystem *io_system_create(const IoSystemConfig *config,
                           const IoSystemHostOps *host_ops)
{
    IoSystem *system = calloc(1, sizeof(*system));

    if (!system) {
        return NULL;
    }

    if (config) {
        system->config = *config;
    }
    if (host_ops) {
        system->host_ops = *host_ops;
    }

    system->manifest = system->config.manifest ?
                       system->config.manifest : io_manifest_default();
    if (!system->config.io2q_max_beat_bytes ||
        system->config.io2q_max_beat_bytes > IO_AXI_MAX_BEAT_BYTES) {
        system->config.io2q_max_beat_bytes = IO_AXI_MAX_BEAT_BYTES;
    }
    if (!system->config.io2q_initial_outstanding) {
        system->config.io2q_initial_outstanding = 1;
    }
    if (!system->config.io2q_outstanding_depth) {
        system->config.io2q_outstanding_depth =
            system->config.io2q_initial_outstanding;
    }
    if (!system->config.trace_capacity) {
        system->config.trace_capacity = IO_SYSTEM_DEFAULT_TRACE_CAPACITY;
    }

    system->trace_capacity = system->config.trace_capacity;
    system->trace = calloc(system->trace_capacity, sizeof(system->trace[0]));
    if (!system->trace) {
        free(system);
        return NULL;
    }

    system->scheduler = io_scheduler_create(&(IoSchedulerConfig) {
        .opaque = system,
        .guest_read = io_system_scheduler_guest_read,
        .guest_write = io_system_scheduler_guest_write,
        .max_beat_bytes = system->config.io2q_max_beat_bytes,
        .outstanding_depth = io_system_outstanding_depth(system),
        .async_enabled = system->config.io2q_async_enabled,
        .completion = io_system_scheduler_completion,
    });
    if (!system->scheduler) {
        free(system->trace);
        free(system);
        return NULL;
    }

    system->backend = io_system_backend_create(system, &system->config);
    if (!system->backend) {
        io_scheduler_destroy(system->scheduler);
        free(system->trace);
        free(system);
        return NULL;
    }
    if (io_iommu_create(system, &system->config, &system->iommu) !=
        IO_SYSTEM_OK) {
        io_system_backend_destroy(system->backend);
        io_scheduler_destroy(system->scheduler);
        free(system->trace);
        free(system);
        return NULL;
    }
    if (io_system_cmodel_devices_create(system) != IO_SYSTEM_OK) {
        io_iommu_destroy(system->iommu);
        io_system_backend_destroy(system->backend);
        io_scheduler_destroy(system->scheduler);
        free(system->trace);
        free(system);
        return NULL;
    }

    return system;
}

void io_system_destroy(IoSystem *system)
{
    if (!system) {
        return;
    }

    io_system_cmodel_devices_destroy(system);
    io_iommu_destroy(system->iommu);
    io_system_backend_destroy(system->backend);
    io_scheduler_destroy(system->scheduler);
    free(system->trace);
    free(system);
}

IoSystemStatus io_system_reset(IoSystem *system)
{
    IoSystemStatus status;

    if (!system || !system->backend || !system->backend->ops ||
        !system->backend->ops->reset) {
        return IO_SYSTEM_ERR_INVALID;
    }

    status = system->backend->ops->reset(system->backend);
    if (status != IO_SYSTEM_OK) {
        return status;
    }
    status = io_iommu_reset(system->iommu);
    if (status != IO_SYSTEM_OK) {
        return status;
    }
    status = io_system_cmodel_devices_reset(system);
    if (status != IO_SYSTEM_OK) {
        return status;
    }

    system->trace_count = 0;
    system->next_transaction_id = 0;
    system->scheduler_completion_status = IO_SYSTEM_OK;
    io_scheduler_reset(system->scheduler);
    return IO_SYSTEM_OK;
}

IoSystemStatus io_system_q2io_read(IoSystem *system, uint64_t addr,
                                   void *data, size_t size)
{
    IoSystemStatus status;

    if (!system || !data || !size) {
        return IO_SYSTEM_ERR_INVALID;
    }
    if (io_iommu_mmio_contains(system->iommu, addr, size)) {
        status = io_iommu_mmio_read(system->iommu, addr, data, size);
        io_system_trace(system, "q2io", "read", addr, data, size,
                        status == IO_SYSTEM_OK ? IO_AXI_RESPONSE_OKAY :
                        IO_AXI_RESPONSE_SLVERR);
        return status;
    }
    if (!system->backend || !system->backend->ops ||
        !system->backend->ops->q2io_read) {
        return IO_SYSTEM_ERR_INVALID;
    }

    return system->backend->ops->q2io_read(system->backend, addr, data, size);
}

IoSystemStatus io_system_q2io_write(IoSystem *system, uint64_t addr,
                                    const void *data, size_t size)
{
    IoSystemStatus status;

    if (!system || !data || !size) {
        return IO_SYSTEM_ERR_INVALID;
    }
    if (io_iommu_mmio_contains(system->iommu, addr, size)) {
        status = io_iommu_mmio_write(system->iommu, addr, data, size);
        io_system_trace(system, "q2io", "write", addr, data, size,
                        status == IO_SYSTEM_OK ? IO_AXI_RESPONSE_OKAY :
                        IO_AXI_RESPONSE_SLVERR);
        return status;
    }
    if (!system->backend || !system->backend->ops ||
        !system->backend->ops->q2io_write) {
        return IO_SYSTEM_ERR_INVALID;
    }

    return system->backend->ops->q2io_write(system->backend, addr, data, size);
}

static IoSystemStatus io_system_guest_memory_access(IoSystem *system,
                                                    uint64_t gpa,
                                                    void *data,
                                                    size_t size,
                                                    bool write)
{
    IoAxiTransaction txn;

    if (!system || !data || !size) {
        return IO_SYSTEM_ERR_INVALID;
    }
    if ((write && !system->host_ops.guest_memory_write) ||
        (!write && !system->host_ops.guest_memory_read)) {
        return IO_SYSTEM_ERR_UNSUPPORTED;
    }
    while (size) {
        size_t chunk = size;

        if (chunk > system->config.io2q_max_beat_bytes) {
            chunk = system->config.io2q_max_beat_bytes;
        }
        io_axi_transaction_init(&txn,
                                write ? IO_AXI_DIRECTION_IO2Q_WRITE :
                                IO_AXI_DIRECTION_IO2Q_READ,
                                gpa, (uint8_t)chunk);
        txn.data = data;
        txn.beat_count = 1;
        if (io_scheduler_submit_sync(system->scheduler, &txn) !=
            IO_SYSTEM_OK) {
            return txn.response == IO_AXI_RESPONSE_SLVERR ?
                   IO_SYSTEM_ERR_IO : IO_SYSTEM_ERR_INVALID;
        }
        gpa += chunk;
        data = (uint8_t *)data + chunk;
        size -= chunk;
    }
    return IO_SYSTEM_OK;
}

IoSystemStatus io_system_guest_memory_read(IoSystem *system, uint64_t gpa,
                                           void *dst, size_t size)
{
    return io_system_guest_memory_access(system, gpa, dst, size, false);
}

IoSystemStatus io_system_guest_memory_write(IoSystem *system, uint64_t gpa,
                                            const void *src, size_t size)
{
    return io_system_guest_memory_access(system, gpa, (void *)src, size, true);
}

IoSystemStatus io_system_guest_memory_atomic(IoSystem *system, uint64_t gpa,
                                             void *value, size_t size,
                                             uint32_t op)
{
    int rc;

    if (!system || !value || !size) {
        return IO_SYSTEM_ERR_INVALID;
    }
    if (!system->host_ops.guest_memory_atomic) {
        return IO_SYSTEM_ERR_UNSUPPORTED;
    }

    rc = system->host_ops.guest_memory_atomic(system->host_ops.opaque, gpa,
                                              value, (uint32_t)size, op);
    io_system_trace(system, "io2q", "memory-atomic", gpa, value, size,
                    rc == (int)size ? IO_AXI_RESPONSE_OKAY :
                    IO_AXI_RESPONSE_SLVERR);
    return rc == (int)size ? IO_SYSTEM_OK : IO_SYSTEM_ERR_IO;
}

IoSystemStatus io_system_dma_read(IoSystem *system,
                                  const IoSystemDmaAttrs *attrs,
                                  uint64_t iova, void *dst, size_t size)
{
    if (!system || !dst || !size) {
        return IO_SYSTEM_ERR_INVALID;
    }
    if (!system->iommu) {
        return io_system_guest_memory_read(system, iova, dst, size);
    }
    return io_iommu_dma_read(system->iommu, attrs, iova, dst, size);
}

IoSystemStatus io_system_dma_write(IoSystem *system,
                                   const IoSystemDmaAttrs *attrs,
                                   uint64_t iova, const void *src,
                                   size_t size)
{
    if (!system || !src || !size) {
        return IO_SYSTEM_ERR_INVALID;
    }
    if (!system->iommu) {
        return io_system_guest_memory_write(system, iova, src, size);
    }
    return io_iommu_dma_write(system->iommu, attrs, iova, src, size);
}

IoSystemStatus io_system_service_io2q_transaction(IoSystem *system,
                                                  IoAxiTransaction *txn)
{
    IoAxiTransaction queued;
    IoSystemStatus status;

    if (!system || !txn) {
        return IO_SYSTEM_ERR_INVALID;
    }
    queued = *txn;
    queued.attributes |= IO_SYSTEM_TXN_ATTR_BACKEND_COMPLETION;
    system->scheduler_completion_status = IO_SYSTEM_OK;
    if (system->config.io2q_async_enabled) {
        status = io_scheduler_submit_async(system->scheduler, &queued);
    } else {
        status = io_scheduler_submit_sync(system->scheduler, &queued);
        *txn = queued;
    }
    if (status != IO_SYSTEM_OK) {
        return status;
    }
    return system->scheduler_completion_status;
}

IoSystemStatus io_system_service(IoSystem *system, uint32_t budget)
{
    IoSystemStatus status;
    uint32_t iterations = 0;

    if (!system || !system->backend || !system->backend->ops) {
        return IO_SYSTEM_ERR_INVALID;
    }
    do {
        if (system->backend->ops->service) {
            status = system->backend->ops->service(system->backend, budget);
            if (status != IO_SYSTEM_OK) {
                return status;
            }
        }
        status = io_iommu_service(system->iommu, budget);
        if (status != IO_SYSTEM_OK) {
            return status;
        }
        status = io_scheduler_service(system->scheduler, budget, NULL);
        if (status != IO_SYSTEM_OK) {
            return status;
        }
        status = system->scheduler_completion_status;
        system->scheduler_completion_status = IO_SYSTEM_OK;
        if (status != IO_SYSTEM_OK) {
            return status;
        }
        iterations++;
        if (budget || !io_system_needs_service(system)) {
            return IO_SYSTEM_OK;
        }
        /* Protect callers from a faulty backend that never quiesces. */
    } while (iterations < 4096);
    return IO_SYSTEM_ERR_BUSY;
}

bool io_system_needs_service(IoSystem *system)
{
    if (!system || !system->backend || !system->backend->ops) {
        return false;
    }
    return io_scheduler_needs_service(system->scheduler) ||
           io_iommu_needs_service(system->iommu) ||
           (system->backend->ops->needs_service &&
            system->backend->ops->needs_service(system->backend));
}

IoSystemStatus io_system_save_state(IoSystem *system, void *buf,
                                    size_t buf_size, size_t *written)
{
    if (!system || !system->backend || !system->backend->ops ||
        !system->backend->ops->save_state) {
        if (written) {
            *written = 0;
        }
        return system ? IO_SYSTEM_ERR_UNSUPPORTED : IO_SYSTEM_ERR_INVALID;
    }

    return system->backend->ops->save_state(system->backend, buf, buf_size,
                                            written);
}

IoSystemStatus io_system_load_state(IoSystem *system, const void *buf,
                                    size_t buf_size)
{
    if (!system || !system->backend || !system->backend->ops ||
        !system->backend->ops->load_state) {
        return system ? IO_SYSTEM_ERR_UNSUPPORTED : IO_SYSTEM_ERR_INVALID;
    }

    return system->backend->ops->load_state(system->backend, buf, buf_size);
}

const IoManifest *io_system_get_manifest(const IoSystem *system)
{
    return system ? system->manifest : NULL;
}

size_t io_system_trace_count(const IoSystem *system)
{
    return system ? system->trace_count : 0;
}

const IoAxiBeatTrace *io_system_trace_at(const IoSystem *system, size_t index)
{
    if (!system || index >= system->trace_count) {
        return NULL;
    }

    return &system->trace[index];
}
