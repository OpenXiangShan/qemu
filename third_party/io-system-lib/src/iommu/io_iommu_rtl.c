#include "io_iommu_internal.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define IO_IOMMU_RTL_WRAPPER_CTRL_OFF UINT32_C(0x1000)
#define IO_IOMMU_RTL_WRAPPER_HW_BYPASS_EN UINT32_C(0x1)
#define IO_IOMMU_RTL_WRAPPER_CTRL_BYPASS \
    (UINT32_C(0x80000000) | IO_IOMMU_RTL_WRAPPER_HW_BYPASS_EN)
#define IO_IOMMU_RTL_STEP_CYCLES 2
#define IO_IOMMU_RTL_QUEUE_WRITE_SETTLE_CYCLES 256
#define IO_IOMMU_RTL_QUEUE_POLL_SETTLE_CYCLES 64
#define IO_IOMMU_RTL_CUSTOM_PULSE_SETTLE_CYCLES 1024
#define IO_IOMMU_RTL_REG_DDTP  0x0010
#define IO_IOMMU_RTL_REG_CQH   0x0020
#define IO_IOMMU_RTL_REG_CQT   0x0024
#define IO_IOMMU_RTL_REG_FQH   0x0030
#define IO_IOMMU_RTL_REG_FQT   0x0034
#define IO_IOMMU_RTL_REG_PQH   0x0040
#define IO_IOMMU_RTL_REG_PQT   0x0044
#define IO_IOMMU_RTL_REG_CQCSR 0x0048
#define IO_IOMMU_RTL_REG_FQCSR 0x004c
#define IO_IOMMU_RTL_REG_PQCSR 0x0050
#define IO_IOMMU_RTL_REG_IPSR  0x0054
#define IO_IOMMU_RTL_REG_CUSTOM_START 0x02b0
#define IO_IOMMU_RTL_REG_CUSTOM_END   0x02f8

static char *io_iommu_strdup(const char *str)
{
    size_t len;
    char *copy;

    if (!str) {
        return NULL;
    }

    len = strlen(str) + 1;
    copy = malloc(len);
    if (copy) {
        memcpy(copy, str, len);
    }
    return copy;
}

static char *io_iommu_join3(const char *a, const char *b, const char *c)
{
    size_t alen;
    size_t blen;
    size_t clen;
    bool slash_ab;
    bool slash_bc;
    char *path;

    if (!a || !*a || !b || !c) {
        return NULL;
    }

    alen = strlen(a);
    blen = strlen(b);
    clen = strlen(c);
    slash_ab = a[alen - 1] != '/' && b[0] != '/';
    slash_bc = blen && b[blen - 1] != '/' && c[0] != '/';
    path = malloc(alen + blen + clen + (slash_ab ? 1 : 0) +
                  (slash_bc ? 1 : 0) + 1);
    if (!path) {
        return NULL;
    }

    snprintf(path, alen + blen + clen + (slash_ab ? 1 : 0) +
             (slash_bc ? 1 : 0) + 1, "%s%s%s%s%s", a,
             slash_ab ? "/" : "", b, slash_bc ? "/" : "", c);
    return path;
}

static bool io_iommu_path_is_file(const char *path)
{
    return path && *path && access(path, R_OK) == 0;
}

static char *io_iommu_first_existing(char **paths, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (io_iommu_path_is_file(paths[i])) {
            return io_iommu_strdup(paths[i]);
        }
    }
    return count ? io_iommu_strdup(paths[0]) : NULL;
}

