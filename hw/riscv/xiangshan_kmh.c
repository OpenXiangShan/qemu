/*
 * QEMU RISC-V Board Compatible with the Xiangshan Kunminghu
 * FPGA prototype platform
 *
 * Copyright (c) 2025 Beijing Institute of Open Source Chip (BOSC)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Provides a board compatible with the Xiangshan Kunminghu
 * FPGA prototype platform:
 *
 * 0) UART (16550A)
 * 1) CLINT (Core-Local Interruptor)
 * 2) IMSIC (Incoming MSI Controller)
 * 3) APLIC (Advanced Platform-Level Interrupt Controller)
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
#include "system/address-spaces.h"
#include "hw/boards.h"
#include "hw/char/serial-mm.h"
#include "hw/char/xilinx_uartlite.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/riscv_aplic.h"
#include "hw/intc/riscv_imsic.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/xiangshan_kmh.h"
#include "hw/riscv/riscv_hart.h"
#include "system/system.h"
#include "hw/misc/unimp.h"


static const MemMapEntry xiangshan_kmh_memmap[] = {
    [XIANGSHAN_KMH_ROM]      =        {     0x1000,       0x40000 },
    [XIANGSHAN_KMH_FLASH]    =        { 0x10000000,     0x4000000 },
    [XIANGSHAN_KMH_UART0]    =        { 0x310B0000,       0x10000 },
    [XIANGSHAN_KMH_CLINT]    =        { 0x38000000,       0x10000 },
    [XIANGSHAN_KMH_APLIC_M]  =        { 0x31100000,        0x4000 },
    [XIANGSHAN_KMH_APLIC_S]  =        { 0x31120000,        0x4000 },
    [XIANGSHAN_KMH_SRAM]     =        { 0x37f00000,      0x100000 },
    [XIANGSHAN_KMH_IMSIC_M]  =        { 0x3A800000,       0x10000 },
    [XIANGSHAN_KMH_IMSIC_S]  =        { 0x3B000000,       0x80000 },
    [XIANGSHAN_KMH_UART1]    =        { 0x40600000,        0x1000 },
    [XIANGSHAN_KMH_DRAM]     =        { 0x80000000,           0x0 },
};

static void xiangshan_kmh_dw_pcie_init(XiangshanKmhSoCState *s)
{
    DesignwarePCIEHost *pcie0 = &s->pcie0;
    qemu_irq irq;

    /*
     * PCIE RC0
     */
    sysbus_realize(SYS_BUS_DEVICE(pcie0), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(pcie0), 0, 0x32000000);
    create_unimplemented_device("pcie0-phy", 0x60000000, 512 * MiB);

    irq = qdev_get_gpio_in(DEVICE(s->irqchip), XIANGSHAN_KMH_RC_MSI0_IRQ); //MSI
    sysbus_connect_irq(SYS_BUS_DEVICE(pcie0), 0, irq);
    irq = qdev_get_gpio_in(DEVICE(s->irqchip), XIANGSHAN_KMH_RC_HP_IRQ); //HP
    sysbus_connect_irq(SYS_BUS_DEVICE(pcie0), 0, irq);
    //DESIGNWARE_PCIE_IRQ_MSI
    pcie0->pci.irqs[3] = qdev_get_gpio_in(DEVICE(s->irqchip), XIANGSHAN_KMH_RC_MSI0_IRQ);
    //FIXME: How to add imisc_s mr on pci.address_space ,  Does have other api???
    pcie0->pci.address_space.root = get_system_memory();
}

static void xiangshan_kmh_fill_pcie_memmap(void)
{
    create_unimplemented_device("pcie0-cfg1", 0x40000000, 512 * MiB);
    create_unimplemented_device("pcie0-phy1", 0x4000000000, 640 * GiB); // 0x40_0000_0000 ~ 0xE0_0000_0000
}

