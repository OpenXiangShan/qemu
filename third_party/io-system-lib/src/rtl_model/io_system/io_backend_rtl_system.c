// SPDX-License-Identifier: Apache-2.0
#include "io_system_internal.h"

#include "io_rtl_system_api.h"

#include "virtio_backend.h"
#include "virtio_wrapper.h"

#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef struct IoSystemRtlSystemApi {
    io_rtl_system_handle_t *(*open)(const io_rtl_system_config_t *config,
                                  const io_rtl_system_callbacks_t *callbacks);
    void (*close)(io_rtl_system_handle_t *handle);
    int (*reset)(io_rtl_system_handle_t *handle);
    int (*mmio_read)(io_rtl_system_handle_t *handle, uint64_t addr,
                     void *data, size_t size);
    int (*mmio_write)(io_rtl_system_handle_t *handle, uint64_t addr,
                      const void *data, size_t size);
    int (*gbus_read)(io_rtl_system_handle_t *handle, uint32_t addr,
                     uint32_t *value);
    int (*gbus_write)(io_rtl_system_handle_t *handle, uint32_t addr,
                      uint32_t value);
    int (*m_axi_next)(io_rtl_system_handle_t *handle,
                      IoAxiTransaction *txn);
    int (*m_axi_complete)(io_rtl_system_handle_t *handle,
                          const IoAxiTransaction *txn);
    int (*step)(io_rtl_system_handle_t *handle, uint64_t cycles);
    int (*poll)(io_rtl_system_handle_t *handle);
    int (*needs_service)(io_rtl_system_handle_t *handle);
    const char *(*last_error)(io_rtl_system_handle_t *handle);
} IoSystemRtlSystemApi;

#define IO_RTL_SYSTEM_MY_VIRTIO_BLK_BASE 0x310a0000ULL
#define IO_RTL_SYSTEM_MY_VIRTIO_BLK_SIZE 0x1000U
#define IO_RTL_SYSTEM_BLK_SEG_MAX 126
#define IO_RTL_SYSTEM_BLK_SIZE 512

typedef struct IoSystemRtlSystemBackend {
    IoSystemBackend base;
    void *api_so;
    io_rtl_system_handle_t *handle;
    IoSystemRtlSystemApi api;
    virtio_backend_handle_t blk_backend;
    virtio_handle_t blk_virtio;
    long owner_tid;
    bool load_failed;
    bool service_pending;
} IoSystemRtlSystemBackend;

static IoSystemRtlSystemBackend *io_system_rtl_system_backend(
    IoSystemBackend *backend)
{
    return (IoSystemRtlSystemBackend *)backend;
}

static void io_rtl_system_log(IoSystem *system, int level,
                            const char *fmt, ...)
{
    va_list ap;

    if (!system || !system->host_ops.log) {
        return;
    }

    va_start(ap, fmt);
    system->host_ops.log(system->host_ops.opaque, level, fmt, ap);
    va_end(ap);
}

static IoSystemStatus io_rtl_system_status_from_api(int status)
{
    switch (status) {
    case IO_RTL_SYSTEM_OK:
        return IO_SYSTEM_OK;
    case IO_RTL_SYSTEM_ERR_INVALID:
        return IO_SYSTEM_ERR_INVALID;
    case IO_RTL_SYSTEM_ERR_NOMEM:
        return IO_SYSTEM_ERR_NOMEM;
    case IO_RTL_SYSTEM_ERR_UNMAPPED:
        return IO_SYSTEM_ERR_UNMAPPED;
    case IO_RTL_SYSTEM_ERR_UNSUPPORTED:
        return IO_SYSTEM_ERR_UNSUPPORTED;
    case IO_RTL_SYSTEM_ERR_NO_TRANSACTION:
        return IO_SYSTEM_ERR_NO_TRANSACTION;
    default:
        return IO_SYSTEM_ERR_IO;
    }
}

static long io_rtl_system_current_tid(void)
{
#ifdef SYS_gettid
    return (long)syscall(SYS_gettid);
#else
    return 0;
#endif
}

