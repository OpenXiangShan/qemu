#include "qemu/osdep.h"
#include "exec/tswap.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/core/hw-switch.h"


static void hw_switch_reset(void *opaque)
{
    HwSwitchState *s = HW_SWITCH(opaque);

    if (s->set_pc) {
        CPUClass *cc = CPU_GET_CLASS(s->cpu);
        cpu_reset(s->cpu);
        if (cc) {
            cc->set_pc(s->cpu, s->addr);
        }
    }
}

static void hw_switch_realize(DeviceState *dev, Error **errp)
{
    HwSwitchState *s = HW_SWITCH(dev);
    int big_endian;
    ssize_t size = 0;
    hwaddr entry;

    if (!strcmp(s->boot, "rom")) {
        s->set_pc = false;
        s->addr = ROM_BOOT_ADDR;
    } else if (!strcmp(s->boot, "flash")) {
        s->set_pc = true;
        s->addr = FLASH_BOOT_ADDR;
    } else {
        s->set_pc = false;
        s->boot = g_strdup("rom");
        s->addr = ROM_BOOT_ADDR;
    }

    s->cpu = first_cpu;

    qemu_register_reset(hw_switch_reset, dev);

    big_endian = target_words_bigendian();

    if (s->file) {
        AddressSpace *as = s->cpu ? s->cpu->as :  NULL;

        size = load_elf_as(s->file, NULL, NULL, NULL, &entry, NULL, NULL,
                            NULL, big_endian, 0, 0, 0, as);

        if (size < 0) {
            size = load_uimage_as(s->file, &entry, NULL, NULL, NULL, NULL,
                                    as);
        }

        if (size < 0) {
            size = load_targphys_hex_as(s->file, &entry, as);
        }

        if (size < 0) {
        /* Default to the maximum size being the machine's ram size */
            size = load_image_targphys_as(s->file, s->addr, current_machine->ram_size, as);
        } else {
            s->addr = entry;
        }

        if (size < 0) {
            error_setg(errp, "Cannot load specified image %s", s->file);
            return;
        }
    }

    /* Convert the data endianness */
    if (s->data_be) {
        s->data = cpu_to_be64(s->data);
    } else {
        s->data = cpu_to_le64(s->data);
    }

}

static void hw_switch_unrealize(DeviceState *dev)
{
    qemu_unregister_reset(hw_switch_reset, dev);
}

static Property hw_switch_props[] = {
    DEFINE_PROP_STRING("boot", HwSwitchState, boot),
    DEFINE_PROP_STRING("file", HwSwitchState, file),
    DEFINE_PROP_END_OF_LIST(),
};

static void hw_switch_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    /* The reset function is not registered here and is instead registered in
     * the realize function to allow this device to be added via the device_add
     * command in the QEMU monitor.
     * TODO: Improve the device_add functionality to allow resets to be
     * connected
     */
    dc->realize = hw_switch_realize;
    dc->unrealize = hw_switch_unrealize;
    device_class_set_props(dc, hw_switch_props);
    dc->desc = "Hw Switch";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo hw_switch_info = {
    .name = TYPE_HW_SWITCH,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(HwSwitchState),
    .class_init = hw_switch_class_init,
};

static void hw_switch_register_type(void)
{
    type_register_static(&hw_switch_info);
}

type_init(hw_switch_register_type)