/*
 * QEMU RISC-V Board Compatible with the Xiangshan Nanhu V5
 * FPGA prototype platform
 *
 * Copyright (c) 2025 Beijing Institute of Open Source Chip (BOSC)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Provides a board compatible with the Xiangshan Nanhu V5
 * FPGA prototype platform:
 *
 * 0) UART
 * 1) CLINT (Core-Local Interruptor)
 * 2) PLIC (Platform-Level Interrupt Controller)
 * 3) PCIE
 * 4) Flash memory emulated as RAM
 *
 * More information can be found in our Github repository:
 * https://github.com/OpenXiangShan/XiangShan
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/intc/sifive_plic.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/riscv_aplic.h"
#include "system/address-spaces.h"
#include "hw/boards.h"
#include "hw/char/serial-mm.h"
#include "hw/char/xilinx_uartlite.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/numa.h"
#include "hw/riscv/xiangshan_nhv5.h"
#include "hw/riscv/riscv_hart.h"
#include "system/system.h"
#include "hw/misc/unimp.h"


static const MemMapEntry xiangshan_nhv5_memmap[] = {
    [XIANGSHAN_NHV5_DEBUG]       =       {        0x0,         0x100 },
    [XIANGSHAN_NHV5_ROM]         =       {     0x1000,        0xf000 },
    [XIANGSHAN_NHV5_FLASH]       =       { 0x10000000,     0x4000000 },
    [XIANGSHAN_NHV5_UART0]       =       { 0x310B0000,       0x10000 },
    [XIANGSHAN_NHV5_CLINT]       =       { 0x38000000,       0x10000 },
    [XIANGSHAN_NHV5_PLIC]        =       { 0x3c000000,      0x4000000},
    [XIANGSHAN_NHV5_UART1]       =       { 0x40600000,        0x1000 },
    [XIANGSHAN_NHV5_DRAM]        =       { 0x80000000,           0x0 },
};

static void xiangshan_nhv5_dw_pcie_init(XiangshanNhv5SoCState *s)
{
    DesignwarePCIEHost *pcie0 = &s->pcie0;
    qemu_irq irq;

    /*
     * PCIE
     */
    sysbus_realize(SYS_BUS_DEVICE(pcie0), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(pcie0), 0, 0x48000000);
    create_unimplemented_device("pcie0-phy", 0x40000000, 128 * MiB);

    irq = qdev_get_gpio_in(DEVICE(s->plic), XIANGSHAN_NHV5_RC0_MSI_IRQ);
    sysbus_connect_irq(SYS_BUS_DEVICE(pcie0), 0, irq);
    irq = qdev_get_gpio_in(DEVICE(s->plic), XIANGSHAN_NHV5_RC0_HP_IRQ);
    sysbus_connect_irq(SYS_BUS_DEVICE(pcie0), 0, irq);

    pcie0->pci.irqs[3] =
            qdev_get_gpio_in(DEVICE(s->plic), XIANGSHAN_NHV5_RC0_MSI_IRQ);
}

static void xiangshan_nhv5_fill_pcie_memmap(void)
{
    create_unimplemented_device("pcie1-cfg1", 0x4c000000, 64 * MiB);
    create_unimplemented_device("pcie1-phy0", 0x60000000, 512 * MiB);
}

static XilinxUARTLite *uartlite_init(hwaddr base, qemu_irq irq, Chardev *chr)
{
    XilinxUARTLite *uartlite = XILINX_UARTLITE(qdev_new(TYPE_XILINX_UARTLITE));

    qdev_prop_set_chr(DEVICE(uartlite), "chardev", chr);
    qdev_prop_set_enum(DEVICE(uartlite), "endianness", ENDIAN_MODE_LITTLE);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(uartlite), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(uartlite), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(uartlite), 0, irq);

    return uartlite;
}

