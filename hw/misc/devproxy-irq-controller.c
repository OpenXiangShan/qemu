/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"

#include "hw/misc/devproxy-irq-controller.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "system/devproxy-wire.h"

#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define DEVPROXY_IRQ_CONTROLLER_MMIO_SIZE 0x4000
#define DEVPROXY_IRQ_CONTROLLER_DEFAULT_NUM_IRQS 4

struct DevProxyIRQControllerState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    char *socket_path;
    int irq_fd;
    uint32_t num_irqs;
    bool *levels;
};

static int devproxy_irq_controller_recv_full(int fd, void *buf, size_t len)
{
    uint8_t *ptr = buf;

    while (len) {
        ssize_t ret = recv(fd, ptr, len, 0);

        if (ret == 0) {
            return 0;
        }
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }

        ptr += ret;
        len -= ret;
    }

    return 1;
}

static int devproxy_irq_controller_send_full(int fd, const void *buf,
                                              size_t len)
{
    const uint8_t *ptr = buf;

    while (len) {
        ssize_t ret = send(fd, ptr, len, 0);

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }

        ptr += ret;
        len -= ret;
    }

    return 0;
}

static void devproxy_irq_controller_disconnect(DevProxyIRQControllerState *s)
{
    if (s->irq_fd >= 0) {
        close(s->irq_fd);
        s->irq_fd = -1;
    }
}

static int devproxy_irq_controller_connect(DevProxyIRQControllerState *s)
{
    struct sockaddr_un addr = {};
    int fd;

    if (s->irq_fd >= 0) {
        return 0;
    }
    if (!s->socket_path || !s->socket_path[0]) {
        return -EINVAL;
    }
    if (strlen(s->socket_path) >= sizeof(addr.sun_path)) {
        return -ENAMETOOLONG;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -errno;
    }

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, s->socket_path);

    if (connect(fd, (struct sockaddr *)&addr, offsetof(struct sockaddr_un,
                                                        sun_path) +
                strlen(addr.sun_path) + 1) < 0) {
        int ret = -errno;

        close(fd);
        return ret;
    }

    s->irq_fd = fd;
    return 0;
}

static int devproxy_irq_controller_send_level(DevProxyIRQControllerState *s,
                                              uint32_t irq, bool level)
{
    DevProxyWireRequest req = {
        .version = cpu_to_le32(DEVPROXY_VERSION),
        .opcode = cpu_to_le32(DEVPROXY_OP_IRQ),
        .cpu_index = cpu_to_le32(0),
        .requester_id = cpu_to_le32(0),
        .pasid = cpu_to_le32(0),
        .flags = cpu_to_le32(0),
        .size = cpu_to_le32(1),
        .phys_addr = cpu_to_le64(irq),
        .data = cpu_to_le64(level ? 1 : 0),
        .data_offset = cpu_to_le64(0),
    };
    DevProxyWireResponse rsp;
    int ret;

    ret = devproxy_irq_controller_connect(s);
    if (ret < 0) {
        return ret;
    }

    ret = devproxy_irq_controller_send_full(s->irq_fd, &req, sizeof(req));
    if (ret < 0) {
        devproxy_irq_controller_disconnect(s);
        return ret;
    }

    ret = devproxy_irq_controller_recv_full(s->irq_fd, &rsp, sizeof(rsp));
    if (ret <= 0) {
        devproxy_irq_controller_disconnect(s);
        return ret < 0 ? ret : -EPIPE;
    }

    if (le32_to_cpu(rsp.version) != DEVPROXY_VERSION ||
        le32_to_cpu(rsp.size) != 1 ||
        le64_to_cpu(rsp.data_offset) != 0) {
        devproxy_irq_controller_disconnect(s);
        return -EPROTO;
    }

    devproxy_irq_controller_disconnect(s);
    return (int32_t)le32_to_cpu(rsp.status);
}

static uint64_t devproxy_irq_controller_read(void *opaque, hwaddr offset,
                                             unsigned size)
{
    return 0;
}

