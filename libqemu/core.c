/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "qemu/rcu.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "hw/qdev-core.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/loader.h"
#include "hw/core/cpu.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "system/reset.h"
#include "libqemu/internal.h"

/* All lists below are accessed on the QEMU worker under BQL. */
typedef struct CoreMemory {
    MemoryRegion mr;
    MemoryRegionOps ops;
    LibQemuReadFn read;
    LibQemuWriteFn write;
    void *opaque;
} CoreMemory;

struct LibQemuTimer {
    QEMUTimer *timer;
    LibQemuTimerFn fn;
    void *opaque;
};

typedef struct CoreIRQ {
    LibQemuIRQFn fn;
    void *opaque;
} CoreIRQ;

static GSList *memories;
static GSList *timers;
static GSList *irqs;
static GSList *reset_cpus;

static int fail(char **errp, const char *message)
{
    if (errp) {
        *errp = g_strdup(message);
    }
    return -1;
}

static int from_error(Error *error, char **errp)
{
    if (!error) {
        return 0;
    }
    fail(errp, error_get_pretty(error));
    error_free(error);
    return -1;
}

/* A small request structure keeps public entrypoints independent of QEMU types. */
typedef struct Request {
    Object *obj, *other;
    const char *name, *text;
    void *result, *buffer;
    uint64_t value, size;
    int64_t signed_value;
    int index, op;
    bool flag;
    MemoryRegion *mr, *child;
    AddressSpace *as;
    const LibQemuMemoryConfig *config;
    qemu_irq irq;
    LibQemuIRQFn irq_fn;
    LibQemuTimerFn timer_fn;
    LibQemuTimer *timer;
} Request;

static int object_new_work(void *opaque, char **errp)
{
    Request *r = opaque;
    ObjectClass *klass = r->name ? object_class_by_name(r->name) : NULL;
    if (!klass || object_class_is_abstract(klass)) {
        return fail(errp, "unknown or abstract QOM type");
    }
    r->result = object_new(r->name);
    return 0;
}

static LibQemuObject *core_object_new(const char *type, char **errp)
{
    Request r = { .name = type };
    return libqemu_call(object_new_work, &r, true, false, errp) ? NULL : r.result;
}

static int object_ref_work(void *opaque, char **errp)
{
    Request *r = opaque;
    if (!r->obj) {
        return fail(errp, "NULL object");
    }
    if (r->flag) {
        object_ref(r->obj);
    } else {
        object_unref(r->obj);
    }
    return 0;
}

static int core_object_ref(LibQemuObject *obj, char **errp)
{
    Request r = { .obj = (Object *)obj, .flag = true };
    return libqemu_call(object_ref_work, &r, true, false, errp);
}

static int core_object_unref(LibQemuObject *obj, char **errp)
{
    Request r = { .obj = (Object *)obj };
    return libqemu_call(object_ref_work, &r, true, false, errp);
}

static int resolve_work(void *opaque, char **errp)
{
    Request *r = opaque;
    bool ambiguous = false;
    r->result = r->name ? object_resolve_path(r->name, &ambiguous) : NULL;
    return !r->result || ambiguous ? fail(errp, "missing or ambiguous QOM path") : 0;
}

static LibQemuObject *core_resolve(const char *path, char **errp)
{
    Request r = { .name = path };
    return libqemu_call(resolve_work, &r, true, false, errp) ? NULL : r.result;
}

static int add_child_work(void *opaque, char **errp)
{
    Request *r = opaque;
    if (!r->obj || !r->other || !r->name || !*r->name || r->other->parent ||
        object_property_find(r->obj, r->name)) {
        return fail(errp, "invalid or already parented child");
    }
    for (Object *parent = r->obj; parent; parent = parent->parent) {
        if (parent == r->other) {
            return fail(errp, "QOM child relationship would form a cycle");
        }
    }
    object_property_add_child(r->obj, r->name, r->other);
    return 0;
}

