/*
 * BOSC NanHuV5 SoC emulation
 *
 * Copyright (c) 2025 Beijing Institute of Open Source Chip (BOSC)
 *
 * Provides a board compatible with the BOSC NanHuV5 SDK:
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
#include "hw/riscv/bosc_nhv5.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/intc/sifive_plic.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/riscv_aplic.h"
#include "hw/char/serial-mm.h"
#include "hw/char/xilinx_uartlite.h"
#include "sysemu/sysemu.h"
#include "hw/qdev-properties.h"
#include "exec/address-spaces.h"
#include "hw/riscv/boot.h"
#include "sysemu/device_tree.h"
#include "kvm/kvm_riscv.h"
#include "sysemu/kvm.h"
#include "hw/riscv/numa.h"
#include "hw/misc/unimp.h"

/**/
static const MemMapEntry bosc_nhv5_memmap[] = {
    [BOSC_NHV5_DEV_DEBUG] 	=	{       0x0,    0x100 },
    [BOSC_NHV5_DEV_MROM] 	=	{    0x1000,    0xf000 },
    [BOSC_NHV5_DEV_FLASH] 	=	{ 0x10000000,   0x4000000 },
    [BOSC_NHV5_DEV_UART0] 	=	{ 0x310B0000,   0x10000 },
    [BOSC_NHV5_DEV_CLINT] 	=	{ 0x38000000,   0x10000 },
    [BOSC_NHV5_DEV_PLIC]     =  { 0x3c000000,   0x4000000},
    [BOSC_NHV5_DEV_UART1] 	=	{ 0x40600000,   0x1000 },
    [BOSC_NHV5_DEV_DRAM] 	=	{ 0x80000000,   0x0 },
};
static void bosc_nh_dw_pcie_init(BoscNhv5SoCState *s)
{
    DesignwarePCIEHost *pcie0 = &s->pcie0;
    qemu_irq irq;

    /*
     * PCIE
     */
    sysbus_realize(SYS_BUS_DEVICE(pcie0), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(pcie0), 0, 0x48000000);
    create_unimplemented_device("pcie0-phy", 0x40000000, 128 * MiB);

    irq = qdev_get_gpio_in(DEVICE(s->plic), BOSC_KMH_RC0_MSI_IRQ); //MSI
    sysbus_connect_irq(SYS_BUS_DEVICE(pcie0), 0, irq);
    irq = qdev_get_gpio_in(DEVICE(s->plic), BOSC_KMH_RC0_HP_IRQ); //HP
    sysbus_connect_irq(SYS_BUS_DEVICE(pcie0), 0, irq);
    //DESIGNWARE_PCIE_IRQ_MSI
    pcie0->pci.irqs[3] = qdev_get_gpio_in(DEVICE(s->plic), BOSC_KMH_RC0_MSI_IRQ);

    create_unimplemented_device("pcie1-cfg1", 0x4c000000, 64 * MiB);
    create_unimplemented_device("pcie1-phy0", 0x60000000, 512 * MiB);
}

static void bosc_nhv5_machine_state_init(MachineState *mstate)
{
    BoscNhv5MachineState *sms = OBJECT_CHECK(BoscNhv5MachineState, mstate,
                                            TYPE_RISCV_NHV5_MACHINE);
    MemoryRegion *system_memory = get_system_memory();

    /* Initialize SoC */
    object_initialize_child(OBJECT(mstate), "soc", &sms->soc,
                            TYPE_RISCV_NHV5_SOC);
    qdev_realize(DEVICE(&sms->soc), NULL, &error_abort);

    /* register RAM */
    memory_region_add_subregion(system_memory,
                                bosc_nhv5_memmap[BOSC_NHV5_DEV_DRAM].base,
                                mstate->ram);
    
    /* ROM reset vector */
    riscv_setup_rom_reset_vec(mstate, &sms->soc.cpus,
                              bosc_nhv5_memmap[BOSC_NHV5_DEV_DRAM].base,
                              bosc_nhv5_memmap[BOSC_NHV5_DEV_MROM].base,
                              bosc_nhv5_memmap[BOSC_NHV5_DEV_MROM].size, 0, 0);
    if (mstate->firmware) {
        riscv_load_firmware(mstate->firmware,
                            (target_ulong *)&bosc_nhv5_memmap[BOSC_NHV5_DEV_DRAM].base,
                            NULL);
    }
}

static void bosc_nhv5_machine_instance_init(Object *obj)
{

}

static void bosc_nhv5_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    static const char * const valid_cpu_types[] = {
        RISCV_CPU_TYPE_NAME("bosc-nhv5"),
		RISCV_CPU_TYPE_NAME("rv64"),
        NULL
    };

    mc->desc = "RISC-V Board compatible with NanhuV5 SDK";
    mc->init = bosc_nhv5_machine_state_init;
    mc->max_cpus = BOSC_NHV5_CPUS_MAX;
    mc->default_cpu_type = TYPE_RISCV_CPU_BOSC_NHV5;
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_id = "riscv.bosc.nhv5.ram";
}

static const TypeInfo bosc_nhv5_machine_type_info = {
    .name = TYPE_RISCV_NHV5_MACHINE,
    .parent = TYPE_MACHINE,
    .class_init = bosc_nhv5_machine_class_init,
    .instance_init = bosc_nhv5_machine_instance_init,
    .instance_size = sizeof(BoscNhv5MachineState),
};

static void bosc_nhv5_machine_type_info_register(void)
{
    type_register_static(&bosc_nhv5_machine_type_info);
}
type_init(bosc_nhv5_machine_type_info_register)

