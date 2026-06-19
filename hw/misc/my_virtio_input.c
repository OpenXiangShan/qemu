#include "qemu/osdep.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "hw/qdev-properties.h"
#include "ui/input.h"
#include "virtio_wrapper.h"
#include "virtio_backend.h"
#include "hw/misc/my_virtio.h"

#include "standard-headers/linux/input-event-codes.h"

typedef struct MyVirtioStateInput {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;

    virtio_handle_t handle;
    virtio_backend_handle_t backend;
    QemuInputHandlerState *handler_state;
    QEMUBH *rx_bh;
    hwaddr base;
    hwaddr size;
    enum virtio_backend_input_profile profile;
    char *backend_name;
    char *evdev_path;
    virtio_backend_ui_handle_t ui;
} MyVirtioStateInput;

#define TYPE_MY_VIRTIO_INPUT "my-virtio-input"
#define TYPE_MY_VIRTIO_KEYBOARD "my-virtio-keyboard"
#define TYPE_MY_VIRTIO_MOUSE "my-virtio-mouse"
OBJECT_DECLARE_SIMPLE_TYPE(MyVirtioStateInput, MY_VIRTIO_INPUT)

static const unsigned short button_map[INPUT_BUTTON__MAX] = {
    [INPUT_BUTTON_LEFT] = BTN_LEFT,
    [INPUT_BUTTON_RIGHT] = BTN_RIGHT,
    [INPUT_BUTTON_MIDDLE] = BTN_MIDDLE,
    [INPUT_BUTTON_SIDE] = BTN_SIDE,
    [INPUT_BUTTON_EXTRA] = BTN_EXTRA,
};

static const unsigned short rel_axis_map[INPUT_AXIS__MAX] = {
    [INPUT_AXIS_X] = REL_X,
    [INPUT_AXIS_Y] = REL_Y,
};

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

    virtio_mmio_write(s->handle, s->base + offset, (uint32_t)value,
                      size, &is_doorbell);
    if (is_doorbell) {
        my_virtio_input_drain(s);
    }
}