static char *io_iommu_default_rtl_api_path(const IoSystemConfig *config)
{
    const char *env = getenv("IO_SYSTEM_IOMMU_RTL_API_SO");
    const char *workspace = getenv("IO_SYSTEM_WORKSPACE");
    const char *picker_out = config && config->iommu_picker_out &&
                             config->iommu_picker_out[0] ?
                             config->iommu_picker_out : NULL;
    char *paths[16] = { 0 };
    char *selected;
    size_t count = 0;

    if (env && *env) {
        return io_iommu_strdup(env);
    }
    if (picker_out) {
        paths[count++] = io_iommu_join3(picker_out, "lib",
                                        "libiommu_api.so");
        paths[count++] = io_iommu_join3(picker_out, "",
                                        "libiommu_api.so");
    }
    if (workspace && *workspace) {
        paths[count++] = io_iommu_join3(workspace,
                                        "bosc-iommu-v2/output/iommu-api/lib",
                                        "libiommu_api.so");
        paths[count++] = io_iommu_join3(workspace,
                                        "io-system-lib/output/iommu-rtl",
                                        "libiommu_api.so");
    }
    paths[count++] = io_iommu_strdup(
        "bosc-iommu-v2/output/iommu-api/lib/libiommu_api.so");
    paths[count++] = io_iommu_strdup(
        "../bosc-iommu-v2/output/iommu-api/lib/libiommu_api.so");
    paths[count++] = io_iommu_strdup(
        "../../bosc-iommu-v2/output/iommu-api/lib/libiommu_api.so");
    paths[count++] = io_iommu_strdup(
        "io-system-lib/output/iommu-rtl/libiommu_api.so");
    paths[count++] = io_iommu_strdup(
        "../io-system-lib/output/iommu-rtl/libiommu_api.so");
    paths[count++] = io_iommu_strdup(
        "../../io-system-lib/output/iommu-rtl/libiommu_api.so");
    paths[count++] = io_iommu_strdup("output/iommu-rtl/libiommu_api.so");
    paths[count++] = io_iommu_strdup("output/rtl-system-picker/libiommu_api.so");
    selected = io_iommu_first_existing(paths, count);
    for (size_t i = 0; i < count; i++) {
        free(paths[i]);
    }
    return selected;
}

static int io_iommu_rtl_post_write_cycles(uint32_t off, uint64_t value,
                                          size_t size)
{
    uint32_t end = off + (uint32_t)size;

    if (end < off) {
        return 0;
    }
    if (size == sizeof(uint32_t) &&
        off >= IO_IOMMU_RTL_REG_CUSTOM_START &&
        end <= IO_IOMMU_RTL_REG_CUSTOM_END) {
        if (off == IO_IOMMU_RTL_REG_CUSTOM_START &&
            (uint32_t)value == 1) {
            return IO_IOMMU_RTL_CUSTOM_PULSE_SETTLE_CYCLES;
        }
        return IO_IOMMU_RTL_STEP_CYCLES;
    }

    switch (off) {
    case IO_IOMMU_RTL_REG_CQT:
    case IO_IOMMU_RTL_REG_FQT:
    case IO_IOMMU_RTL_REG_PQT:
        return size == sizeof(uint32_t) ?
               IO_IOMMU_RTL_QUEUE_WRITE_SETTLE_CYCLES : 0;
    case IO_IOMMU_RTL_REG_CQCSR:
    case IO_IOMMU_RTL_REG_FQCSR:
    case IO_IOMMU_RTL_REG_PQCSR:
        return size == sizeof(uint32_t) ? IO_IOMMU_RTL_STEP_CYCLES : 0;
    case IO_IOMMU_RTL_REG_DDTP:
        return size == sizeof(uint64_t) ? IO_IOMMU_RTL_STEP_CYCLES : 0;
    default:
        return 0;
    }
}

static int io_iommu_rtl_pre_read_cycles(uint32_t off, size_t size)
{
    if (size != sizeof(uint32_t)) {
        return 0;
    }

    switch (off) {
    case IO_IOMMU_RTL_REG_CQH:
    case IO_IOMMU_RTL_REG_FQH:
    case IO_IOMMU_RTL_REG_PQH:
    case IO_IOMMU_RTL_REG_CQCSR:
    case IO_IOMMU_RTL_REG_FQCSR:
    case IO_IOMMU_RTL_REG_PQCSR:
    case IO_IOMMU_RTL_REG_IPSR:
        return IO_IOMMU_RTL_QUEUE_POLL_SETTLE_CYCLES;
    default:
        return 0;
    }
}

