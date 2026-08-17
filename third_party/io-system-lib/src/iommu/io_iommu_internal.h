#ifndef IO_IOMMU_INTERNAL_H
#define IO_IOMMU_INTERNAL_H

#include "io_system_internal.h"
#include "io_iommu_api_compat.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct IoIommuOps {
    void (*destroy)(IoIommu *iommu);
    IoSystemStatus (*reset)(IoIommu *iommu);
    IoSystemStatus (*mmio_read)(IoIommu *iommu, uint64_t addr,
                                void *data, size_t size);
    IoSystemStatus (*mmio_write)(IoIommu *iommu, uint64_t addr,
                                 const void *data, size_t size);
    IoSystemStatus (*dma_read)(IoIommu *iommu,
                               const IoSystemDmaAttrs *attrs,
                               uint64_t iova, void *dst, size_t size);
    IoSystemStatus (*dma_write)(IoIommu *iommu,
                                const IoSystemDmaAttrs *attrs,
                                uint64_t iova, const void *src, size_t size);
    IoSystemStatus (*service)(IoIommu *iommu, uint32_t budget);
    bool (*needs_service)(IoIommu *iommu);
} IoIommuOps;

struct IoIommu {
    const IoIommuOps *ops;
    IoSystem *system;
    IoSystemIommuKind kind;
    IoSystemIommuPlacement placement;
    uint64_t mmio_base;
    uint64_t mmio_size;
};

typedef struct IoIommuApi {
    iommu_handle_t *(*open)(void);
    iommu_handle_t *(*open_with_args)(int argc, char **argv);
    void (*close)(iommu_handle_t *handle);
    void (*set_trace)(iommu_handle_t *handle, int enabled);
    void (*set_trace_txn_id)(iommu_handle_t *handle, uint32_t id,
                             int enabled);
    int (*mmio_write)(iommu_handle_t *handle, uint32_t addr, uint32_t data);
    int (*mmio_read)(iommu_handle_t *handle, uint32_t addr, uint32_t *data);
    int (*step)(iommu_handle_t *handle, int cycles);
    int (*memory_write_with_context)(iommu_handle_t *handle, uint64_t addr,
                                     const uint8_t *data, size_t len,
                                     const iommu_request_context_t *context);
    int (*memory_read_with_context)(iommu_handle_t *handle, uint64_t addr,
                                    uint8_t *data, size_t len,
                                    const iommu_request_context_t *context);
    void (*downstream_set_callbacks_v2)(iommu_handle_t *handle,
                                        io_iommu_write_callback_v2 write_cb,
                                        io_iommu_read_callback_v2 read_cb,
                                        void *user_data);
    void (*translation_set_callbacks_v2)(iommu_handle_t *handle,
                                         io_iommu_write_callback_v2 write_cb,
                                         io_iommu_read_callback_v2 read_cb,
                                         void *user_data);
} IoIommuApi;

typedef struct IoIommuApiBackend IoIommuApiBackend;

typedef struct IoIommuApiBackendModelOps {
    IoSystemStatus (*post_open)(IoIommuApiBackend *backend);
    IoSystemStatus (*reset)(IoIommuApiBackend *backend);
    IoSystemStatus (*pre_mmio_read)(IoIommuApiBackend *backend,
                                    uint32_t off, size_t size);
    IoSystemStatus (*post_mmio_write)(IoIommuApiBackend *backend,
                                      uint32_t off, uint64_t value,
                                      size_t size);
    IoSystemStatus (*pre_dma)(IoIommuApiBackend *backend);
    void (*destroy)(IoIommuApiBackend *backend);
} IoIommuApiBackendModelOps;

struct IoIommuApiBackend {
    IoIommu base;
    IoIommuApi api;
    void *api_so;
    iommu_handle_t *handle;
    char *api_path;
    const IoIommuApiBackendModelOps *model_ops;
    void *model_opaque;
};

typedef struct IoIommuApiBackendConfig {
    IoSystemIommuKind kind;
    IoSystemIommuPlacement placement;
    const IoIommuApi *api;
    void *api_so;
    char *api_path;
    const IoIommuApiBackendModelOps *model_ops;
    void *model_opaque;
} IoIommuApiBackendConfig;

bool io_iommu_env_enabled(const char *name);
bool io_iommu_trace_enabled(void);
bool io_iommu_trace_txn_id(uint32_t *id);
void io_iommu_trace_bytes(const char *prefix, const uint8_t *data,
                          size_t data_len);
void io_iommu_log(IoSystem *system, int level, const char *fmt, ...);
bool io_iommu_range_contains(uint64_t base, uint64_t size,
                             uint64_t addr, size_t access_size);

IoSystemStatus io_iommu_api_bind_symbols(IoSystem *system, IoIommuApi *api,
                                          void *api_so,
                                          const char *api_path);
IoSystemStatus io_iommu_api_backend_create(
    IoSystem *system, const IoIommuApiBackendConfig *config,
    IoIommu **out_iommu);

IoSystemStatus io_iommu_cmodel_create(IoSystem *system,
                                      const IoSystemConfig *config,
                                      IoIommu **out_iommu);
IoSystemStatus io_iommu_rtl_create(IoSystem *system,
                                   const IoSystemConfig *config,
                                   IoIommu **out_iommu);

#endif
