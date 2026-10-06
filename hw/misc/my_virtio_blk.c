#include "qemu/osdep.h"
#include "qom/object.h"
#include "qemu/typedefs.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "hw/irq.h"
#include "hw/virtio/virtio-mmio.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "virtio_wrapper.h"
#include "virtio_backend.h"
#include "hw/misc/my_virtio.h"
#ifdef CONFIG_LIBQEMU
#include "system/system.h"
#endif

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

    virtio_handle_t handle;
    virtio_backend_handle_t backend;
    QEMUBH *rw_bh;
#ifdef CONFIG_LIBQEMU
    Notifier exit_notifier;
    bool exit_registered;
#endif
    uint64_t mmio_base;
    uint32_t mmio_size;
    char *image_path;
    bool packed;
    void *iommu;
    uint32_t endpoint_id;
};

#define TYPE_MY_VIRTIO_BLK "my-virtio-blk"
OBJECT_DECLARE_SIMPLE_TYPE(MyVirtioStateBlk, MY_VIRTIO_BLK)

static void my_blk_rw_bh(void *opaque)
{
    MyVirtioStateBlk *s = opaque;

    virtio_process_req(s->handle);
}

static uint64_t my_virtio_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    MyVirtioStateBlk *s = opaque;
    uint32_t val;

    virtio_mmio_read(s->handle, s->mmio_base + offset, &val, size);
//    printf("%s offset:0x%lx size:%d value:0x%x\n", __FUNCTION__, offset, size, val);
    return (uint64_t)val;
}