static int io_rtl_system_host_guest_read(void *opaque, uint64_t gpa,
                                         void *dst, uint32_t len)
{
    IoSystem *system = opaque;

    return io_system_guest_memory_read(system, gpa, dst, len) ==
           IO_SYSTEM_OK ? (int)len : -1;
}

static int io_rtl_system_host_guest_write(void *opaque, uint64_t gpa,
                                          const void *src, uint32_t len)
{
    IoSystem *system = opaque;

    return io_system_guest_memory_write(system, gpa, src, len) ==
           IO_SYSTEM_OK ? (int)len : -1;
}

static void io_rtl_system_runtime_log_cb(void *opaque, int level,
                                       const char *msg)
{
    IoSystem *system = opaque;

    io_rtl_system_log(system, level, "io-rtl-system: %s\n",
                    msg ? msg : "");
}

static const char *io_rtl_system_default_api_path(IoSystem *system)
{
    const char *env = getenv("IO_RTL_SYSTEM_API_SO");

    if (system && system->config.backend_library_path &&
        system->config.backend_library_path[0]) {
        return system->config.backend_library_path;
    }
    if (env && env[0]) {
        return env;
    }
    return "io-system-lib/output/libio_rtl_system_api.so";
}

static bool io_rtl_system_bind_symbols(IoSystemRtlSystemBackend *rtl,
                                     const char *api_path)
{
    const char *error;

#define IO_RTL_SYSTEM_BIND(_field, _sym)                                      \
    do {                                                                    \
        dlerror();                                                          \
        *(void **)(&rtl->api._field) = dlsym(rtl->api_so, _sym);            \
        error = dlerror();                                                  \
        if (error || !rtl->api._field) {                                    \
            io_rtl_system_log(rtl->base.system, 0,                            \
                            "io-rtl-system: failed to resolve %s from %s: %s\n", \
                            _sym, api_path, error ? error : "missing");    \
            return false;                                                   \
        }                                                                   \
    } while (0)

    IO_RTL_SYSTEM_BIND(open, "io_rtl_system_open");
    IO_RTL_SYSTEM_BIND(close, "io_rtl_system_close");
    IO_RTL_SYSTEM_BIND(reset, "io_rtl_system_reset");
    IO_RTL_SYSTEM_BIND(mmio_read, "io_rtl_system_mmio_read");
    IO_RTL_SYSTEM_BIND(mmio_write, "io_rtl_system_mmio_write");
    IO_RTL_SYSTEM_BIND(gbus_read, "io_rtl_system_gbus_read");
    IO_RTL_SYSTEM_BIND(gbus_write, "io_rtl_system_gbus_write");
    IO_RTL_SYSTEM_BIND(m_axi_next, "io_rtl_system_m_axi_next");
    IO_RTL_SYSTEM_BIND(m_axi_complete, "io_rtl_system_m_axi_complete");
    IO_RTL_SYSTEM_BIND(step, "io_rtl_system_step");
    IO_RTL_SYSTEM_BIND(poll, "io_rtl_system_poll");
    IO_RTL_SYSTEM_BIND(needs_service, "io_rtl_system_needs_service");
    IO_RTL_SYSTEM_BIND(last_error, "io_rtl_system_last_error");
    return true;

#undef IO_RTL_SYSTEM_BIND
}

