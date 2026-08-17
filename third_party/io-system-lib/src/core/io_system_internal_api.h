#ifndef IO_SYSTEM_INTERNAL_API_H
#define IO_SYSTEM_INTERNAL_API_H

#include "io_system.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IoSystemDmaAttrs {
    uint32_t requester_id;
    uint32_t pasid;
    bool has_pasid;
    bool translated;
} IoSystemDmaAttrs;

typedef IoSystemStatus (*IoSystemMmioRead)(void *opaque, uint64_t addr,
                                           void *data, size_t size);
typedef IoSystemStatus (*IoSystemMmioWrite)(void *opaque, uint64_t addr,
                                            const void *data, size_t size);

IoSystemStatus io_system_guest_memory_read(IoSystem *system, uint64_t gpa,
                                           void *dst, size_t size);
IoSystemStatus io_system_guest_memory_write(IoSystem *system, uint64_t gpa,
                                            const void *src, size_t size);
IoSystemStatus io_system_guest_memory_atomic(IoSystem *system, uint64_t gpa,
                                             void *value, size_t size,
                                             uint32_t op);
IoSystemStatus io_system_dma_read(IoSystem *system,
                                  const IoSystemDmaAttrs *attrs,
                                  uint64_t iova, void *dst, size_t size);
IoSystemStatus io_system_dma_write(IoSystem *system,
                                   const IoSystemDmaAttrs *attrs,
                                   uint64_t iova, const void *src,
                                   size_t size);

IoSystemStatus io_system_save_state(IoSystem *system, void *buf,
                                    size_t buf_size, size_t *written);
IoSystemStatus io_system_load_state(IoSystem *system, const void *buf,
                                    size_t buf_size);

const IoManifest *io_system_get_manifest(const IoSystem *system);

#ifdef __cplusplus
}
#endif

#endif