static IoSystemStatus io_iommu_rtl_step(IoIommuApiBackend *backend,
                                        int cycles,
                                        const char *why, uint32_t off)
{
    if (!backend || backend->base.kind != IO_SYSTEM_IOMMU_RTL ||
        !backend->handle || !backend->api.step || cycles <= 0) {
        return IO_SYSTEM_OK;
    }
    if (!backend->api.step(backend->handle, cycles)) {
        io_iommu_log(backend->base.system, 0,
                     "io-iommu: RTL iommu_step failed during %s "
                     "off=0x%04x cycles=%d\n",
                     why ? why : "settle", off, cycles);
        return IO_SYSTEM_ERR_IO;
    }
    if (io_iommu_trace_enabled()) {
        fprintf(stderr,
                "[io-iommu] rtl-step %s off=0x%04x cycles=%d\n",
                why ? why : "settle", off, cycles);
    }
    return IO_SYSTEM_OK;
}

static IoSystemStatus io_iommu_rtl_apply_wrapper_bypass(
    IoIommuApiBackend *backend)
{
    uint32_t value = 0;

    if (!backend || !backend->handle || !backend->api.mmio_write ||
        !backend->api.mmio_read) {
        return IO_SYSTEM_ERR_INVALID;
    }
    if (!io_iommu_env_enabled("IO_SYSTEM_IOMMU_RTL_HW_BYPASS")) {
        return IO_SYSTEM_OK;
    }

    if (!backend->api.mmio_write(backend->handle,
                                 IO_IOMMU_RTL_WRAPPER_CTRL_OFF,
                                 IO_IOMMU_RTL_WRAPPER_CTRL_BYPASS)) {
        io_iommu_log(backend->base.system, 0,
                     "io-iommu: failed to enable RTL wrapper hw bypass "
                     "at offset 0x%x\n",
                     IO_IOMMU_RTL_WRAPPER_CTRL_OFF);
        return IO_SYSTEM_ERR_IO;
    }
    if (!backend->api.mmio_read(backend->handle,
                                IO_IOMMU_RTL_WRAPPER_CTRL_OFF,
                                &value)) {
        io_iommu_log(backend->base.system, 0,
                     "io-iommu: failed to read RTL wrapper control "
                     "at offset 0x%x\n",
                     IO_IOMMU_RTL_WRAPPER_CTRL_OFF);
        return IO_SYSTEM_ERR_IO;
    }
    if ((value & IO_IOMMU_RTL_WRAPPER_HW_BYPASS_EN) == 0) {
        io_iommu_log(backend->base.system, 0,
                     "io-iommu: RTL wrapper hw bypass did not stick: "
                     "ctrl=0x%08x\n",
                     value);
        return IO_SYSTEM_ERR_IO;
    }

    io_iommu_log(backend->base.system, 2,
                 "io-iommu: RTL wrapper hw bypass enabled: "
                 "ctrl_off=0x%x ctrl=0x%08x\n",
                 IO_IOMMU_RTL_WRAPPER_CTRL_OFF, value);
    return IO_SYSTEM_OK;
}

