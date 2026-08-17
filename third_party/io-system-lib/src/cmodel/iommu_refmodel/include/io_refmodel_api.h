#ifndef IO_REFMODEL_API_H
#define IO_REFMODEL_API_H

#include "io_iommu_api_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

iommu_handle_t *io_refmodel_iommu_open(void);
iommu_handle_t *io_refmodel_iommu_open_with_args(int argc, char **argv);
void io_refmodel_iommu_close(iommu_handle_t *handle);
void io_refmodel_iommu_set_trace(iommu_handle_t *handle, int enabled);
void io_refmodel_iommu_set_trace_txn_id(iommu_handle_t *handle,
                                        uint32_t id, int enabled);

int io_refmodel_mmio_write(iommu_handle_t *handle, uint32_t addr,
                           uint32_t data);
int io_refmodel_mmio_read(iommu_handle_t *handle, uint32_t addr,
                          uint32_t *data);
int io_refmodel_iommu_step(iommu_handle_t *handle, int cycles);

int io_refmodel_memory_write(iommu_handle_t *handle, uint64_t addr,
                             const uint8_t *data, size_t len);
int io_refmodel_memory_read(iommu_handle_t *handle, uint64_t addr,
                            uint8_t *data, size_t len);
int io_refmodel_memory_write_with_context(
    iommu_handle_t *handle, uint64_t addr, const uint8_t *data, size_t len,
    const iommu_request_context_t *context);
int io_refmodel_memory_read_with_context(
    iommu_handle_t *handle, uint64_t addr, uint8_t *data, size_t len,
    const iommu_request_context_t *context);
int io_refmodel_memory_write_with_context_ace(
    iommu_handle_t *handle, uint64_t addr, const uint8_t *data, size_t len,
    const iommu_request_context_t *context,
    const iommu_ace_lite_attrs_t *attrs);
int io_refmodel_memory_read_with_context_ace(
    iommu_handle_t *handle, uint64_t addr, uint8_t *data, size_t len,
    const iommu_request_context_t *context,
    const iommu_ace_lite_attrs_t *attrs);

void io_refmodel_downstream_set_callbacks_v2(
    iommu_handle_t *handle, io_iommu_write_callback_v2 write_cb,
    io_iommu_read_callback_v2 read_cb, void *user_data);
void io_refmodel_translation_set_callbacks_v2(
    iommu_handle_t *handle, io_iommu_write_callback_v2 write_cb,
    io_iommu_read_callback_v2 read_cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif
