#include "qemu/osdep.h"
#include "qom/object.h"
#include "qemu/typedefs.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/irq.h"
#include "hw/virtio/virtio-mmio.h"
#include "hw/sysbus.h"
#include "virtio_wrapper.h"
#include "virtio_backend.h"
#include "hw/misc/my_virtio.h"
#include "qemu/main-loop.h"
#include "system/system.h"

#include "chardev/char-fe.h"
#include "hw/qdev-properties.h"

#include "qemu/cutils.h"
#include <fcntl.h>
#include <sys/ioctl.h>

struct MyVirtioStateConsole {
    /*< private >*/
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    qemu_irq irq;

    CharBackend chr;

    virtio_handle_t handle;
    virtio_backend_handle_t backend;
    QEMUBH *rx_bh;

    char *backend_name;
    char *input_path;
    char *output_path;
    int backend_input_fd;
    int backend_output_fd;
};

#define TYPE_MY_VIRTIO_CONSOLE "my-virtio-console"
OBJECT_DECLARE_SIMPLE_TYPE(MyVirtioStateConsole, MY_VIRTIO_CONSOLE)

static hwaddr base;
static int len;

static void my_virtio_console_drain_rx(MyVirtioStateConsole *s)
{
    if (!s->backend || !s->handle) {
        return;
    }

    for (;;) {
        uint8_t buf[4096];
        struct virtio_backend_io io = {
            .type = VIRTIO_BACKEND_IO_STREAM,
            .buf = buf,
            .cap = sizeof(buf),
        };
        int ret;

        ret = virtio_backend_read(s->backend, &io);
        if (ret < 0) {
            break;
        }

        ret = virtio_receive(s->handle, io.buf, io.len);
        if (ret <= 0) {
            virtio_backend_read_done(s->backend, io.token, 0);
            break;
        }

        virtio_backend_read_done(s->backend, io.token, 1);
    }
}

static void my_virtio_console_rx_bh(void *opaque)
{
    MyVirtioStateConsole *s = opaque;

    my_virtio_console_drain_rx(s);
    qemu_chr_fe_accept_input(&s->chr);
}

static int my_virtio_console_chr_can_read(void *opaque)
{
    return 4096;
}

static void my_virtio_console_chr_read(void *opaque, const uint8_t *buf, int size)
{
    MyVirtioStateConsole *s = opaque;

    if (virtio_backend_push_readable(s->backend, buf, size) < 0) {
        return;
    }
    qemu_bh_schedule(s->rx_bh);
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
    struct virtio_backend_io io = {
        .type = VIRTIO_BACKEND_IO_STREAM,
        .buf = buf,
        .len = len,
    };

    return virtio_backend_write(s->backend, &io);
}

static int my_virtio_console_host_write(void *opaque, const uint8_t *buf,
                                        size_t len)
{
    MyVirtioStateConsole *s = opaque;

    return qemu_chr_fe_write_all(&s->chr, buf, len);
}

static void my_virtio_console_backend_event(void *opaque,
                                            virtio_backend_handle_t handle,
                                            unsigned int events)
{
    MyVirtioStateConsole *s = opaque;

    if (events & VIRTIO_BACKEND_EVENT_READABLE) {
        qemu_bh_schedule(s->rx_bh);
    }
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

void my_virtio_console_create(hwaddr start, hwaddr size, qemu_irq irq,
                              const char *backend, const char *input_path,
                              const char *output_path)
{
    MyVirtioStateConsole *s = MY_VIRTIO_CONSOLE(qdev_new(TYPE_MY_VIRTIO_CONSOLE));

    base = start;
    len = size;

    qdev_prop_set_string(DEVICE(s), "backend", backend ? backend : "external");
    if (input_path && *input_path) {
        qdev_prop_set_string(DEVICE(s), "input-path", input_path);
    }
    if (output_path && *output_path) {
        qdev_prop_set_string(DEVICE(s), "output-path", output_path);
    }

    if (!backend || !g_strcmp0(backend, "external")) {
        qdev_prop_set_chr(DEVICE(s), "chardev", serial_hd(2));
    }

    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(s), 0, base);
    //sysbus_create_simple("my-virtio", start, 0);

    sysbus_connect_irq(SYS_BUS_DEVICE(s), 0, irq);
}

