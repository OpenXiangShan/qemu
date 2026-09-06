#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "qom/object.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "virtio_wrapper.h"
#include "hw/misc/my_virtio.h"

typedef struct MyVirtioIommuState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    MemoryRegion relay_iomem;
    qemu_irq irq;
    QEMUBH *request_bh;
    virtio_handle_t handle;
    hwaddr base;
    hwaddr size;
    int mode;
    uint32_t relay_command;
    uint64_t relay_namespace;
    uint32_t relay_domain;
    uint32_t relay_endpoint;
    uint32_t relay_flags;
    uint64_t relay_virt_start;
    uint64_t relay_virt_end;
    uint64_t relay_phys_start;
    int32_t relay_result;
} MyVirtioIommuState;

#define TYPE_MY_VIRTIO_IOMMU "my-virtio-iommu"
OBJECT_DECLARE_SIMPLE_TYPE(MyVirtioIommuState, MY_VIRTIO_IOMMU)

static uint64_t my_virtio_iommu_mmio_read(void *opaque, hwaddr offset,
                                          unsigned size)
{
    MyVirtioIommuState *s = opaque;
    uint32_t value = 0;

    virtio_mmio_read(s->handle, s->base + offset, &value, size);
    return value;
}

static void my_virtio_iommu_mmio_write(void *opaque, hwaddr offset,
                                       uint64_t value, unsigned size)
{
    MyVirtioIommuState *s = opaque;
    int doorbell = 0;

    if (!virtio_mmio_write(s->handle, s->base + offset, value, size,
                           &doorbell) && doorbell) {
        qemu_bh_schedule(s->request_bh);
    }
}

