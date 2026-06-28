#include "qemu/osdep.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "hw/qdev-properties.h"
#include "virtio_wrapper.h"
#include "virtio_backend.h"
#include "hw/misc/my_virtio.h"

typedef struct MyVirtioStateInput {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;

    virtio_handle_t handle;
    virtio_backend_handle_t backend;
    QEMUBH *rx_bh;
    hwaddr base;
    hwaddr size;
    enum virtio_backend_input_profile profile;
    char *backend_name;
    char *evdev_path;
    virtio_backend_handle_t ui;
} MyVirtioStateInput;

#define TYPE_MY_VIRTIO_INPUT "my-virtio-input"
#define TYPE_MY_VIRTIO_KEYBOARD "my-virtio-keyboard"
#define TYPE_MY_VIRTIO_MOUSE "my-virtio-mouse"
#define TYPE_MY_VIRTIO_TABLET "my-virtio-tablet"
OBJECT_DECLARE_SIMPLE_TYPE(MyVirtioStateInput, MY_VIRTIO_INPUT)

static uint64_t my_virtio_input_mmio_read(void *opaque, hwaddr offset,
                                          unsigned size)
{
    MyVirtioStateInput *s = opaque;
    uint32_t val = 0;

    virtio_mmio_read(s->handle, s->base + offset, &val, size);
    return val;
}

static void my_virtio_input_drain(MyVirtioStateInput *s)
{
    if (!s->backend || !s->handle) {
        return;
    }

    for (;;) {
        struct virtio_backend_input_event backend_event;
        struct virtio_backend_io io = {
            .type = VIRTIO_BACKEND_IO_INPUT_EVENT,
            .buf = &backend_event,
            .cap = sizeof(backend_event),
        };
        struct {
            uint16_t type;
            uint16_t code;
            uint32_t value;
        } event;
        int ret;

        ret = virtio_backend_read(s->backend, &io);
        if (ret < 0) {
            break;
        }

        event.type = cpu_to_le16(backend_event.type);
        event.code = cpu_to_le16(backend_event.code);
        event.value = cpu_to_le32((uint32_t)backend_event.value);
        ret = virtio_receive(s->handle, &event, sizeof(event));
        if (ret <= 0) {
            virtio_backend_read_done(s->backend, io.token, 0);
            break;
        }

        virtio_backend_read_done(s->backend, io.token, 1);
    }
}

static void my_virtio_input_mmio_write(void *opaque, hwaddr offset,
                                       uint64_t value, unsigned size)
{
    MyVirtioStateInput *s = opaque;
    int is_doorbell = 0;
    int ret;

    ret = virtio_mmio_write(s->handle, s->base + offset, (uint32_t)value,
                            size, &is_doorbell);
    if (!ret && is_doorbell) {
        qemu_bh_schedule(s->rx_bh);
    }
}

static void my_virtio_input_rx_bh(void *opaque)
{
    MyVirtioStateInput *s = opaque;

    virtio_process_req(s->handle);
    my_virtio_input_drain(s);
}

static const MemoryRegionOps my_virtio_input_mmio_ops = {
    .read = my_virtio_input_mmio_read,
    .write = my_virtio_input_mmio_write,
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
    MyVirtioStateInput *s = priv;

    qemu_irq_pulse(s->irq);
    return 0;
}

static int my_virtio_input_status(void *event, int len, void *priv)
{
    MyVirtioStateInput *s = priv;
    struct virtio_backend_io io = {
        .type = VIRTIO_BACKEND_IO_INPUT_EVENT,
        .buf = event,
        .len = len,
    };

    return virtio_backend_write(s->backend, &io);
}

static void my_virtio_input_backend_event(void *opaque,
                                          virtio_backend_handle_t handle,
                                          unsigned int events)
{
    MyVirtioStateInput *s = opaque;

    if (events & VIRTIO_BACKEND_EVENT_READABLE) {
        qemu_bh_schedule(s->rx_bh);
    }
    if (events & VIRTIO_BACKEND_EVENT_ERROR) {
        warn_report("my-virtio input backend reported an error");
    }
}