static void io_rtl_system_unload(IoSystemRtlSystemBackend *rtl)
{
    bool same_thread;

    if (!rtl) {
        return;
    }

    same_thread = !rtl->owner_tid ||
                  rtl->owner_tid == io_rtl_system_current_tid();

    /*
     * Picker/VCS-generated runtime state is not safe to construct on one
     * host thread and tear down from another.  QEMU creates the machine on
     * the main loop thread, but TCG MMIO runs on the vCPU thread, so the RTL
     * runtime is lazy-created on first Q2IO access.  If machine teardown later
     * happens from a different thread, deliberately leak the VCS runtime and
     * shared objects instead of calling into libvcsnew from the wrong thread.
     * The normal standalone tests still close on their owner thread.
     */
    if (!same_thread && (rtl->handle || rtl->api_so)) {
        io_rtl_system_log(rtl->base.system, 1,
                          "io-rtl-system: skip VCS cleanup from non-owner "
                          "thread owner_tid=%ld current_tid=%ld\n",
                          rtl->owner_tid, io_rtl_system_current_tid());
        rtl->handle = NULL;
        rtl->api_so = NULL;
        memset(&rtl->api, 0, sizeof(rtl->api));
        rtl->owner_tid = 0;
        return;
    }

    if (rtl->blk_backend) {
        virtio_backend_destroy(rtl->blk_backend);
        rtl->blk_backend = NULL;
        rtl->blk_virtio = NULL;
    }

    if (rtl->handle && rtl->api.close) {
        rtl->api.close(rtl->handle);
    }
    rtl->handle = NULL;

    if (rtl->api_so) {
        dlclose(rtl->api_so);
        rtl->api_so = NULL;
    }

    memset(&rtl->api, 0, sizeof(rtl->api));
    rtl->owner_tid = 0;
}

static int io_rtl_system_backend_gbus_read(void *opaque, uint32_t addr,
                                           uint32_t *value)
{
    IoSystemRtlSystemBackend *rtl = opaque;

    if (!rtl || !rtl->handle || !rtl->api.gbus_read || !value) {
        return -1;
    }
    return rtl->api.gbus_read(rtl->handle, addr, value) == IO_RTL_SYSTEM_OK ?
           0 : -1;
}

static int io_rtl_system_backend_gbus_write(void *opaque, uint32_t addr,
                                            uint32_t value)
{
    IoSystemRtlSystemBackend *rtl = opaque;

    if (!rtl || !rtl->handle || !rtl->api.gbus_write) {
        return -1;
    }
    return rtl->api.gbus_write(rtl->handle, addr, value) == IO_RTL_SYSTEM_OK ?
           0 : -1;
}

static IoSystemStatus io_rtl_system_service_m_axi(
    IoSystemRtlSystemBackend *rtl, uint32_t budget)
{
    IoSystemStatus status;
    IoSystem *system;
    uint32_t limit;

    if (!rtl || !rtl->base.system) {
        return IO_SYSTEM_ERR_INVALID;
    }
    system = rtl->base.system;
    limit = budget ? budget : io_system_outstanding_depth(system);
    if (!limit) {
        limit = 1;
    }

    for (uint32_t i = 0; i < limit; i++) {
        IoAxiTransaction txn;

        if (system->config.io2q_async_enabled &&
            !io_scheduler_available_slots(system->scheduler)) {
            return IO_SYSTEM_OK;
        }

        memset(&txn, 0, sizeof(txn));
        status = io_rtl_system_status_from_api(
            rtl->api.m_axi_next(rtl->handle, &txn));
        if (status == IO_SYSTEM_ERR_NO_TRANSACTION) {
            return IO_SYSTEM_OK;
        }
        if (status != IO_SYSTEM_OK) {
            return status;
        }
        status = io_system_service_io2q_transaction(system, &txn);
        if (status == IO_SYSTEM_ERR_BUSY) {
            txn.response = IO_AXI_RESPONSE_SLVERR;
            status = io_rtl_system_status_from_api(
                rtl->api.m_axi_complete(rtl->handle, &txn));
        }
        if (status != IO_SYSTEM_OK) {
            return status;
        }
    }
    return IO_SYSTEM_OK;
}

static uint64_t io_rtl_system_virtio_alloc(int size)
{
    return (uint64_t)(uintptr_t)calloc(1, size);
}

static void io_rtl_system_virtio_free(uint64_t addr, int size)
{
    (void)size;

    free((void *)(uintptr_t)addr);
}

static int io_rtl_system_virtio_vprint(const char *fmt, va_list ap)
{
    return vfprintf(stderr, fmt, ap);
}

