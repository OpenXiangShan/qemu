#include "io_system_internal.h"

IoSystemBackend *io_system_backend_create(IoSystem *system,
                                         const IoSystemConfig *config)
{
    IoSystemBackendKind kind;

    if (!system || !config) {
        return NULL;
    }

    kind = config->backend_kind;
    switch (kind) {
    case IO_SYSTEM_BACKEND_CMODEL:
        return io_system_backend_cmodel_create(system);
    case IO_SYSTEM_BACKEND_RTL_TEMPLATE:
        return io_system_backend_rtl_template_create(system);
    case IO_SYSTEM_BACKEND_RTL_SYSTEM:
        return io_system_backend_rtl_system_create(system);
    default:
        return NULL;
    }
}

void io_system_backend_destroy(IoSystemBackend *backend)
{
    if (!backend) {
        return;
    }
    if (backend->ops && backend->ops->destroy) {
        backend->ops->destroy(backend);
    }
}