static IoSystemStatus io_iommu_rtl_ensure_wrapper_bypass(
    IoIommuApiBackend *backend)
{
    uint32_t value = 0;

    if (!io_iommu_env_enabled("IO_SYSTEM_IOMMU_RTL_HW_BYPASS")) {
        return IO_SYSTEM_OK;
    }
    if (!backend || backend->base.kind != IO_SYSTEM_IOMMU_RTL ||
        !backend->handle || !backend->api.mmio_read) {
        return IO_SYSTEM_ERR_INVALID;
    }
    if (!backend->api.mmio_read(backend->handle,
                                IO_IOMMU_RTL_WRAPPER_CTRL_OFF,
                                &value)) {
        return IO_SYSTEM_ERR_IO;
    }
    if ((value & IO_IOMMU_RTL_WRAPPER_HW_BYPASS_EN) != 0) {
        if (io_iommu_trace_enabled()) {
            fprintf(stderr,
                    "[io-iommu] rtl-wrapper-bypass ctrl=0x%08x\n",
                    value);
        }
        return IO_SYSTEM_OK;
    }
    return io_iommu_rtl_apply_wrapper_bypass(backend);
}

static IoSystemStatus io_iommu_rtl_post_open(IoIommuApiBackend *backend)
{
    return io_iommu_rtl_apply_wrapper_bypass(backend);
}

static IoSystemStatus io_iommu_rtl_reset(IoIommuApiBackend *backend)
{
    return io_iommu_rtl_apply_wrapper_bypass(backend);
}

static IoSystemStatus io_iommu_rtl_pre_mmio_read(IoIommuApiBackend *backend,
                                                 uint32_t off, size_t size)
{
    return io_iommu_rtl_step(backend, io_iommu_rtl_pre_read_cycles(off, size),
                             "pre-read", off);
}

static IoSystemStatus io_iommu_rtl_post_mmio_write(IoIommuApiBackend *backend,
                                                   uint32_t off,
                                                   uint64_t value,
                                                   size_t size)
{
    return io_iommu_rtl_step(
        backend, io_iommu_rtl_post_write_cycles(off, value, size),
        "post-write", off);
}

static IoSystemStatus io_iommu_rtl_pre_dma(IoIommuApiBackend *backend)
{
    return io_iommu_rtl_ensure_wrapper_bypass(backend);
}

static const IoIommuApiBackendModelOps io_iommu_rtl_backend_ops = {
    .post_open = io_iommu_rtl_post_open,
    .reset = io_iommu_rtl_reset,
    .pre_mmio_read = io_iommu_rtl_pre_mmio_read,
    .post_mmio_write = io_iommu_rtl_post_mmio_write,
    .pre_dma = io_iommu_rtl_pre_dma,
};

IoSystemStatus io_iommu_rtl_create(IoSystem *system,
                                   const IoSystemConfig *config,
                                   IoIommu **out_iommu)
{
    IoIommuApi api;
    IoIommuApiBackendConfig backend_config;
    char *api_path;
    void *api_so;
    const char *error;
    IoSystemStatus status;

    if (!system || !config || !out_iommu) {
        return IO_SYSTEM_ERR_INVALID;
    }

    api_path = io_iommu_default_rtl_api_path(config);
    if (!api_path) {
        return IO_SYSTEM_ERR_NOMEM;
    }

    api_so = dlopen(api_path, RTLD_NOW | RTLD_LOCAL);
    if (!api_so) {
        error = dlerror();
        io_iommu_log(system, 0, "io-iommu: failed to load %s: %s\n",
                     api_path, error ? error : "unknown");
        free(api_path);
        return IO_SYSTEM_ERR_IO;
    }

    status = io_iommu_api_bind_symbols(system, &api, api_so, api_path);
    if (status != IO_SYSTEM_OK) {
        dlclose(api_so);
        free(api_path);
        return status;
    }

    memset(&backend_config, 0, sizeof(backend_config));
    backend_config.kind = IO_SYSTEM_IOMMU_RTL;
    backend_config.placement = IO_SYSTEM_IOMMU_PLACEMENT_EXTERNAL;
    backend_config.api = &api;
    backend_config.api_so = api_so;
    backend_config.api_path = api_path;
    backend_config.model_ops = &io_iommu_rtl_backend_ops;

    return io_iommu_api_backend_create(system, &backend_config, out_iommu);
}
