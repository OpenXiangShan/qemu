#include "qemu/osdep.h"
#include "qom/object.h"
#include "qemu/typedefs.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/irq.h"
#include "hw/virtio/virtio-mmio.h"
#include "hw/sysbus.h"
#include "virtio_wrapper.h"
#include "virtio_backend.h"
#include "hw/misc/my_virtio.h"
#include "qemu/main-loop.h"

#include "qemu/cutils.h"

struct MyVirtioStateNet {
    /*< private >*/
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    qemu_irq irq;

    virtio_handle_t handle;
    virtio_backend_handle_t backend;
    QEMUBH *rx_bh;
    char *hostfwd;
    char *network;
    char *netmask;
    char *host_ip;
    char *dhcp_start;
    char *dns_ip;
    uint8_t mac[6];
};

#define TYPE_MY_VIRTIO_NET "my-virtio-net"
OBJECT_DECLARE_SIMPLE_TYPE(MyVirtioStateNet, MY_VIRTIO_NET)

static hwaddr base;
static int len;


static uint64_t my_virtio_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    MyVirtioStateNet *s = (MyVirtioStateNet *)opaque;
    uint32_t val;

    virtio_mmio_read(s->handle, base + offset, &val, size);
    //printf("%s offset:0x%lx size:%d value:0x%x\n", __FUNCTION__, offset, size, val);
    return (uint64_t)val;
}

static void my_virtio_mmio_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size)
{
    MyVirtioStateNet *s = (MyVirtioStateNet *)opaque;
    int is_doorbell = 0;
    int ret;

    //printf("%s offset:0x%lx size:%d value:0x%lx\n", __FUNCTION__, offset, size, value);
    ret = virtio_mmio_write(s->handle, base + offset, (uint32_t)value, size,
                            &is_doorbell);
    if (!ret && is_doorbell) {
        qemu_bh_schedule(s->rx_bh);
    }
}

static const MemoryRegionOps my_virtio_net_mmio_ops = {
    .read = my_virtio_mmio_read,
    .write = my_virtio_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};
typedef struct MyVirtioStateNet MyVirtioStateNet;

static uint64_t my_alloc(int size)
{
    return (uint64_t)g_malloc0(size);
}

static void my_free(uint64_t addr, int size)
{
    g_free((void *)addr);
}

static int my_guest_memory_read(uint64_t gpa, void *dst, uint32_t len,
                                void *priv)
{
    (void)priv;
    cpu_physical_memory_read(gpa, dst, len);

    //printf("%s gpa:0x%lx dst:0x%p len:0x%x -- read 0x%lx : 0x%lx\n", __FUNCTION__, gpa, dst, len, gpa, *(uint64_t *)dst);

    return len;
}

static int my_guest_memory_write(uint64_t gpa, void *src, uint32_t len,
                                 void *priv)
{
    (void)priv;
    //printf("%s gpa:0x%lx dst:0x%px len:0x%x -- write 0x%lx to 0x%lx\n", __FUNCTION__, gpa, src, len, *(uint64_t *)src, gpa);

    cpu_physical_memory_write(gpa, src, len);

    return len;
}

static int my_set_irq(void *priv)
{
    MyVirtioStateNet *s = (MyVirtioStateNet *)priv;

    qemu_irq_pulse(s->irq);

    return 0;
}

static void my_virtio_net_drain_rx(MyVirtioStateNet *s)
{
    for (;;) {
        uint8_t buf[65536];
        struct virtio_backend_io io = {
            .type = VIRTIO_BACKEND_IO_PACKET,
            .buf = buf,
            .cap = sizeof(buf),
        };
        int ret;

        ret = virtio_backend_read(s->backend, &io);
        if (ret < 0) {
            break;
        }

        ret = virtio_receive(s->handle, io.buf, io.len);
        if (ret) {
            virtio_backend_read_done(s->backend, io.token, 0);
            break;
        }

        virtio_backend_read_done(s->backend, io.token, 1);
    }
}

static void my_virtio_net_rx_bh(void *opaque)
{
    MyVirtioStateNet *s = opaque;

    virtio_process_req(s->handle);
    my_virtio_net_drain_rx(s);
}

static void my_virtio_net_set_mac(uint8_t *mac, void *priv)
{
    MyVirtioStateNet *s = priv;

    memcpy(mac, s->mac, sizeof(s->mac));
}

static int my_virtio_net_write_tap(uint64_t offset, void *buf, int len, void *priv)
{
    MyVirtioStateNet *s = priv;
    struct virtio_backend_io io = {
        .type = VIRTIO_BACKEND_IO_PACKET,
        .buf = buf,
        .len = len,
    };

    return virtio_backend_write(s->backend, &io);
}

static int my_virtio_net_ctrl_mq(int vq_pairs, void *priv)
{
    return (vq_pairs == 1) ? 0 : -1;
}

