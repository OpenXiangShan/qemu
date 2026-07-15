/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"

#include "devproxy-ipc.h"
#include "hw/misc/devproxy-mmio-proxy.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "system/cpus.h"

struct DevProxyMMIOProxyState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    char *socket_path;
    uint64_t phys_addr;
    uint64_t mmio_size;
    void *mmio_client;
};

static int devproxy_mmio_proxy_read_remote(DevProxyMMIOProxyState *s,
                                           uint32_t cpu_index,
                                           uint32_t size,
                                           uint64_t phys_addr,
                                           uint64_t *data)
{
    int ret;

    bql_unlock();
    ret = devproxy_mmio_client_read(s->mmio_client, cpu_index, size,
                                    phys_addr, data);
    bql_lock();
    return ret;
}

static int devproxy_mmio_proxy_write_remote(DevProxyMMIOProxyState *s,
                                            uint32_t cpu_index,
                                            uint32_t size,
                                            uint64_t phys_addr,
                                            uint64_t data)
{
    int ret;

    bql_unlock();
    ret = devproxy_mmio_client_write(s->mmio_client, cpu_index, size,
                                     phys_addr, data);
    bql_lock();
    return ret;
}

static uint64_t devproxy_mmio_proxy_read(void *opaque, hwaddr offset,
                                         unsigned size)
{
    DevProxyMMIOProxyState *s = opaque;
    uint64_t data = 0;
    int ret;

    ret = devproxy_mmio_proxy_read_remote(
        s, current_cpu ? current_cpu->cpu_index : 0,
        size, s->phys_addr + offset, &data);
    if (ret < 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "devproxy-mmio-proxy: read IPC failed: %s\n",
                      strerror(-ret));
        return MAKE_64BIT_MASK(0, size * 8);
    }

    return data;
}

static void devproxy_mmio_proxy_write(void *opaque, hwaddr offset,
                                      uint64_t value, unsigned size)
{
    DevProxyMMIOProxyState *s = opaque;
    int ret;

    ret = devproxy_mmio_proxy_write_remote(
        s, current_cpu ? current_cpu->cpu_index : 0,
        size, s->phys_addr + offset, value);
    if (ret < 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "devproxy-mmio-proxy: write IPC failed: %s\n",
                      strerror(-ret));
    }
}

static const MemoryRegionOps devproxy_mmio_proxy_ops = {
    .read = devproxy_mmio_proxy_read,
    .write = devproxy_mmio_proxy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void devproxy_mmio_proxy_realize(DeviceState *dev, Error **errp)
{
    DevProxyMMIOProxyState *s = DEVPROXY_MMIO_PROXY(dev);

    if (!s->socket_path || !s->socket_path[0]) {
        error_setg(errp, "socket property is required");
        return;
    }
    if (!s->phys_addr) {
        error_setg(errp, "phys-addr property is required");
        return;
    }
    if (!s->mmio_size) {
        error_setg(errp, "mmio-size property must be non-zero");
        return;
    }

    s->mmio_client = devproxy_mmio_client_create(s->socket_path);
    if (!s->mmio_client) {
        error_setg(errp, "failed to create devproxy MMIO client");
        return;
    }

    memory_region_init_io(&s->mmio, OBJECT(s), &devproxy_mmio_proxy_ops, s,
                          TYPE_DEVPROXY_MMIO_PROXY, s->mmio_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

static void devproxy_mmio_proxy_unrealize(DeviceState *dev)
{
    DevProxyMMIOProxyState *s = DEVPROXY_MMIO_PROXY(dev);

    if (s->mmio_client) {
        devproxy_mmio_client_destroy(s->mmio_client);
        s->mmio_client = NULL;
    }
}

static void devproxy_mmio_proxy_instance_init(Object *obj)
{
    DevProxyMMIOProxyState *s = DEVPROXY_MMIO_PROXY(obj);

    s->socket_path = g_build_filename(g_get_user_runtime_dir(),
                                      "qemu-devproxy-mmio.sock", NULL);
    s->phys_addr = 0;
    s->mmio_size = 0x1000;
}

static void devproxy_mmio_proxy_finalize(Object *obj)
{
    DevProxyMMIOProxyState *s = DEVPROXY_MMIO_PROXY(obj);

    g_free(s->socket_path);
    if (s->mmio_client) {
        devproxy_mmio_client_destroy(s->mmio_client);
        s->mmio_client = NULL;
    }
}

static const Property devproxy_mmio_proxy_properties[] = {
    DEFINE_PROP_STRING("socket", DevProxyMMIOProxyState, socket_path),
    DEFINE_PROP_UINT64("phys-addr", DevProxyMMIOProxyState, phys_addr, 0),
    DEFINE_PROP_UINT64("mmio-size", DevProxyMMIOProxyState, mmio_size, 0x1000),
};

static void devproxy_mmio_proxy_class_init(ObjectClass *klass,
                                           const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = devproxy_mmio_proxy_realize;
    dc->unrealize = devproxy_mmio_proxy_unrealize;
    dc->desc = "Devproxy frontend MMIO proxy";
    device_class_set_props(dc, devproxy_mmio_proxy_properties);
}

static const TypeInfo devproxy_mmio_proxy_info = {
    .name = TYPE_DEVPROXY_MMIO_PROXY,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DevProxyMMIOProxyState),
    .instance_init = devproxy_mmio_proxy_instance_init,
    .instance_finalize = devproxy_mmio_proxy_finalize,
    .class_init = devproxy_mmio_proxy_class_init,
};

static void devproxy_mmio_proxy_register_types(void)
{
    type_register_static(&devproxy_mmio_proxy_info);
}

type_init(devproxy_mmio_proxy_register_types)
