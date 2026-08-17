#include "io_iommu_internal.h"

#include "io_refmodel_api.h"

static bool io_iommu_refmodel_active;

static const IoIommuApi io_iommu_refmodel_api = {
    .open = io_refmodel_iommu_open,
    .open_with_args = io_refmodel_iommu_open_with_args,
    .close = io_refmodel_iommu_close,
    .set_trace = io_refmodel_iommu_set_trace,
    .set_trace_txn_id = io_refmodel_iommu_set_trace_txn_id,
    .mmio_write = io_refmodel_mmio_write,
    .mmio_read = io_refmodel_mmio_read,
    .step = io_refmodel_iommu_step,
    .memory_write_with_context =
        io_refmodel_memory_write_with_context,
    .memory_read_with_context = io_refmodel_memory_read_with_context,
    .downstream_set_callbacks_v2 =
        io_refmodel_downstream_set_callbacks_v2,
    .translation_set_callbacks_v2 =
        io_refmodel_translation_set_callbacks_v2,
};

static void io_iommu_cmodel_backend_destroy(IoIommuApiBackend *backend)
{
    (void)backend;

    io_iommu_refmodel_active = false;
}

static const IoIommuApiBackendModelOps io_iommu_cmodel_backend_ops = {
    .destroy = io_iommu_cmodel_backend_destroy,
};

IoSystemStatus io_iommu_cmodel_create(IoSystem *system,
                                      const IoSystemConfig *config,
                                      IoIommu **out_iommu)
{
    IoIommuApiBackendConfig backend_config = {
        .kind = IO_SYSTEM_IOMMU_CMODEL,
        .placement = IO_SYSTEM_IOMMU_PLACEMENT_EXTERNAL,
        .api = &io_iommu_refmodel_api,
        .model_ops = &io_iommu_cmodel_backend_ops,
    };
    IoSystemStatus status;

    (void)config;

    if (io_iommu_refmodel_active) {
        return IO_SYSTEM_ERR_BUSY;
    }

    status = io_iommu_api_backend_create(system, &backend_config, out_iommu);
    if (status == IO_SYSTEM_OK) {
        io_iommu_refmodel_active = true;
    }
    return status;
}