static int core_add_child(LibQemuObject *obj, const char *name,
                          LibQemuObject *child, char **errp)
{
    Request r = { .obj = (Object *)obj, .other = (Object *)child, .name = name };
    return libqemu_call(add_child_work, &r, true, false, errp);
}

enum { PROP_STR, PROP_BOOL, PROP_INT, PROP_UINT, PROP_LINK };

static int property_work(void *opaque, char **errp)
{
    Request *r = opaque;
    Error *error = NULL;
    if (!r->obj || !r->name) {
        return fail(errp, "property requires object and name");
    }
    if (r->flag) {
        if (!strcmp(r->name, "realized")) {
            return fail(errp, "use device_realize; runtime unplug is unsupported");
        }
        switch (r->op) {
        case PROP_STR:
            if (!r->text) {
                return fail(errp, "NULL property string");
            }
            object_property_set_str(r->obj, r->name, r->text, &error);
            break;
        case PROP_BOOL:
            object_property_set_bool(r->obj, r->name, r->value, &error);
            break;
        case PROP_INT:
            object_property_set_int(r->obj, r->name, r->signed_value, &error);
            break;
        case PROP_UINT:
            object_property_set_uint(r->obj, r->name, r->value, &error);
            break;
        case PROP_LINK:
            object_property_set_link(r->obj, r->name, r->other, &error);
            break;
        }
    } else {
        switch (r->op) {
        case PROP_STR:
            r->result = object_property_get_str(r->obj, r->name, &error);
            break;
        case PROP_BOOL:
            r->value = object_property_get_bool(r->obj, r->name, &error);
            break;
        case PROP_INT:
            r->signed_value = object_property_get_int(r->obj, r->name, &error);
            break;
        case PROP_UINT:
            r->value = object_property_get_uint(r->obj, r->name, &error);
            break;
        case PROP_LINK:
            r->result = object_property_get_link(r->obj, r->name, &error);
            break;
        }
    }
    return from_error(error, errp);
}

#define SETTER(suffix, ctype, field, kind) \
static int core_set_##suffix(LibQemuObject *obj, const char *name, \
                             ctype value, char **errp) \
{ \
    Request r = { .obj = (Object *)obj, .name = name, .flag = true, \
                  .op = kind, .field = value }; \
    return libqemu_call(property_work, &r, true, false, errp); \
}
SETTER(str, const char *, text, PROP_STR)
SETTER(bool, bool, value, PROP_BOOL)
SETTER(int, int64_t, signed_value, PROP_INT)
SETTER(uint, uint64_t, value, PROP_UINT)
#undef SETTER

static int core_set_link(LibQemuObject *obj, const char *name,
                        LibQemuObject *value, char **errp)
{
    Request r = { .obj = (Object *)obj, .name = name, .flag = true,
                  .op = PROP_LINK, .other = (Object *)value };
    return libqemu_call(property_work, &r, true, false, errp);
}

#define GETTER(suffix, ctype, field, kind) \
static int core_get_##suffix(LibQemuObject *obj, const char *name, \
                             ctype *value, char **errp) \
{ \
    Request r = { .obj = (Object *)obj, .name = name, .op = kind }; \
    int ret; \
    if (!value) { return fail(errp, "NULL property result"); } \
    ret = libqemu_call(property_work, &r, true, false, errp); \
    if (!ret) { *value = r.field; } \
    return ret; \
}
GETTER(bool, bool, value, PROP_BOOL)
GETTER(int, int64_t, signed_value, PROP_INT)
GETTER(uint, uint64_t, value, PROP_UINT)
#undef GETTER

static char *core_get_str(LibQemuObject *obj, const char *name, char **errp)
{
    Request r = { .obj = (Object *)obj, .name = name, .op = PROP_STR };
    return libqemu_call(property_work, &r, true, false, errp) ? NULL : r.result;
}

static LibQemuObject *core_get_link(LibQemuObject *obj, const char *name, char **errp)
{
    Request r = { .obj = (Object *)obj, .name = name, .op = PROP_LINK };
    return libqemu_call(property_work, &r, true, false, errp) ? NULL : r.result;
}