static int io_rtl_system_virtio_map(uint64_t gphys_addr,
                                    uint64_t gphys_size,
                                    uint64_t *hphys_addr,
                                    uint64_t *hphys_size,
                                    void *priv)
{
    (void)priv;

    if (!hphys_addr || !hphys_size || !gphys_size) {
        return -1;
    }

    /*
     * my-virtio-lib only needs a stable address translation token.  The
     * actual data moves through guest_mem_read/write callbacks below.
     */
    *hphys_addr = gphys_addr;
    *hphys_size = gphys_size;
    return 0;
}

static int io_rtl_system_virtio_guest_read(uint64_t gpa, void *dst,
                                           uint32_t len, void *priv)
{
    IoSystemRtlSystemBackend *rtl = priv;

    if (!rtl || !dst) {
        return -1;
    }

    return io_system_guest_memory_read(rtl->base.system, gpa, dst, len) ==
           IO_SYSTEM_OK ? (int)len : -1;
}

static int io_rtl_system_virtio_guest_write(uint64_t gpa, void *src,
                                            uint32_t len, void *priv)
{
    IoSystemRtlSystemBackend *rtl = priv;

    if (!rtl || !src) {
        return -1;
    }

    return io_system_guest_memory_write(rtl->base.system, gpa, src, len) ==
           IO_SYSTEM_OK ? (int)len : -1;
}

static int io_rtl_system_virtio_set_irq(void *priv)
{
    (void)priv;

    return 0;
}

static int io_rtl_system_submit_blk_io(uint64_t sector, void *buf, int len,
                                       uint8_t flags, void *priv)
{
    IoSystemRtlSystemBackend *rtl = priv;
    struct virtio_backend_io io = {
        .type = VIRTIO_BACKEND_IO_BLK,
        .buf = buf,
        .len = (size_t)len,
        .u.blk.sector = sector,
    };

    if (!rtl || !rtl->blk_backend || len < 0) {
        return -1;
    }

    switch (flags) {
    case MY_BLK_REQ_READ:
        io.u.blk.op = VIRTIO_BACKEND_BLK_READ;
        return virtio_backend_read(rtl->blk_backend, &io);
    case MY_BLK_REQ_WRITE:
        io.u.blk.op = VIRTIO_BACKEND_BLK_WRITE;
        return virtio_backend_write(rtl->blk_backend, &io);
    case MY_BLK_REQ_FLUSH:
        io.u.blk.op = VIRTIO_BACKEND_BLK_FLUSH;
        return virtio_backend_write(rtl->blk_backend, &io);
    default:
        return -1;
    }
}

static int io_rtl_system_get_blk_capacity(void *priv)
{
    IoSystemRtlSystemBackend *rtl = priv;
    struct virtio_backend_info info;

    if (!rtl || !rtl->blk_backend ||
        virtio_backend_get_info(rtl->blk_backend, &info) < 0 ||
        info.type != VIRTIO_BACKEND_BLK) {
        return -1;
    }

    return (int)info.u.blk.capacity;
}

static const struct virtio_gbus_ops io_rtl_system_virtio_gbus_ops = {
    .read = io_rtl_system_backend_gbus_read,
    .write = io_rtl_system_backend_gbus_write,
};

static struct libvirtio_ops io_rtl_system_virtio_ops = {
    .vprint = io_rtl_system_virtio_vprint,
    .mm_alloc = io_rtl_system_virtio_alloc,
    .mm_free = io_rtl_system_virtio_free,
    .map = io_rtl_system_virtio_map,
    .guest_mem_read = io_rtl_system_virtio_guest_read,
    .guest_mem_write = io_rtl_system_virtio_guest_write,
    .set_irq = io_rtl_system_virtio_set_irq,
    .blk_ops = {
        .submit_blk_io = io_rtl_system_submit_blk_io,
        .get_blk_capacity = io_rtl_system_get_blk_capacity,
    },
};

