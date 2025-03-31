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



static const MemMapEntry bosc_kmh_memmap[] = {
    [BOSC_KMH_DEV_DEBUG] 	=	{       0x0,    0x100 },
    [BOSC_KMH_DEV_MROM] 	=	{    0x1000,    0xf000 },
    [BOSC_KMH_DEV_FLASH] 	=	{ 0x10000000,   0x4000000 },
    [BOSC_KMH_DEV_UART0] 	=	{ 0x310B0000,   0x10000 },
    [BOSC_KMH_DEV_CLINT] 	=	{ 0x38000000,   0x10000 },
    [BOSC_KMH_DEV_PLIC]         =       { 0x3c000000,   0x4000000},
    [BOSC_KMH_APLIC_M] =      {  0x31100000, APLIC_SIZE(BOSC_KMH_CPUS_MAX) },
    [BOSC_KMH_APLIC_S] =      {  0x31120000, APLIC_SIZE(BOSC_KMH_CPUS_MAX) },
    [BOSC_KMH_IMSIC_M] =      { 0x3a800000, BOSC_KMH_IMSIC_MAX_SIZE },
    [BOSC_KMH_IMSIC_S] =      { 0x3b000000, BOSC_KMH_IMSIC_MAX_SIZE },
    [BOSC_KMH_DEV_UART1] 	=	{ 0x40600000,   0x1000 },
    [BOSC_KMH_DEV_DRAM] 	=	{ 0x80000000,   0x0 },
};

static uint32_t imsic_num_bits(uint32_t count)
{
    uint32_t ret = 0;

    while (BIT(ret) < count) {
        ret++;
    }

    return ret;
}

static DeviceState *bosc_kmh_create_aia(RISCVKmhAIAType aia_type, int aia_guests,
                                    const MemMapEntry *memmap, int socket,
                                    int base_hartid, int hart_count)
{
    int i;
    hwaddr addr;
    uint32_t guest_bits;
    DeviceState *aplic_s = NULL;
    DeviceState *aplic_m = NULL;
    bool msimode = aia_type == BOSC_KMH_AIA_TYPE_APLIC_IMSIC;

    if (msimode) {
        if (!kvm_enabled()) {
            /* Per-socket M-level IMSICs */
            addr = memmap[BOSC_KMH_IMSIC_M].base +
                   socket * BOSC_KMH_IMSIC_GROUP_MAX_SIZE;
            for (i = 0; i < hart_count; i++) {
                riscv_imsic_create(addr + i * IMSIC_HART_SIZE(0),
                                   base_hartid + i, true, 1,
                                   BOSC_KMH_IRQCHIP_NUM_MSIS);
            }
        }

        /* Per-socket S-level IMSICs */
        guest_bits = imsic_num_bits(aia_guests + 1);
        addr = memmap[BOSC_KMH_IMSIC_S].base + socket * BOSC_KMH_IMSIC_GROUP_MAX_SIZE;
        for (i = 0; i < hart_count; i++) {
            riscv_imsic_create(addr + i * IMSIC_HART_SIZE(guest_bits),
                               base_hartid + i, false, 1 + aia_guests,
                               BOSC_KMH_IRQCHIP_NUM_MSIS);
        }
    }

    if (!kvm_enabled()) {
        /* Per-socket M-level APLIC */
        aplic_m = riscv_aplic_create(memmap[BOSC_KMH_APLIC_M].base +
                                     socket * memmap[BOSC_KMH_APLIC_M].size,
                                     memmap[BOSC_KMH_APLIC_M].size,
                                     (msimode) ? 0 : base_hartid,
                                     (msimode) ? 0 : hart_count,
                                     BOSC_KMH_IRQCHIP_NUM_SOURCES,
                                     BOSC_KMH_IRQCHIP_NUM_PRIO_BITS,
                                     msimode, true, NULL);
    }

    /* Per-socket S-level APLIC */
    aplic_s = riscv_aplic_create(memmap[BOSC_KMH_APLIC_S].base +
                                 socket * memmap[BOSC_KMH_APLIC_S].size,
                                 memmap[BOSC_KMH_APLIC_S].size,
                                 (msimode) ? 0 : base_hartid,
                                 (msimode) ? 0 : hart_count,
                                 BOSC_KMH_IRQCHIP_NUM_SOURCES,
                                 BOSC_KMH_IRQCHIP_NUM_PRIO_BITS,
                                 msimode, false, aplic_m);

    return kvm_enabled() ? aplic_s : aplic_m;
}

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
                            (target_ulong *)&bosc_kmh_memmap[BOSC_KMH_DEV_DRAM].base,
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
    mc->max_cpus = BOSC_KMH_CPUS_MAX;
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