static void xiangshan_nhv5_soc_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    int hart_count;
    char *plic_hart_config;
    XiangshanNhv5SoCState *s = XIANGSHAN_NHV5_SOC(dev);
    const MemMapEntry *memmap = xiangshan_nhv5_memmap;
    MemoryRegion *system_memory = get_system_memory();

    hart_count = riscv_socket_hart_count(ms, 0);

    qdev_prop_set_uint32(DEVICE(&s->cpus), "num-harts", hart_count);
    qdev_prop_set_uint32(DEVICE(&s->cpus), "hartid-base", 0);
    qdev_prop_set_string(DEVICE(&s->cpus), "cpu-type",
                         TYPE_RISCV_CPU_XIANGSHAN_NHV5);
    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_fatal);

    /* Per-socket PLIC hart topology configuration string */
    plic_hart_config = riscv_plic_hart_config_string(hart_count);

    /* Per-socket PLIC */
    s->plic = sifive_plic_create(memmap[XIANGSHAN_NHV5_PLIC].base,
        plic_hart_config, ms->smp.cpus, 0,
        XIANGSHAN_NHV5_PLIC_NUM_SOURCES,
        XIANGSHAN_NHV5_PLIC_NUM_PRIORITIES,
        XIANGSHAN_NHV5_PLIC_PRIORITY_BASE,
        XIANGSHAN_NHV5_PLIC_PENDING_BASE,
        XIANGSHAN_NHV5_PLIC_ENABLE_BASE,
        XIANGSHAN_NHV5_PLIC_ENABLE_STRIDE,
        XIANGSHAN_NHV5_PLIC_CONTEXT_BASE,
        XIANGSHAN_NHV5_PLIC_CONTEXT_STRIDE,
        memmap[XIANGSHAN_NHV5_PLIC].size);

    /* UART0: 16550A */
    serial_mm_init(system_memory, memmap[XIANGSHAN_NHV5_UART0].base, 2,
               qdev_get_gpio_in(DEVICE(s->plic), XIANGSHAN_NHV5_UART0_IRQ),
               115200, serial_hd(0), DEVICE_LITTLE_ENDIAN);

    /* UART1: Xilinx UART Lite */
    uartlite_init(memmap[XIANGSHAN_NHV5_UART1].base,
                  qdev_get_gpio_in(DEVICE(s->plic), XIANGSHAN_NHV5_UART1_IRQ),
                  serial_hd(1));

    /* CLINT */
    riscv_aclint_swi_create(memmap[XIANGSHAN_NHV5_CLINT].base,
        0, hart_count, false);
    riscv_aclint_mtimer_create(memmap[XIANGSHAN_NHV5_CLINT].base +
        RISCV_ACLINT_SWI_SIZE, RISCV_ACLINT_DEFAULT_MTIMER_SIZE, 0, hart_count,
        RISCV_ACLINT_DEFAULT_MTIMECMP, RISCV_ACLINT_DEFAULT_MTIME,
        RISCV_ACLINT_NHV5_TIMEBASE_FREQ, true);

    /* ROM */
    memory_region_init_rom(&s->rom, OBJECT(dev), "xiangshan.nhv5.rom",
                           memmap[XIANGSHAN_NHV5_ROM].size, &error_fatal);
    memory_region_add_subregion(system_memory,
                            memmap[XIANGSHAN_NHV5_ROM].base, &s->rom);

    xiangshan_nhv5_dw_pcie_init(s);
    xiangshan_nhv5_fill_pcie_memmap();
}

static void xiangshan_nhv5_soc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xiangshan_nhv5_soc_realize;
    dc->user_creatable = false;
}

static void xiangshan_nhv5_soc_instance_init(Object *obj)
{
    XiangshanNhv5SoCState *s = XIANGSHAN_NHV5_SOC(obj);
    MachineState *ms = MACHINE(qdev_get_machine());

    object_initialize_child(obj, "cpus", &s->cpus, TYPE_RISCV_HART_ARRAY);
    object_initialize_child(OBJECT(ms), "pcie0", &s->pcie0,
                                               TYPE_DESIGNWARE_PCIE_HOST);
}

static const TypeInfo xiangshan_nhv5_soc_info = {
    .name = TYPE_XIANGSHAN_NHV5_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(XiangshanNhv5SoCState),
    .instance_init = xiangshan_nhv5_soc_instance_init,
    .class_init = xiangshan_nhv5_soc_class_init,
};

static void xiangshan_nhv5_soc_register_types(void)
{
    type_register_static(&xiangshan_nhv5_soc_info);
}
type_init(xiangshan_nhv5_soc_register_types)

static void xiangshan_nhv5_machine_init(MachineState *machine)
{
    XiangshanNhv5State *s = XIANGSHAN_NHV5_MACHINE(machine);
    const MemMapEntry *memmap = xiangshan_nhv5_memmap;
    MemoryRegion *system_memory = get_system_memory();
    hwaddr start_addr = memmap[XIANGSHAN_NHV5_DRAM].base;

    /* Initialize SoC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_XIANGSHAN_NHV5_SOC);
    qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

    /* register RAM */
    memory_region_add_subregion(system_memory,
                                memmap[XIANGSHAN_NHV5_DRAM].base,
                                machine->ram);

    /* ROM reset vector */
    riscv_setup_rom_reset_vec(machine, &s->soc.cpus,
                              start_addr,
                              memmap[XIANGSHAN_NHV5_ROM].base,
                              memmap[XIANGSHAN_NHV5_ROM].size, 0, 0);
    if (machine->firmware) {
        riscv_load_firmware(machine->firmware, &start_addr, NULL);
    }

    /* Note: dtb has been integrated into firmware(OpenSBI) when compiling */
}

static void
xiangshan_nhv5_machine_class_init(ObjectClass *klass, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(klass);
    static const char *const valid_cpu_types[] = {
        TYPE_RISCV_CPU_XIANGSHAN_NHV5,
        NULL
    };

    mc->desc = "RISC-V Board compatible with the Xiangshan " \
               "Nanhu v5 FPGA prototype platform";
    mc->init = xiangshan_nhv5_machine_init;
    mc->max_cpus = XIANGSHAN_NHV5_MAX_CPUS;
    mc->default_cpu_type = TYPE_RISCV_CPU_XIANGSHAN_NHV5;
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_id = "xiangshan.nanhuv5.ram";
}

static const TypeInfo xiangshan_nhv5_machine_info = {
    .name = TYPE_XIANGSHAN_NHV5_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(XiangshanNhv5State),
    .class_init = xiangshan_nhv5_machine_class_init,
};

static void xiangshan_nhv5_machine_register_types(void)
{
    type_register_static(&xiangshan_nhv5_machine_info);
}
type_init(xiangshan_nhv5_machine_register_types)