static DeviceState *xiangshan_kmh_create_aia(uint32_t num_harts)
{
    int i;
    const MemMapEntry *memmap = xiangshan_kmh_memmap;
    hwaddr addr = 0;
    DeviceState *aplic_m = NULL;

    /* M-level IMSICs */
    addr = memmap[XIANGSHAN_KMH_IMSIC_M].base;
    for (i = 0; i < num_harts; i++) {
        riscv_imsic_create(addr + i * IMSIC_HART_SIZE(0), i, true,
                           1, XIANGSHAN_KMH_IMSIC_NUM_IDS);
    }

    /* S-level IMSICs */
    addr = memmap[XIANGSHAN_KMH_IMSIC_S].base;
    for (i = 0; i < num_harts; i++) {
        riscv_imsic_create(addr +
                           i * IMSIC_HART_SIZE(XIANGSHAN_KMH_IMSIC_GUEST_BITS),
                           i, false, 1 + XIANGSHAN_KMH_IMSIC_GUEST_BITS,
                           XIANGSHAN_KMH_IMSIC_NUM_IDS);
    }

    /* M-level APLIC */
    aplic_m = riscv_aplic_create(memmap[XIANGSHAN_KMH_APLIC_M].base,
                                 memmap[XIANGSHAN_KMH_APLIC_M].size,
                                 0, 0, XIANGSHAN_KMH_APLIC_NUM_SOURCES,
                                 1, true, true, NULL);

    /* S-level APLIC */
    riscv_aplic_create(memmap[XIANGSHAN_KMH_APLIC_S].base,
                       memmap[XIANGSHAN_KMH_APLIC_S].size,
                       0, 0, XIANGSHAN_KMH_APLIC_NUM_SOURCES,
                       1, true, false, aplic_m);

    return aplic_m;
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

static void xiangshan_kmh_soc_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    XiangshanKmhSoCState *s = XIANGSHAN_KMH_SOC(dev);
    const MemMapEntry *memmap = xiangshan_kmh_memmap;
    MemoryRegion *system_memory = get_system_memory();
    uint32_t num_harts = ms->smp.cpus;

    qdev_prop_set_uint32(DEVICE(&s->cpus), "num-harts", num_harts);
    qdev_prop_set_uint32(DEVICE(&s->cpus), "hartid-base", 0);
    qdev_prop_set_string(DEVICE(&s->cpus), "cpu-type",
                         TYPE_RISCV_CPU_XIANGSHAN_KMH);
    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_fatal);

    /* AIA */
    s->irqchip = xiangshan_kmh_create_aia(num_harts);

    /* UART */
    serial_mm_init(system_memory, memmap[XIANGSHAN_KMH_UART0].base, 2,
                   qdev_get_gpio_in(s->irqchip, XIANGSHAN_KMH_UART0_IRQ),
                   115200, serial_hd(0), DEVICE_LITTLE_ENDIAN);

    /* UART1: Xilinx UART Lite */
    uartlite_init(xiangshan_kmh_memmap[XIANGSHAN_KMH_UART1].base,
                  qdev_get_gpio_in(DEVICE(s->irqchip), XIANGSHAN_KMH_UART1_IRQ),
                  serial_hd(1));

    /* CLINT */
    riscv_aclint_swi_create(memmap[XIANGSHAN_KMH_CLINT].base,
                            0, num_harts, false);
    riscv_aclint_mtimer_create(memmap[XIANGSHAN_KMH_CLINT].base +
                               RISCV_ACLINT_SWI_SIZE,
                               RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                               0, num_harts, RISCV_ACLINT_DEFAULT_MTIMECMP,
                               RISCV_ACLINT_DEFAULT_MTIME,
                               XIANGSHAN_KMH_CLINT_TIMEBASE_FREQ, true);

    /* ROM */
    memory_region_init_rom(&s->rom, OBJECT(dev), "xiangshan.kunminghu.rom",
                           memmap[XIANGSHAN_KMH_ROM].size, &error_fatal);
    memory_region_add_subregion(system_memory,
                                memmap[XIANGSHAN_KMH_ROM].base, &s->rom);

    /* SRAM */
    memory_region_init_ram(&s->sram, OBJECT(dev), "riscv.bosc.kmh.sram",
                           memmap[XIANGSHAN_KMH_SRAM].size, &error_fatal);
    memory_region_add_subregion(system_memory,
                           memmap[XIANGSHAN_KMH_SRAM].base, &s->sram);

    /* FLASH */
    memory_region_init_rom(&s->flash, NULL, "riscv.bosc.kmh.flash0",
                           memmap[XIANGSHAN_KMH_FLASH].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[XIANGSHAN_KMH_FLASH].base,
                                &s->flash);

    xiangshan_kmh_dw_pcie_init(s);
    /*
     * PCIe PHY
     */
    xiangshan_kmh_fill_pcie_memmap();
}