static bool io_rtl_system_init_blk_rtl_config(IoSystemRtlSystemBackend *rtl)
{
    struct virtio_backend_info info;
    uint64_t capacity = 0;

    if (virtio_backend_get_info(rtl->blk_backend, &info) == 0 &&
        info.type == VIRTIO_BACKEND_BLK && info.u.blk.capacity > 0) {
        capacity = (uint64_t)info.u.blk.capacity;
    }

    return io_rtl_system_backend_gbus_write(
               rtl, VIRTIO_GBUS_CSR_BLK_CAPACITY_LOW,
               (uint32_t)capacity) == 0 &&
           io_rtl_system_backend_gbus_write(
               rtl, VIRTIO_GBUS_CSR_BLK_CAPACITY_HIGH,
               (uint32_t)(capacity >> 32)) == 0 &&
           io_rtl_system_backend_gbus_write(
               rtl, VIRTIO_GBUS_CSR_BLK_SEG_MAX,
               IO_RTL_SYSTEM_BLK_SEG_MAX) == 0 &&
           io_rtl_system_backend_gbus_write(
               rtl, VIRTIO_GBUS_CSR_BLK_SIZE,
               IO_RTL_SYSTEM_BLK_SIZE) == 0;
}

static bool io_rtl_system_create_blk(IoSystemRtlSystemBackend *rtl)
{
    IoSystem *system = rtl->base.system;
    struct virtio_backend_config backend_config = {
        .type = VIRTIO_BACKEND_BLK,
    };
    uint32_t magic = 0;
    uint32_t version = 0;

    if (!system || !system->config.my_virtio_blk_enabled) {
        return true;
    }

    backend_config.u.blk.image_path = system->config.my_virtio_blk_image_path;
    rtl->blk_backend = virtio_backend_create(&backend_config);
    if (!rtl->blk_backend) {
        io_rtl_system_log(system, 0,
                          "io-rtl-system: failed to create blk backend image=%s\n",
                          backend_config.u.blk.image_path ?
                          backend_config.u.blk.image_path : "");
        return false;
    }

    if (io_rtl_system_backend_gbus_read(
            rtl, VIRTIO_GBUS_CSR_MAGIC, &magic) < 0 ||
        io_rtl_system_backend_gbus_read(
            rtl, VIRTIO_GBUS_CSR_VERSION, &version) < 0 ||
        magic != VIRTIO_GBUS_MAGIC || version != VIRTIO_GBUS_VERSION) {
        io_rtl_system_log(system, 0,
                          "io-rtl-system: bad virtio gbus magic=0x%08x version=%u\n",
                          magic, version);
        return false;
    }

    if (!io_rtl_system_init_blk_rtl_config(rtl)) {
        io_rtl_system_log(system, 0,
                          "io-rtl-system: failed to initialize blk RTL config\n");
        return false;
    }

    rtl->blk_virtio = virtio_gbus_create(VIRTIO_EMU_NAME_BLK,
                                         IO_RTL_SYSTEM_MY_VIRTIO_BLK_BASE,
                                         IO_RTL_SYSTEM_MY_VIRTIO_BLK_SIZE,
                                         &io_rtl_system_virtio_ops, rtl,
                                         &io_rtl_system_virtio_gbus_ops,
                                         rtl);
    if (!rtl->blk_virtio) {
        io_rtl_system_log(system, 0,
                          "io-rtl-system: failed to create virtio-gbus blk\n");
        return false;
    }

    return virtio_gbus_poll(rtl->blk_virtio) == 0;
}