static int device_work(void *opaque, char **errp)
{
    Request *r = opaque;
    DeviceState *dev = (DeviceState *)object_dynamic_cast(r->obj, TYPE_DEVICE);
    Error *error = NULL;
    if (!dev) {
        return fail(errp, "object is not a device");
    }
    if (r->op == 0) {
        r->result = r->name ? qdev_get_child_bus(dev, r->name) : NULL;
        return r->result ? 0 : fail(errp, "child bus not found");
    }
    if (r->op == 1) {
        BusState *bus = r->other ?
            (BusState *)object_dynamic_cast(r->other, TYPE_BUS) : NULL;
        if (dev->realized || (r->other && !bus)) {
            return fail(errp, "device already realized or invalid bus");
        }
        if (!bus && object_dynamic_cast(r->obj, TYPE_SYS_BUS_DEVICE)) {
            bus = sysbus_get_default();
        }
        qdev_realize(dev, bus, &error);
        return from_error(error, errp);
    }
    SysBusDevice *sbd = (SysBusDevice *)object_dynamic_cast(r->obj, TYPE_SYS_BUS_DEVICE);
    if (!sbd || !sysbus_has_mmio(sbd, r->index)) {
        return fail(errp, "invalid SysBus MMIO index");
    }
    r->result = sysbus_mmio_get_region(sbd, r->index);
    return 0;
}

static LibQemuObject *core_bus(LibQemuObject *obj, const char *name, char **errp)
{
    Request r = { .obj = (Object *)obj, .name = name };
    return libqemu_call(device_work, &r, true, false, errp) ? NULL : r.result;
}

static int core_realize(LibQemuObject *obj, LibQemuObject *bus, char **errp)
{
    Request r = { .obj = (Object *)obj, .other = (Object *)bus, .op = 1 };
    return libqemu_call(device_work, &r, true, false, errp);
}

static LibQemuMemory *core_mmio(LibQemuObject *obj, unsigned index, char **errp)
{
    Request r = { .obj = (Object *)obj, .index = index, .op = 2 };
    return libqemu_call(device_work, &r, true, false, errp) ? NULL : r.result;
}

static MemTxResult mmio_read(void *opaque, hwaddr addr, uint64_t *value,
                            unsigned size, MemTxAttrs attrs)
{
    CoreMemory *m = opaque;
    int result;
    if (!m->read) {
        return MEMTX_ERROR;
    }
    libqemu_callback_enter();
    result = m->read(m->opaque, addr, value, size);
    libqemu_callback_leave();
    return result ? MEMTX_ERROR : MEMTX_OK;
}

static MemTxResult mmio_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned size, MemTxAttrs attrs)
{
    CoreMemory *m = opaque;
    int result;
    if (!m->write) {
        return MEMTX_ERROR;
    }
    libqemu_callback_enter();
    result = m->write(m->opaque, addr, value, size);
    libqemu_callback_leave();
    return result ? MEMTX_ERROR : MEMTX_OK;
}

