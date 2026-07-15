/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"

#include "devproxy-ipc.h"
#include "hw/misc/devproxy-dma-bridge.h"
#include "hw/qdev-properties.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/notify.h"
#include "qapi/error.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "system/runstate.h"

struct DevProxyDmaBridgeState {
    DeviceState parent_obj;

    Notifier shutdown_notifier;
    char *socket_path;
    char *shm_path;
    uint32_t shm_size;
    void *dma_server;
    bool shutdown_notifier_registered;
};

static void devproxy_dma_bridge_remove_shutdown_notifier(
    DevProxyDmaBridgeState *s)
{
    if (!s->shutdown_notifier_registered) {
        return;
    }

    notifier_remove(&s->shutdown_notifier);
    s->shutdown_notifier_registered = false;
}

static void devproxy_dma_bridge_stop_server(DevProxyDmaBridgeState *s)
{
    if (!s->dma_server) {
        return;
    }

    devproxy_dma_server_stop(s->dma_server);
    devproxy_dma_server_destroy(s->dma_server);
    s->dma_server = NULL;
}

static void devproxy_dma_bridge_shutdown_notify(Notifier *notifier, void *data)
{
    DevProxyDmaBridgeState *s = container_of(notifier, DevProxyDmaBridgeState,
                                             shutdown_notifier);
    bool dropped_bql = false;

    (void)data;

    if (bql_locked()) {
        bql_unlock();
        dropped_bql = true;
    }

    devproxy_dma_bridge_stop_server(s);

    if (dropped_bql) {
        bql_lock();
    }
}

static int devproxy_dma_bridge_handle_dma(void *opaque,
                                          uint32_t opcode,
                                          uint32_t cpu_index,
                                          uint32_t requester_id,
                                          uint32_t pasid,
                                          uint32_t flags,
                                          uint64_t phys_addr,
                                          void *buf,
                                          uint32_t len,
                                          bool is_write,
                                          uint32_t *response_size,
                                          uint64_t *response_data,
                                          uint64_t *response_data_offset)
{
    MemTxResult result;

    (void)opaque;
    (void)cpu_index;
    (void)flags;
    (void)response_data_offset;

    if (opcode != DEVPROXY_IPC_OPCODE_DMA_READ &&
        opcode != DEVPROXY_IPC_OPCODE_DMA_WRITE) {
        return -ENOSYS;
    }

    bql_lock();
    result = address_space_rw(&address_space_memory, phys_addr,
                              MEMTXATTRS_UNSPECIFIED, buf,
                              len, is_write);
    bql_unlock();

    if (result == MEMTX_OK) {
        *response_size = len;
        *response_data = 0;
        return 0;
    }
    warn_report("devproxy-dma-bridge: direct dma failed "
                "rid=0x%x pasid=0x%x addr=0x%" PRIx64
                " len=%u dir=%s memtx=%d",
                requester_id, pasid, phys_addr, len,
                is_write ? "write" : "read", result);
    if (result == MEMTX_DECODE_ERROR) {
        return -ENODEV;
    }
    return -EIO;
}

static void devproxy_dma_bridge_realize(DeviceState *dev, Error **errp)
{
    DevProxyDmaBridgeState *s = DEVPROXY_DMA_BRIDGE(dev);
    int ret;

    if (!s->socket_path || !s->socket_path[0]) {
        error_setg(errp, "socket property must be set");
        return;
    }
    if (!s->shm_path || !s->shm_path[0]) {
        error_setg(errp, "dma-shm property must be set");
        return;
    }

    s->dma_server = devproxy_dma_server_create(s->socket_path,
                                               s->shm_path,
                                               s->shm_size);
    if (!s->dma_server) {
        error_setg(errp, "devproxy-dma-bridge: dma server init failed: %s",
                   strerror(ENOMEM));
        return;
    }

    ret = devproxy_dma_server_start(s->dma_server,
                                    devproxy_dma_bridge_handle_dma, s);
    if (ret < 0) {
        devproxy_dma_server_destroy(s->dma_server);
        s->dma_server = NULL;
        error_setg(errp, "devproxy-dma-bridge: dma start failed: %s",
                   strerror(-ret));
        return;
    }

    if (!s->shutdown_notifier_registered) {
        s->shutdown_notifier.notify = devproxy_dma_bridge_shutdown_notify;
        qemu_register_shutdown_notifier(&s->shutdown_notifier);
        s->shutdown_notifier_registered = true;
    }
}

static void devproxy_dma_bridge_unrealize(DeviceState *dev)
{
    DevProxyDmaBridgeState *s = DEVPROXY_DMA_BRIDGE(dev);

    devproxy_dma_bridge_remove_shutdown_notifier(s);
    devproxy_dma_bridge_stop_server(s);
}

static void devproxy_dma_bridge_instance_init(Object *obj)
{
    DevProxyDmaBridgeState *s = DEVPROXY_DMA_BRIDGE(obj);
    const char *runtime_dir = g_get_user_runtime_dir();

    s->socket_path = g_build_filename(runtime_dir,
                                      "qemu-devproxy-dma.sock", NULL);
    s->shm_path = g_build_filename(runtime_dir,
                                   "qemu-devproxy-dma-bounce", NULL);
    s->shm_size = 64 * 1024;
    s->dma_server = NULL;
    s->shutdown_notifier_registered = false;
}

static void devproxy_dma_bridge_finalize(Object *obj)
{
    DevProxyDmaBridgeState *s = DEVPROXY_DMA_BRIDGE(obj);

    devproxy_dma_bridge_remove_shutdown_notifier(s);
    g_free(s->socket_path);
    g_free(s->shm_path);
}

static const Property devproxy_dma_bridge_properties[] = {
    DEFINE_PROP_STRING("socket", DevProxyDmaBridgeState, socket_path),
    DEFINE_PROP_STRING("dma-shm", DevProxyDmaBridgeState, shm_path),
    DEFINE_PROP_UINT32("dma-shm-size", DevProxyDmaBridgeState, shm_size,
                       64 * 1024),
};

static void devproxy_dma_bridge_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = devproxy_dma_bridge_realize;
    dc->unrealize = devproxy_dma_bridge_unrealize;
    dc->desc = "Devproxy frontend DMA bridge";
    device_class_set_props(dc, devproxy_dma_bridge_properties);
}

static const TypeInfo devproxy_dma_bridge_info = {
    .name = TYPE_DEVPROXY_DMA_BRIDGE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(DevProxyDmaBridgeState),
    .instance_init = devproxy_dma_bridge_instance_init,
    .instance_finalize = devproxy_dma_bridge_finalize,
    .class_init = devproxy_dma_bridge_class_init,
};

static void devproxy_dma_bridge_register_types(void)
{
    type_register_static(&devproxy_dma_bridge_info);
}

type_init(devproxy_dma_bridge_register_types)
