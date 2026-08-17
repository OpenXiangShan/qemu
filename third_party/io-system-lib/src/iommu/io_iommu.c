#include "io_iommu_internal.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool io_iommu_env_enabled(const char *name)
{
    const char *env = getenv(name);

    return env && env[0] && env[0] != '0';
}

bool io_iommu_trace_enabled(void)
{
    return io_iommu_env_enabled("IO_SYSTEM_IOMMU_TRACE");
}

bool io_iommu_trace_txn_id(uint32_t *id)
{
    const char *env = getenv("IO_SYSTEM_IOMMU_TRACE_TXN_ID");
    char *end = NULL;
    unsigned long value;

    if (!env || !*env || env[0] == '0') {
        return false;
    }

    errno = 0;
    value = strtoul(env, &end, 0);
    if (errno || !end || *end || value > UINT16_MAX) {
        return false;
    }
    if (id) {
        *id = (uint32_t)value;
    }
    return true;
}

void io_iommu_trace_bytes(const char *prefix, const uint8_t *data,
                          size_t data_len)
{
    if (!io_iommu_trace_enabled() || !data || !data_len) {
        return;
    }

    fprintf(stderr, "%s data=", prefix);
    for (size_t i = 0; i < data_len; i++) {
        fprintf(stderr, "%02x", data[i]);
    }
    fputc('\n', stderr);
}

void io_iommu_log(IoSystem *system, int level, const char *fmt, ...)
{
    va_list ap;

    if (!system || !system->host_ops.log) {
        return;
    }

    va_start(ap, fmt);
    system->host_ops.log(system->host_ops.opaque, level, fmt, ap);
    va_end(ap);
}

bool io_iommu_range_contains(uint64_t base, uint64_t size,
                             uint64_t addr, size_t access_size)
{
    uint64_t end = base + size;
    uint64_t access_end = addr + access_size;

    if (end < base) {
        end = UINT64_MAX;
    }
    if (access_end < addr) {
        access_end = UINT64_MAX;
    }
    return size && access_size && addr >= base && access_end <= end &&
           access_end > addr;
}

static IoSystemIommuPlacement io_iommu_resolve_placement(
    const IoSystemConfig *config)
{
    if (!config || config->iommu_placement != IO_SYSTEM_IOMMU_PLACEMENT_AUTO) {
        return config ? config->iommu_placement :
               IO_SYSTEM_IOMMU_PLACEMENT_AUTO;
    }
    if (config->iommu_kind == IO_SYSTEM_IOMMU_NONE) {
        return IO_SYSTEM_IOMMU_PLACEMENT_AUTO;
    }
    if (config->backend_kind == IO_SYSTEM_BACKEND_CMODEL) {
        return IO_SYSTEM_IOMMU_PLACEMENT_EXTERNAL;
    }
    if (config->backend_kind == IO_SYSTEM_BACKEND_RTL_SYSTEM &&
        config->iommu_kind == IO_SYSTEM_IOMMU_RTL) {
        return IO_SYSTEM_IOMMU_PLACEMENT_EMBEDDED;
    }
    return IO_SYSTEM_IOMMU_PLACEMENT_AUTO;
}