static void my_virtio_net_backend_event(void *opaque,
                                        virtio_backend_handle_t handle,
                                        unsigned int events)
{
    MyVirtioStateNet *s = opaque;

    if (events & VIRTIO_BACKEND_EVENT_READABLE) {
        qemu_bh_schedule(s->rx_bh);
    }
}

static struct libvirtio_ops ops = {
    .vprint = vprintf,
    .mm_alloc = my_alloc,
    .mm_free = my_free,
    .guest_mem_read = my_guest_memory_read,
    .guest_mem_write = my_guest_memory_write,
    .set_irq = my_set_irq,
    .net_ops = {
        .set_mac = my_virtio_net_set_mac,
        .ctrl_mq = my_virtio_net_ctrl_mq,
        .read_tap = NULL,
        .write_tap = my_virtio_net_write_tap,
    },
};

void my_virtio_net_create(hwaddr start, hwaddr size, qemu_irq irq,
                          const char *hostfwd, const char *network,
                          const char *netmask, const char *host_ip,
                          const char *dhcp_start, const char *dns_ip)
{
    MyVirtioStateNet *s = MY_VIRTIO_NET(qdev_new("my-virtio-net"));

    base = start;
    len = size;
    s->hostfwd = g_strdup(hostfwd ? hostfwd : "");
    s->network = g_strdup(network ? network : "");
    s->netmask = g_strdup(netmask ? netmask : "");
    s->host_ip = g_strdup(host_ip ? host_ip : "");
    s->dhcp_start = g_strdup(dhcp_start ? dhcp_start : "");
    s->dns_ip = g_strdup(dns_ip ? dns_ip : "");

    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(s), 0, base);
    //sysbus_create_simple("my-virtio", start, 0);

    sysbus_connect_irq(SYS_BUS_DEVICE(s), 0, irq);
}

static void my_virtio_net_realize(DeviceState *dev, Error **errp)
{
    MyVirtioStateNet *s = MY_VIRTIO_NET(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);
    struct virtio_backend_callbacks backend_callbacks = {
        .event = my_virtio_net_backend_event,
    };
    struct virtio_backend_config backend_config = {
        .type = VIRTIO_BACKEND_NET,
        .callbacks = &backend_callbacks,
        .callback_opaque = s,
    };

    memory_region_init_io(&s->iomem, OBJECT(dev), &my_virtio_net_mmio_ops, s,
        "my-virtio-net-regs", 0x1000);

    sysbus_init_mmio(sbd, &s->iomem);

    sysbus_init_irq(sbd, &s->irq);

    s->mac[0] = 0x52;
    s->mac[1] = 0x54;
    s->mac[2] = 0x00;
    s->mac[3] = 0x12;
    s->mac[4] = 0x34;
    s->mac[5] = 0x56;

    s->rx_bh = qemu_bh_new(my_virtio_net_rx_bh, s);
    backend_config.u.net.hostfwd = s->hostfwd;
    backend_config.u.net.mac = s->mac;
    backend_config.u.net.network = s->network;
    backend_config.u.net.netmask = s->netmask;
    backend_config.u.net.host_ip = s->host_ip;
    backend_config.u.net.dhcp_start = s->dhcp_start;
    backend_config.u.net.dns_ip = s->dns_ip;
    s->backend = virtio_backend_create(&backend_config);
    if (!s->backend) {
        error_setg(errp, "failed to create my-virtio-net backend");
        return;
    }

    s->handle = virtio_mmio_create(VIRTIO_EMU_NAME_NET, base, len, &ops, (void *)s);
    if (!s->handle) {
        error_setg(errp, "failed to create my-virtio-net protocol device");
        return;
    }
}

static void my_virtio_net_unrealize(DeviceState *dev)
{
    MyVirtioStateNet *s = MY_VIRTIO_NET(dev);

    if (s->rx_bh) {
        qemu_bh_delete(s->rx_bh);
        s->rx_bh = NULL;
    }
    virtio_backend_destroy(s->backend);
    s->backend = NULL;
    g_free(s->hostfwd);
    s->hostfwd = NULL;
    g_free(s->network);
    s->network = NULL;
    g_free(s->netmask);
    s->netmask = NULL;
    g_free(s->host_ip);
    s->host_ip = NULL;
    g_free(s->dhcp_start);
    s->dhcp_start = NULL;
    g_free(s->dns_ip);
    s->dns_ip = NULL;
}

static void my_virtio_net_class_init(ObjectClass *klass, const void* data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = my_virtio_net_realize;
    dc->unrealize = my_virtio_net_unrealize;
}

static const TypeInfo my_virtio_net_info = {
    .name = TYPE_MY_VIRTIO_NET,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MyVirtioStateNet),
    .class_init = my_virtio_net_class_init,
};

static void my_virtio_net_types(void)
{
    type_register_static(&my_virtio_net_info);
}

type_init(my_virtio_net_types);