static int memory_new_work(void *opaque, char **errp)
{
    Request *r = opaque;
    const LibQemuMemoryConfig *c = r->config;
    CoreMemory *m;
    Error *error = NULL;
    if (!c || !c->name || !*c->name || !c->size ||
        c->kind < LIBQEMU_CONTAINER || c->kind > LIBQEMU_ROM ||
        (c->kind == LIBQEMU_RAM_PTR && !c->ram_ptr) ||
        (c->kind == LIBQEMU_ALIAS && (!c->alias ||
         c->alias_offset > memory_region_size((MemoryRegion *)c->alias) ||
         c->size > memory_region_size((MemoryRegion *)c->alias) - c->alias_offset))) {
        return fail(errp, "invalid memory configuration");
    }
    if (c->kind == LIBQEMU_MMIO &&
        ((!c->read && !c->write) || !is_power_of_2(c->min_access_size) ||
         !is_power_of_2(c->max_access_size) ||
         c->min_access_size > c->max_access_size || c->max_access_size > 8)) {
        return fail(errp, "invalid MMIO callbacks/access sizes");
    }
    m = g_new0(CoreMemory, 1);
    switch (c->kind) {
    case LIBQEMU_CONTAINER:
        memory_region_init(&m->mr, NULL, c->name, c->size);
        break;
    case LIBQEMU_RAM:
        memory_region_init_ram(&m->mr, NULL, c->name, c->size, &error);
        break;
    case LIBQEMU_ROM:
        memory_region_init_rom(&m->mr, NULL, c->name, c->size, &error);
        break;
    case LIBQEMU_RAM_PTR:
        memory_region_init_ram_ptr(&m->mr, NULL, c->name, c->size, c->ram_ptr);
        break;
    case LIBQEMU_ALIAS:
        memory_region_init_alias(&m->mr, NULL, c->name, (MemoryRegion *)c->alias,
                                 c->alias_offset, c->size);
        break;
    case LIBQEMU_MMIO:
        m->read = c->read;
        m->write = c->write;
        m->opaque = c->opaque;
        m->ops.read_with_attrs = mmio_read;
        m->ops.write_with_attrs = mmio_write;
        m->ops.endianness = c->big_endian ? DEVICE_BIG_ENDIAN : DEVICE_LITTLE_ENDIAN;
        m->ops.valid.min_access_size = m->ops.impl.min_access_size = c->min_access_size;
        m->ops.valid.max_access_size = m->ops.impl.max_access_size = c->max_access_size;
        memory_region_init_io(&m->mr, NULL, &m->ops, m, c->name, c->size);
        break;
    }
    /* MemoryRegion is an embedded QOM object parented under /unattached. */
    memories = g_slist_prepend(memories, m);
    if (error) {
        return from_error(error, errp);
    }
    r->result = &m->mr;
    return 0;
}

static LibQemuMemory *core_memory_new(const LibQemuMemoryConfig *c, char **errp)
{
    Request r = { .config = c };
    return libqemu_call(memory_new_work, &r, true, false, errp) ? NULL : r.result;
}

static int memory_work(void *opaque, char **errp)
{
    Request *r = opaque;
    switch (r->op) {
    case 0:
        r->result = get_system_memory();
        return 0;
    case 1:
        if (!r->mr || !object_dynamic_cast(OBJECT(r->mr), TYPE_MEMORY_REGION)) {
            return fail(errp, "invalid memory region");
        }
        r->result = OBJECT(r->mr);
        return 0;
    case 2:
        if (!r->mr || !r->child || r->child->container || r->mr == r->child ||
            r->value > memory_region_size(r->mr) ||
            memory_region_size(r->child) > memory_region_size(r->mr) - r->value) {
            return fail(errp, "invalid memory mapping");
        }
        for (MemoryRegion *parent = r->mr; parent; parent = parent->container) {
            if (parent == r->child) {
                return fail(errp, "memory mapping would form a cycle");
            }
        }
        memory_region_add_subregion_overlap(r->mr, r->value, r->child, r->index);
        return 0;
    case 3:
        if (!r->mr || !r->child || r->child->container != r->mr) {
            return fail(errp, "region is not mapped in this container");
        }
        memory_region_del_subregion(r->mr, r->child);
        return 0;
    case 4:
        if (!r->mr || !r->name) {
            return fail(errp, "address space requires root and name");
        }
        r->result = g_new0(AddressSpace, 1);
        address_space_init(r->result, r->mr, r->name);
        return 0;
    case 5:
        r->result = &address_space_memory;
        return 0;
    case 6:
        if (!r->as || (!r->buffer && r->size) || UINT64_MAX - r->value < r->size) {
            return fail(errp, "invalid memory transaction");
        }
        if (address_space_rw(r->as, r->value, MEMTXATTRS_UNSPECIFIED,
                             r->buffer, r->size, r->flag) != MEMTX_OK) {
            return fail(errp, "memory transaction failed");
        }
        return 0;
    }
    g_assert_not_reached();
}