static struct libvirtio_ops ops = {
    .vprint = vprintf,
    .mm_alloc = my_alloc,
    .mm_free = my_free,
    .guest_mem_read = my_guest_memory_read,
    .guest_mem_write = my_guest_memory_write,
    .set_irq = my_set_irq,
    .input_ops = {
        .status = my_virtio_input_status,
    },
};

static void my_virtio_input_create(hwaddr start, hwaddr size, qemu_irq irq,
                                   const char *type, const char *backend,
                                   const char *evdev_path, void *ui)
{
    MyVirtioStateInput *s = MY_VIRTIO_INPUT(qdev_new(type));

    s->base = start;
    s->size = size;
    s->ui = ui;
    qdev_prop_set_string(DEVICE(s), "backend", backend ? backend : "vnc");
    if (evdev_path && *evdev_path) {
        qdev_prop_set_string(DEVICE(s), "evdev-path", evdev_path);
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(s), 0, start);
    sysbus_connect_irq(SYS_BUS_DEVICE(s), 0, irq);
}

void my_virtio_keyboard_create(hwaddr start, hwaddr size, qemu_irq irq,
                               const char *backend, const char *evdev_path,
                               void *ui)
{
    my_virtio_input_create(start, size, irq, TYPE_MY_VIRTIO_KEYBOARD,
                           backend, evdev_path, ui);
}

void my_virtio_mouse_create(hwaddr start, hwaddr size, qemu_irq irq,
                            const char *backend, const char *evdev_path,
                            void *ui)
{
    my_virtio_input_create(start, size, irq, TYPE_MY_VIRTIO_MOUSE,
                           backend, evdev_path, ui);
}

void my_virtio_tablet_create(hwaddr start, hwaddr size, qemu_irq irq,
                             const char *backend, const char *evdev_path,
                             void *ui)
{
    my_virtio_input_create(start, size, irq, TYPE_MY_VIRTIO_TABLET,
                           backend, evdev_path, ui);
}

