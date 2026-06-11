#include "qemu/osdep.h"
#include "qom/object.h"
#include "qemu/typedefs.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/irq.h"
#include "hw/virtio/virtio-mmio.h"
#include "hw/sysbus.h"
#include "virtio_wrapper.h"
#include "hw/misc/my_virtio.h"
#include "qemu/main-loop.h"

#include "tap-linux.h"
#include "qemu/cutils.h"
#include <net/if.h>
#include <sys/ioctl.h>

struct MyVirtioStateNet {
    /*< private >*/
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    qemu_irq irq;

    virtio_handle_t handle;
    int tap_fd;
    uint8_t buf[4096 + 65536];
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

    //printf("%s offset:0x%lx size:%d value:0x%lx\n", __FUNCTION__, offset, size, value);
    virtio_mmio_write(s->handle, base + offset, (uint32_t)value, size, &is_doorbell);
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

static int my_guest_memory_read(uint64_t gpa, void *dst, uint32_t len)
{
    cpu_physical_memory_read(gpa, dst, len);

    //printf("%s gpa:0x%lx dst:0x%p len:0x%x -- read 0x%lx : 0x%lx\n", __FUNCTION__, gpa, dst, len, gpa, *(uint64_t *)dst);

    return len;
}

static int my_guest_memory_write(uint64_t gpa, void *src, uint32_t len)
{
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

static void my_receive_callback(void *opaque)
{
    int len = 0;
    MyVirtioStateNet *s = opaque;

    len = read(s->tap_fd, s->buf, sizeof(s->buf));
    if (len > 0) {
        virtio_receive(s->handle, s->buf, len);
    }
}

static void my_virtio_net_set_mac(uint8_t *mac, void *priv)
{
    mac[0] = 0x52;
    mac[1] = 0x54;
    mac[2] = 0x00;
    mac[3] = 0x12;
    mac[4] = 0x34;
    mac[5] = 0x56;
}

static int my_virtio_net_read_tap(uint64_t offset, void *buf, int len, void *priv)
{
    MyVirtioStateNet *s = priv;

    return read(s->tap_fd, buf, len);
}

static int my_virtio_net_write_tap(uint64_t offset, void *buf, int len, void *priv)
{
    MyVirtioStateNet *s = priv;

    return write(s->tap_fd, buf, len);
}

static int my_virtio_net_ctrl_mq(int vq_pairs, void *priv)
{
    MyVirtioStateNet *s = priv;

    qemu_set_fd_handler(s->tap_fd, my_receive_callback, NULL, s);

    return 0;
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
        .read_tap = my_virtio_net_read_tap,
        .write_tap = my_virtio_net_write_tap,
    },
};

void my_virtio_net_create(hwaddr start, hwaddr size, qemu_irq irq)
{
    MyVirtioStateNet *s = MY_VIRTIO_NET(qdev_new("my-virtio-net"));

    base = start;
    len = size;

    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(s), 0, base);
    //sysbus_create_simple("my-virtio", start, 0);

    sysbus_connect_irq(SYS_BUS_DEVICE(s), 0, irq);
}

static void my_virtio_net_realize(DeviceState *dev, Error **errp)
{
    struct ifreq ifr = { 0 };
    MyVirtioStateNet *s = MY_VIRTIO_NET(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);

    memory_region_init_io(&s->iomem, OBJECT(dev), &my_virtio_net_mmio_ops, s,
        "my-virtio-net-regs", 0x1000);

    sysbus_init_mmio(sbd, &s->iomem);

    sysbus_init_irq(sbd, &s->irq);

    s->handle = virtio_mmio_create(VIRTIO_EMU_NAME_NET, base, len, &ops, (void *)s);

    s->tap_fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
    if (s->tap_fd < 0) {
        printf("open %s failed\n", "/dev/net/tun");
        return;
    }

    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    strncpy(ifr.ifr_name, "tap0", IFNAMSIZ);

    if (ioctl(s->tap_fd, TUNSETIFF, (void *)&ifr) < 0) {
        printf("ioctl TUNSETIFF failed\n");
        close(s->tap_fd);
        return;
    }
}

static void my_virtio_net_unrealize(DeviceState *dev)
{

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