static void my_virtio_input_rx_bh(void *opaque)
{
    MyVirtioStateInput *s = opaque;

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

static void my_virtio_input_push(MyVirtioStateInput *s, uint16_t type,
                                 uint16_t code, int32_t value)
{
    struct virtio_backend_input_event event = {
        .type = type,
        .code = code,
        .value = value,
    };

    if (virtio_backend_push_readable(s->backend, &event, sizeof(event)) < 0) {
        warn_report("my-virtio input event dropped type=%u code=%u value=%d",
                    type, code, value);
    }
}

static void my_virtio_keyboard_event(DeviceState *dev, QemuConsole *src,
                                     InputEvent *evt)
{
    MyVirtioStateInput *s = MY_VIRTIO_INPUT(dev);
    InputKeyEvent *key;
    int qcode;

    if (evt->type != INPUT_EVENT_KIND_KEY) {
        return;
    }

    key = evt->u.key.data;
    qcode = qemu_input_key_value_to_qcode(key->key);
    if (qcode >= 0 && qcode < qemu_input_map_qcode_to_linux_len &&
        qemu_input_map_qcode_to_linux[qcode]) {
        my_virtio_input_push(s, EV_KEY, qemu_input_map_qcode_to_linux[qcode],
                             key->down ? 1 : 0);
    } else if (key->down) {
        warn_report("my-virtio-keyboard unmapped qcode=%d", qcode);
    }
}

static void my_virtio_mouse_event(DeviceState *dev, QemuConsole *src,
                                  InputEvent *evt)
{
    MyVirtioStateInput *s = MY_VIRTIO_INPUT(dev);

    switch (evt->type) {
    case INPUT_EVENT_KIND_BTN: {
        InputBtnEvent *btn = evt->u.btn.data;

        if ((btn->button == INPUT_BUTTON_WHEEL_UP ||
             btn->button == INPUT_BUTTON_WHEEL_DOWN) && btn->down) {
            my_virtio_input_push(s, EV_REL, REL_WHEEL,
                                 btn->button == INPUT_BUTTON_WHEEL_UP ?
                                 1 : -1);
        } else if (btn->button < INPUT_BUTTON__MAX &&
                   button_map[btn->button]) {
            my_virtio_input_push(s, EV_KEY, button_map[btn->button],
                                 btn->down ? 1 : 0);
        } else if (btn->down) {
            warn_report("my-virtio-mouse unmapped button=%d", btn->button);
        }
        break;
    }
    case INPUT_EVENT_KIND_REL: {
        InputMoveEvent *move = evt->u.rel.data;

        if (move->axis < INPUT_AXIS__MAX && rel_axis_map[move->axis]) {
            my_virtio_input_push(s, EV_REL, rel_axis_map[move->axis],
                                 move->value);
        }
        break;
    }
    default:
        break;
    }
}

static void my_virtio_input_sync(DeviceState *dev)
{
    MyVirtioStateInput *s = MY_VIRTIO_INPUT(dev);

    my_virtio_input_push(s, EV_SYN, SYN_REPORT, 0);
}

static const QemuInputHandler my_virtio_keyboard_handler = {
    .name = "my-virtio-keyboard",
    .mask = INPUT_EVENT_MASK_KEY,
    .event = my_virtio_keyboard_event,
    .sync = my_virtio_input_sync,
};

static const QemuInputHandler my_virtio_mouse_handler = {
    .name = "my-virtio-mouse",
    .mask = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_REL,
    .event = my_virtio_mouse_event,
    .sync = my_virtio_input_sync,
};

static void my_virtio_input_create(hwaddr start, hwaddr size, qemu_irq irq,
                                   const char *type, const char *backend,
                                   const char *evdev_path, void *ui)
{
    MyVirtioStateInput *s = MY_VIRTIO_INPUT(qdev_new(type));

    s->base = start;
    s->size = size;
    s->ui = ui;
    qdev_prop_set_string(DEVICE(s), "backend", backend ? backend : "external");
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
        VIRTIO_BACKEND_INPUT_SOURCE_EXTERNAL;
    const char *emu_name;
    const QemuInputHandler *handler;

    if (!s->backend_name || !*s->backend_name ||
        !g_strcmp0(s->backend_name, "external")) {
        source = VIRTIO_BACKEND_INPUT_SOURCE_EXTERNAL;
    } else if (!g_strcmp0(s->backend_name, "evdev")) {
        source = VIRTIO_BACKEND_INPUT_SOURCE_EVDEV;
    } else if (!g_strcmp0(s->backend_name, "vnc") ||
               !g_strcmp0(s->backend_name, "ui")) {
        source = VIRTIO_BACKEND_INPUT_SOURCE_UI;
    } else {
        error_setg(errp,
                   "invalid my-virtio input backend '%s', expected external, evdev, vnc, or ui",
                   s->backend_name);
        return;
    }
    backend_config.u.input.source = source;
    if (source == VIRTIO_BACKEND_INPUT_SOURCE_EVDEV) {
        backend_config.u.input.evdev_path = s->evdev_path;
    } else if (source == VIRTIO_BACKEND_INPUT_SOURCE_UI) {
        if (!s->ui) {
            error_setg(errp, "%s backend '%s' requires my-virtio-vnc=on",
                       s->profile == VIRTIO_BACKEND_INPUT_KEYBOARD ?
                       "my-virtio-keyboard" : "my-virtio-mouse",
                       s->backend_name);
            return;
        }
        backend_config.u.input.ui = s->ui;
    }

    if (s->profile == VIRTIO_BACKEND_INPUT_KEYBOARD) {
        emu_name = VIRTIO_EMU_NAME_KEYBOARD;
        handler = &my_virtio_keyboard_handler;
    } else {
        emu_name = VIRTIO_EMU_NAME_MOUSE;
        handler = &my_virtio_mouse_handler;
    }

    memory_region_init_io(&s->iomem, OBJECT(dev), &my_virtio_input_mmio_ops, s,
                          handler->name, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->rx_bh = qemu_bh_new(my_virtio_input_rx_bh, s);
    s->backend = virtio_backend_create(&backend_config);
    if (!s->backend) {
        error_setg(errp, "failed to create %s backend", handler->name);
        qemu_bh_delete(s->rx_bh);
        s->rx_bh = NULL;
        return;
    }

    s->handle = virtio_mmio_create(emu_name, s->base, s->size, &ops, s);
    if (!s->handle) {
        error_setg(errp, "failed to create %s protocol device",
                   handler->name);
        virtio_backend_destroy(s->backend);
        s->backend = NULL;
        qemu_bh_delete(s->rx_bh);
        s->rx_bh = NULL;
        return;
    }

    if (source == VIRTIO_BACKEND_INPUT_SOURCE_EXTERNAL) {
        s->handler_state = qemu_input_handler_register(dev, handler);
        qemu_input_handler_activate(s->handler_state);
    } else if (source == VIRTIO_BACKEND_INPUT_SOURCE_EVDEV) {
        info_report("%s using evdev backend%s%s",
                    handler->name,
                    s->evdev_path && *s->evdev_path ? " path=" : "",
                    s->evdev_path && *s->evdev_path ? s->evdev_path : "");
    } else {
        info_report("%s using backend VNC input", handler->name);
    }
}

static void my_virtio_input_unrealize(DeviceState *dev)
{
    MyVirtioStateInput *s = MY_VIRTIO_INPUT(dev);

    if (s->handler_state) {
        qemu_input_handler_unregister(s->handler_state);
        s->handler_state = NULL;
    }
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

static void my_virtio_input_types(void)
{
    type_register_static(&my_virtio_input_info);
    type_register_static(&my_virtio_keyboard_info);
    type_register_static(&my_virtio_mouse_info);
}

type_init(my_virtio_input_types);