static LibQemuMemory *core_system_memory(char **errp)
{
    Request r = { 0 };
    return libqemu_call(memory_work, &r, true, false, errp) ? NULL : r.result;
}

static LibQemuObject *core_memory_object(LibQemuMemory *mr, char **errp)
{
    Request r = { .op = 1, .mr = (MemoryRegion *)mr };
    return libqemu_call(memory_work, &r, true, false, errp) ? NULL : r.result;
}

static int core_memory_add(LibQemuMemory *mr, uint64_t addr, LibQemuMemory *child,
                           int priority, char **errp)
{
    Request r = { .op = 2, .mr = (MemoryRegion *)mr, .child = (MemoryRegion *)child,
                  .value = addr, .index = priority };
    return libqemu_call(memory_work, &r, true, false, errp);
}

static int core_memory_del(LibQemuMemory *mr, LibQemuMemory *child, char **errp)
{
    Request r = { .op = 3, .mr = (MemoryRegion *)mr, .child = (MemoryRegion *)child };
    return libqemu_call(memory_work, &r, true, false, errp);
}

static LibQemuAddressSpace *core_as_new(LibQemuMemory *mr, const char *name, char **errp)
{
    Request r = { .op = 4, .mr = (MemoryRegion *)mr, .name = name };
    return libqemu_call(memory_work, &r, true, false, errp) ? NULL : r.result;
}

static LibQemuAddressSpace *core_system_as(char **errp)
{
    Request r = { .op = 5 };
    return libqemu_call(memory_work, &r, false, false, errp) ? NULL : r.result;
}

static int core_memory_rw(LibQemuAddressSpace *as, uint64_t addr, void *buf,
                          size_t size, bool write, char **errp)
{
    Request r = { .op = 6, .as = (AddressSpace *)as, .value = addr,
                  .buffer = buf, .size = size, .flag = write };
    return libqemu_call(memory_work, &r, false, true, errp);
}

static void irq_callback(void *opaque, int n, int level)
{
    CoreIRQ *irq = opaque;
    if (irq->fn) {
        libqemu_callback_enter();
        irq->fn(irq->opaque, level);
        libqemu_callback_leave();
    }
}

static int irq_work(void *opaque, char **errp)
{
    Request *r = opaque;
    DeviceState *dev;
    NamedGPIOList *gpio;
    if (r->op == 0) {
        if (!r->irq_fn) {
            return fail(errp, "IRQ requires callback");
        }
        CoreIRQ *irq = g_new0(CoreIRQ, 1);
        irq->fn = r->irq_fn;
        irq->opaque = r->buffer;
        irqs = g_slist_prepend(irqs, irq);
        r->result = qemu_allocate_irq(irq_callback, irq, 0);
        return 0;
    }
    if (r->op == 1) {
        if (!r->irq) {
            return fail(errp, "NULL IRQ");
        }
        qemu_set_irq(r->irq, r->index != 0);
        return 0;
    }
    dev = (DeviceState *)object_dynamic_cast(r->obj, TYPE_DEVICE);
    if (!dev || r->index < 0) {
        return fail(errp, "invalid GPIO device/index");
    }
    if (r->op == 4) {
        SysBusDevice *sbd = (SysBusDevice *)object_dynamic_cast(
            r->obj, TYPE_SYS_BUS_DEVICE);
        if (!sbd || !sysbus_has_irq(sbd, r->index) || !r->irq) {
            return fail(errp, "invalid SysBus IRQ");
        }
        sysbus_connect_irq(sbd, r->index, r->irq);
        return 0;
    }
    QLIST_FOREACH(gpio, &dev->gpios, node) {
        if (!g_strcmp0(gpio->name, r->name)) {
            if (r->op == 2 && r->index < gpio->num_in) {
                r->result = qdev_get_gpio_in_named(dev, r->name, r->index);
                return 0;
            }
            if (r->op == 3 && r->index < gpio->num_out && r->irq) {
                qdev_connect_gpio_out_named(dev, r->name, r->index, r->irq);
                return 0;
            }
        }
    }
    return fail(errp, "GPIO name/index not found");
}