static void my_virtio_mmio_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size)
{
    MyVirtioStateBlk *s = opaque;
    int is_doorbell = 0;
    int ret;

//    printf("%s offset:0x%lx size:%d value:0x%lx\n", __FUNCTION__, offset, size, value);
    ret = virtio_mmio_write(s->handle, s->mmio_base + offset,
                           (uint32_t)value, size, &is_doorbell);

    if (!ret && is_doorbell) {
        qemu_bh_schedule(s->rw_bh);
    }
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

static int my_guest_memory_read(uint64_t gpa, void *dst, uint32_t len,
                                void *priv)
{
    (void)priv;
    cpu_physical_memory_read(gpa, dst, len);

    //printf("%s gpa:0x%lx dst:0x%p len:0x%x\n", __FUNCTION__, gpa, dst, len);

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

static int my_get_blk_capacity(void *priv)
{
    MyVirtioStateBlk *s = (MyVirtioStateBlk *)priv;
    struct virtio_backend_info info;

    if (virtio_backend_get_info(s->backend, &info) < 0 ||
        info.type != VIRTIO_BACKEND_BLK) {
        return 0;
    }

    return info.u.blk.capacity < 0 ? 0 : info.u.blk.capacity;
}

static int my_submit_blk_io(uint64_t sector, void *buf, int len, uint8_t flags, void *priv)
{
    MyVirtioStateBlk *s = (MyVirtioStateBlk *)priv;
    struct virtio_backend_io io = {
        .type = VIRTIO_BACKEND_IO_BLK,
        .buf = buf,
        .len = len,
        .cap = len,
        .u.blk.sector = sector,
    };

    if (flags == MY_BLK_REQ_READ) {
        io.u.blk.op = VIRTIO_BACKEND_BLK_READ;
        return virtio_backend_read(s->backend, &io);
    } else if (flags == MY_BLK_REQ_WRITE) {
        io.u.blk.op = VIRTIO_BACKEND_BLK_WRITE;
        return virtio_backend_write(s->backend, &io);
    }

    io.u.blk.op = VIRTIO_BACKEND_BLK_FLUSH;
    return virtio_backend_write(s->backend, &io);
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

void my_virtio_blk_create(hwaddr start, hwaddr size, qemu_irq irq,
                          const char *image_path, bool packed,
                          const MyVirtioDmaConfig *dma)
{
    MyVirtioStateBlk *s = MY_VIRTIO_BLK(qdev_new("my-virtio-blk"));

    qdev_prop_set_uint64(DEVICE(s), "mmio-base", start);
    qdev_prop_set_uint32(DEVICE(s), "mmio-size", size);
    qdev_prop_set_string(DEVICE(s), "image-path",
                         image_path && *image_path ? image_path : "disk.img");
    qdev_prop_set_bit(DEVICE(s), "packed", packed);
    s->iommu = dma ? dma->iommu : NULL;
    s->endpoint_id = dma ? dma->endpoint_id : 0;

    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(s), 0, start);
    //sysbus_create_simple("my-virtio", start, 0);

    sysbus_connect_irq(SYS_BUS_DEVICE(s), 0, irq);
}

static void my_virtio_blk_release(MyVirtioStateBlk *s)
{
    if (s->rw_bh) {
        qemu_bh_delete(s->rw_bh);
        s->rw_bh = NULL;
    }
    if (s->backend) {
        virtio_backend_destroy(s->backend);
        s->backend = NULL;
    }
}

#ifdef CONFIG_LIBQEMU
static void my_virtio_blk_exit(Notifier *notifier, void *data)
{
    MyVirtioStateBlk *s = container_of(notifier, MyVirtioStateBlk,
                                     exit_notifier);
    my_virtio_blk_release(s);
}
#endif

static void my_virtio_blk_realize(DeviceState *dev, Error **errp)
{
    MyVirtioStateBlk *s = MY_VIRTIO_BLK(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);
    struct virtio_mmio_options opts = {
        .packed = s->packed,
        .iommu = s->iommu,
        .endpoint_id = s->endpoint_id,
    };
    struct virtio_backend_config backend_config = {
        .type = VIRTIO_BACKEND_BLK,
    };

    memory_region_init_io(&s->iomem, OBJECT(dev), &my_virtio_blk_mmio_ops, s,
        "my-virtio-blk-regs", s->mmio_size);

    sysbus_init_mmio(sbd, &s->iomem);

    sysbus_init_irq(sbd, &s->irq);

    s->rw_bh = qemu_bh_new(my_blk_rw_bh, s);

    backend_config.u.blk.image_path = s->image_path;
    s->backend = virtio_backend_create(&backend_config);
    if (!s->backend) {
        error_setg(errp, "failed to create my-virtio-blk backend image=%s",
                   s->image_path ? s->image_path : "");
        return;
    }

    s->handle = virtio_mmio_create_ex(VIRTIO_EMU_NAME_BLK,
                                      s->mmio_base, s->mmio_size, &ops,
                                      (void *)s, &opts);
    if (s->handle) {
#ifdef CONFIG_LIBQEMU
        s->exit_notifier.notify = my_virtio_blk_exit;
        qemu_add_exit_notifier(&s->exit_notifier);
        s->exit_registered = true;
#endif
        return;
    }

    error_setg(errp, "failed to create my-virtio-blk protocol device");
}

static void my_virtio_blk_unrealize(DeviceState *dev)
{
    MyVirtioStateBlk *s = MY_VIRTIO_BLK(dev);

#ifdef CONFIG_LIBQEMU
    if (s->exit_registered) {
        qemu_remove_exit_notifier(&s->exit_notifier);
        s->exit_registered = false;
    }
#endif
    my_virtio_blk_release(s);
}

static const Property my_virtio_blk_properties[] = {
    DEFINE_PROP_UINT64("mmio-base", MyVirtioStateBlk, mmio_base, 0),
    DEFINE_PROP_UINT32("mmio-size", MyVirtioStateBlk, mmio_size, 0x1000),
    DEFINE_PROP_STRING("image-path", MyVirtioStateBlk, image_path),
    DEFINE_PROP_BOOL("packed", MyVirtioStateBlk, packed, false),
};

static void my_virtio_blk_class_init(ObjectClass *klass, const void* data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, my_virtio_blk_properties);
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