static void my_virtio_input_realize(DeviceState *dev, Error **errp)
{
    MyVirtioStateInput *s = MY_VIRTIO_INPUT(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);
    struct virtio_backend_callbacks callbacks = {
        .event = my_virtio_input_backend_event,
    };
    struct virtio_backend_config backend_config = {
        .type = VIRTIO_BACKEND_INPUT,
        .callbacks = &callbacks,
        .callback_opaque = s,
        .u.input.profile = s->profile,
    };
    enum virtio_backend_input_source source =
        VIRTIO_BACKEND_INPUT_SOURCE_UI;
    const char *backend_name = s->backend_name && *s->backend_name ?
                               s->backend_name : "vnc";
    const char *emu_name;
    const char *device_name;

    if (!g_strcmp0(backend_name, "evdev")) {
        source = VIRTIO_BACKEND_INPUT_SOURCE_EVDEV;
    } else if (!g_strcmp0(backend_name, "vnc") ||
               !g_strcmp0(backend_name, "ui")) {
        source = VIRTIO_BACKEND_INPUT_SOURCE_UI;
    } else {
        error_setg(errp,
                   "invalid my-virtio input backend '%s', expected evdev, vnc, or ui",
                   backend_name);
        return;
    }

    if (s->profile == VIRTIO_BACKEND_INPUT_KEYBOARD) {
        emu_name = VIRTIO_EMU_NAME_KEYBOARD;
        device_name = "my-virtio-keyboard";
    } else if (s->profile == VIRTIO_BACKEND_INPUT_TABLET) {
        emu_name = VIRTIO_EMU_NAME_TABLET;
        device_name = "my-virtio-tablet";
    } else {
        emu_name = VIRTIO_EMU_NAME_MOUSE;
        device_name = "my-virtio-mouse";
    }

    backend_config.u.input.source = source;
    if (source == VIRTIO_BACKEND_INPUT_SOURCE_EVDEV) {
        backend_config.u.input.evdev_path = s->evdev_path;
    } else {
        if (!s->ui) {
            error_setg(errp, "%s backend '%s' requires my-virtio-gpu=on",
                       device_name, backend_name);
            return;
        }
        backend_config.u.input.ui = s->ui;
    }

    memory_region_init_io(&s->iomem, OBJECT(dev), &my_virtio_input_mmio_ops, s,
                          device_name, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->rx_bh = qemu_bh_new(my_virtio_input_rx_bh, s);
    s->backend = virtio_backend_create(&backend_config);
    if (!s->backend) {
        error_setg(errp, "failed to create %s backend", device_name);
        qemu_bh_delete(s->rx_bh);
        s->rx_bh = NULL;
        return;
    }

    s->handle = virtio_mmio_create(emu_name, s->base, s->size, &ops, s);
    if (!s->handle) {
        error_setg(errp, "failed to create %s protocol device",
                   device_name);
        virtio_backend_destroy(s->backend);
        s->backend = NULL;
        qemu_bh_delete(s->rx_bh);
        s->rx_bh = NULL;
        return;
    }

    if (source == VIRTIO_BACKEND_INPUT_SOURCE_EVDEV) {
        info_report("%s using evdev backend%s%s",
                    device_name,
                    s->evdev_path && *s->evdev_path ? " path=" : "",
                    s->evdev_path && *s->evdev_path ? s->evdev_path : "");
    } else {
        info_report("%s using backend VNC input", device_name);
    }
}

static void my_virtio_input_unrealize(DeviceState *dev)
{
    MyVirtioStateInput *s = MY_VIRTIO_INPUT(dev);

    if (s->rx_bh) {
        qemu_bh_delete(s->rx_bh);
        s->rx_bh = NULL;
    }
    virtio_backend_destroy(s->backend);
    s->backend = NULL;
}

static void my_virtio_keyboard_instance_init(Object *obj)
{
    MyVirtioStateInput *s = MY_VIRTIO_INPUT(obj);

    s->profile = VIRTIO_BACKEND_INPUT_KEYBOARD;
}

static void my_virtio_mouse_instance_init(Object *obj)
{
    MyVirtioStateInput *s = MY_VIRTIO_INPUT(obj);

    s->profile = VIRTIO_BACKEND_INPUT_MOUSE;
}

static void my_virtio_tablet_instance_init(Object *obj)
{
    MyVirtioStateInput *s = MY_VIRTIO_INPUT(obj);

    s->profile = VIRTIO_BACKEND_INPUT_TABLET;
}

static const Property my_virtio_input_properties[] = {
    DEFINE_PROP_STRING("backend", MyVirtioStateInput, backend_name),
    DEFINE_PROP_STRING("evdev-path", MyVirtioStateInput, evdev_path),
};

static void my_virtio_input_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = my_virtio_input_realize;
    dc->unrealize = my_virtio_input_unrealize;
    device_class_set_props(dc, my_virtio_input_properties);
}

static const TypeInfo my_virtio_input_info = {
    .name = TYPE_MY_VIRTIO_INPUT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MyVirtioStateInput),
    .class_init = my_virtio_input_class_init,
    .abstract = true,
};

static const TypeInfo my_virtio_keyboard_info = {
    .name = TYPE_MY_VIRTIO_KEYBOARD,
    .parent = TYPE_MY_VIRTIO_INPUT,
    .instance_init = my_virtio_keyboard_instance_init,
};

static const TypeInfo my_virtio_mouse_info = {
    .name = TYPE_MY_VIRTIO_MOUSE,
    .parent = TYPE_MY_VIRTIO_INPUT,
    .instance_init = my_virtio_mouse_instance_init,
};

static const TypeInfo my_virtio_tablet_info = {
    .name = TYPE_MY_VIRTIO_TABLET,
    .parent = TYPE_MY_VIRTIO_INPUT,
    .instance_init = my_virtio_tablet_instance_init,
};

static void my_virtio_input_types(void)
{
    type_register_static(&my_virtio_input_info);
    type_register_static(&my_virtio_keyboard_info);
    type_register_static(&my_virtio_mouse_info);
    type_register_static(&my_virtio_tablet_info);
}

type_init(my_virtio_input_types);