static XilinxUARTLite *uartlite_init(hwaddr base, qemu_irq irq, Chardev *chr)
{
    XilinxUARTLite *uartlite = XILINX_UARTLITE(qdev_new(TYPE_XILINX_UARTLITE));

    qdev_prop_set_chr(DEVICE(uartlite), "chardev", chr);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(uartlite), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(uartlite), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(uartlite), 0, irq);

    return uartlite;
}

static void bosc_nhv5_soc_state_realize(DeviceState *dev, Error **errp)
{
    int hart_count;
    char *plic_hart_config;
    const MemMapEntry *memmap = bosc_nhv5_memmap;
    MachineState *ms = MACHINE(qdev_get_machine());
    BoscNhv5SoCState *state = RISCV_NHV5_SOC(dev);
    MemoryRegion *system_memory = get_system_memory();

    sysbus_realize(SYS_BUS_DEVICE(&state->cpus), &error_abort);

//    state->aia_type =  BOSC_NHV5_AIA_TYPE_APLIC;
    hart_count = riscv_socket_hart_count(ms, 0);

    /* Per-socket PLIC hart topology configuration string */
    plic_hart_config = riscv_plic_hart_config_string(hart_count);

    /* Per-socket PLIC */
    state->plic = sifive_plic_create(memmap[BOSC_NHV5_DEV_PLIC].base,
        plic_hart_config, ms->smp.cpus, 0,
        BOSC_NHV5_PLIC_NUM_SOURCES,
        BOSC_NHV5_PLIC_NUM_PRIORITIES,
        BOSC_NHV5_PLIC_PRIORITY_BASE,
        BOSC_NHV5_PLIC_PENDING_BASE,
        BOSC_NHV5_PLIC_ENABLE_BASE,
        BOSC_NHV5_PLIC_ENABLE_STRIDE,
        BOSC_NHV5_PLIC_CONTEXT_BASE,
        BOSC_NHV5_PLIC_CONTEXT_STRIDE,
        memmap[BOSC_NHV5_DEV_PLIC].size);

    /* UART0: 16550A */
    serial_mm_init(get_system_memory(), memmap[BOSC_NHV5_DEV_UART0].base, 2,
               qdev_get_gpio_in(DEVICE(state->plic), BOSC_NHV5_UART0_IRQ),
               115200, serial_hd(0), DEVICE_LITTLE_ENDIAN);

    /* UART1: Xilinx UART Lite */
    uartlite_init(bosc_nhv5_memmap[BOSC_NHV5_DEV_UART1].base,
                  qdev_get_gpio_in(DEVICE(state->plic), BOSC_NHV5_UART1_IRQ),
                  serial_hd(1));  

    /* CLINT */
    riscv_aclint_swi_create(memmap[BOSC_NHV5_DEV_CLINT].base,
        0, hart_count, false);
    riscv_aclint_mtimer_create(memmap[BOSC_NHV5_DEV_CLINT].base +
        RISCV_ACLINT_SWI_SIZE, RISCV_ACLINT_DEFAULT_MTIMER_SIZE, 0, hart_count,
        RISCV_ACLINT_DEFAULT_MTIMECMP, RISCV_ACLINT_DEFAULT_MTIME,
        RISCV_ACLINT_NHV5_TIMEBASE_FREQ, true);              

    bosc_nh_dw_pcie_init(state);
    /* ROM */
    memory_region_init_rom(&state->rom, OBJECT(dev), "riscv.bosc.nhv5.rom",
                            memmap[BOSC_NHV5_DEV_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory,
                            memmap[BOSC_NHV5_DEV_MROM].base, &state->rom);
}

static void bosc_nhv5_soc_instance_init(Object *obj)
{
    int hart_count;

    MachineState *ms = MACHINE(qdev_get_machine());

    hart_count = riscv_socket_hart_count(ms, 0);
    if (hart_count < 0) {
	    error_report("can't find hart count");
	    exit(1);
    }

    BoscNhv5SoCState *state = RISCV_NHV5_SOC(obj);

    object_initialize_child(obj, "cpus", &state->cpus, TYPE_RISCV_HART_ARRAY);

    /*
     * CPU type is fixed and we are not supporting passing from commandline so far.
     */
    object_property_set_str(OBJECT(&state->cpus), "cpu-type",
                            TYPE_RISCV_CPU_BOSC_NHV5, &error_abort);
    object_property_set_int(OBJECT(&state->cpus), "num-harts", hart_count,
                            &error_abort);

    object_initialize_child(OBJECT(ms), "pcie0", &state->pcie0, TYPE_DESIGNWARE_PCIE_HOST);
}

static void bosc_nhv5_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = bosc_nhv5_soc_state_realize;
    /*
     * Reasons:
     *     - Creates CPUS in riscv_hart_realize(), and can create unintended
     *       CPUs
     *     - Uses serial_hds in realize function, thus can't be used twice
     */
    dc->user_creatable = false;
}

static const TypeInfo bosc_nhv5_type_info = {
    .name = TYPE_RISCV_NHV5_SOC,
    .parent = TYPE_DEVICE,
    .class_init = bosc_nhv5_soc_class_init,
    .instance_init = bosc_nhv5_soc_instance_init,
    .instance_size = sizeof(BoscNhv5SoCState),
};

static void bosc_kmh_type_info_register(void)
{
    type_register_static(&bosc_nhv5_type_info);
}
type_init(bosc_kmh_type_info_register)

