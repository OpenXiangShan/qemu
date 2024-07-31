/*
 * BOSC KunMingHu SoC emulation
 *
 * Copyright (c) 2024 Beijing Institute of Open Source Chip (BOSC)
 *
 * Provides a board compatible with the BOSC KunMingHu SDK:
 *
 * 0) UART
 * 1) CLINT (Core Level Interruptor)
 * 2) PLIC (Platform Level Interrupt Controller)
 * 3) PCIE
 * 4) Flash memory emulated as RAM
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
#include "hw/boards.h"
#include "hw/riscv/bosc_kmh.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/intc/sifive_plic.h"
#include "hw/intc/riscv_aclint.h"
#include "sysemu/sysemu.h"
#include "hw/qdev-properties.h"
#include "exec/address-spaces.h"
#include "hw/riscv/boot.h"
#include "sysemu/device_tree.h"
#include "hw/pci-host/xilinx-pcie.h"



static const MemMapEntry bosc_kmh_memmap[] = {
    [BOSC_KMH_DEV_DEBUG] 	=	{       0x0,    0x100 },
    [BOSC_KMH_DEV_MROM] 	=	{    0x1000,    0xf000 },
    [BOSC_KMH_DEV_FLASH] 	=	{ 0x10000000,   0x4000000 },
    [BOSC_KMH_DEV_UART0] 	=	{ 0x310B0000,   0x10000 },
    [BOSC_KMH_DEV_CLINT] 	=	{ 0x38000000,   0x10000 },
    [BOSC_KMH_DEV_PLIC]         =       { 0x3c000000,   0x4000000},
    [BOSC_KMH_DEV_PCIE_MMIO] 	=	{ 0x40000000,   0x8000000},
    [BOSC_KMH_DEV_PCIE_CFG] 	=	{ 0x48000000,   0x2000000},
    [BOSC_KMH_DEV_DRAM] 	=	{ 0x80000000,   0x0 },
};


static void bosc_kmh_machine_state_init(MachineState *mstate)
{
    BoscKmhMachineState *sms = OBJECT_CHECK(BoscKmhMachineState, mstate,
                                            TYPE_RISCV_KMH_MACHINE);
    MemoryRegion *system_memory = get_system_memory();

    /* Initialize SoC */
    object_initialize_child(OBJECT(mstate), "soc", &sms->soc,
                            TYPE_RISCV_KMH_SOC);
    qdev_realize(DEVICE(&sms->soc), NULL, &error_abort);

    /* register RAM */
    memory_region_add_subregion(system_memory,
                                bosc_kmh_memmap[BOSC_KMH_DEV_DRAM].base,
                                mstate->ram);

    /* ROM reset vector */
    riscv_setup_rom_reset_vec(mstate, &sms->soc.cpus,
                              bosc_kmh_memmap[BOSC_KMH_DEV_DRAM].base,
                              bosc_kmh_memmap[BOSC_KMH_DEV_MROM].base,
                              bosc_kmh_memmap[BOSC_KMH_DEV_MROM].size, 0, 0);
    if (mstate->firmware) {
        riscv_load_firmware(mstate->firmware,
                            bosc_kmh_memmap[BOSC_KMH_DEV_DRAM].base,
                            NULL);
    }

    /* Note: dtb has been integrated into firmware(OpenSBI) when compiling */
}

static void bosc_kmh__machine_instance_init(Object *obj)
{
}

static void bosc_kmh_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    static const char * const valid_cpu_types[] = {
        RISCV_CPU_TYPE_NAME("bosc-kmh"),
		RISCV_CPU_TYPE_NAME("rv64"),
        NULL
    };

    mc->desc = "RISC-V Board compatible with Kunminghu SDK";
    mc->init = bosc_kmh_machine_state_init;
    mc->default_cpu_type = TYPE_RISCV_CPU_BOSC_KMH;
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_id = "riscv.bosc.kmh.ram";
}

static const TypeInfo bosc_kmh_machine_type_info = {
    .name = TYPE_RISCV_KMH_MACHINE,
    .parent = TYPE_MACHINE,
    .class_init = bosc_kmh_machine_class_init,
    .instance_init = bosc_kmh__machine_instance_init,
    .instance_size = sizeof(BoscKmhMachineState),
};

static void bosc_kmh_machine_type_info_register(void)
{
    type_register_static(&bosc_kmh_machine_type_info);
}
type_init(bosc_kmh_machine_type_info_register)


