#ifndef HW_SWITCH_H
#define HW_SWITCH_H

#include "elf.h"
#include "hw/qdev-core.h"
#include "qom/object.h"

struct HwSwitchState {
    /* <private> */
    DeviceState parent_obj;

    /* <public> */
    CPUState *cpu;

    char *boot;
    char *file;

    bool set_pc;
    uint64_t addr;
    uint64_t data;
    bool data_be;
};



#define TYPE_HW_SWITCH  "hw_switch"
#define ROM_BOOT_ADDR       0X1000
#define FLASH_BOOT_ADDR     0X10000000


OBJECT_DECLARE_SIMPLE_TYPE(HwSwitchState, HW_SWITCH)

#endif