static XilinxUARTLite *uartlite_init(hwaddr base, qemu_irq irq, Chardev *chr)
{
    XilinxUARTLite *uartlite = XILINX_UARTLITE(qdev_new(TYPE_XILINX_UARTLITE));

    qdev_prop_set_chr(DEVICE(uartlite), "chardev", chr);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(uartlite), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(uartlite), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(uartlite), 0, irq);

    return uartlite;
}

static void bosc_kmh_soc_state_realize(DeviceState *dev, Error **errp)
{
    int hart_count;
    const MemMapEntry *memmap = bosc_kmh_memmap;
    MachineState *ms = MACHINE(qdev_get_machine());
    BoscKmhSoCState *state = RISCV_KMH_SOC(dev);
    MemoryRegion *system_memory = get_system_memory();

    sysbus_realize(SYS_BUS_DEVICE(&state->cpus), &error_abort);

    state->aia_type =  BOSC_KMH_AIA_TYPE_APLIC_IMSIC;

    hart_count = riscv_socket_hart_count(ms, 0);
    state->irqchip = bosc_kmh_create_aia(state->aia_type, BOSC_KMH_IRQCHIP_MAX_GUESTS, memmap, 0, 0, hart_count);

    /* UART0: 16550A */
    serial_mm_init(get_system_memory(), bosc_kmh_memmap[BOSC_KMH_DEV_UART0].base, 2,
               qdev_get_gpio_in(DEVICE(state->irqchip), BOSC_KMH_UART0_IRQ),
               115200, serial_hd(0), DEVICE_LITTLE_ENDIAN);

    /* UART1: Xilinx UART Lite */
    uartlite_init(bosc_kmh_memmap[BOSC_KMH_DEV_UART1].base,
                  qdev_get_gpio_in(DEVICE(state->irqchip), BOSC_KMH_UART1_IRQ),
                  serial_hd(0)); // Share the same serial port with UART0

    riscv_aclint_swi_create(bosc_kmh_memmap[BOSC_KMH_DEV_CLINT].base,
        0, hart_count, false);
    riscv_aclint_mtimer_create(bosc_kmh_memmap[BOSC_KMH_DEV_CLINT].base +
        RISCV_ACLINT_SWI_SIZE, RISCV_ACLINT_DEFAULT_MTIMER_SIZE, 0, hart_count,
        RISCV_ACLINT_DEFAULT_MTIMECMP, RISCV_ACLINT_DEFAULT_MTIME,
        RISCV_ACLINT_KMH_TIMEBASE_FREQ, true);

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
    int hart_count;

    MachineState *ms = MACHINE(qdev_get_machine());

    hart_count = riscv_socket_hart_count(ms, 0);
    if (hart_count < 0) {
	    error_report("can't find hart count");
	    exit(1);
    }

    BoscKmhSoCState *state = RISCV_KMH_SOC(obj);

    object_initialize_child(obj, "cpus", &state->cpus, TYPE_RISCV_HART_ARRAY);

    /*
     * CPU type is fixed and we are not supporting passing from commandline so far.
     */
    object_property_set_str(OBJECT(&state->cpus), "cpu-type",
                            TYPE_RISCV_CPU_BOSC_KMH, &error_abort);
    object_property_set_int(OBJECT(&state->cpus), "num-harts", hart_count,
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
