#include "io_cmodel_internal.h"

#include <stdlib.h>

static IoSystemCModelBackend *io_system_cmodel_backend(IoSystemBackend *backend)
{
    return (IoSystemCModelBackend *)backend;
}

static void io_system_cmodel_destroy(IoSystemBackend *backend)
{
    free(io_system_cmodel_backend(backend));
}

static IoSystemStatus io_system_cmodel_reset(IoSystemBackend *backend)
{
    (void)backend;

    return IO_SYSTEM_OK;
}

IoSystemStatus io_cmodel_register_mmio_window(IoSystem *system,
                                              const char *name,
                                              uint64_t base,
                                              uint64_t size,
                                              IoSystemMmioRead read,
                                              IoSystemMmioWrite write,
                                              void *opaque)
{
    IoSystemCModelBackend *cmodel;

    if (!system || system->config.backend_kind != IO_SYSTEM_BACKEND_CMODEL ||
        !system->backend) {
        return IO_SYSTEM_ERR_UNSUPPORTED;
    }

    cmodel = io_system_cmodel_backend(system->backend);

    return io_cmodel_mmio_window_table_register(&cmodel->windows, name, base,
                                                size, read, write, opaque);
}

IoSystemStatus io_cmodel_register_service(IoSystem *system,
                                          IoCModelService service,
                                          IoCModelNeedsService needs_service,
                                          void *opaque)
{
    IoSystemCModelBackend *cmodel;

    if (!system || !system->backend ||
        system->config.backend_kind != IO_SYSTEM_BACKEND_CMODEL) {
        return IO_SYSTEM_ERR_UNSUPPORTED;
    }
    cmodel = io_system_cmodel_backend(system->backend);
    if (cmodel->service_cb) {
        return IO_SYSTEM_ERR_BUSY;
    }
    cmodel->service_cb = service;
    cmodel->needs_service_cb = needs_service;
    cmodel->service_opaque = opaque;
    return IO_SYSTEM_OK;
}

static IoSystemStatus io_system_cmodel_q2io_read(IoSystemBackend *backend,
                                                 uint64_t addr,
                                                 void *data,
                                                 size_t size)
{
    IoSystemCModelBackend *cmodel = io_system_cmodel_backend(backend);

    return io_cmodel_mmio_read_dispatch(backend->system, &cmodel->windows,
                                        addr, data, size);
}

static IoSystemStatus io_system_cmodel_q2io_write(IoSystemBackend *backend,
                                                  uint64_t addr,
                                                  const void *data,
                                                  size_t size)
{
    IoSystemCModelBackend *cmodel = io_system_cmodel_backend(backend);

    return io_cmodel_mmio_write_dispatch(backend->system, &cmodel->windows,
                                         addr, data, size);
}

static IoSystemStatus io_system_cmodel_service(IoSystemBackend *backend,
                                               uint32_t budget)
{
    IoSystemCModelBackend *cmodel = io_system_cmodel_backend(backend);

    if (cmodel->service_cb) {
        return cmodel->service_cb(cmodel->service_opaque, budget);
    }

    return IO_SYSTEM_OK;
}

static bool io_system_cmodel_needs_service(IoSystemBackend *backend)
{
    IoSystemCModelBackend *cmodel = io_system_cmodel_backend(backend);

    if (cmodel->needs_service_cb) {
        return cmodel->needs_service_cb(cmodel->service_opaque);
    }
    return false;
}

static IoSystemStatus io_system_cmodel_save_state(IoSystemBackend *backend,
                                                  void *buf,
                                                  size_t buf_size,
                                                  size_t *written)
{
    (void)backend;
    (void)buf;
    (void)buf_size;

    if (written) {
        *written = 0;
    }
    return IO_SYSTEM_ERR_UNSUPPORTED;
}

static IoSystemStatus io_system_cmodel_load_state(IoSystemBackend *backend,
                                                  const void *buf,
                                                  size_t buf_size)
{
    (void)backend;
    (void)buf;
    (void)buf_size;

    return IO_SYSTEM_ERR_UNSUPPORTED;
}

static const IoSystemBackendOps io_system_cmodel_ops = {
    .destroy = io_system_cmodel_destroy,
    .reset = io_system_cmodel_reset,
    .q2io_read = io_system_cmodel_q2io_read,
    .q2io_write = io_system_cmodel_q2io_write,
    .service = io_system_cmodel_service,
    .needs_service = io_system_cmodel_needs_service,
    .save_state = io_system_cmodel_save_state,
    .load_state = io_system_cmodel_load_state,
};

IoSystemBackend *io_system_backend_cmodel_create(IoSystem *system)
{
    IoSystemCModelBackend *cmodel = calloc(1, sizeof(*cmodel));

    if (!cmodel) {
        return NULL;
    }

    cmodel->base.system = system;
    cmodel->base.ops = &io_system_cmodel_ops;
    io_cmodel_mmio_window_table_init(&cmodel->windows);

    return &cmodel->base;
}