static void devproxy_irq_controller_write(void *opaque, hwaddr offset,
                                          uint64_t value, unsigned size)
{
}

static const MemoryRegionOps devproxy_irq_controller_ops = {
    .read = devproxy_irq_controller_read,
    .write = devproxy_irq_controller_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void devproxy_irq_controller_set_irq(void *opaque, int n, int level)
{
    DevProxyIRQControllerState *s = opaque;
    int ret;

    if (n < 0 || n >= s->num_irqs) {
        return;
    }
    if (s->levels[n] == !!level) {
        return;
    }

    s->levels[n] = !!level;
    ret = devproxy_irq_controller_send_level(s, n, !!level);
    if (ret < 0) {
        error_report("devproxy-irq-controller: irq %d send failed: %s",
                     n, strerror(-ret));
    }
}

static void devproxy_irq_controller_realize(DeviceState *dev, Error **errp)
{
    DevProxyIRQControllerState *s = DEVPROXY_IRQ_CONTROLLER(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (!s->socket_path || !s->socket_path[0]) {
        error_setg(errp, "socket property must be set");
        return;
    }
    if (!s->num_irqs) {
        error_setg(errp, "num-lines property must be non-zero");
        return;
    }

    s->levels = g_new0(bool, s->num_irqs);

    memory_region_init_io(&s->mmio, OBJECT(s), &devproxy_irq_controller_ops, s,
                          TYPE_DEVPROXY_IRQ_CONTROLLER,
                          DEVPROXY_IRQ_CONTROLLER_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    qdev_init_gpio_in(dev, devproxy_irq_controller_set_irq,
                      s->num_irqs);
}

static void devproxy_irq_controller_unrealize(DeviceState *dev)
{
    DevProxyIRQControllerState *s = DEVPROXY_IRQ_CONTROLLER(dev);

    devproxy_irq_controller_disconnect(s);
    g_clear_pointer(&s->levels, g_free);
}

static void devproxy_irq_controller_instance_init(Object *obj)
{
    DevProxyIRQControllerState *s = DEVPROXY_IRQ_CONTROLLER(obj);

    s->socket_path = g_build_filename(g_get_user_runtime_dir(),
                                      "qemu-devproxy-irq.sock", NULL);
    s->irq_fd = -1;
    s->num_irqs = DEVPROXY_IRQ_CONTROLLER_DEFAULT_NUM_IRQS;
    s->levels = NULL;
}

static void devproxy_irq_controller_finalize(Object *obj)
{
    DevProxyIRQControllerState *s = DEVPROXY_IRQ_CONTROLLER(obj);

    g_free(s->socket_path);
    g_clear_pointer(&s->levels, g_free);
}

static const Property devproxy_irq_controller_properties[] = {
    DEFINE_PROP_STRING("socket", DevProxyIRQControllerState, socket_path),
    DEFINE_PROP_UINT32("num-lines", DevProxyIRQControllerState, num_irqs,
                       DEVPROXY_IRQ_CONTROLLER_DEFAULT_NUM_IRQS),
};

static void devproxy_irq_controller_class_init(ObjectClass *klass,
                                               const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = devproxy_irq_controller_realize;
    dc->unrealize = devproxy_irq_controller_unrealize;
    dc->desc = "Devproxy backend IRQ controller";
    device_class_set_props(dc, devproxy_irq_controller_properties);
}

static const TypeInfo devproxy_irq_controller_info = {
    .name = TYPE_DEVPROXY_IRQ_CONTROLLER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DevProxyIRQControllerState),
    .instance_init = devproxy_irq_controller_instance_init,
    .instance_finalize = devproxy_irq_controller_finalize,
    .class_init = devproxy_irq_controller_class_init,
};

static void devproxy_irq_controller_register_types(void)
{
    type_register_static(&devproxy_irq_controller_info);
}

type_init(devproxy_irq_controller_register_types)
