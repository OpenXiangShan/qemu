#include "qemu/osdep.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "ui/console.h"
#include "ui/qemu-pixman.h"
#include "virtio_wrapper.h"
#include "virtio_backend.h"
#include "hw/misc/my_virtio.h"

struct MyVirtioStateGpu {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;

    virtio_handle_t handle;
    virtio_backend_handle_t backend;
    QemuConsole *con;
    DisplaySurface *surface;
    hwaddr base;
    hwaddr size;
    uint32_t width;
    uint32_t height;
    virtio_backend_ui_handle_t ui;
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

static void my_virtio_gpu_scanout_disable(void *opaque, uint32_t scanout_id)
{
    MyVirtioStateGpu *s = opaque;

    if (s->con) {
        dpy_gfx_replace_surface(s->con, NULL);
    }
    s->surface = NULL;
}

static void my_virtio_gpu_scanout_update(void *opaque, uint32_t scanout_id,
                                         const void *pixels, uint32_t width,
                                         uint32_t height, uint32_t stride,
                                         uint32_t x, uint32_t y,
                                         uint32_t w, uint32_t h)
{
    MyVirtioStateGpu *s = opaque;

    if (!s->con || !pixels || !width || !height) {
        return;
    }

    if (!s->surface ||
        surface_width(s->surface) != width ||
        surface_height(s->surface) != height ||
        surface_data(s->surface) != pixels) {
        s->surface = qemu_create_displaysurface_from(width, height,
                                                     PIXMAN_x8r8g8b8,
                                                     stride,
                                                     (uint8_t *)pixels);
        dpy_gfx_replace_surface(s->con, s->surface);
    }

    if (x >= width || y >= height) {
        return;
    }
    if (x + w > width) {
        w = width - x;
    }
    if (y + h > height) {
        h = height - y;
    }
    if (w && h) {
        dpy_gfx_update(s->con, x, y, w, h);
    }
}

static void my_virtio_gpu_invalidate_display(void *opaque)
{
}

static void my_virtio_gpu_update_display(void *opaque)
{
}

static void my_virtio_gpu_text_update(void *opaque, console_ch_t *chardata)
{
}

static const GraphicHwOps my_virtio_gpu_ops = {
    .invalidate = my_virtio_gpu_invalidate_display,
    .gfx_update = my_virtio_gpu_update_display,
    .text_update = my_virtio_gpu_text_update,
};

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

void *my_virtio_ui_create_vnc(const char *listen, uint32_t width,
                              uint32_t height)
{
    return virtio_backend_ui_create_vnc(listen, width, height);
}

void my_virtio_ui_destroy(void *ui)
{
    virtio_backend_ui_destroy(ui);
}

void my_virtio_gpu_create(hwaddr start, hwaddr size, qemu_irq irq, void *ui)
{
    MyVirtioStateGpu *s = MY_VIRTIO_GPU(qdev_new(TYPE_MY_VIRTIO_GPU));

    s->base = start;
    s->size = size;
    s->ui = ui;
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(s), 0, start);
    sysbus_connect_irq(SYS_BUS_DEVICE(s), 0, irq);
}

static void my_virtio_gpu_realize(DeviceState *dev, Error **errp)
{
    MyVirtioStateGpu *s = MY_VIRTIO_GPU(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);
    struct virtio_backend_config backend_config = {
        .type = VIRTIO_BACKEND_GPU,
        .u.gpu = {
            .width = 1280,
            .height = 800,
            .max_outputs = 1,
            .ui = s->ui,
            .guest_read = my_virtio_gpu_guest_read,
            .scanout_update = my_virtio_gpu_scanout_update,
            .scanout_disable = my_virtio_gpu_scanout_disable,
            .opaque = s,
        },
    };

    memory_region_init_io(&s->iomem, OBJECT(dev), &my_virtio_gpu_mmio_ops, s,
                          "my-virtio-gpu-regs", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->width = backend_config.u.gpu.width;
    s->height = backend_config.u.gpu.height;
    s->con = graphic_console_init(dev, 0, &my_virtio_gpu_ops, s);

    s->backend = virtio_backend_create(&backend_config);
    if (!s->backend) {
        error_setg(errp, "failed to create my-virtio-gpu backend");
        return;
    }

    s->handle = virtio_mmio_create(VIRTIO_EMU_NAME_GPU, s->base, s->size,
                                   &ops, s);
    if (!s->handle) {
        error_setg(errp, "failed to create my-virtio-gpu protocol device");
        return;
    }
}

static void my_virtio_gpu_unrealize(DeviceState *dev)
{
    MyVirtioStateGpu *s = MY_VIRTIO_GPU(dev);

    virtio_backend_destroy(s->backend);
    s->backend = NULL;
}

static void my_virtio_gpu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = my_virtio_gpu_realize;
    dc->unrealize = my_virtio_gpu_unrealize;
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
