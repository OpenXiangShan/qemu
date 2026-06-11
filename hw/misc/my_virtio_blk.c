#include "qemu/osdep.h"
#include "qom/object.h"
#include "qemu/typedefs.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/irq.h"
#include "hw/virtio/virtio-mmio.h"
#include "hw/sysbus.h"
#include "system/block-backend.h"
#include "block/block.h"
#include "virtio_wrapper.h"
#include "hw/misc/my_virtio.h"
#include "block/block.h"

struct myBlkRequest {
    uint64_t sector;
    void *buf;
    int len;
    uint8_t flags;
    void *priv;
};

struct MyVirtioStateBlk {
    /*< private >*/
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    qemu_irq irq;

    BlockBackend *blk;

    virtio_handle_t handle;
};

#define TYPE_MY_VIRTIO_BLK "my-virtio-blk"
OBJECT_DECLARE_SIMPLE_TYPE(MyVirtioStateBlk, MY_VIRTIO_BLK)

static hwaddr base;
static int len;

static void my_blk_rw_bh(void *opaque)
{
    MyVirtioStateBlk *s = opaque;

    virtio_process_req(s->handle);
}

static uint64_t my_virtio_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    MyVirtioStateBlk *s = opaque;
    uint32_t val;

    virtio_mmio_read(s->handle, base + offset, &val, size);
//    printf("%s offset:0x%lx size:%d value:0x%x\n", __FUNCTION__, offset, size, val);
    return (uint64_t)val;
}

static void my_virtio_mmio_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size)
{
    MyVirtioStateBlk *s = opaque;
    AioContext *ctx = blk_get_aio_context(s->blk);
    int is_doorbell = 0;

//    printf("%s offset:0x%lx size:%d value:0x%lx\n", __FUNCTION__, offset, size, value);
    virtio_mmio_write(s->handle, base + offset, (uint32_t)value, size, &is_doorbell);

    if (is_doorbell)
        aio_bh_schedule_oneshot(ctx, my_blk_rw_bh, s);
}

static const MemoryRegionOps my_virtio_blk_mmio_ops = {
    .read = my_virtio_mmio_read,
    .write = my_virtio_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};
typedef struct MyVirtioStateBlk MyVirtioStateBlk;

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

    //printf("%s gpa:0x%lx dst:0x%p len:0x%x\n", __FUNCTION__, gpa, dst, len);

    return len;
}

static int my_guest_memory_write(uint64_t gpa, void *src, uint32_t len)
{
    //printf("%s gpa:0x%lx dst:0x%px len:0x%x -- write 0x%lx to 0x%lx\n", __FUNCTION__, gpa, src, len, *(uint64_t *)src, gpa);

    cpu_physical_memory_write(gpa, src, len);

    return len;
}

static int my_get_blk_capacity(void *priv)
{
    MyVirtioStateBlk *s = (MyVirtioStateBlk *)priv;

    return blk_getlength(s->blk) / 512;
}

static int my_submit_blk_io(uint64_t sector, void *buf, int len, uint8_t flags, void *priv)
{
    MyVirtioStateBlk *s = (MyVirtioStateBlk *)priv;

    if (flags == MY_BLK_REQ_READ)
        blk_pread(s->blk, (int64_t)sector * 512, len, buf, 0);
    else
        blk_pwrite(s->blk, (int64_t)sector * 512, len, buf, 0);

    return 0;
}

static int my_set_irq(void *priv)
{
    MyVirtioStateBlk *s = (MyVirtioStateBlk *)priv;

    qemu_irq_pulse(s->irq);

    return 0;
}

static struct libvirtio_ops ops = {
    .vprint = vprintf,
    .mm_alloc = my_alloc,
    .mm_free = my_free,
    .guest_mem_read = my_guest_memory_read,
    .guest_mem_write = my_guest_memory_write,
    .set_irq = my_set_irq,
    .blk_ops = {
        .submit_blk_io = my_submit_blk_io,
        .get_blk_capacity = my_get_blk_capacity,
    },
};

void my_virtio_blk_create(hwaddr start, hwaddr size, qemu_irq irq)
{
    MyVirtioStateBlk *s = MY_VIRTIO_BLK(qdev_new("my-virtio-blk"));

    base = start;
    len = size;

    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(s), 0, base);
    //sysbus_create_simple("my-virtio", start, 0);

    sysbus_connect_irq(SYS_BUS_DEVICE(s), 0, irq);
}

static void my_virtio_blk_realize(DeviceState *dev, Error **errp)
{
    MyVirtioStateBlk *s = MY_VIRTIO_BLK(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);
    uint64_t perm = BLK_PERM_CONSISTENT_READ;
    int ret;

    memory_region_init_io(&s->iomem, OBJECT(dev), &my_virtio_blk_mmio_ops, s,
        "my-virtio-blk-regs", 0x1000);

    sysbus_init_mmio(sbd, &s->iomem);

    sysbus_init_irq(sbd, &s->irq);

    s->blk = blk_by_name("my-virtio-blk");
    if (!s->blk) {
        printf("my-virtio-blk not found\n");
        return;
    }
    if (blk_supports_write_perm(s->blk)) {
        perm |= BLK_PERM_WRITE;
    }
    ret = blk_set_perm(s->blk, perm, BLK_PERM_ALL, errp);
    if (ret < 0)
        return;

    s->handle = virtio_mmio_create(VIRTIO_EMU_NAME_BLK, base, len, &ops, (void *)s);
    if (s->handle)
        return;
}

static void my_virtio_blk_unrealize(DeviceState *dev)
{

}

static void my_virtio_blk_class_init(ObjectClass *klass, const void* data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = my_virtio_blk_realize;
    dc->unrealize = my_virtio_blk_unrealize;
}

static const TypeInfo my_virtio_blk_info = {
    .name = TYPE_MY_VIRTIO_BLK,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MyVirtioStateBlk),
    .class_init = my_virtio_blk_class_init,
};

static void my_virtio_blk_types(void)
{
    type_register_static(&my_virtio_blk_info);
}

type_init(my_virtio_blk_types);
