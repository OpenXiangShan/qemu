#ifndef IO_SYSTEM_INTERNAL_H
#define IO_SYSTEM_INTERNAL_H

#include "io_system_internal_api.h"
#include "io_scheduler.h"

#define IO_SYSTEM_DEFAULT_TRACE_CAPACITY 4096

typedef struct IoSystemBackend IoSystemBackend;
typedef struct IoIommu IoIommu;
typedef struct IoSystemCModelDevices IoSystemCModelDevices;

typedef struct IoSystemBackendOps {
    void (*destroy)(IoSystemBackend *backend);
    IoSystemStatus (*reset)(IoSystemBackend *backend);
    IoSystemStatus (*q2io_read)(IoSystemBackend *backend, uint64_t addr,
                                void *data, size_t size);
    IoSystemStatus (*q2io_write)(IoSystemBackend *backend, uint64_t addr,
                                 const void *data, size_t size);
    IoSystemStatus (*service)(IoSystemBackend *backend, uint32_t budget);
    bool (*needs_service)(IoSystemBackend *backend);
    IoSystemStatus (*io2q_complete)(IoSystemBackend *backend,
                                    const IoAxiTransaction *txn);
    IoSystemStatus (*save_state)(IoSystemBackend *backend,
                                 void *buf,
                                 size_t buf_size,
                                 size_t *written);
    IoSystemStatus (*load_state)(IoSystemBackend *backend,
                                 const void *buf,
                                 size_t buf_size);
} IoSystemBackendOps;

struct IoSystemBackend {
    const IoSystemBackendOps *ops;
    IoSystem *system;
};

struct IoSystem {
    IoSystemConfig config;
    IoSystemHostOps host_ops;
    const IoManifest *manifest;
    IoAxiBeatTrace *trace;
    size_t trace_capacity;
    size_t trace_count;
    uint16_t next_transaction_id;
    IoScheduler *scheduler;
    IoSystemStatus scheduler_completion_status;
    IoSystemBackend *backend;
    IoIommu *iommu;
    IoSystemCModelDevices *cmodel_devices;
};

/* Internal marker used only for scheduler-owned RTL M_AXI transactions. */
#define IO_SYSTEM_TXN_ATTR_BACKEND_COMPLETION UINT32_C(0x80000000)

void io_system_trace(IoSystem *system, const char *port, const char *channel,
                     uint64_t addr, const void *data, size_t size,
                     IoAxiResponse response);
uint32_t io_system_outstanding_depth(const IoSystem *system);
IoSystemStatus io_system_service_io2q_transaction(IoSystem *system,
                                                  IoAxiTransaction *txn);

IoSystemBackend *io_system_backend_create(IoSystem *system,
                                         const IoSystemConfig *config);
void io_system_backend_destroy(IoSystemBackend *backend);

IoSystemBackend *io_system_backend_cmodel_create(IoSystem *system);
IoSystemBackend *io_system_backend_rtl_template_create(IoSystem *system);
IoSystemBackend *io_system_backend_rtl_system_create(IoSystem *system);

IoSystemStatus io_system_cmodel_devices_create(IoSystem *system);
void io_system_cmodel_devices_destroy(IoSystem *system);
IoSystemStatus io_system_cmodel_devices_reset(IoSystem *system);

IoSystemStatus io_iommu_create(IoSystem *system,
                               const IoSystemConfig *config,
                               IoIommu **out_iommu);
void io_iommu_destroy(IoIommu *iommu);
IoSystemStatus io_iommu_reset(IoIommu *iommu);
bool io_iommu_mmio_contains(IoIommu *iommu, uint64_t addr, size_t size);
IoSystemStatus io_iommu_mmio_read(IoIommu *iommu, uint64_t addr,
                                  void *data, size_t size);
IoSystemStatus io_iommu_mmio_write(IoIommu *iommu, uint64_t addr,
                                   const void *data, size_t size);
IoSystemStatus io_iommu_dma_read(IoIommu *iommu,
                                 const IoSystemDmaAttrs *attrs,
                                 uint64_t iova, void *dst, size_t size);
IoSystemStatus io_iommu_dma_write(IoIommu *iommu,
                                  const IoSystemDmaAttrs *attrs,
                                  uint64_t iova, const void *src,
                                  size_t size);
IoSystemStatus io_iommu_service(IoIommu *iommu, uint32_t budget);
bool io_iommu_needs_service(IoIommu *iommu);

#endif