static const MemoryRegionOps my_virtio_iommu_mmio_ops = {
    .read = my_virtio_iommu_mmio_read,
    .write = my_virtio_iommu_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

enum {
    MY_VIRTIO_IOMMU_RELAY_MAGIC = 0x52494f56,
    MY_VIRTIO_IOMMU_RELAY_VERSION = 1,
    MY_VIRTIO_IOMMU_RELAY_CAP_S1 = 1,
    MY_VIRTIO_IOMMU_RELAY_COMMAND = 0x10,
    MY_VIRTIO_IOMMU_RELAY_NAMESPACE = 0x18,
    MY_VIRTIO_IOMMU_RELAY_DOMAIN = 0x20,
    MY_VIRTIO_IOMMU_RELAY_ENDPOINT = 0x24,
    MY_VIRTIO_IOMMU_RELAY_FLAGS = 0x28,
    MY_VIRTIO_IOMMU_RELAY_VIRT_START = 0x30,
    MY_VIRTIO_IOMMU_RELAY_VIRT_END = 0x38,
    MY_VIRTIO_IOMMU_RELAY_PHYS_START = 0x40,
    MY_VIRTIO_IOMMU_RELAY_RESULT = 0x48,
};

static uint64_t my_virtio_iommu_relay_read(void *opaque, hwaddr offset,
                                           unsigned size)
{
    MyVirtioIommuState *s = opaque;
    uint64_t value = 0;

    switch (offset) {
    case 0x00:
        value = MY_VIRTIO_IOMMU_RELAY_MAGIC;
        break;
    case 0x04:
        value = MY_VIRTIO_IOMMU_RELAY_VERSION;
        break;
    case 0x08:
        value = MY_VIRTIO_IOMMU_RELAY_CAP_S1;
        break;
    case MY_VIRTIO_IOMMU_RELAY_COMMAND:
        value = s->relay_command;
        break;
    case MY_VIRTIO_IOMMU_RELAY_NAMESPACE:
        value = s->relay_namespace;
        break;
    case MY_VIRTIO_IOMMU_RELAY_NAMESPACE + 4:
        value = s->relay_namespace >> 32;
        break;
    case MY_VIRTIO_IOMMU_RELAY_DOMAIN:
        value = s->relay_domain;
        break;
    case MY_VIRTIO_IOMMU_RELAY_ENDPOINT:
        value = s->relay_endpoint;
        break;
    case MY_VIRTIO_IOMMU_RELAY_FLAGS:
        value = s->relay_flags;
        break;
    case MY_VIRTIO_IOMMU_RELAY_VIRT_START:
        value = s->relay_virt_start;
        break;
    case MY_VIRTIO_IOMMU_RELAY_VIRT_START + 4:
        value = s->relay_virt_start >> 32;
        break;
    case MY_VIRTIO_IOMMU_RELAY_VIRT_END:
        value = s->relay_virt_end;
        break;
    case MY_VIRTIO_IOMMU_RELAY_VIRT_END + 4:
        value = s->relay_virt_end >> 32;
        break;
    case MY_VIRTIO_IOMMU_RELAY_PHYS_START:
        value = s->relay_phys_start;
        break;
    case MY_VIRTIO_IOMMU_RELAY_PHYS_START + 4:
        value = s->relay_phys_start >> 32;
        break;
    case MY_VIRTIO_IOMMU_RELAY_RESULT:
        value = (uint32_t)s->relay_result;
        break;
    default:
        break;
    }
    return size == 8 ? value : (uint32_t)value;
}

static void my_virtio_iommu_relay_process(MyVirtioIommuState *s)
{
    switch (s->relay_command) {
    case 1:
        s->relay_result = virtio_iommu_relay_attach(
            s->handle, s->relay_namespace, s->relay_domain,
            s->relay_endpoint);
        break;
    case 2:
        s->relay_result = virtio_iommu_relay_detach(
            s->handle, s->relay_namespace, s->relay_domain,
            s->relay_endpoint);
        break;
    case 3:
        s->relay_result = virtio_iommu_relay_map(
            s->handle, s->relay_namespace, s->relay_domain,
            s->relay_endpoint, s->relay_virt_start, s->relay_virt_end,
            s->relay_phys_start, s->relay_flags);
        break;
    case 4:
        s->relay_result = virtio_iommu_relay_unmap(
            s->handle, s->relay_namespace, s->relay_domain,
            s->relay_endpoint, s->relay_virt_start, s->relay_virt_end);
        break;
    default:
        s->relay_result = -EINVAL;
        break;
    }
}

static void my_virtio_iommu_relay_write(void *opaque, hwaddr offset,
                                        uint64_t value, unsigned size)
{
    MyVirtioIommuState *s = opaque;
    uint32_t v = value;

    (void)size;
    switch (offset) {
    case MY_VIRTIO_IOMMU_RELAY_COMMAND:
        s->relay_command = v;
        my_virtio_iommu_relay_process(s);
        break;
    case MY_VIRTIO_IOMMU_RELAY_NAMESPACE:
        s->relay_namespace = (s->relay_namespace & UINT64_C(0xffffffff00000000)) | v;
        break;
    case MY_VIRTIO_IOMMU_RELAY_NAMESPACE + 4:
        s->relay_namespace = (s->relay_namespace & UINT64_C(0xffffffff)) |
                             ((uint64_t)v << 32);
        break;
    case MY_VIRTIO_IOMMU_RELAY_DOMAIN:
        s->relay_domain = v;
        break;
    case MY_VIRTIO_IOMMU_RELAY_ENDPOINT:
        s->relay_endpoint = v;
        break;
    case MY_VIRTIO_IOMMU_RELAY_FLAGS:
        s->relay_flags = v;
        break;
    case MY_VIRTIO_IOMMU_RELAY_VIRT_START:
        s->relay_virt_start = (s->relay_virt_start & UINT64_C(0xffffffff00000000)) | v;
        break;
    case MY_VIRTIO_IOMMU_RELAY_VIRT_START + 4:
        s->relay_virt_start = (s->relay_virt_start & UINT64_C(0xffffffff)) |
                              ((uint64_t)v << 32);
        break;
    case MY_VIRTIO_IOMMU_RELAY_VIRT_END:
        s->relay_virt_end = (s->relay_virt_end & UINT64_C(0xffffffff00000000)) | v;
        break;
    case MY_VIRTIO_IOMMU_RELAY_VIRT_END + 4:
        s->relay_virt_end = (s->relay_virt_end & UINT64_C(0xffffffff)) |
                            ((uint64_t)v << 32);
        break;
    case MY_VIRTIO_IOMMU_RELAY_PHYS_START:
        s->relay_phys_start = (s->relay_phys_start & UINT64_C(0xffffffff00000000)) | v;
        break;
    case MY_VIRTIO_IOMMU_RELAY_PHYS_START + 4:
        s->relay_phys_start = (s->relay_phys_start & UINT64_C(0xffffffff)) |
                              ((uint64_t)v << 32);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps my_virtio_iommu_relay_ops = {
    .read = my_virtio_iommu_relay_read,
    .write = my_virtio_iommu_relay_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static uint64_t my_virtio_iommu_alloc(int size)
{
    return (uint64_t)(uintptr_t)g_malloc0(size);
}

static void my_virtio_iommu_free(uint64_t address, int size)
{
    g_free((void *)(uintptr_t)address);
}

static int my_virtio_iommu_guest_read(uint64_t gpa, void *dst, uint32_t len,
                                      void *opaque)
{
    cpu_physical_memory_read(gpa, dst, len);
    return len;
}

static int my_virtio_iommu_guest_write(uint64_t gpa, void *src, uint32_t len,
                                       void *opaque)
{
    cpu_physical_memory_write(gpa, src, len);
    return len;
}

static int my_virtio_iommu_set_irq(void *opaque)
{
    MyVirtioIommuState *s = opaque;

    qemu_irq_pulse(s->irq);
    return 0;
}

static struct libvirtio_ops my_virtio_iommu_ops = {
    .vprint = vprintf,
    .mm_alloc = my_virtio_iommu_alloc,
    .mm_free = my_virtio_iommu_free,
    .guest_mem_read = my_virtio_iommu_guest_read,
    .guest_mem_write = my_virtio_iommu_guest_write,
    .set_irq = my_virtio_iommu_set_irq,
};

static void my_virtio_iommu_request_bh(void *opaque)
{
    MyVirtioIommuState *s = opaque;

    virtio_process_req(s->handle);
}

static void my_virtio_iommu_realize(DeviceState *dev, Error **errp)
{
    MyVirtioIommuState *s = MY_VIRTIO_IOMMU(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    struct virtio_mmio_options options = {
        .modern = true,
        .iommu_mode = s->mode,
    };

    memory_region_init_io(&s->iomem, OBJECT(dev),
                          &my_virtio_iommu_mmio_ops, s,
                          TYPE_MY_VIRTIO_IOMMU, s->size);
    sysbus_init_mmio(sbd, &s->iomem);
    memory_region_init_io(&s->relay_iomem, OBJECT(dev),
                          &my_virtio_iommu_relay_ops, s,
                          "my-virtio-iommu-relay",
                          MY_VIRTIO_IOMMU_RELAY_SIZE);
    sysbus_init_mmio(sbd, &s->relay_iomem);
    sysbus_init_irq(sbd, &s->irq);
    s->request_bh = qemu_bh_new(my_virtio_iommu_request_bh, s);
    s->handle = virtio_mmio_create_ex(VIRTIO_EMU_NAME_IOMMU, s->base,
                                      s->size, &my_virtio_iommu_ops, s,
                                      &options);
    if (!s->handle) {
        error_setg(errp, "failed to create virtio-iommu protocol device");
    }
}

static void my_virtio_iommu_unrealize(DeviceState *dev)
{
    MyVirtioIommuState *s = MY_VIRTIO_IOMMU(dev);

    virtio_mmio_destroy(s->handle);
    s->handle = NULL;
    if (s->request_bh) {
        qemu_bh_delete(s->request_bh);
        s->request_bh = NULL;
    }
}

static void my_virtio_iommu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = my_virtio_iommu_realize;
    dc->unrealize = my_virtio_iommu_unrealize;
}

static const TypeInfo my_virtio_iommu_info = {
    .name = TYPE_MY_VIRTIO_IOMMU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MyVirtioIommuState),
    .class_init = my_virtio_iommu_class_init,
};

static void my_virtio_iommu_register_types(void)
{
    type_register_static(&my_virtio_iommu_info);
}
type_init(my_virtio_iommu_register_types)

void *my_virtio_iommu_create(hwaddr start, hwaddr size, qemu_irq irq, int mode)
{
    MyVirtioIommuState *s = MY_VIRTIO_IOMMU(qdev_new(TYPE_MY_VIRTIO_IOMMU));

    s->base = start;
    s->size = size;
    s->mode = mode;
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(s), 0, start);
    sysbus_mmio_map(SYS_BUS_DEVICE(s), 1, MY_VIRTIO_IOMMU_RELAY_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(s), 0, irq);
    return s->handle;
}