static LibQemuIRQ *core_irq_new(LibQemuIRQFn fn, void *opaque, char **errp)
{
    Request r = { .irq_fn = fn, .buffer = opaque };
    return libqemu_call(irq_work, &r, true, false, errp) ? NULL : r.result;
}

static int core_irq_set(LibQemuIRQ *irq, int level, char **errp)
{
    Request r = { .op = 1, .irq = (qemu_irq)irq, .index = level };
    return libqemu_call(irq_work, &r, false, true, errp);
}

static LibQemuIRQ *core_gpio_in(LibQemuObject *obj, const char *name, int index,
                               char **errp)
{
    Request r = { .op = 2, .obj = (Object *)obj, .name = name, .index = index };
    return libqemu_call(irq_work, &r, true, false, errp) ? NULL : r.result;
}

static int core_gpio_connect(LibQemuObject *obj, const char *name, int index,
                             LibQemuIRQ *irq, char **errp)
{
    Request r = { .op = 3, .obj = (Object *)obj, .name = name,
                  .index = index, .irq = (qemu_irq)irq };
    return libqemu_call(irq_work, &r, true, false, errp);
}

static int core_sysbus_irq(LibQemuObject *obj, unsigned index, LibQemuIRQ *irq,
                           char **errp)
{
    Request r = { .op = 4, .obj = (Object *)obj, .index = index, .irq = (qemu_irq)irq };
    return libqemu_call(irq_work, &r, true, false, errp);
}

static void timer_callback(void *opaque)
{
    LibQemuTimer *timer = opaque;
    if (timer->fn) {
        libqemu_callback_enter();
        timer->fn(timer->opaque);
        libqemu_callback_leave();
    }
}

static int timer_work(void *opaque, char **errp)
{
    Request *r = opaque;
    if (r->op == 0) {
        if (!r->timer_fn) {
            return fail(errp, "timer requires callback");
        }
        LibQemuTimer *timer = g_new0(LibQemuTimer, 1);
        timer->fn = r->timer_fn;
        timer->opaque = r->buffer;
        timer->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, timer_callback, timer);
        timers = g_slist_prepend(timers, timer);
        r->result = timer;
    } else if (r->op == 3) {
        r->signed_value = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    } else {
        if (!r->timer || !g_slist_find(timers, r->timer)) {
            return fail(errp, "invalid timer handle");
        }
        if (r->op == 1) {
            timer_mod_ns(r->timer->timer, r->signed_value);
        } else {
            timer_del(r->timer->timer);
        }
    }
    return 0;
}

static LibQemuTimer *core_timer_new(LibQemuTimerFn fn, void *opaque, char **errp)
{
    Request r = { .timer_fn = fn, .buffer = opaque };
    return libqemu_call(timer_work, &r, true, false, errp) ? NULL : r.result;
}

static int core_timer_mod(LibQemuTimer *timer, int64_t deadline, char **errp)
{
    Request r = { .op = 1, .timer = timer, .signed_value = deadline };
    return libqemu_call(timer_work, &r, false, false, errp);
}

static int core_timer_del(LibQemuTimer *timer, char **errp)
{
    Request r = { .op = 2, .timer = timer };
    return libqemu_call(timer_work, &r, false, false, errp);
}

static int core_clock(int64_t *value, char **errp)
{
    Request r = { .op = 3 };
    int ret;
    if (!value) {
        return fail(errp, "NULL clock result");
    }
    ret = libqemu_call(timer_work, &r, false, false, errp);
    if (!ret) {
        *value = r.signed_value;
    }
    return ret;
}

static void reset_cpu(void *opaque)
{
    cpu_reset(CPU(opaque));
}

