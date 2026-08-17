#include "io_iommu_api_compat.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define IOMMU_STUB_MMIO_WORDS (0x10000u / sizeof(uint32_t))
#define IOMMU_STUB_EXPECTED_DEVICE_ID 0x1234u
#define IOMMU_STUB_EXPECTED_PROCESS_ID 0x56u
#define IOMMU_STUB_CALLBACK_BEAT_BYTES 32u

struct iommu_handle {
    uint32_t mmio[IOMMU_STUB_MMIO_WORDS];
    io_iommu_write_callback_v2 downstream_write;
    io_iommu_read_callback_v2 downstream_read;
    void *downstream_user_data;
    io_iommu_write_callback_v2 translation_write;
    io_iommu_read_callback_v2 translation_read;
    void *translation_user_data;
};

iommu_handle_t *iommu_open(void)
{
    return calloc(1, sizeof(struct iommu_handle));
}

void iommu_close(iommu_handle_t *handle)
{
    free(handle);
}

void iommu_set_trace(iommu_handle_t *handle, int enabled)
{
    (void)handle;
    (void)enabled;
}

int mmio_write(iommu_handle_t *handle, uint32_t addr, uint32_t data)
{
    uint32_t index = addr / 4;

    if (!handle || (addr & 3u) || index >= IOMMU_STUB_MMIO_WORDS) {
        return 0;
    }
    ((struct iommu_handle *)handle)->mmio[index] = data;
    return 1;
}

int mmio_read(iommu_handle_t *handle, uint32_t addr, uint32_t *data)
{
    uint32_t index = addr / 4;

    if (!handle || !data || (addr & 3u) ||
        index >= IOMMU_STUB_MMIO_WORDS) {
        return 0;
    }
    *data = ((struct iommu_handle *)handle)->mmio[index];
    return 1;
}

int iommu_step(iommu_handle_t *handle, int cycles)
{
    (void)handle;
    (void)cycles;

    return 1;
}

static int iommu_stub_valid_context(const iommu_request_context_t *context)
{
    return context &&
           context->device_id == IOMMU_STUB_EXPECTED_DEVICE_ID &&
           context->process_id == IOMMU_STUB_EXPECTED_PROCESS_ID &&
           context->process_id_valid == 1 &&
           context->is_translated == 1;
}

int memory_write_with_context(iommu_handle_t *handle, uint64_t addr,
                              const uint8_t *data, size_t len,
                              const iommu_request_context_t *context)
{
    struct iommu_handle *stub = (struct iommu_handle *)handle;
    const iommu_ace_lite_attrs_t attrs = { 0 };
    uint8_t beat[IOMMU_STUB_CALLBACK_BEAT_BYTES] = { 0 };
    uint64_t beat_base =
        addr & ~(uint64_t)(IOMMU_STUB_CALLBACK_BEAT_BYTES - 1u);
    size_t lane = (size_t)(addr - beat_base);
    uint32_t strobe;

    if (!stub || !data || !iommu_stub_valid_context(context) ||
        !stub->downstream_write) {
        return 0;
    }
    if (lane > IOMMU_STUB_CALLBACK_BEAT_BYTES ||
        len > IOMMU_STUB_CALLBACK_BEAT_BYTES - lane) {
        return 0;
    }
    memcpy(beat + lane, data, len);
    strobe = len >= 32 ? UINT32_MAX : ((uint32_t)1 << len) - 1u;
    strobe <<= lane;
    stub->downstream_write(beat_base, beat, sizeof(beat), strobe, 0, 0, 1,
                           &attrs,
                           stub->downstream_user_data);
    return 1;
}

int memory_read_with_context(iommu_handle_t *handle, uint64_t addr,
                             uint8_t *data, size_t len,
                             const iommu_request_context_t *context)
{
    struct iommu_handle *stub = (struct iommu_handle *)handle;
    const iommu_ace_lite_attrs_t attrs = { 0 };
    uint8_t beat[IOMMU_STUB_CALLBACK_BEAT_BYTES] = { 0 };
    uint64_t beat_base =
        addr & ~(uint64_t)(IOMMU_STUB_CALLBACK_BEAT_BYTES - 1u);
    size_t lane = (size_t)(addr - beat_base);

    if (!stub || !data || !iommu_stub_valid_context(context) ||
        !stub->downstream_read) {
        return 0;
    }
    if (lane > IOMMU_STUB_CALLBACK_BEAT_BYTES ||
        len > IOMMU_STUB_CALLBACK_BEAT_BYTES - lane) {
        return 0;
    }
    if (!stub->downstream_read(beat_base, beat, sizeof(beat), 0, 0, 1,
                               &attrs, stub->downstream_user_data)) {
        return 0;
    }
    memcpy(data, beat + lane, len);
    return 1;
}

void downstream_set_callbacks_v2(iommu_handle_t *handle,
                                 io_iommu_write_callback_v2 write_cb,
                                 io_iommu_read_callback_v2 read_cb,
                                 void *user_data)
{
    struct iommu_handle *stub = (struct iommu_handle *)handle;

    if (!stub) {
        return;
    }
    stub->downstream_write = write_cb;
    stub->downstream_read = read_cb;
    stub->downstream_user_data = user_data;
}

void translation_set_callbacks_v2(iommu_handle_t *handle,
                                  io_iommu_write_callback_v2 write_cb,
                                  io_iommu_read_callback_v2 read_cb,
                                  void *user_data)
{
    struct iommu_handle *stub = (struct iommu_handle *)handle;

    if (!stub) {
        return;
    }
    stub->translation_write = write_cb;
    stub->translation_read = read_cb;
    stub->translation_user_data = user_data;
}
