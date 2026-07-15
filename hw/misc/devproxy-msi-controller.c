/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"

#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "accel/devproxy/devproxy.h"
#include "hw/misc/devproxy-msi-controller.h"
#include "hw/pci/msi.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "system/devproxy-wire.h"

#define DEVPROXY_MSI_CONTROLLER_DEFAULT_MMIO_SIZE 0x80000

struct DevProxyMSIControllerState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    char *socket_path;
    uint64_t phys_addr;
    uint64_t mmio_size;
};

static int devproxy_msi_controller_recv_full(int fd, void *buf, size_t len)
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

static int devproxy_msi_controller_send_full(int fd, const void *buf,
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

static int devproxy_msi_controller_send(DevProxyMSIControllerState *s,
                                        hwaddr offset, uint32_t value,
                                        unsigned size)
{
    struct sockaddr_un addr = {};
    DevProxyWireRequest req = {
        .version = cpu_to_le32(DEVPROXY_VERSION),
        .opcode = cpu_to_le32(DEVPROXY_OP_MSI),
        .cpu_index = cpu_to_le32(0),
        .requester_id = cpu_to_le32(0),
        .pasid = cpu_to_le32(0),
        .flags = cpu_to_le32(0),
        .size = cpu_to_le32(size),
        .phys_addr = cpu_to_le64(s->phys_addr + offset),
        .data = cpu_to_le64(value),
        .data_offset = cpu_to_le64(0),
    };
    DevProxyWireResponse rsp;
    int fd;
    int ret;

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

    ret = connect(fd, (struct sockaddr *)&addr,
                  offsetof(struct sockaddr_un, sun_path) +
                  strlen(addr.sun_path) + 1);
    if (ret < 0) {
        ret = -errno;
        close(fd);
        return ret;
    }

    ret = devproxy_msi_controller_send_full(fd, &req, sizeof(req));
    if (ret < 0) {
        close(fd);
        return ret;
    }

    ret = devproxy_msi_controller_recv_full(fd, &rsp, sizeof(rsp));
    close(fd);
    if (ret <= 0) {
        return ret < 0 ? ret : -EPIPE;
    }

    if (le32_to_cpu(rsp.version) != DEVPROXY_VERSION ||
        le32_to_cpu(rsp.size) != size ||
        le64_to_cpu(rsp.data_offset) != 0) {
        return -EPROTO;
    }

    return (int32_t)le32_to_cpu(rsp.status);
}

static uint64_t devproxy_msi_controller_read(void *opaque, hwaddr offset,
                                             unsigned size)
{
    return 0;
}

static void devproxy_msi_controller_write(void *opaque, hwaddr offset,
                                          uint64_t value, unsigned size)
{
    DevProxyMSIControllerState *s = opaque;
    int ret;

    ret = devproxy_msi_controller_send(s, offset, value, size);
    if (ret < 0) {
        error_report("devproxy-msi-controller: MSI send failed at 0x%"
                     HWADDR_PRIx ": %s", s->phys_addr + offset,
                     strerror(-ret));
    }
}

static const MemoryRegionOps devproxy_msi_controller_ops = {
    .read = devproxy_msi_controller_read,
    .write = devproxy_msi_controller_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void devproxy_msi_controller_realize(DeviceState *dev, Error **errp)
{
    DevProxyMSIControllerState *s = DEVPROXY_MSI_CONTROLLER(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (!s->socket_path || !s->socket_path[0]) {
        error_setg(errp, "socket property must be set");
        return;
    }
    if (!s->mmio_size) {
        error_setg(errp, "mmio-size property must be non-zero");
        return;
    }

    memory_region_init_io(&s->mmio, OBJECT(s), &devproxy_msi_controller_ops, s,
                          TYPE_DEVPROXY_MSI_CONTROLLER, s->mmio_size);
    sysbus_init_mmio(sbd, &s->mmio);

    /*
     * The backend machine does not instantiate the normal IMSIC device, but
     * PCI MSI capability gating still depends on the global MSI controller
     * availability bit. Mark it enabled here so backend PCI devices can expose
     * MSI and route their doorbell writes into this proxy controller.
     */
    msi_nonbroken = true;

    /*
     * PCI MSI/MSI-X writes are emitted on bus_master_as. In the devproxy
     * backend that address space is redirected to devproxy_dma_as, so mark the
     * IMSIC window as local to bypass the remote DMA proxy for MSI doorbells.
     */
    devproxy_dma_set_local_window(s->phys_addr, s->mmio_size);
}

static void devproxy_msi_controller_instance_init(Object *obj)
{
    DevProxyMSIControllerState *s = DEVPROXY_MSI_CONTROLLER(obj);

    s->socket_path = g_build_filename(g_get_user_runtime_dir(),
                                      "qemu-devproxy-msi.sock", NULL);
    s->mmio_size = DEVPROXY_MSI_CONTROLLER_DEFAULT_MMIO_SIZE;
}

static void devproxy_msi_controller_finalize(Object *obj)
{
    DevProxyMSIControllerState *s = DEVPROXY_MSI_CONTROLLER(obj);

    g_free(s->socket_path);
}

static const Property devproxy_msi_controller_properties[] = {
    DEFINE_PROP_STRING("socket", DevProxyMSIControllerState, socket_path),
    DEFINE_PROP_UINT64("phys-addr", DevProxyMSIControllerState, phys_addr, 0),
    DEFINE_PROP_UINT64("mmio-size", DevProxyMSIControllerState, mmio_size,
                       DEVPROXY_MSI_CONTROLLER_DEFAULT_MMIO_SIZE),
};

static void devproxy_msi_controller_class_init(ObjectClass *klass,
                                               const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = devproxy_msi_controller_realize;
    dc->desc = "Devproxy backend MSI controller";
    device_class_set_props(dc, devproxy_msi_controller_properties);
}

static const TypeInfo devproxy_msi_controller_info = {
    .name = TYPE_DEVPROXY_MSI_CONTROLLER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DevProxyMSIControllerState),
    .instance_init = devproxy_msi_controller_instance_init,
    .instance_finalize = devproxy_msi_controller_finalize,
    .class_init = devproxy_msi_controller_class_init,
};

static void devproxy_msi_controller_register_types(void)
{
    type_register_static(&devproxy_msi_controller_info);
}

type_init(devproxy_msi_controller_register_types)