static bool io_rtl_system_load(IoSystemRtlSystemBackend *rtl)
{
    IoSystem *system = rtl->base.system;
    const char *api_path = io_rtl_system_default_api_path(system);
    io_rtl_system_config_t config = {
        .mmio_base_hint = 0x30040000ULL,
        .mmio_size_hint = 0x1000ULL,
        .io2q_max_beat_bytes = system->config.io2q_max_beat_bytes,
        .io2q_outstanding = io_system_outstanding_depth(system),
        .trace = system && system->config.trace_capacity ? 1 : 0,
    };
    io_rtl_system_callbacks_t callbacks = {
        .opaque = system,
        .guest_read = io_rtl_system_host_guest_read,
        .guest_write = io_rtl_system_host_guest_write,
        .log = io_rtl_system_runtime_log_cb,
    };
    const char *error;

    rtl->api_so = dlopen(api_path, RTLD_NOW | RTLD_LOCAL);
    if (!rtl->api_so) {
        error = dlerror();
        io_rtl_system_log(system, 0, "io-rtl-system: failed to load %s: %s\n",
                        api_path, error ? error : "unknown");
        return false;
    }

    if (!io_rtl_system_bind_symbols(rtl, api_path)) {
        return false;
    }

    rtl->handle = rtl->api.open(&config, &callbacks);
    if (!rtl->handle) {
        io_rtl_system_log(system, 0, "io-rtl-system: io_rtl_system_open failed\n");
        return false;
    }
    rtl->owner_tid = io_rtl_system_current_tid();

    if (!io_rtl_system_create_blk(rtl)) {
        return false;
    }

    io_rtl_system_log(system, 2, "io-rtl-system: loaded %s owner_tid=%ld\n",
                      api_path, rtl->owner_tid);
    return true;
}

static bool io_rtl_system_ensure_loaded(IoSystemRtlSystemBackend *rtl)
{
    if (!rtl) {
        return false;
    }
    if (rtl->handle) {
        return true;
    }
    if (rtl->load_failed) {
        return false;
    }
    if (!io_rtl_system_load(rtl)) {
        io_rtl_system_unload(rtl);
        rtl->load_failed = true;
        return false;
    }
    return true;
}

static void io_rtl_system_destroy(IoSystemBackend *backend)
{
    IoSystemRtlSystemBackend *rtl = io_system_rtl_system_backend(backend);

    io_rtl_system_unload(rtl);
    free(rtl);
}

static IoSystemStatus io_rtl_system_poll_blk(IoSystemRtlSystemBackend *rtl)
{
    if (!rtl->blk_virtio) {
        return IO_SYSTEM_OK;
    }

    return virtio_gbus_poll(rtl->blk_virtio) == 0 ?
           IO_SYSTEM_OK : IO_SYSTEM_ERR_IO;
}

static IoSystemStatus io_rtl_system_backend_reset(IoSystemBackend *backend)
{
    IoSystemRtlSystemBackend *rtl = io_system_rtl_system_backend(backend);

    rtl->service_pending = false;
    if (!rtl->handle) {
        return IO_SYSTEM_OK;
    }
    return io_rtl_system_status_from_api(rtl->api.reset(rtl->handle));
}

static IoSystemStatus io_rtl_system_io2q_complete(
    IoSystemBackend *backend, const IoAxiTransaction *txn)
{
    IoSystemRtlSystemBackend *rtl = io_system_rtl_system_backend(backend);

    return io_rtl_system_status_from_api(
        rtl->api.m_axi_complete(rtl->handle, txn));
}

static IoSystemStatus io_rtl_system_q2io_read(IoSystemBackend *backend,
                                            uint64_t addr,
                                            void *data,
                                            size_t size)
{
    IoSystemRtlSystemBackend *rtl = io_system_rtl_system_backend(backend);
    int status;

    if (!io_rtl_system_ensure_loaded(rtl)) {
        if (data) {
            memset(data, 0xff, size);
        }
        io_system_trace(backend->system, "q2io", "rtl-read", addr, data,
                        size, IO_AXI_RESPONSE_SLVERR);
        return IO_SYSTEM_ERR_IO;
    }

    status = rtl->api.mmio_read(rtl->handle, addr, data, size);
    if (status == IO_RTL_SYSTEM_OK) {
        rtl->service_pending = true;
    }

    io_system_trace(backend->system, "q2io", "rtl-read", addr, data, size,
                    status == IO_RTL_SYSTEM_OK ? IO_AXI_RESPONSE_OKAY :
                    status == IO_RTL_SYSTEM_ERR_UNMAPPED ?
                    IO_AXI_RESPONSE_DECERR : IO_AXI_RESPONSE_SLVERR);
    return io_rtl_system_status_from_api(status);
}

