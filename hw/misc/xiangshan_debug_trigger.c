#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "qemu/module.h"
#include "exec/address-spaces.h"
#include "exec/cpu-common.h"
#include "qapi/error.h"

#define TYPE_MY_DEBUG_TRIGGER "my-debug-trigger"

OBJECT_DECLARE_SIMPLE_TYPE(MyDebugTriggerState, MY_DEBUG_TRIGGER)

struct MyDebugTriggerState{
    DeviceState parent_obj;

    uint64_t addr;
    char *file;
};

static Property my_debug_trigger_props[] = {
    DEFINE_PROP_STRING("file", MyDebugTriggerState, file),
    DEFINE_PROP_UINT64("addr", MyDebugTriggerState, addr, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void my_debug_trigger_realize(DeviceState *dev, Error **errp)
{
    MyDebugTriggerState *s = MY_DEBUG_TRIGGER(dev);
    g_autofree char *data = NULL;
    gsize len = 0;

    if (!s->file) {
        error_setg(errp, "my-debug-trigger: 'file' property not set");
        return;
    }

    if (!g_file_get_contents(s->file, &data, &len, NULL))
        return;

    if (len == 0)
        return;

    if (len > 4096) {
        error_setg(errp, "my-debug-trigger: file contents is too larget");
        return;
    }

    cpu_physical_memory_write(s->addr, data, len);
}

static void my_debug_trigger_unrealize(DeviceState *dev)
{

}

static void my_debug_trigger_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = my_debug_trigger_realize;
    dc->unrealize = my_debug_trigger_unrealize;
    dc->desc = "my-debug-trigger";
    dc->user_creatable = true;
    device_class_set_props(dc, my_debug_trigger_props);
}

static const TypeInfo my_debug_trigger = {
    .name = TYPE_MY_DEBUG_TRIGGER,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(MyDebugTriggerState),
    .class_init = my_debug_trigger_class_init,
};

static void my_debug_trigger_type(void)
{
    type_register_static(&my_debug_trigger);
}
type_init(my_debug_trigger_type);