static void xiangshan_kmh_soc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xiangshan_kmh_soc_realize;
    dc->user_creatable = false;
    
}

static void xiangshan_kmh_soc_instance_init(Object *obj)
{
    XiangshanKmhSoCState *s = XIANGSHAN_KMH_SOC(obj);
    MachineState *ms = MACHINE(qdev_get_machine());

    object_initialize_child(obj, "cpus", &s->cpus, TYPE_RISCV_HART_ARRAY);
    object_initialize_child(OBJECT(ms), "pcie0", &s->pcie0, TYPE_DESIGNWARE_PCIE_HOST);
}

static const TypeInfo xiangshan_kmh_soc_info = {
    .name = TYPE_XIANGSHAN_KMH_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(XiangshanKmhSoCState),
    .instance_init = xiangshan_kmh_soc_instance_init,
    .class_init = xiangshan_kmh_soc_class_init,
};

static void xiangshan_kmh_soc_register_types(void)
{
    type_register_static(&xiangshan_kmh_soc_info);
}
type_init(xiangshan_kmh_soc_register_types)

static void xiangshan_kmh_machine_init(MachineState *machine)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(machine);
    const MemMapEntry *memmap = xiangshan_kmh_memmap;
    MemoryRegion *system_memory = get_system_memory();
    hwaddr start_addr = memmap[XIANGSHAN_KMH_DRAM].base;

    /* Initialize SoC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_XIANGSHAN_KMH_SOC);
    qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

    /* Register RAM */
    memory_region_add_subregion(system_memory,
                                memmap[XIANGSHAN_KMH_DRAM].base,
                                machine->ram);

    /* ROM reset vector */
    riscv_setup_rom_reset_vec(machine, &s->soc.cpus,
                              start_addr,
                              memmap[XIANGSHAN_KMH_ROM].base,
                              memmap[XIANGSHAN_KMH_ROM].size, 0, 0);
    if (machine->firmware) {
        riscv_load_firmware(machine->firmware, &start_addr, NULL);
    }

    /* Note: dtb has been integrated into firmware(OpenSBI) when compiling */
}

static void xiangshan_kmh_machine_class_init(ObjectClass *klass, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(klass);
    static const char *const valid_cpu_types[] = {
        TYPE_RISCV_CPU_XIANGSHAN_KMH,
        NULL
    };

    mc->desc = "RISC-V Board compatible with the Xiangshan " \
               "Kunminghu FPGA prototype platform";
    mc->init = xiangshan_kmh_machine_init;
    mc->max_cpus = XIANGSHAN_KMH_MAX_CPUS;
    mc->default_cpu_type = TYPE_RISCV_CPU_XIANGSHAN_KMH;
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_id = "xiangshan.kunminghu.ram";
}

static const TypeInfo xiangshan_kmh_machine_info = {
    .name = TYPE_XIANGSHAN_KMH_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(XiangshanKmhState),
    .class_init = xiangshan_kmh_machine_class_init,
};

static void xiangshan_kmh_machine_register_types(void)
{
    type_register_static(&xiangshan_kmh_machine_info);
}
type_init(xiangshan_kmh_machine_register_types)
