#include "qemu/osdep.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "virtio_wrapper.h"
#include "virtio_backend.h"
#include "hw/misc/my_virtio.h"

struct MyVirtioStateGpu {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;

    virtio_handle_t handle;
    virtio_backend_handle_t backend;
    hwaddr base;
    hwaddr size;
    uint32_t width;
    uint32_t height;
    virtio_backend_handle_t ui;
    char *vnc_listen;
};

#define TYPE_MY_VIRTIO_GPU "my-virtio-gpu"
OBJECT_DECLARE_SIMPLE_TYPE(MyVirtioStateGpu, MY_VIRTIO_GPU)

static uint64_t my_virtio_gpu_mmio_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    MyVirtioStateGpu *s = opaque;
    uint32_t val = 0;

    virtio_mmio_read(s->handle, s->base + offset, &val, size);
    return val;
}

static void my_virtio_gpu_mmio_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned size)
{
    MyVirtioStateGpu *s = opaque;
    int is_doorbell = 0;

    virtio_mmio_write(s->handle, s->base + offset, (uint32_t)value,
                      size, &is_doorbell);
}

static const MemoryRegionOps my_virtio_gpu_mmio_ops = {
    .read = my_virtio_gpu_mmio_read,
    .write = my_virtio_gpu_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t my_alloc(int size)
{
    return (uint64_t)g_malloc0(size);
}

static void my_free(uint64_t addr, int size)
{
    g_free((void *)(uintptr_t)addr);
}

static int my_guest_memory_read(uint64_t gpa, void *dst, uint32_t len)
{
    cpu_physical_memory_read(gpa, dst, len);
    return len;
}

static int my_guest_memory_write(uint64_t gpa, void *src, uint32_t len)
{
    cpu_physical_memory_write(gpa, src, len);
    return len;
}

static int my_set_irq(void *priv)
{
    MyVirtioStateGpu *s = priv;

    qemu_irq_pulse(s->irq);
    return 0;
}

static int my_virtio_gpu_submit(void *cmd, int cmd_len, void *resp,
                                int resp_cap, int *resp_len, void *priv)
{
    MyVirtioStateGpu *s = priv;
    size_t out_len = 0;
    struct virtio_backend_io io = {
        .type = VIRTIO_BACKEND_IO_GPU_CMD,
        .buf = cmd,
        .len = cmd_len,
        .cap = resp_cap,
        .u.gpu = {
            .resp = resp,
            .resp_len = &out_len,
        },
    };
    int ret;

    ret = virtio_backend_write(s->backend, &io);
    if (resp_len) {
        *resp_len = out_len <= INT_MAX ? (int)out_len : 0;
    }
    return ret;
}

static int my_virtio_gpu_guest_read(void *opaque, uint64_t gpa,
                                    void *dst, uint32_t len)
{
    cpu_physical_memory_read(gpa, dst, len);
    return len;
}

static struct libvirtio_ops ops = {
    .vprint = vprintf,
    .mm_alloc = my_alloc,
    .mm_free = my_free,
    .guest_mem_read = my_guest_memory_read,
    .guest_mem_write = my_guest_memory_write,
    .set_irq = my_set_irq,
    .gpu_ops = {
        .submit_ctrl = my_virtio_gpu_submit,
        .submit_cursor = my_virtio_gpu_submit,
    },
};

void *my_virtio_gpu_create(hwaddr start, hwaddr size, qemu_irq irq,
                           const char *vnc_listen)
{
    MyVirtioStateGpu *s = MY_VIRTIO_GPU(qdev_new(TYPE_MY_VIRTIO_GPU));

    s->base = start;
    s->size = size;
    if (vnc_listen && *vnc_listen) {
        qdev_prop_set_string(DEVICE(s), "vnc-listen", vnc_listen);
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(s), 0, start);
    sysbus_connect_irq(SYS_BUS_DEVICE(s), 0, irq);
    return s->ui;
}

static void my_virtio_gpu_realize(DeviceState *dev, Error **errp)
{
    MyVirtioStateGpu *s = MY_VIRTIO_GPU(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);
    struct virtio_backend_config ui_config = {
        .type = VIRTIO_BACKEND_UI,
        .u.ui = {
            .listen = s->vnc_listen,
            .width = 1280,
            .height = 800,
        },
    };
    struct virtio_backend_config backend_config = {
        .type = VIRTIO_BACKEND_GPU,
        .u.gpu = {
            .width = 1280,
            .height = 800,
            .max_outputs = 1,
            .guest_read = my_virtio_gpu_guest_read,
            .opaque = s,
        },
    };

    memory_region_init_io(&s->iomem, OBJECT(dev), &my_virtio_gpu_mmio_ops, s,
                          "my-virtio-gpu-regs", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->width = backend_config.u.gpu.width;
    s->height = backend_config.u.gpu.height;
    ui_config.u.ui.width = s->width;
    ui_config.u.ui.height = s->height;
    s->ui = virtio_backend_create(&ui_config);
    if (!s->ui) {
        error_setg(errp, "failed to create my-virtio-gpu VNC backend at %s",
                   s->vnc_listen ? s->vnc_listen : "");
        return;
    }
    backend_config.u.gpu.ui = s->ui;

    s->backend = virtio_backend_create(&backend_config);
    if (!s->backend) {
        error_setg(errp, "failed to create my-virtio-gpu backend");
        virtio_backend_destroy(s->ui);
        s->ui = NULL;
        return;
    }

    s->handle = virtio_mmio_create(VIRTIO_EMU_NAME_GPU, s->base, s->size,
                                   &ops, s);
    if (!s->handle) {
        error_setg(errp, "failed to create my-virtio-gpu protocol device");
        virtio_backend_destroy(s->backend);
        s->backend = NULL;
        virtio_backend_destroy(s->ui);
        s->ui = NULL;
        return;
    }
}

static void my_virtio_gpu_unrealize(DeviceState *dev)
{
    MyVirtioStateGpu *s = MY_VIRTIO_GPU(dev);

    virtio_backend_destroy(s->backend);
    s->backend = NULL;
    virtio_backend_destroy(s->ui);
    s->ui = NULL;
}

static const Property my_virtio_gpu_properties[] = {
    DEFINE_PROP_STRING("vnc-listen", MyVirtioStateGpu, vnc_listen),
};

static void my_virtio_gpu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = my_virtio_gpu_realize;
    dc->unrealize = my_virtio_gpu_unrealize;
    device_class_set_props(dc, my_virtio_gpu_properties);
}

static const TypeInfo my_virtio_gpu_info = {
    .name = TYPE_MY_VIRTIO_GPU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MyVirtioStateGpu),
    .class_init = my_virtio_gpu_class_init,
};

static void my_virtio_gpu_types(void)
{
    type_register_static(&my_virtio_gpu_info);
}

type_init(my_virtio_gpu_types);