IoSystemStatus io_iommu_create(IoSystem *system,
                               const IoSystemConfig *config,
                               IoIommu **out_iommu)
{
    IoSystemIommuKind kind;
    IoSystemIommuPlacement placement;

    if (!out_iommu) {
        return IO_SYSTEM_ERR_INVALID;
    }
    *out_iommu = NULL;
    if (!system || !config) {
        return IO_SYSTEM_ERR_INVALID;
    }

    kind = config->iommu_kind;
    if (kind == IO_SYSTEM_IOMMU_NONE) {
        return IO_SYSTEM_OK;
    }
    placement = io_iommu_resolve_placement(config);

    if (placement == IO_SYSTEM_IOMMU_PLACEMENT_EXTERNAL) {
        if (config->backend_kind != IO_SYSTEM_BACKEND_CMODEL) {
            return IO_SYSTEM_ERR_UNSUPPORTED;
        }
        if (kind == IO_SYSTEM_IOMMU_CMODEL) {
            return io_iommu_cmodel_create(system, config, out_iommu);
        }
        if (kind == IO_SYSTEM_IOMMU_RTL) {
            return io_iommu_rtl_create(system, config, out_iommu);
        }
        return IO_SYSTEM_ERR_UNSUPPORTED;
    }

    if (placement == IO_SYSTEM_IOMMU_PLACEMENT_EMBEDDED) {
        if (config->backend_kind == IO_SYSTEM_BACKEND_RTL_SYSTEM &&
            kind == IO_SYSTEM_IOMMU_RTL) {
            return IO_SYSTEM_OK;
        }
        return IO_SYSTEM_ERR_UNSUPPORTED;
    }

    return IO_SYSTEM_ERR_UNSUPPORTED;
}

void io_iommu_destroy(IoIommu *iommu)
{
    if (iommu && iommu->ops && iommu->ops->destroy) {
        iommu->ops->destroy(iommu);
    }
}

IoSystemStatus io_iommu_reset(IoIommu *iommu)
{
    if (!iommu) {
        return IO_SYSTEM_OK;
    }
    if (!iommu->ops || !iommu->ops->reset) {
        return IO_SYSTEM_ERR_UNSUPPORTED;
    }
    return iommu->ops->reset(iommu);
}

bool io_iommu_mmio_contains(IoIommu *iommu, uint64_t addr, size_t size)
{
    if (!iommu) {
        return false;
    }
    return io_iommu_range_contains(iommu->mmio_base, iommu->mmio_size,
                                   addr, size);
}

IoSystemStatus io_iommu_mmio_read(IoIommu *iommu, uint64_t addr,
                                  void *data, size_t size)
{
    if (!iommu || !iommu->ops || !iommu->ops->mmio_read) {
        return IO_SYSTEM_ERR_INVALID;
    }
    return iommu->ops->mmio_read(iommu, addr, data, size);
}

IoSystemStatus io_iommu_mmio_write(IoIommu *iommu, uint64_t addr,
                                   const void *data, size_t size)
{
    if (!iommu || !iommu->ops || !iommu->ops->mmio_write) {
        return IO_SYSTEM_ERR_INVALID;
    }
    return iommu->ops->mmio_write(iommu, addr, data, size);
}

IoSystemStatus io_iommu_dma_read(IoIommu *iommu,
                                 const IoSystemDmaAttrs *attrs,
                                 uint64_t iova, void *dst, size_t size)
{
    if (!iommu || !iommu->ops || !iommu->ops->dma_read) {
        return IO_SYSTEM_ERR_INVALID;
    }
    return iommu->ops->dma_read(iommu, attrs, iova, dst, size);
}

IoSystemStatus io_iommu_dma_write(IoIommu *iommu,
                                  const IoSystemDmaAttrs *attrs,
                                  uint64_t iova, const void *src,
                                  size_t size)
{
    if (!iommu || !iommu->ops || !iommu->ops->dma_write) {
        return IO_SYSTEM_ERR_INVALID;
    }
    return iommu->ops->dma_write(iommu, attrs, iova, src, size);
}

IoSystemStatus io_iommu_service(IoIommu *iommu, uint32_t budget)
{
    if (!iommu) {
        return IO_SYSTEM_OK;
    }
    if (!iommu->ops || !iommu->ops->service) {
        return IO_SYSTEM_OK;
    }
    return iommu->ops->service(iommu, budget);
}

bool io_iommu_needs_service(IoIommu *iommu)
{
    if (!iommu || !iommu->ops || !iommu->ops->needs_service) {
        return false;
    }
    return iommu->ops->needs_service(iommu);
}