static IoSystemStatus io_rtl_system_q2io_write(IoSystemBackend *backend,
                                             uint64_t addr,
                                             const void *data,
                                             size_t size)
{
    IoSystemRtlSystemBackend *rtl = io_system_rtl_system_backend(backend);
    int status;

    if (!io_rtl_system_ensure_loaded(rtl)) {
        io_system_trace(backend->system, "q2io", "rtl-write", addr, data,
                        size, IO_AXI_RESPONSE_SLVERR);
        return IO_SYSTEM_ERR_IO;
    }

    status = rtl->api.mmio_write(rtl->handle, addr, data, size);
    if (status == IO_RTL_SYSTEM_OK) {
        rtl->service_pending = true;
    }
    io_system_trace(backend->system, "q2io", "rtl-write", addr, data, size,
                    status == IO_RTL_SYSTEM_OK ? IO_AXI_RESPONSE_OKAY :
                    status == IO_RTL_SYSTEM_ERR_UNMAPPED ?
                    IO_AXI_RESPONSE_DECERR : IO_AXI_RESPONSE_SLVERR);
    return io_rtl_system_status_from_api(status);
}

static IoSystemStatus io_rtl_system_service(IoSystemBackend *backend,
                                             uint32_t budget)
{
    IoSystemRtlSystemBackend *rtl = io_system_rtl_system_backend(backend);
    IoSystemStatus status;

    if (!io_rtl_system_ensure_loaded(rtl)) {
        return IO_SYSTEM_ERR_IO;
    }
    status = io_rtl_system_status_from_api(rtl->api.step(rtl->handle, 1));
    if (status != IO_SYSTEM_OK) {
        return status;
    }
    status = io_rtl_system_poll_blk(rtl);
    if (status != IO_SYSTEM_OK) {
        return status;
    }
    status = io_rtl_system_service_m_axi(rtl, budget);
    if (status == IO_SYSTEM_OK && (!rtl->api.needs_service ||
                                   rtl->api.needs_service(rtl->handle) <= 0)) {
        rtl->service_pending = false;
    }
    return status;
}

static bool io_rtl_system_backend_needs_service(IoSystemBackend *backend)
{
    IoSystemRtlSystemBackend *rtl = io_system_rtl_system_backend(backend);

    if (!rtl->handle || !rtl->api.needs_service) {
        return false;
    }
    return rtl->service_pending || rtl->api.needs_service(rtl->handle) > 0;
}

static IoSystemStatus io_rtl_system_save_state(IoSystemBackend *backend,
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

static IoSystemStatus io_rtl_system_load_state(IoSystemBackend *backend,
                                             const void *buf,
                                             size_t buf_size)
{
    (void)backend;
    (void)buf;
    (void)buf_size;

    return IO_SYSTEM_ERR_UNSUPPORTED;
}

static const IoSystemBackendOps io_rtl_system_ops = {
    .destroy = io_rtl_system_destroy,
    .reset = io_rtl_system_backend_reset,
    .q2io_read = io_rtl_system_q2io_read,
    .q2io_write = io_rtl_system_q2io_write,
    .service = io_rtl_system_service,
    .needs_service = io_rtl_system_backend_needs_service,
    .io2q_complete = io_rtl_system_io2q_complete,
    .save_state = io_rtl_system_save_state,
    .load_state = io_rtl_system_load_state,
};

IoSystemBackend *io_system_backend_rtl_system_create(IoSystem *system)
{
    IoSystemRtlSystemBackend *rtl = calloc(1, sizeof(*rtl));

    if (!rtl) {
        return NULL;
    }

    rtl->base.system = system;
    rtl->base.ops = &io_rtl_system_ops;
    return &rtl->base;
}