static int cpu_reset_work(void *opaque, char **errp)
{
    Request *r = opaque;
    if (!object_dynamic_cast(r->obj, TYPE_CPU) || g_slist_find(reset_cpus, r->obj)) {
        return fail(errp, "invalid CPU or duplicate reset registration");
    }
    object_ref(r->obj);
    reset_cpus = g_slist_prepend(reset_cpus, r->obj);
    qemu_register_reset(reset_cpu, r->obj);
    return 0;
}

static int core_cpu_reset(LibQemuObject *obj, char **errp)
{
    Request r = { .obj = (Object *)obj };
    return libqemu_call(cpu_reset_work, &r, true, false, errp);
}

static int rom_blob_work(void *opaque, char **errp)
{
    Request *r = opaque;
    MemoryRegionSection section;
    bool valid;

    if (!r->name || !*r->name || !r->buffer || !r->size ||
        r->size > UINT64_MAX - r->value) {
        return fail(errp, "invalid boot blob");
    }
    section = memory_region_find(get_system_memory(), r->value, r->size);
    valid = section.mr && memory_region_is_ram(section.mr) &&
            int128_ge(section.size, int128_make64(r->size));
    if (section.mr) {
        memory_region_unref(section.mr);
    }
    if (!valid) {
        return fail(errp, "boot blob must fit in one mapped RAM/ROM region");
    }
    rom_add_blob_fixed_as(r->name, r->buffer, r->size, r->value,
                         &address_space_memory);
    return 0;
}

static int core_rom_blob(const char *name, uint64_t addr, const void *buf,
                         size_t size, char **errp)
{
    Request r = { .name = name, .value = addr, .buffer = (void *)buf,
                  .size = size };
    return libqemu_call(rom_blob_work, &r, true, false, errp);
}

void libqemu_core_cleanup(void)
{
    for (GSList *p = timers; p; p = p->next) {
        LibQemuTimer *timer = p->data;
        timer_del(timer->timer);
        timer->fn = NULL;
        timer->opaque = NULL;
    }
    for (GSList *p = memories; p; p = p->next) {
        CoreMemory *m = p->data;
        m->read = NULL;
        m->write = NULL;
        m->opaque = NULL;
    }
    for (GSList *p = irqs; p; p = p->next) {
        CoreIRQ *irq = p->data;
        irq->fn = NULL;
        irq->opaque = NULL;
    }
}

const LibQemuCoreExports libqemu_core_exports = {
    .struct_size = sizeof(LibQemuCoreExports),
    .object_new = core_object_new,
    .object_ref = core_object_ref,
    .object_unref = core_object_unref,
    .object_resolve = core_resolve,
    .object_add_child = core_add_child,
    .property_set_str = core_set_str,
    .property_set_bool = core_set_bool,
    .property_set_int = core_set_int,
    .property_set_uint = core_set_uint,
    .property_set_link = core_set_link,
    .property_get_str = core_get_str,
    .property_get_bool = core_get_bool,
    .property_get_int = core_get_int,
    .property_get_uint = core_get_uint,
    .property_get_link = core_get_link,
    .device_get_bus = core_bus,
    .device_realize = core_realize,
    .sysbus_get_mmio = core_mmio,
    .system_memory = core_system_memory,
    .memory_new = core_memory_new,
    .memory_object = core_memory_object,
    .memory_add = core_memory_add,
    .memory_del = core_memory_del,
    .address_space_new = core_as_new,
    .system_address_space = core_system_as,
    .memory_rw = core_memory_rw,
    .irq_new = core_irq_new,
    .gpio_in = core_gpio_in,
    .gpio_connect = core_gpio_connect,
    .sysbus_connect_irq = core_sysbus_irq,
    .irq_set = core_irq_set,
    .timer_new = core_timer_new,
    .timer_mod = core_timer_mod,
    .timer_del = core_timer_del,
    .clock_get_ns = core_clock,
    .cpu_register_reset = core_cpu_reset,
    .rom_add_blob = core_rom_blob,
};
