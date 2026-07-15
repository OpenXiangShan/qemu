/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"

#include "devproxy-ipc.h"
#include "hw/irq.h"
#include "hw/misc/devproxy-pcie-ecam-proxy.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/bswap.h"
#include "qapi/error.h"
#include "system/address-spaces.h"
#include "system/cpus.h"

struct DevProxyPcieEcamProxyState {
    SysBusDevice parent_obj;

    MemoryRegion ecam;
    MemoryRegion mmio;
    char *socket_path;
    char *irq_socket_path;
    char *msi_socket_path;
    uint64_t ecam_base;
    uint64_t ecam_size;
    uint64_t mmio_base;
    uint64_t mmio_size;
    uint64_t msi_base;
    uint64_t msi_size;
    void *mmio_client;
    void *irq_server;
    void *msi_server;
    qemu_irq irq_out;
};

static void devproxy_pcie_ecam_proxy_set_irq(void *opaque, uint32_t irq,
                                             bool level)
{
    DevProxyPcieEcamProxyState *s = opaque;

    if (!level) {
        return;
    }

    bql_lock();
    qemu_irq_pulse(s->irq_out);
    bql_unlock();
}

static int devproxy_pcie_ecam_proxy_notify_msi(void *opaque,
                                               uint64_t phys_addr,
                                               uint32_t data,
                                               uint32_t size)
{
    DevProxyPcieEcamProxyState *s = opaque;
    uint32_t le_data = cpu_to_le32(data);
    MemTxResult ret;

    if (size != 4 || phys_addr < s->msi_base ||
        phys_addr - s->msi_base >= s->msi_size) {
        return -EINVAL;
    }

    bql_lock();
    ret = address_space_write(&address_space_memory, phys_addr,
                              MEMTXATTRS_UNSPECIFIED, &le_data, size);
    bql_unlock();

    if (ret == MEMTX_OK) {
        return 0;
    }

    return ret == MEMTX_DECODE_ERROR ? -ENODEV : -EIO;
}

static uint64_t devproxy_pcie_ecam_proxy_read_common(
    DevProxyPcieEcamProxyState *s, hwaddr base, hwaddr offset,
    unsigned size, const char *name)
{
    uint64_t data;
    int ret;

    bql_unlock();
    ret = devproxy_mmio_client_read(s->mmio_client,
                                    current_cpu ? current_cpu->cpu_index : 0,
                                    size, base + offset, &data);
    bql_lock();
    if (ret < 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "devproxy-pcie-ecam-proxy: %s read IPC failed: %s\n",
                      name, strerror(-ret));
        return MAKE_64BIT_MASK(0, size * 8);
    }

    return data;
}

static void devproxy_pcie_ecam_proxy_write_common(
    DevProxyPcieEcamProxyState *s, hwaddr base, hwaddr offset,
    uint64_t value, unsigned size, const char *name)
{
    int ret;

    bql_unlock();
    ret = devproxy_mmio_client_write(s->mmio_client,
                                     current_cpu ? current_cpu->cpu_index : 0,
                                     size, base + offset, value);
    bql_lock();
    if (ret < 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "devproxy-pcie-ecam-proxy: %s write IPC failed: %s\n",
                      name, strerror(-ret));
        return;
    }
}

static uint64_t devproxy_pcie_ecam_proxy_ecam_read(void *opaque,
                                                   hwaddr offset,
                                                   unsigned size)
{
    DevProxyPcieEcamProxyState *s = opaque;

    return devproxy_pcie_ecam_proxy_read_common(s, s->ecam_base, offset, size,
                                                "pcie-ecam");
}

static void devproxy_pcie_ecam_proxy_ecam_write(void *opaque, hwaddr offset,
                                                uint64_t value, unsigned size)
{
    DevProxyPcieEcamProxyState *s = opaque;

    devproxy_pcie_ecam_proxy_write_common(s, s->ecam_base, offset, value,
                                          size, "pcie-ecam");
}

static uint64_t devproxy_pcie_ecam_proxy_mmio_read(void *opaque,
                                                   hwaddr offset,
                                                   unsigned size)
{
    DevProxyPcieEcamProxyState *s = opaque;

    return devproxy_pcie_ecam_proxy_read_common(s, s->mmio_base, offset, size,
                                                "pcie-mmio");
}

static void devproxy_pcie_ecam_proxy_mmio_write(void *opaque, hwaddr offset,
                                                uint64_t value, unsigned size)
{
    DevProxyPcieEcamProxyState *s = opaque;

    devproxy_pcie_ecam_proxy_write_common(s, s->mmio_base, offset, value,
                                          size, "pcie-mmio");
}

