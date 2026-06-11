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
#include "system/system.h"

#include "chardev/char-fe.h"
#include "hw/qdev-properties.h"

#include "qemu/cutils.h"
#include <sys/ioctl.h>

struct MyVirtioStateConsole {
    /*< private >*/
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    qemu_irq irq;

    CharBackend chr;

    virtio_handle_t handle;
    GByteArray *rx_buf;
};

#define TYPE_MY_VIRTIO_CONSOLE "my-virtio-console"
OBJECT_DECLARE_SIMPLE_TYPE(MyVirtioStateConsole, MY_VIRTIO_CONSOLE)

static hwaddr base;
static int len;

static void my_virtio_console_drain_rx(MyVirtioStateConsole *s)
{
    int consumed;

    while (s->rx_buf->len) {
        consumed = virtio_receive(s->handle, s->rx_buf->data, s->rx_buf->len);
        if (consumed <= 0) {
            break;
        }
        g_byte_array_remove_range(s->rx_buf, 0, consumed);
    }
}

static int my_virtio_console_chr_can_read(void *opaque)
{
    MyVirtioStateConsole *s = opaque;

    return s->rx_buf->len ? 0 : 4096;
}

static void my_virtio_console_chr_read(void *opaque, const uint8_t *buf, int size)
{
    MyVirtioStateConsole *s = opaque;

    g_byte_array_append(s->rx_buf, buf, size);
    my_virtio_console_drain_rx(s);
}

static uint64_t my_virtio_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    MyVirtioStateConsole *s = opaque;
    uint32_t val;

    virtio_mmio_read(s->handle, base + offset, &val, size);
    //printf("%s offset:0x%lx size:%d value:0x%x\n", __FUNCTION__, offset, size, val);
    return (uint64_t)val;
}

static void my_virtio_mmio_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size)
{
    MyVirtioStateConsole *s = opaque;
    int is_doorbell = 0;

    //printf("%s offset:0x%lx size:%d value:0x%lx\n", __FUNCTION__, offset, size, value);
    virtio_mmio_write(s->handle, base + offset, (uint32_t)value, size, &is_doorbell);
    if (is_doorbell) {
        my_virtio_console_drain_rx(s);
        qemu_chr_fe_accept_input(&s->chr);
    }
}

static const MemoryRegionOps my_virtio_console_mmio_ops = {
    .read = my_virtio_mmio_read,
    .write = my_virtio_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};
typedef struct MyVirtioStateConsole MyVirtioStateConsole;

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
    MyVirtioStateConsole *s = (MyVirtioStateConsole *)priv;

    qemu_irq_pulse(s->irq);

    return 0;
}

static int my_virtio_console_send(void *buf, int len, void *priv)
{
    MyVirtioStateConsole *s = (MyVirtioStateConsole *)priv;

    qemu_chr_fe_write_all(&s->chr, (const uint8_t *)buf, len);

    return 0;
}

static struct libvirtio_ops ops = {
    .vprint = vprintf,
    .mm_alloc = my_alloc,
    .mm_free = my_free,
    .guest_mem_read = my_guest_memory_read,
    .guest_mem_write = my_guest_memory_write,
    .set_irq = my_set_irq,
    .console_ops = {
        .send = my_virtio_console_send,
    },
};

void my_virtio_console_create(hwaddr start, hwaddr size, qemu_irq irq)
{
    MyVirtioStateConsole *s = MY_VIRTIO_CONSOLE(qdev_new(TYPE_MY_VIRTIO_CONSOLE));

    base = start;
    len = size;

    qdev_prop_set_chr(DEVICE(s), "chardev", serial_hd(2));

    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(s), 0, base);
    //sysbus_create_simple("my-virtio", start, 0);

    sysbus_connect_irq(SYS_BUS_DEVICE(s), 0, irq);
}

static void my_virtio_console_realize(DeviceState *dev, Error **errp)
{
    MyVirtioStateConsole *s = MY_VIRTIO_CONSOLE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);

    memory_region_init_io(&s->iomem, OBJECT(dev), &my_virtio_console_mmio_ops, s,
        "my-virtio-console-regs", 0x1000);

    sysbus_init_mmio(sbd, &s->iomem);

    sysbus_init_irq(sbd, &s->irq);
    s->rx_buf = g_byte_array_new();

    s->handle = virtio_mmio_create(VIRTIO_EMU_NAME_CONSOLE, base, len, &ops, (void *)s);
    if (!s->handle)
        return;

    qemu_chr_fe_set_handlers(&s->chr,
                             my_virtio_console_chr_can_read,
                             my_virtio_console_chr_read,
                             NULL,
                             NULL,
                             s,
                             NULL,
                             true);
}

static void my_virtio_console_unrealize(DeviceState *dev)
{
    MyVirtioStateConsole *s = MY_VIRTIO_CONSOLE(dev);

    g_clear_pointer(&s->rx_buf, g_byte_array_unref);
}

static const Property my_virtio_console_properties[] = {
    DEFINE_PROP_CHR("chardev", MyVirtioStateConsole, chr),
};

static void my_virtio_console_class_init(ObjectClass *klass, const void* data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = my_virtio_console_realize;
    dc->unrealize = my_virtio_console_unrealize;
    device_class_set_props(dc, my_virtio_console_properties);
}

static const TypeInfo my_virtio_console_info = {
    .name = TYPE_MY_VIRTIO_CONSOLE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MyVirtioStateConsole),
    .class_init = my_virtio_console_class_init,
};

static void my_virtio_console_types(void)
{
    type_register_static(&my_virtio_console_info);
}

type_init(my_virtio_console_types);