static inline XilinxPCIEHost *
xilinx_pcie_init(MemoryRegion *sys_mem, uint32_t bus_nr,
                 hwaddr cfg_base, uint64_t cfg_size,
                 hwaddr mmio_base, uint64_t mmio_size,
                 qemu_irq irq)
{
    DeviceState *dev;
    MemoryRegion *cfg, *mmio;

    dev = qdev_new(TYPE_XILINX_PCIE_HOST);

    qdev_prop_set_uint32(dev, "bus_nr", bus_nr);
    qdev_prop_set_uint64(dev, "cfg_base", cfg_base);
    qdev_prop_set_uint64(dev, "cfg_size", cfg_size);
    qdev_prop_set_uint64(dev, "mmio_base", mmio_base);
    qdev_prop_set_uint64(dev, "mmio_size", mmio_size);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    cfg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion_overlap(sys_mem, cfg_base, cfg, 0);

    mmio = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_add_subregion_overlap(sys_mem, 0, mmio, 0);

    qdev_connect_gpio_out_named(dev, "interrupt_out", 0, irq);

    return XILINX_PCIE_HOST(dev);
}

static void bosc_kmh_soc_state_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    BoscKmhSoCState *state = RISCV_KMH_SOC(dev);
    MemoryRegion *system_memory = get_system_memory();

    sysbus_realize(SYS_BUS_DEVICE(&state->cpus), &error_abort);

    state->plic = sifive_plic_create(bosc_kmh_memmap[BOSC_KMH_DEV_PLIC].base,
        (char *)BOSC_KMH_PLIC_HART_CONFIG, ms->smp.cpus, 0,
        96,
        7,
        BOSC_KMH_PLIC_PRIORITY_BASE,
        BOSC_KMH_PLIC_PENDING_BASE,
        BOSC_KMH_PLIC_ENABLE_BASE,
        BOSC_KMH_PLIC_ENABLE_STRIDE,
        BOSC_KMH_PLIC_CONTEXT_BASE,
        BOSC_KMH_PLIC_CONTEXT_STRIDE,
        bosc_kmh_memmap[BOSC_KMH_DEV_PLIC].size);

    serial_mm_init(get_system_memory(), bosc_kmh_memmap[BOSC_KMH_DEV_UART0].base, 2,
               qdev_get_gpio_in(DEVICE(state->plic), BOSC_KMH_UART0_IRQ),
               115200, serial_hd(0), DEVICE_LITTLE_ENDIAN);


    riscv_aclint_swi_create(bosc_kmh_memmap[BOSC_KMH_DEV_CLINT].base,
        0, 1, false);
    riscv_aclint_mtimer_create(bosc_kmh_memmap[BOSC_KMH_DEV_CLINT].base +
        RISCV_ACLINT_SWI_SIZE, RISCV_ACLINT_DEFAULT_MTIMER_SIZE, 0, 1,
        RISCV_ACLINT_DEFAULT_MTIMECMP, RISCV_ACLINT_DEFAULT_MTIME,
        RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, true);

    /*
     * PCIe
     */
    xilinx_pcie_init(get_system_memory(), 0,
                     bosc_kmh_memmap[BOSC_KMH_DEV_PCIE_CFG].base,
                     bosc_kmh_memmap[BOSC_KMH_DEV_PCIE_CFG].size,
                     bosc_kmh_memmap[BOSC_KMH_DEV_PCIE_MMIO].base,
                     bosc_kmh_memmap[BOSC_KMH_DEV_PCIE_MMIO].size,
                     qdev_get_gpio_in(DEVICE(state->plic), BOSC_KMH_PCIE0_IRQ0));


    /* ROM */
    memory_region_init_rom(&state->rom, OBJECT(dev), "riscv.bosc.kmh.rom",
                           bosc_kmh_memmap[BOSC_KMH_DEV_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory,
        bosc_kmh_memmap[BOSC_KMH_DEV_MROM].base, &state->rom);
}

static void bosc_kmh_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = bosc_kmh_soc_state_realize;
    /*
     * Reasons:
     *     - Creates CPUS in riscv_hart_realize(), and can create unintended
     *       CPUs
     *     - Uses serial_hds in realize function, thus can't be used twice
     */
    dc->user_creatable = false;
}

static void bosc_kmh_soc_instance_init(Object *obj)
{
    BoscKmhSoCState *state = RISCV_KMH_SOC(obj);

    object_initialize_child(obj, "cpus", &state->cpus, TYPE_RISCV_HART_ARRAY);

    /*
     * CPU type is fixed and we are not supporting passing from commandline so far.
     */
    object_property_set_str(OBJECT(&state->cpus), "cpu-type",
                            TYPE_RISCV_CPU_BOSC_KMH, &error_abort);
    object_property_set_int(OBJECT(&state->cpus), "num-harts", 1,
                            &error_abort);
}

static const TypeInfo bosc_kmh_type_info = {
    .name = TYPE_RISCV_KMH_SOC,
    .parent = TYPE_DEVICE,
    .class_init = bosc_kmh_soc_class_init,
    .instance_init = bosc_kmh_soc_instance_init,
    .instance_size = sizeof(BoscKmhSoCState),
};

static void bosc_kmh_type_info_register(void)
{
    type_register_static(&bosc_kmh_type_info);
}
type_init(bosc_kmh_type_info_register)
