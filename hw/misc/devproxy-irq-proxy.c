/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"

#include "devproxy-ipc.h"
#include "hw/irq.h"
#include "hw/misc/devproxy-irq-proxy.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"

struct DevProxyIRQProxyState {
    SysBusDevice parent_obj;

    char *socket_path;
    void *irq_server;
    qemu_irq irq_out;
};

static void devproxy_irq_proxy_set_irq(void *opaque, uint32_t irq, bool level)
{
    DevProxyIRQProxyState *s = opaque;

    if (irq != 0) {
        return;
    }

    bql_lock();
    qemu_set_irq(s->irq_out, level);
    bql_unlock();
}

static void devproxy_irq_proxy_realize(DeviceState *dev, Error **errp)
{
    DevProxyIRQProxyState *s = DEVPROXY_IRQ_PROXY(dev);
    int ret;

    if (!s->socket_path || !s->socket_path[0]) {
        error_setg(errp, "socket property must be set");
        return;
    }

    s->irq_server = devproxy_irq_server_create(s->socket_path);
    if (!s->irq_server) {
        error_setg(errp, "failed to initialize devproxy IRQ server for %s",
                   s->socket_path);
        return;
    }

    ret = devproxy_irq_server_start(s->irq_server,
                                    devproxy_irq_proxy_set_irq, s);
    if (ret < 0) {
        devproxy_irq_server_destroy(s->irq_server);
        s->irq_server = NULL;
        error_setg(errp, "failed to start devproxy IRQ server on %s: %s",
                   s->socket_path, strerror(-ret));
        return;
    }

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq_out);
}

static void devproxy_irq_proxy_unrealize(DeviceState *dev)
{
    DevProxyIRQProxyState *s = DEVPROXY_IRQ_PROXY(dev);

    if (s->irq_server) {
        devproxy_irq_server_destroy(s->irq_server);
        s->irq_server = NULL;
    }
}

static void devproxy_irq_proxy_instance_init(Object *obj)
{
    DevProxyIRQProxyState *s = DEVPROXY_IRQ_PROXY(obj);

    s->socket_path = g_build_filename(g_get_user_runtime_dir(),
                                      "qemu-devproxy-irq.sock", NULL);
}

static void devproxy_irq_proxy_finalize(Object *obj)
{
    DevProxyIRQProxyState *s = DEVPROXY_IRQ_PROXY(obj);

    g_free(s->socket_path);
}

static const Property devproxy_irq_proxy_properties[] = {
    DEFINE_PROP_STRING("socket", DevProxyIRQProxyState, socket_path),
};

static void devproxy_irq_proxy_class_init(ObjectClass *klass,
                                          const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = devproxy_irq_proxy_realize;
    dc->unrealize = devproxy_irq_proxy_unrealize;
    dc->desc = "Devproxy frontend single-line IRQ proxy";
    device_class_set_props(dc, devproxy_irq_proxy_properties);
}

static const TypeInfo devproxy_irq_proxy_info = {
    .name = TYPE_DEVPROXY_IRQ_PROXY,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DevProxyIRQProxyState),
    .instance_init = devproxy_irq_proxy_instance_init,
    .instance_finalize = devproxy_irq_proxy_finalize,
    .class_init = devproxy_irq_proxy_class_init,
};

static void devproxy_irq_proxy_register_types(void)
{
    type_register_static(&devproxy_irq_proxy_info);
}

type_init(devproxy_irq_proxy_register_types)