static const MemoryRegionOps devproxy_pcie_ecam_proxy_ecam_ops = {
    .read = devproxy_pcie_ecam_proxy_ecam_read,
    .write = devproxy_pcie_ecam_proxy_ecam_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps devproxy_pcie_ecam_proxy_mmio_ops = {
    .read = devproxy_pcie_ecam_proxy_mmio_read,
    .write = devproxy_pcie_ecam_proxy_mmio_write,
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

static void devproxy_pcie_ecam_proxy_realize(DeviceState *dev, Error **errp)
{
    DevProxyPcieEcamProxyState *s = DEVPROXY_PCIE_ECAM_PROXY(dev);

    if (!s->socket_path || !s->socket_path[0]) {
        error_setg(errp, "socket property must be set");
        return;
    }
    if (!s->ecam_size || !s->mmio_size) {
        error_setg(errp, "ecam-size and mmio-size must be non-zero");
        return;
    }
    if (s->msi_socket_path && s->msi_socket_path[0] &&
        (!s->msi_size || !s->msi_base)) {
        error_setg(errp, "msi-base and msi-size must be set when msi-socket"
                   " is enabled");
        return;
    }

    s->mmio_client = devproxy_mmio_client_create(s->socket_path);
    if (!s->mmio_client) {
        error_setg(errp, "failed to initialize devproxy IPC client for %s",
                   s->socket_path);
        return;
    }

    if (s->irq_socket_path && s->irq_socket_path[0]) {
        int ret;

        s->irq_server = devproxy_irq_server_create(s->irq_socket_path);
        if (!s->irq_server) {
            error_setg(errp, "failed to initialize devproxy IRQ server for %s",
                       s->irq_socket_path);
            return;
        }

        ret = devproxy_irq_server_start(s->irq_server,
                                        devproxy_pcie_ecam_proxy_set_irq, s);
        if (ret < 0) {
            devproxy_irq_server_destroy(s->irq_server);
            s->irq_server = NULL;
            error_setg(errp, "failed to start devproxy IRQ server on %s: %s",
                       s->irq_socket_path, strerror(-ret));
            return;
        }
    }

    if (s->msi_socket_path && s->msi_socket_path[0]) {
        int ret;

        s->msi_server = devproxy_msi_server_create(s->msi_socket_path);
        if (!s->msi_server) {
            error_setg(errp, "failed to initialize devproxy MSI server for %s",
                       s->msi_socket_path);
            return;
        }

        ret = devproxy_msi_server_start(s->msi_server,
                                        devproxy_pcie_ecam_proxy_notify_msi, s);
        if (ret < 0) {
            devproxy_msi_server_destroy(s->msi_server);
            s->msi_server = NULL;
            error_setg(errp, "failed to start devproxy MSI server on %s: %s",
                       s->msi_socket_path, strerror(-ret));
            return;
        }
    }

    memory_region_init_io(&s->ecam, OBJECT(s),
                          &devproxy_pcie_ecam_proxy_ecam_ops, s,
                          "devproxy-pcie-ecam-proxy-ecam", s->ecam_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->ecam);

    memory_region_init_io(&s->mmio, OBJECT(s),
                          &devproxy_pcie_ecam_proxy_mmio_ops, s,
                          "devproxy-pcie-ecam-proxy-mmio", s->mmio_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq_out);
}

static void devproxy_pcie_ecam_proxy_unrealize(DeviceState *dev)
{
    DevProxyPcieEcamProxyState *s = DEVPROXY_PCIE_ECAM_PROXY(dev);

    if (s->irq_server) {
        devproxy_irq_server_destroy(s->irq_server);
        s->irq_server = NULL;
    }
    if (s->msi_server) {
        devproxy_msi_server_destroy(s->msi_server);
        s->msi_server = NULL;
    }

    if (s->mmio_client) {
        devproxy_mmio_client_destroy(s->mmio_client);
        s->mmio_client = NULL;
    }
}

static const Property devproxy_pcie_ecam_proxy_properties[] = {
    DEFINE_PROP_STRING("socket", DevProxyPcieEcamProxyState, socket_path),
    DEFINE_PROP_STRING("irq-socket", DevProxyPcieEcamProxyState,
                       irq_socket_path),
    DEFINE_PROP_STRING("msi-socket", DevProxyPcieEcamProxyState,
                       msi_socket_path),
    DEFINE_PROP_UINT64("ecam-base", DevProxyPcieEcamProxyState, ecam_base, 0),
    DEFINE_PROP_UINT64("ecam-size", DevProxyPcieEcamProxyState, ecam_size, 0),
    DEFINE_PROP_UINT64("mmio-base", DevProxyPcieEcamProxyState, mmio_base, 0),
    DEFINE_PROP_UINT64("mmio-size", DevProxyPcieEcamProxyState, mmio_size, 0),
    DEFINE_PROP_UINT64("msi-base", DevProxyPcieEcamProxyState, msi_base, 0),
    DEFINE_PROP_UINT64("msi-size", DevProxyPcieEcamProxyState, msi_size, 0),
};

static void devproxy_pcie_ecam_proxy_finalize(Object *obj)
{
    DevProxyPcieEcamProxyState *s = DEVPROXY_PCIE_ECAM_PROXY(obj);

    g_free(s->socket_path);
    g_free(s->irq_socket_path);
    g_free(s->msi_socket_path);
}

static void devproxy_pcie_ecam_proxy_class_init(ObjectClass *klass,
                                                const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, devproxy_pcie_ecam_proxy_properties);
    dc->realize = devproxy_pcie_ecam_proxy_realize;
    dc->unrealize = devproxy_pcie_ecam_proxy_unrealize;
}

static const TypeInfo devproxy_pcie_ecam_proxy_info = {
    .name = TYPE_DEVPROXY_PCIE_ECAM_PROXY,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DevProxyPcieEcamProxyState),
    .class_init = devproxy_pcie_ecam_proxy_class_init,
    .instance_finalize = devproxy_pcie_ecam_proxy_finalize,
};

static void devproxy_pcie_ecam_proxy_register_types(void)
{
    type_register_static(&devproxy_pcie_ecam_proxy_info);
}

type_init(devproxy_pcie_ecam_proxy_register_types)