static void my_virtio_console_realize(DeviceState *dev, Error **errp)
{
    MyVirtioStateConsole *s = MY_VIRTIO_CONSOLE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);
    enum virtio_backend_console_backend backend_type =
        VIRTIO_BACKEND_CONSOLE_EXTERNAL;
    struct virtio_backend_callbacks backend_callbacks = {
        .event = my_virtio_console_backend_event,
    };
    struct virtio_backend_config backend_config = {
        .type = VIRTIO_BACKEND_CONSOLE,
        .callbacks = &backend_callbacks,
        .callback_opaque = s,
        .u.console.backend = VIRTIO_BACKEND_CONSOLE_EXTERNAL,
        .u.console.host_write = my_virtio_console_host_write,
        .u.console.host_opaque = s,
    };

    s->backend_input_fd = -1;
    s->backend_output_fd = -1;

    if (!s->backend_name || !*s->backend_name ||
        !g_strcmp0(s->backend_name, "external")) {
        backend_type = VIRTIO_BACKEND_CONSOLE_EXTERNAL;
    } else if (!g_strcmp0(s->backend_name, "stdio")) {
        backend_type = VIRTIO_BACKEND_CONSOLE_STDIO;
    } else if (!g_strcmp0(s->backend_name, "fd")) {
        backend_type = VIRTIO_BACKEND_CONSOLE_FD;
    } else if (!g_strcmp0(s->backend_name, "pty")) {
        backend_type = VIRTIO_BACKEND_CONSOLE_PTY;
    } else {
        error_setg(errp,
                   "invalid my-virtio-console backend '%s', expected external, stdio, fd, or pty",
                   s->backend_name);
        return;
    }

    backend_config.u.console.backend = backend_type;
    if (backend_type == VIRTIO_BACKEND_CONSOLE_FD) {
        if (!s->input_path || !*s->input_path ||
            !s->output_path || !*s->output_path) {
            error_setg(errp,
                       "my-virtio-console backend=fd requires input-path and output-path");
            return;
        }

        s->backend_input_fd = qemu_open_old(s->input_path, O_RDONLY | O_NONBLOCK);
        if (s->backend_input_fd < 0) {
            error_setg_errno(errp, errno,
                             "failed to open my-virtio-console input-path '%s'",
                             s->input_path);
            return;
        }

        if (!g_strcmp0(s->input_path, s->output_path)) {
            s->backend_output_fd = s->backend_input_fd;
        } else {
            s->backend_output_fd = qemu_open_old(s->output_path,
                                                O_WRONLY | O_NONBLOCK);
            if (s->backend_output_fd < 0) {
                error_setg_errno(errp, errno,
                                 "failed to open my-virtio-console output-path '%s'",
                                 s->output_path);
                goto fail_open;
            }
        }
        backend_config.u.console.input_fd = s->backend_input_fd;
        backend_config.u.console.output_fd = s->backend_output_fd;
        backend_config.u.console.close_fds = 1;
    }

    memory_region_init_io(&s->iomem, OBJECT(dev), &my_virtio_console_mmio_ops, s,
        "my-virtio-console-regs", 0x1000);

    sysbus_init_mmio(sbd, &s->iomem);

    sysbus_init_irq(sbd, &s->irq);

    s->rx_bh = qemu_bh_new(my_virtio_console_rx_bh, s);
    s->handle = virtio_mmio_create(VIRTIO_EMU_NAME_CONSOLE, base, len, &ops, (void *)s);
    if (!s->handle) {
        error_setg(errp, "failed to create my-virtio-console protocol device");
        goto fail_open;
    }

    s->backend = virtio_backend_create(&backend_config);
    if (backend_type == VIRTIO_BACKEND_CONSOLE_FD) {
        s->backend_input_fd = -1;
        s->backend_output_fd = -1;
    }
    if (!s->backend) {
        error_setg(errp, "failed to create my-virtio-console backend");
        goto fail_open;
    }

    if (backend_type == VIRTIO_BACKEND_CONSOLE_PTY) {
        struct virtio_backend_info info;

        if (!virtio_backend_get_info(s->backend, &info) &&
            info.type == VIRTIO_BACKEND_CONSOLE &&
            info.u.console.pty_path) {
            info_report("my-virtio-console pty backend slave path: %s",
                        info.u.console.pty_path);
        }
    }

    if (backend_type == VIRTIO_BACKEND_CONSOLE_EXTERNAL) {
        qemu_chr_fe_set_handlers(&s->chr,
                                 my_virtio_console_chr_can_read,
                                 my_virtio_console_chr_read,
                                 NULL,
                                 NULL,
                                 s,
                                 NULL,
                                 true);
    }
    return;

fail_open:
    if (s->backend_output_fd >= 0 && s->backend_output_fd != s->backend_input_fd) {
        close(s->backend_output_fd);
    }
    if (s->backend_input_fd >= 0) {
        close(s->backend_input_fd);
    }
    s->backend_input_fd = -1;
    s->backend_output_fd = -1;
}

static void my_virtio_console_unrealize(DeviceState *dev)
{
    MyVirtioStateConsole *s = MY_VIRTIO_CONSOLE(dev);

    if (s->rx_bh) {
        qemu_bh_delete(s->rx_bh);
        s->rx_bh = NULL;
    }
    virtio_backend_destroy(s->backend);
    s->backend = NULL;
}

static const Property my_virtio_console_properties[] = {
    DEFINE_PROP_CHR("chardev", MyVirtioStateConsole, chr),
    DEFINE_PROP_STRING("backend", MyVirtioStateConsole, backend_name),
    DEFINE_PROP_STRING("input-path", MyVirtioStateConsole, input_path),
    DEFINE_PROP_STRING("output-path", MyVirtioStateConsole, output_path),
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
