#include "qemu/osdep.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "system/address-spaces.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "virtio_wrapper.h"
#include "hw/misc/my_virtio.h"

typedef struct MyVirtioStateGenirq {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
    virtio_handle_t handle;
    QEMUBH *bh;
    hwaddr base;
    hwaddr size;
    bool packed;
    void *iommu;
    uint32_t endpoint_id;
} MyVirtioStateGenirq;

#define TYPE_MY_VIRTIO_GENIRQ "my-virtio-genirq"
OBJECT_DECLARE_SIMPLE_TYPE(MyVirtioStateGenirq, MY_VIRTIO_GENIRQ)

static uint64_t my_alloc(int size)
{
    return (uint64_t)(uintptr_t)g_malloc0(size);
}

static void my_free(uint64_t addr, int size)
{
    g_free((void *)(uintptr_t)addr);
}

static int my_guest_memory_read(uint64_t gpa, void *dst, uint32_t len,
                                void *priv)
{
    (void)priv;
    cpu_physical_memory_read(gpa, dst, len);
    return len;
}

static int my_guest_memory_write(uint64_t gpa, void *src, uint32_t len,
                                 void *priv)
{
    (void)priv;
    cpu_physical_memory_write(gpa, src, len);
    return len;
}

static int my_set_irq(void *priv)
{
    MyVirtioStateGenirq *s = priv;

    qemu_irq_pulse(s->irq);
    return 0;
}

static int my_send_msi(uint64_t addr, uint32_t data, void *priv)
{
    MemTxResult result;

    (void)priv;
    address_space_stl_le(&address_space_memory, addr, data,
                         MEMTXATTRS_UNSPECIFIED, &result);
    if (result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: MSI write failed addr=0x%" PRIx64 " data=0x%x\n",
                      __func__, addr, data);
        return -1;
    }

    return 0;
}

static struct libvirtio_ops ops = {
    .vprint = vprintf,
    .mm_alloc = my_alloc,
    .mm_free = my_free,
    .guest_mem_read = my_guest_memory_read,
    .guest_mem_write = my_guest_memory_write,
    .set_irq = my_set_irq,
    .genirq_ops = {
        .send_msi = my_send_msi,
    },
};

static uint64_t my_virtio_genirq_mmio_read(void *opaque, hwaddr offset,
                                           unsigned size)
{
    MyVirtioStateGenirq *s = opaque;
    uint32_t val = 0;

    virtio_mmio_read(s->handle, s->base + offset, &val, size);
    return val;
}

static void my_virtio_genirq_mmio_write(void *opaque, hwaddr offset,
                                        uint64_t value, unsigned size)
{
    MyVirtioStateGenirq *s = opaque;
    int is_doorbell = 0;
    int ret;

    ret = virtio_mmio_write(s->handle, s->base + offset, (uint32_t)value,
                            size, &is_doorbell);
    if (!ret && is_doorbell) {
        qemu_bh_schedule(s->bh);
    }
}

static const MemoryRegionOps my_virtio_genirq_mmio_ops = {
    .read = my_virtio_genirq_mmio_read,
    .write = my_virtio_genirq_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void my_virtio_genirq_bh(void *opaque)
{
    MyVirtioStateGenirq *s = opaque;

    virtio_process_req(s->handle);
}

void my_virtio_genirq_create(hwaddr start, hwaddr size, qemu_irq irq,
                             bool packed, const MyVirtioDmaConfig *dma)
{
    MyVirtioStateGenirq *s = MY_VIRTIO_GENIRQ(qdev_new(TYPE_MY_VIRTIO_GENIRQ));

    s->base = start;
    s->size = size;
    s->packed = packed;
    s->iommu = dma ? dma->iommu : NULL;
    s->endpoint_id = dma ? dma->endpoint_id : 0;
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(s), 0, start);
    sysbus_connect_irq(SYS_BUS_DEVICE(s), 0, irq);
}

static void my_virtio_genirq_realize(DeviceState *dev, Error **errp)
{
    MyVirtioStateGenirq *s = MY_VIRTIO_GENIRQ(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);
    struct virtio_mmio_options opts = {
        .packed = s->packed,
        .iommu = s->iommu,
        .endpoint_id = s->endpoint_id,
    };

    memory_region_init_io(&s->iomem, OBJECT(dev), &my_virtio_genirq_mmio_ops,
                          s, "my-virtio-genirq-regs", s->size);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->bh = qemu_bh_new(my_virtio_genirq_bh, s);
    s->handle = virtio_mmio_create_ex(VIRTIO_EMU_NAME_GENIRQ, s->base, s->size,
                                      &ops, s, &opts);
    if (!s->handle) {
        error_setg(errp, "failed to create my-virtio-genirq protocol device");
    }
}

static void my_virtio_genirq_unrealize(DeviceState *dev)
{
    MyVirtioStateGenirq *s = MY_VIRTIO_GENIRQ(dev);

    if (s->bh) {
        qemu_bh_delete(s->bh);
        s->bh = NULL;
    }
}

static void my_virtio_genirq_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = my_virtio_genirq_realize;
    dc->unrealize = my_virtio_genirq_unrealize;
}

static const TypeInfo my_virtio_genirq_info = {
    .name = TYPE_MY_VIRTIO_GENIRQ,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MyVirtioStateGenirq),
    .class_init = my_virtio_genirq_class_init,
};

static void my_virtio_genirq_types(void)
{
    type_register_static(&my_virtio_genirq_info);
}

type_init(my_virtio_genirq_types);
