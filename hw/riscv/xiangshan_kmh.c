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
#include CONFIG_DEVICES
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "system/address-spaces.h"
#include "system/device_tree.h"
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
#ifdef CONFIG_MY_VIRTIO
#include "hw/misc/my_virtio.h"
#endif
#include "hw/riscv/iommu.h"
#include "hw/riscv/riscv-iommu.h"
#include "hw/riscv/riscv-iommu-bits.h"
#include "hw/loader.h"
#include "qapi/qapi-visit-common.h"
#include "kvm/kvm_riscv.h"
#include "system/kvm.h"
#include "target/riscv/cpu.h"
#include "target/riscv/cpu_bits.h"

#define XIANGSHAN_KMH_BIOS_BIN "opensbi-riscv64-xiangshan-kmh-fw_dynamic.bin"
#define XIANGSHAN_KMH_FW_JUMP_FDT_ADDR 0x80200000ULL
#define XIANGSHAN_KMH_AUTOTEST_IMAGE_ADDR 0x80400000ULL
#define XIANGSHAN_KMH_AUTOTEST_ROOTFS_ADDR 0x3c0000000ULL
#define XIANGSHAN_KMH_AUTOTEST_WORKLOAD_ADDR 0x3e0000000ULL
#define XIANGSHAN_KMH_AUTOTEST_TRIGGER_ADDR 0x90000000ULL
#define XIANGSHAN_KMH_ACPI_HANDOFF_ADDR 0x90200000ULL
#define XIANGSHAN_KMH_AUTOTEST_ROOTFS_SIZE 0x20000000ULL
#define XIANGSHAN_KMH_AUTOTEST_WORKLOAD_SIZE 0x80000000ULL
#define XIANGSHAN_KMH_AUTOTEST_TRIGGER_SIZE 0x200000ULL
#define XIANGSHAN_KMH_ACPI_HANDOFF_SIZE 0x20000ULL
#define XIANGSHAN_KMH_UART0_CLOCK 50000000
#define FDT_IRQ_TYPE_EDGE_RISING 4
#define DESIGNWARE_PCIE_IRQ_MSI 4

#define XIANGSHAN_KMH_PCIE0_CFG_BASE 0x67ff0000ULL
#define XIANGSHAN_KMH_PCIE0_CFG_SIZE 0x00010000ULL
#define XIANGSHAN_KMH_PCIE0_LOW_BUS_BASE 0x40000000ULL

static const MemMapEntry xiangshan_kmh_memmap[] = {
    [XIANGSHAN_KMH_ROM]      =        {     0x1000,       0x40000 },
    [XIANGSHAN_KMH_FLASH]    =        { 0x10000000,     0x4000000 },
    [XIANGSHAN_KMH_MY_VIRTIO_CONSOLE] = { 0x31080000,      0x1000 },
    [XIANGSHAN_KMH_MY_VIRTIO_NET] =   { 0x31090000,        0x1000 },
    [XIANGSHAN_KMH_MY_VIRTIO_BLK] =   { 0x310A0000,        0x1000 },
    [XIANGSHAN_KMH_UART0]    =        { 0x310B0000,       0x10000 },
    [XIANGSHAN_KMH_MY_VIRTIO_GPU] =   { 0x310C0000,        0x1000 },
    [XIANGSHAN_KMH_MY_VIRTIO_KEYBOARD] = { 0x310D0000,      0x1000 },
    [XIANGSHAN_KMH_MY_VIRTIO_MOUSE] = { 0x310E0000,         0x1000 },
    [XIANGSHAN_KMH_MY_VIRTIO_TABLET] = { 0x310F0000,        0x1000 },
    [XIANGSHAN_KMH_IOMMU_SYS] =       { 0x311f0000,       0x1000 },
    [XIANGSHAN_KMH_PCIE0_DBI] =       { 0x32000000,     0x1000000 },
    [XIANGSHAN_KMH_CLINT]    =        { 0x38000000,       0x10000 },
    [XIANGSHAN_KMH_APLIC_M]  =        { 0x31100000,        0x4000 },
    [XIANGSHAN_KMH_APLIC_S]  =        { 0x31120000,        0x4000 },
    [XIANGSHAN_KMH_SRAM]     =        { 0x37f00000,      0x100000 },
    [XIANGSHAN_KMH_IMSIC_M]  =        { 0x3A800000,       0x10000 },
    [XIANGSHAN_KMH_IMSIC_S]  =        { 0x3B000000,       0x80000 },
    [XIANGSHAN_KMH_UART1]    =        { 0x40600000,        0x1000 },
    [XIANGSHAN_KMH_PCIE0_BAR] =       { 0x60000000,     0x7ff0000 },
    [XIANGSHAN_KMH_DRAM]     =        { 0x80000000,           0x0 },
};

#ifndef CONFIG_MY_VIRTIO
static bool xiangshan_kmh_my_virtio_requested(const XiangshanKmhState *s)
{
    return s->my_virtio_blk || s->my_virtio_net || s->my_virtio_console ||
           s->my_virtio_gpu || s->my_virtio_keyboard || s->my_virtio_mouse ||
           s->my_virtio_tablet;
}
#endif

static void xiangshan_kmh_dw_pcie_init(XiangshanKmhSoCState *s)
{
    DesignwarePCIEHost *pcie0 = &s->pcie0;
    const MemMapEntry *memmap = xiangshan_kmh_memmap;
    PCIHostState *pci_host;

    /*
     * PCIE RC0
     */
    sysbus_realize(SYS_BUS_DEVICE(pcie0), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(pcie0), 0,
                    memmap[XIANGSHAN_KMH_PCIE0_DBI].base);

    sysbus_connect_irq(SYS_BUS_DEVICE(pcie0), DESIGNWARE_PCIE_IRQ_MSI,
                       qdev_get_gpio_in(DEVICE(s->irqchip),
                                        XIANGSHAN_KMH_RC_MSI0_IRQ));

    pci_host = PCI_HOST_BRIDGE(pcie0);
    /*
     * The generated DT uses IMSIC as the PCI MSI parent. Without the optional
     * IOMMU, let PCI devices issue DMA/MSI writes directly to system memory.
     */
    pci_host->bus->iommu_ops = NULL;
    pci_host->bus->iommu_opaque = NULL;
}

static DeviceState *xiangshan_kmh_create_aia(uint32_t num_harts,
                                             bool kvm_m_mode)
{
    int i;
    const MemMapEntry *memmap = xiangshan_kmh_memmap;
    hwaddr addr = 0;
    DeviceState *aplic_s;
    DeviceState *aplic_m = NULL;

    if (!kvm_enabled() || kvm_m_mode) {
        /* M-level IMSICs */
        addr = memmap[XIANGSHAN_KMH_IMSIC_M].base;
        for (i = 0; i < num_harts; i++) {
            riscv_imsic_create(addr + i * IMSIC_HART_SIZE(0), i, true,
                               1, XIANGSHAN_KMH_IMSIC_NUM_IDS);
#ifdef CONFIG_KVM_M_MODE
            if (kvm_m_mode) {
                RISCV_CPU(cpu_by_arch_id(i))->cfg.ext_smaia = false;
            }
#endif
        }
    }

    /* S-level IMSICs */
    addr = memmap[XIANGSHAN_KMH_IMSIC_S].base;
    for (i = 0; i < num_harts; i++) {
        riscv_imsic_create(addr +
                           i * IMSIC_HART_SIZE(XIANGSHAN_KMH_IMSIC_GUEST_BITS),
                           i, false, 1 + XIANGSHAN_KMH_IMSIC_NUM_GUESTS,
                           XIANGSHAN_KMH_IMSIC_NUM_IDS);
    }

    if (!kvm_enabled() || kvm_m_mode) {
        /* M-level APLIC */
        aplic_m = riscv_aplic_create(memmap[XIANGSHAN_KMH_APLIC_M].base,
                                     memmap[XIANGSHAN_KMH_APLIC_M].size,
                                     0, 0, XIANGSHAN_KMH_APLIC_NUM_SOURCES,
                                     1, true, true, NULL);
    }

    /* S-level APLIC */
    aplic_s = riscv_aplic_create(memmap[XIANGSHAN_KMH_APLIC_S].base,
                                 memmap[XIANGSHAN_KMH_APLIC_S].size,
                                 0, 0, XIANGSHAN_KMH_APLIC_NUM_SOURCES,
                                 1, true, false, aplic_m);

    if (kvm_enabled()) {
        riscv_aplic_set_kvm_msicfgaddr(RISCV_APLIC(aplic_s), addr);
    }

    return kvm_enabled() && !kvm_m_mode ? aplic_s : aplic_m;
}

static XilinxUARTLite *uartlite_init(hwaddr base, qemu_irq irq, Chardev *chr)
{
    XilinxUARTLite *uartlite = XILINX_UARTLITE(qdev_new(TYPE_XILINX_UARTLITE));

    qdev_prop_set_chr(DEVICE(uartlite), "chardev", chr);
    qdev_prop_set_enum(DEVICE(uartlite), "endianness", ENDIAN_MODE_LITTLE);
    qdev_prop_set_bit(DEVICE(uartlite), "tx-at-rx", true);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(uartlite), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(uartlite), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(uartlite), 0, irq);

    return uartlite;
}

static void xiangshan_kmh_soc_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    XiangshanKmhState *machine = XIANGSHAN_KMH_MACHINE(ms);
    XiangshanKmhSoCState *s = XIANGSHAN_KMH_SOC(dev);
    const MemMapEntry *memmap = xiangshan_kmh_memmap;
    MemoryRegion *system_memory = get_system_memory();
    uint32_t num_harts = ms->smp.cpus;
    bool kvm_m_mode = false;

#ifdef CONFIG_KVM_M_MODE
    kvm_m_mode = s->kvm_m_mode;
#endif

    qdev_prop_set_uint32(DEVICE(&s->cpus), "num-harts", num_harts);
    qdev_prop_set_uint32(DEVICE(&s->cpus), "hartid-base", 0);
    qdev_prop_set_string(DEVICE(&s->cpus), "cpu-type", ms->cpu_type);
    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_fatal);

    /*
     * Sting uses 0x5006b as a platform-specific simulator exit.  An
     * autotest machine hosts another QEMU/KVM instance, so its outer TCG
     * CPU must let an inner guest's instruction trap reach KVM instead of
     * shutting down the whole outer machine.
     */
    for (uint32_t i = 0; i < num_harts; i++) {
        RISCV_CPU(cpu_by_arch_id(i))->cfg.bosc_debug_inst =
            !machine->autotest_dtb;
    }

    /* AIA */
    s->irqchip = xiangshan_kmh_create_aia(num_harts, kvm_m_mode);
    if (kvm_enabled() && riscv_is_kvm_aia_aplic_imsic(true)) {
        kvm_riscv_aia_create(ms, IMSIC_MMIO_GROUP_MIN_SHIFT,
                             XIANGSHAN_KMH_APLIC_NUM_SOURCES,
                             XIANGSHAN_KMH_IMSIC_NUM_IDS,
                             memmap[XIANGSHAN_KMH_APLIC_S].base,
                             memmap[XIANGSHAN_KMH_IMSIC_S].base,
                             XIANGSHAN_KMH_IMSIC_NUM_GUESTS);
    }

    /* UART */
    serial_mm_init(system_memory, memmap[XIANGSHAN_KMH_UART0].base, 2,
                   qdev_get_gpio_in(s->irqchip, XIANGSHAN_KMH_UART0_IRQ),
                   115200, serial_hd(0), DEVICE_LITTLE_ENDIAN);

    /* UART1: Xilinx UART Lite */
    uartlite_init(xiangshan_kmh_memmap[XIANGSHAN_KMH_UART1].base,
                  qdev_get_gpio_in(DEVICE(s->irqchip), XIANGSHAN_KMH_UART1_IRQ),
                  serial_hd(1));

    if (!kvm_enabled() || kvm_m_mode) {
        uint32_t timebase_freq = XIANGSHAN_KMH_CLINT_TIMEBASE_FREQ;
#ifdef CONFIG_KVM_M_MODE
        DeviceState *mtimer;
#endif

#ifdef CONFIG_KVM_M_MODE
        if (kvm_m_mode) {
            timebase_freq = kvm_riscv_get_timebase_frequency(
                &s->cpus.harts[0]);
        }
#endif
        /* CLINT */
        riscv_aclint_swi_create(memmap[XIANGSHAN_KMH_CLINT].base,
                                0, num_harts, false);
#ifdef CONFIG_KVM_M_MODE
        mtimer =
#endif
        riscv_aclint_mtimer_create(
            memmap[XIANGSHAN_KMH_CLINT].base + RISCV_ACLINT_SWI_SIZE,
            RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
            0, num_harts, RISCV_ACLINT_DEFAULT_MTIMECMP,
            RISCV_ACLINT_DEFAULT_MTIME, timebase_freq, true);
#ifdef CONFIG_KVM_M_MODE
        if (kvm_m_mode) {
            riscv_aclint_mtimer_set_time(
                mtimer, kvm_riscv_get_timer_time(&s->cpus.harts[0]));
        }
#endif
    }

    /* KVM direct boot requires -bios none and starts directly in DRAM. */
    if (!kvm_enabled()) {
        memory_region_init_rom(&s->rom, OBJECT(dev),
                               "xiangshan.kunminghu.rom",
                               memmap[XIANGSHAN_KMH_ROM].size, &error_fatal);
        memory_region_add_subregion(system_memory,
                                    memmap[XIANGSHAN_KMH_ROM].base, &s->rom);

        memory_region_init_ram(&s->sram, OBJECT(dev), "riscv.bosc.kmh.sram",
                               memmap[XIANGSHAN_KMH_SRAM].size, &error_fatal);
        memory_region_add_subregion(system_memory,
                                    memmap[XIANGSHAN_KMH_SRAM].base,
                                    &s->sram);

        memory_region_init_rom(&s->flash, NULL, "riscv.bosc.kmh.flash0",
                               memmap[XIANGSHAN_KMH_FLASH].size,
                               &error_fatal);
        memory_region_add_subregion(system_memory,
                                    memmap[XIANGSHAN_KMH_FLASH].base,
                                    &s->flash);
    }

    if (s->dw_pcie) {
        xiangshan_kmh_dw_pcie_init(s);
    }

#ifdef CONFIG_MY_VIRTIO
    if (s->my_virtio_blk) {
        my_virtio_blk_create(memmap[XIANGSHAN_KMH_MY_VIRTIO_BLK].base,
                             memmap[XIANGSHAN_KMH_MY_VIRTIO_BLK].size,
                             qdev_get_gpio_in(DEVICE(s->irqchip),
                                              XIANGSHAN_KMH_MY_VIRTIO_BLK_IRQ),
                             s->my_virtio_blk_image);
    }

    if (s->my_virtio_net) {
        my_virtio_net_create(memmap[XIANGSHAN_KMH_MY_VIRTIO_NET].base,
                             memmap[XIANGSHAN_KMH_MY_VIRTIO_NET].size,
                             qdev_get_gpio_in(DEVICE(s->irqchip),
                                              XIANGSHAN_KMH_MY_VIRTIO_NET_IRQ),
                             s->my_virtio_net_hostfwd,
                             s->my_virtio_net_network,
                             s->my_virtio_net_netmask,
                             s->my_virtio_net_host_ip,
                             s->my_virtio_net_dhcp_start,
                             s->my_virtio_net_dns_ip);
    }

    if (s->my_virtio_console) {
        my_virtio_console_create(
            memmap[XIANGSHAN_KMH_MY_VIRTIO_CONSOLE].base,
            memmap[XIANGSHAN_KMH_MY_VIRTIO_CONSOLE].size,
            qdev_get_gpio_in(DEVICE(s->irqchip),
                             XIANGSHAN_KMH_MY_VIRTIO_CONSOLE_IRQ),
            s->my_virtio_console_backend,
            s->my_virtio_console_input_path,
            s->my_virtio_console_output_path);
    }

    if (s->my_virtio_gpu) {
        s->my_virtio_ui =
            my_virtio_gpu_create(memmap[XIANGSHAN_KMH_MY_VIRTIO_GPU].base,
                                 memmap[XIANGSHAN_KMH_MY_VIRTIO_GPU].size,
                                 qdev_get_gpio_in(
                                     DEVICE(s->irqchip),
                                     XIANGSHAN_KMH_MY_VIRTIO_GPU_IRQ),
                                 s->my_virtio_vnc_listen);
    }

    if (s->my_virtio_keyboard) {
        my_virtio_keyboard_create(
            memmap[XIANGSHAN_KMH_MY_VIRTIO_KEYBOARD].base,
            memmap[XIANGSHAN_KMH_MY_VIRTIO_KEYBOARD].size,
            qdev_get_gpio_in(DEVICE(s->irqchip),
                             XIANGSHAN_KMH_MY_VIRTIO_KEYBOARD_IRQ),
            s->my_virtio_keyboard_backend,
            s->my_virtio_keyboard_evdev_path,
            s->my_virtio_ui);
    }

    if (s->my_virtio_mouse) {
        my_virtio_mouse_create(
            memmap[XIANGSHAN_KMH_MY_VIRTIO_MOUSE].base,
            memmap[XIANGSHAN_KMH_MY_VIRTIO_MOUSE].size,
            qdev_get_gpio_in(DEVICE(s->irqchip),
                             XIANGSHAN_KMH_MY_VIRTIO_MOUSE_IRQ),
            s->my_virtio_mouse_backend,
            s->my_virtio_mouse_evdev_path,
            s->my_virtio_ui);
    }

    if (s->my_virtio_tablet) {
        my_virtio_tablet_create(
            memmap[XIANGSHAN_KMH_MY_VIRTIO_TABLET].base,
            memmap[XIANGSHAN_KMH_MY_VIRTIO_TABLET].size,
            qdev_get_gpio_in(DEVICE(s->irqchip),
                             XIANGSHAN_KMH_MY_VIRTIO_TABLET_IRQ),
            s->my_virtio_tablet_backend,
            s->my_virtio_tablet_evdev_path,
            s->my_virtio_ui);
    }
#endif
}

static void xiangshan_kmh_soc_unrealize(DeviceState *dev)
{
    XiangshanKmhSoCState *s = XIANGSHAN_KMH_SOC(dev);

    s->my_virtio_ui = NULL;
}

static void xiangshan_kmh_soc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xiangshan_kmh_soc_realize;
    dc->unrealize = xiangshan_kmh_soc_unrealize;
    dc->user_creatable = false;
    
}

static void xiangshan_kmh_soc_instance_init(Object *obj)
{
    XiangshanKmhSoCState *s = XIANGSHAN_KMH_SOC(obj);

    object_initialize_child(obj, "cpus", &s->cpus, TYPE_RISCV_HART_ARRAY);
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

static bool kmh_is_iommu_sys_enabled(XiangshanKmhState *s)
{
    return s->iommu_sys == ON_OFF_AUTO_ON;
}

static bool xiangshan_kmh_should_generate_dtb(XiangshanKmhState *s)
{
    MachineState *machine = MACHINE(s);

    return s->generated_dtb == ON_OFF_AUTO_ON ||
           (s->generated_dtb == ON_OFF_AUTO_AUTO && !machine->dtb &&
            (machine->kernel_filename || s->autotest_dtb));
}

static uint64_t xiangshan_kmh_fw_jump_fdt_addr(XiangshanKmhState *s)
{
    return s->fw_jump_fdt_addr;
}

static void xiangshan_kmh_fdt_add_reg_node(void *fdt, const char *fmt,
                                           hwaddr addr, uint64_t size,
                                           const char *compatible,
                                           const char *status)
{
    g_autofree char *name = g_strdup_printf(fmt, addr);

    qemu_fdt_add_subnode(fdt, name);
    if (compatible) {
        qemu_fdt_setprop_string(fdt, name, "compatible", compatible);
    }
    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, addr, 2, size);
    if (status) {
        qemu_fdt_setprop_string(fdt, name, "status", status);
    }
}

static void xiangshan_kmh_fdt_add_pmem(void *fdt, hwaddr addr, uint64_t size)
{
    g_autofree char *name = g_strdup_printf("/pmem@%"HWADDR_PRIx, addr);
    static const char * const compat[2] = {
        "pmem-region", "nvdimm"
    };

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string_array(fdt, name, "compatible",
                                  (char **)&compat, ARRAY_SIZE(compat));
    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, addr, 2, size);
    qemu_fdt_setprop_string(fdt, name, "status", "okay");
}

static void xiangshan_kmh_fdt_ensure_reserved_memory(void *fdt)
{
    qemu_fdt_add_path(fdt, "/reserved-memory");
    qemu_fdt_setprop_cell(fdt, "/reserved-memory", "#address-cells", 2);
    qemu_fdt_setprop_cell(fdt, "/reserved-memory", "#size-cells", 2);
    qemu_fdt_setprop(fdt, "/reserved-memory", "ranges", NULL, 0);
}

static void xiangshan_kmh_fdt_add_acpi_handoff(XiangshanKmhState *s)
{
    MachineState *ms = MACHINE(s);
    void *fdt = ms->fdt;
    hwaddr addr = s->acpi_handoff_addr;
    uint64_t size = s->acpi_handoff_size;
    g_autofree char *name = g_strdup_printf(
        "/reserved-memory/acpi-handoff@%"HWADDR_PRIx, addr);

    xiangshan_kmh_fdt_ensure_reserved_memory(fdt);
    xiangshan_kmh_fdt_add_reg_node(fdt,
                                   "/reserved-memory/acpi-handoff@%"HWADDR_PRIx,
                                   addr, size,
                                   "bosc,kmh-acpi-handoff", "okay");
    qemu_fdt_setprop(fdt, name, "no-map", NULL, 0);
}

typedef struct XiangshanKmhMemRange {
    hwaddr start;
    hwaddr end;
} XiangshanKmhMemRange;

static hwaddr xiangshan_kmh_range_end(hwaddr start, uint64_t size)
{
    hwaddr end = start + size;

    return end < start ? HWADDR_MAX : end;
}

static bool xiangshan_kmh_range_in_dram(XiangshanKmhState *s, hwaddr start,
                                        uint64_t size)
{
    MachineState *ms = MACHINE(s);
    const MemMapEntry *memmap = xiangshan_kmh_memmap;
    hwaddr dram_base = memmap[XIANGSHAN_KMH_DRAM].base;
    hwaddr dram_end = xiangshan_kmh_range_end(dram_base, ms->ram_size);
    hwaddr end = xiangshan_kmh_range_end(start, size);

    return size && start >= dram_base && end <= dram_end && end > start;
}

static void xiangshan_kmh_fdt_add_memory(XiangshanKmhState *s)
{
    MachineState *ms = MACHINE(s);
    const MemMapEntry *memmap = xiangshan_kmh_memmap;
    void *fdt = ms->fdt;
    hwaddr dram_base = memmap[XIANGSHAN_KMH_DRAM].base;
    hwaddr dram_end = xiangshan_kmh_range_end(dram_base, ms->ram_size);
    g_autofree char *name = g_strdup_printf("/memory@%"HWADDR_PRIx, dram_base);
    uint64_t reg[12];
    unsigned int cells = 0;

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "device_type", "memory");

    if (s->autotest_dtb) {
        XiangshanKmhMemRange holes[2] = {
            {
                .start = s->autotest_rootfs_addr,
                .end = xiangshan_kmh_range_end(s->autotest_rootfs_addr,
                                               s->autotest_rootfs_size),
            }, {
                .start = s->autotest_workload_addr,
                .end = xiangshan_kmh_range_end(s->autotest_workload_addr,
                                               s->autotest_workload_size),
            }
        };
        XiangshanKmhMemRange merged[2];
        int nr_merged = 0;
        hwaddr cursor = dram_base;

        if (holes[1].start < holes[0].start) {
            XiangshanKmhMemRange tmp = holes[0];

            holes[0] = holes[1];
            holes[1] = tmp;
        }

        for (int i = 0; i < ARRAY_SIZE(holes); i++) {
            hwaddr start = MAX(holes[i].start, dram_base);
            hwaddr end = MIN(holes[i].end, dram_end);

            if (end <= start) {
                continue;
            }
            if (nr_merged && start <= merged[nr_merged - 1].end) {
                merged[nr_merged - 1].end = MAX(merged[nr_merged - 1].end, end);
            } else {
                merged[nr_merged++] = (XiangshanKmhMemRange) {
                    .start = start,
                    .end = end,
                };
            }
        }

        for (int i = 0; i < nr_merged; i++) {
            if (cursor < merged[i].start) {
                reg[cells++] = 2;
                reg[cells++] = cursor;
                reg[cells++] = 2;
                reg[cells++] = merged[i].start - cursor;
            }
            cursor = MAX(cursor, merged[i].end);
        }
        if (cursor < dram_end) {
            reg[cells++] = 2;
            reg[cells++] = cursor;
            reg[cells++] = 2;
            reg[cells++] = dram_end - cursor;
        }

        if (cells) {
            qemu_fdt_setprop_sized_cells_from_array(fdt, name, "reg",
                                                    cells / 2, reg);
            return;
        }
    }

    qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                 2, dram_base, 2, ms->ram_size);
}

static void xiangshan_kmh_fdt_add_autotest(XiangshanKmhState *s)
{
    MachineState *ms = MACHINE(s);
    void *fdt = ms->fdt;
    hwaddr rootfs_addr = s->autotest_rootfs_addr;
    hwaddr workload_addr = s->autotest_workload_addr;
    hwaddr trigger_addr = s->autotest_trigger_addr;
    uint64_t rootfs_size = s->autotest_rootfs_size;
    uint64_t workload_size = s->autotest_workload_size;
    uint64_t trigger_size = s->autotest_trigger_size;

    xiangshan_kmh_fdt_ensure_reserved_memory(fdt);

    {
        g_autofree char *name = g_strdup_printf(
            "/reserved-memory/region@%"HWADDR_PRIx, rootfs_addr);

        xiangshan_kmh_fdt_add_reg_node(fdt,
                                       "/reserved-memory/region@%"HWADDR_PRIx,
                                       rootfs_addr,
                                       rootfs_size,
                                       NULL, NULL);
        qemu_fdt_setprop(fdt, name, "no-map", NULL, 0);
    }

    {
        g_autofree char *name = g_strdup_printf(
            "/reserved-memory/region@%"HWADDR_PRIx, workload_addr);

        xiangshan_kmh_fdt_add_reg_node(fdt,
                                       "/reserved-memory/region@%"HWADDR_PRIx,
                                       workload_addr,
                                       workload_size,
                                       NULL, NULL);
        qemu_fdt_setprop(fdt, name, "no-map", NULL, 0);
    }

    xiangshan_kmh_fdt_add_reg_node(fdt,
                                   "/reserved-memory/my_reserved_buffer@%"HWADDR_PRIx,
                                   trigger_addr,
                                   trigger_size,
                                   "my,reserved-mem", "okay");

    /*
     * qemu_fdt_add_subnode() prepends children. Add workload first so rootfs
     * remains before workload in the DT and is normally probed as /dev/pmem0.
     */
    xiangshan_kmh_fdt_add_pmem(fdt, workload_addr, workload_size);
    xiangshan_kmh_fdt_add_pmem(fdt, rootfs_addr, rootfs_size);
}

static uint32_t xiangshan_kmh_fdt_add_iommu_sys(XiangshanKmhState *s,
                                                uint32_t aplic_s_phandle,
                                                uint32_t imsic_s_phandle,
                                                uint32_t *phandle)
{
    MachineState *ms = MACHINE(s);
    const MemMapEntry *memmap = xiangshan_kmh_memmap;
    void *fdt = ms->fdt;
    uint32_t iommu_phandle = (*phandle)++;
    uint32_t iommu_irqs[RISCV_IOMMU_INTR_COUNT] = {
        XIANGSHAN_KMH_IOMMU_SYS_IRQ + RISCV_IOMMU_INTR_CQ,
        XIANGSHAN_KMH_IOMMU_SYS_IRQ + RISCV_IOMMU_INTR_FQ,
        XIANGSHAN_KMH_IOMMU_SYS_IRQ + RISCV_IOMMU_INTR_PM,
        XIANGSHAN_KMH_IOMMU_SYS_IRQ + RISCV_IOMMU_INTR_PQ,
    };
    g_autofree char *name = g_strdup_printf("/soc/iommu@%"HWADDR_PRIx,
        memmap[XIANGSHAN_KMH_IOMMU_SYS].base);

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,iommu");
    qemu_fdt_setprop_cell(fdt, name, "#iommu-cells", 1);
    qemu_fdt_setprop_cell(fdt, name, "phandle", iommu_phandle);
    qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                 2, memmap[XIANGSHAN_KMH_IOMMU_SYS].base,
                                 2, memmap[XIANGSHAN_KMH_IOMMU_SYS].size);
    qemu_fdt_setprop_cell(fdt, name, "interrupt-parent", aplic_s_phandle);
    qemu_fdt_setprop_cells(fdt, name, "interrupts",
                           iommu_irqs[0], FDT_IRQ_TYPE_EDGE_LOW,
                           iommu_irqs[1], FDT_IRQ_TYPE_EDGE_LOW,
                           iommu_irqs[2], FDT_IRQ_TYPE_EDGE_LOW,
                           iommu_irqs[3], FDT_IRQ_TYPE_EDGE_LOW);
    qemu_fdt_setprop_cell(fdt, name, "msi-parent", imsic_s_phandle);
    qemu_fdt_setprop_string(fdt, name, "status", "okay");

    return iommu_phandle;
}

static void xiangshan_kmh_fdt_add_pcie(XiangshanKmhState *s,
                                        uint32_t aplic_s_phandle,
                                        uint32_t imsic_s_phandle,
                                        uint32_t iommu_sys_phandle)
{
    MachineState *ms = MACHINE(s);
    const MemMapEntry *memmap = xiangshan_kmh_memmap;
    void *fdt = ms->fdt;
    g_autofree char *name = g_strdup_printf("/soc/pcie@%"HWADDR_PRIx,
        memmap[XIANGSHAN_KMH_PCIE0_DBI].base);
    static const char * const reg_names[2] = {
        "dbi", "config"
    };
    static const char * const interrupt_names[2] = {
        "msi", "hp"
    };

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "snps,dw-pcie");
    qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                 2, memmap[XIANGSHAN_KMH_PCIE0_DBI].base,
                                 2, memmap[XIANGSHAN_KMH_PCIE0_DBI].size,
                                 2, XIANGSHAN_KMH_PCIE0_CFG_BASE,
                                 2, XIANGSHAN_KMH_PCIE0_CFG_SIZE);
    qemu_fdt_setprop_string_array(fdt, name, "reg-names",
                                  (char **)&reg_names,
                                  ARRAY_SIZE(reg_names));
    qemu_fdt_setprop_cell(fdt, name, "#address-cells", 3);
    qemu_fdt_setprop_cell(fdt, name, "#size-cells", 2);
    qemu_fdt_setprop_string(fdt, name, "device_type", "pci");
    qemu_fdt_setprop_cells(fdt, name, "bus-range", 0x0, 0xff);
    qemu_fdt_setprop_sized_cells(fdt, name, "ranges",
                                 1, FDT_PCI_RANGE_MMIO,
                                 2, XIANGSHAN_KMH_PCIE0_LOW_BUS_BASE,
                                 2, memmap[XIANGSHAN_KMH_PCIE0_BAR].base,
                                 2, memmap[XIANGSHAN_KMH_PCIE0_BAR].size);
    qemu_fdt_setprop_cell(fdt, name, "num-ib-windows", 1);
    qemu_fdt_setprop_cell(fdt, name, "interrupt-parent", aplic_s_phandle);
    qemu_fdt_setprop_cell(fdt, name, "msi-parent", imsic_s_phandle);
    qemu_fdt_setprop_cells(fdt, name, "interrupts",
                           XIANGSHAN_KMH_RC_MSI0_IRQ,
                           FDT_IRQ_TYPE_EDGE_RISING,
                           XIANGSHAN_KMH_RC_HP_IRQ,
                           FDT_IRQ_TYPE_EDGE_RISING);
    if (iommu_sys_phandle) {
        qemu_fdt_setprop_cells(fdt, name, "iommu-map",
                               0, iommu_sys_phandle, 0, 0,
                               0, iommu_sys_phandle, 0, 0xffff);
    }
    qemu_fdt_setprop_string_array(fdt, name, "interrupt-names",
                                  (char **)&interrupt_names,
                                  ARRAY_SIZE(interrupt_names));
    qemu_fdt_setprop_cell(fdt, name, "num-lanes", 1);
    qemu_fdt_setprop_string(fdt, name, "status", "okay");
}

static void xiangshan_kmh_fdt_add_my_virtio(XiangshanKmhState *s,
                                            const char *node_name,
                                            int memmap_index,
                                            int irq,
                                            uint32_t aplic_s_phandle)
{
    MachineState *ms = MACHINE(s);
    const MemMapEntry *memmap = xiangshan_kmh_memmap;
    void *fdt = ms->fdt;
    g_autofree char *name = g_strdup_printf("/soc/%s@%"HWADDR_PRIx,
        node_name, memmap[memmap_index].base);

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "virtio,mmio");
    qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                 2, memmap[memmap_index].base,
                                 2, memmap[memmap_index].size);
    qemu_fdt_setprop_cell(fdt, name, "interrupt-parent", aplic_s_phandle);
    qemu_fdt_setprop_cells(fdt, name, "interrupts",
                           irq, FDT_IRQ_TYPE_EDGE_RISING);
    qemu_fdt_setprop_string(fdt, name, "status", "okay");
}

static void xiangshan_kmh_create_fdt(XiangshanKmhState *s)
{
    MachineState *ms = MACHINE(s);
    const MemMapEntry *memmap = xiangshan_kmh_memmap;
    uint32_t phandle = 1;
    uint32_t imsic_m_phandle = 0, imsic_s_phandle;
    uint32_t aplic_m_phandle = 0, aplic_s_phandle;
    uint32_t iommu_sys_phandle = 0;
    g_autofree uint32_t *intc_phandles = g_new0(uint32_t, ms->smp.cpus);
    g_autofree uint32_t *clint_cells = NULL;
    g_autofree uint32_t *imsic_m_cells = NULL;
    g_autofree uint32_t *imsic_s_cells = g_new0(uint32_t, ms->smp.cpus * 2);
    void *fdt;
    int cpu;
    bool has_m_mode = !kvm_enabled();
    static const char * const cpu_compat[2] = {
        "bosc,kmh-v2", "riscv"
    };
    static const char * const soc_compat[2] = {
        "bosc,kmh-v2-soc", "simple-bus"
    };

#ifdef CONFIG_KVM_M_MODE
    has_m_mode |= s->kvm_m_mode;
#endif

    fdt = ms->fdt = create_device_tree(&s->fdt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(fdt, "/", "model",
                            "Xiangshan Kunminghu QEMU machine");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "bosc,kmh-v2-dev");
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 2);
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 2);

    qemu_fdt_add_subnode(fdt, "/aliases");
    qemu_fdt_setprop_string(fdt, "/aliases", "serial0",
                            "/soc/serial@310b0000");

    qemu_fdt_add_subnode(fdt, "/chosen");
    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path",
                            "/soc/serial@310b0000:115200n8");
    if (s->my_virtio_console) {
        qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path",
                                "/soc/my_virtio_console@31080000");
    }
    if (ms->kernel_cmdline && *ms->kernel_cmdline) {
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
                                ms->kernel_cmdline);
    } else if (s->my_virtio_console) {
        qemu_fdt_setprop_string(
            fdt, "/chosen", "bootargs",
            "console=hvc1 earlycon=sbi vt.nr_consoles=6 "
            "task=0x0000000000 guest_task=0x0000000000");
    } else {
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
                                "console=ttyS0,115200 earlycon=sbi loglevel=8");
    }

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 1);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
                          kvm_enabled() ?
                          kvm_riscv_get_timebase_frequency(
                              &s->soc.cpus.harts[0]) :
                          XIANGSHAN_KMH_CLINT_TIMEBASE_FREQ);

    for (cpu = ms->smp.cpus - 1; cpu >= 0; cpu--) {
        RISCVCPU *cpu_ptr = &s->soc.cpus.harts[cpu];
        g_autofree char *cpu_name = g_strdup_printf("/cpus/cpu@%x", cpu);
        g_autofree char *intc_name = g_strdup_printf(
            "/cpus/cpu@%x/interrupt-controller", cpu);
        uint32_t cpu_phandle = phandle++;

        qemu_fdt_add_subnode(fdt, cpu_name);
        qemu_fdt_setprop_string_array(fdt, cpu_name, "compatible",
                                      (char **)&cpu_compat,
                                      ARRAY_SIZE(cpu_compat));
        qemu_fdt_setprop_string(fdt, cpu_name, "device_type", "cpu");
        qemu_fdt_setprop_string(fdt, cpu_name, "status", "okay");
        qemu_fdt_setprop_cell(fdt, cpu_name, "reg", cpu);
        qemu_fdt_setprop_string(fdt, cpu_name, "mmu-type", "riscv,sv48");
        qemu_fdt_setprop_cell(fdt, cpu_name, "d-cache-block-size", 64);
        qemu_fdt_setprop_cell(fdt, cpu_name, "i-cache-block-size", 64);
        qemu_fdt_setprop_cell(fdt, cpu_name, "riscv,cbom-block-size", 64);
        qemu_fdt_setprop_cell(fdt, cpu_name, "riscv,cboz-block-size", 64);
        riscv_isa_write_fdt(cpu_ptr, fdt, cpu_name);
        qemu_fdt_setprop_cell(fdt, cpu_name, "phandle", cpu_phandle);

        intc_phandles[cpu] = phandle++;
        qemu_fdt_add_subnode(fdt, intc_name);
        qemu_fdt_setprop_string(fdt, intc_name, "compatible",
                                "riscv,cpu-intc");
        qemu_fdt_setprop(fdt, intc_name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, intc_name, "#interrupt-cells", 1);
        qemu_fdt_setprop_cell(fdt, intc_name, "phandle", intc_phandles[cpu]);
    }

    xiangshan_kmh_fdt_add_memory(s);

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string_array(fdt, "/soc", "compatible",
                                  (char **)&soc_compat,
                                  ARRAY_SIZE(soc_compat));
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 2);

    if (has_m_mode) {
        clint_cells = g_new0(uint32_t, ms->smp.cpus * 4);
        imsic_m_cells = g_new0(uint32_t, ms->smp.cpus * 2);
    }

    for (cpu = 0; cpu < ms->smp.cpus; cpu++) {
        if (has_m_mode) {
            clint_cells[cpu * 4 + 0] = cpu_to_be32(intc_phandles[cpu]);
            clint_cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_SOFT);
            clint_cells[cpu * 4 + 2] = cpu_to_be32(intc_phandles[cpu]);
            clint_cells[cpu * 4 + 3] = cpu_to_be32(IRQ_M_TIMER);
            imsic_m_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
            imsic_m_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_M_EXT);
        }
        imsic_s_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        imsic_s_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_S_EXT);
    }

    if (has_m_mode) {
        g_autofree char *name = g_strdup_printf("/soc/clint@%"HWADDR_PRIx,
            memmap[XIANGSHAN_KMH_CLINT].base);
        static const char * const compat[2] = {
            "sifive,clint0", "riscv,clint0"
        };

        qemu_fdt_add_subnode(fdt, name);
        qemu_fdt_setprop_string_array(fdt, name, "compatible",
                                      (char **)&compat, ARRAY_SIZE(compat));
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, memmap[XIANGSHAN_KMH_CLINT].base,
                                     2, memmap[XIANGSHAN_KMH_CLINT].size);
        qemu_fdt_setprop(fdt, name, "interrupts-extended", clint_cells,
                         ms->smp.cpus * sizeof(uint32_t) * 4);
    }

    if (has_m_mode) {
        imsic_m_phandle = phandle++;
    }
    imsic_s_phandle = phandle++;
    if (has_m_mode) {
        g_autofree char *name = g_strdup_printf("/soc/imsics@%"HWADDR_PRIx,
            memmap[XIANGSHAN_KMH_IMSIC_M].base);

        qemu_fdt_add_subnode(fdt, name);
        qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,imsics");
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, memmap[XIANGSHAN_KMH_IMSIC_M].base,
                                     2, memmap[XIANGSHAN_KMH_IMSIC_M].size);
        qemu_fdt_setprop(fdt, name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop(fdt, name, "msi-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, name, "#interrupt-cells", 0);
        qemu_fdt_setprop_cell(fdt, name, "riscv,num-ids",
                              XIANGSHAN_KMH_IMSIC_NUM_IDS);
        qemu_fdt_setprop(fdt, name, "interrupts-extended", imsic_m_cells,
                         ms->smp.cpus * sizeof(uint32_t) * 2);
        qemu_fdt_setprop_cell(fdt, name, "phandle", imsic_m_phandle);
    }
    {
        g_autofree char *name = g_strdup_printf("/soc/imsics@%"HWADDR_PRIx,
            memmap[XIANGSHAN_KMH_IMSIC_S].base);

        qemu_fdt_add_subnode(fdt, name);
        qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,imsics");
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, memmap[XIANGSHAN_KMH_IMSIC_S].base,
                                     2, memmap[XIANGSHAN_KMH_IMSIC_S].size);
        qemu_fdt_setprop(fdt, name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop(fdt, name, "msi-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, name, "#interrupt-cells", 0);
        qemu_fdt_setprop_cell(fdt, name, "riscv,num-ids",
                              XIANGSHAN_KMH_IMSIC_NUM_IDS);
        qemu_fdt_setprop_cell(fdt, name, "riscv,guest-index-bits",
                              XIANGSHAN_KMH_IMSIC_GUEST_BITS);
        qemu_fdt_setprop(fdt, name, "interrupts-extended", imsic_s_cells,
                         ms->smp.cpus * sizeof(uint32_t) * 2);
        qemu_fdt_setprop_cell(fdt, name, "phandle", imsic_s_phandle);
    }

    aplic_s_phandle = phandle++;
    if (has_m_mode) {
        aplic_m_phandle = phandle++;
    }
    {
        g_autofree char *name = g_strdup_printf("/soc/aplic@%"HWADDR_PRIx,
            memmap[XIANGSHAN_KMH_APLIC_S].base);

        qemu_fdt_add_subnode(fdt, name);
        qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,aplic");
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, memmap[XIANGSHAN_KMH_APLIC_S].base,
                                     2, memmap[XIANGSHAN_KMH_APLIC_S].size);
        qemu_fdt_setprop(fdt, name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, name, "#interrupt-cells", 2);
        qemu_fdt_setprop_cell(fdt, name, "riscv,num-sources",
                              XIANGSHAN_KMH_APLIC_NUM_SOURCES);
        qemu_fdt_setprop_cell(fdt, name, "msi-parent", imsic_s_phandle);
        qemu_fdt_setprop_cell(fdt, name, "phandle", aplic_s_phandle);
    }
    if (has_m_mode) {
        g_autofree char *name = g_strdup_printf("/soc/aplic@%"HWADDR_PRIx,
            memmap[XIANGSHAN_KMH_APLIC_M].base);

        qemu_fdt_add_subnode(fdt, name);
        qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,aplic");
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, memmap[XIANGSHAN_KMH_APLIC_M].base,
                                     2, memmap[XIANGSHAN_KMH_APLIC_M].size);
        qemu_fdt_setprop(fdt, name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, name, "#interrupt-cells", 2);
        qemu_fdt_setprop_cell(fdt, name, "riscv,num-sources",
                              XIANGSHAN_KMH_APLIC_NUM_SOURCES);
        qemu_fdt_setprop_cell(fdt, name, "msi-parent", imsic_m_phandle);
        qemu_fdt_setprop_cell(fdt, name, "riscv,children", aplic_s_phandle);
        qemu_fdt_setprop_cells(fdt, name, "riscv,delegate",
                               aplic_s_phandle, 1,
                               XIANGSHAN_KMH_APLIC_NUM_SOURCES);
        qemu_fdt_setprop_cell(fdt, name, "phandle", aplic_m_phandle);
    }

    {
        g_autofree char *name = g_strdup_printf("/soc/serial@%"HWADDR_PRIx,
            memmap[XIANGSHAN_KMH_UART0].base);

        qemu_fdt_add_subnode(fdt, name);
        qemu_fdt_setprop_string(fdt, name, "compatible", "ns16550a");
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, memmap[XIANGSHAN_KMH_UART0].base,
                                     2, memmap[XIANGSHAN_KMH_UART0].size);
        qemu_fdt_setprop_cell(fdt, name, "reg-shift", 2);
        qemu_fdt_setprop_cell(fdt, name, "reg-io-width", 4);
        qemu_fdt_setprop_cell(fdt, name, "clock-frequency",
                              XIANGSHAN_KMH_UART0_CLOCK);
        qemu_fdt_setprop_cell(fdt, name, "current-speed", 115200);
        qemu_fdt_setprop_cell(fdt, name, "interrupt-parent", aplic_s_phandle);
        qemu_fdt_setprop_cells(fdt, name, "interrupts",
                               XIANGSHAN_KMH_UART0_IRQ,
                               FDT_IRQ_TYPE_EDGE_RISING);
        qemu_fdt_setprop_string(fdt, name, "status", "okay");
    }

    if (s->my_virtio_blk) {
        xiangshan_kmh_fdt_add_my_virtio(s, "my_virtio_blk",
                                        XIANGSHAN_KMH_MY_VIRTIO_BLK,
                                        XIANGSHAN_KMH_MY_VIRTIO_BLK_IRQ,
                                        aplic_s_phandle);
    }

    if (s->my_virtio_net) {
        xiangshan_kmh_fdt_add_my_virtio(s, "my_virtio_net",
                                        XIANGSHAN_KMH_MY_VIRTIO_NET,
                                        XIANGSHAN_KMH_MY_VIRTIO_NET_IRQ,
                                        aplic_s_phandle);
    }

    if (s->my_virtio_console) {
        xiangshan_kmh_fdt_add_my_virtio(s, "my_virtio_console",
                                        XIANGSHAN_KMH_MY_VIRTIO_CONSOLE,
                                        XIANGSHAN_KMH_MY_VIRTIO_CONSOLE_IRQ,
                                        aplic_s_phandle);
    }

    if (s->my_virtio_gpu) {
        xiangshan_kmh_fdt_add_my_virtio(s, "my_virtio_gpu",
                                        XIANGSHAN_KMH_MY_VIRTIO_GPU,
                                        XIANGSHAN_KMH_MY_VIRTIO_GPU_IRQ,
                                        aplic_s_phandle);
    }

    if (s->my_virtio_keyboard) {
        xiangshan_kmh_fdt_add_my_virtio(s, "my_virtio_keyboard",
                                        XIANGSHAN_KMH_MY_VIRTIO_KEYBOARD,
                                        XIANGSHAN_KMH_MY_VIRTIO_KEYBOARD_IRQ,
                                        aplic_s_phandle);
    }

    if (s->my_virtio_mouse) {
        xiangshan_kmh_fdt_add_my_virtio(s, "my_virtio_mouse",
                                        XIANGSHAN_KMH_MY_VIRTIO_MOUSE,
                                        XIANGSHAN_KMH_MY_VIRTIO_MOUSE_IRQ,
                                        aplic_s_phandle);
    }

    if (s->my_virtio_tablet) {
        xiangshan_kmh_fdt_add_my_virtio(s, "my_virtio_tablet",
                                        XIANGSHAN_KMH_MY_VIRTIO_TABLET,
                                        XIANGSHAN_KMH_MY_VIRTIO_TABLET_IRQ,
                                        aplic_s_phandle);
    }

    if (kmh_is_iommu_sys_enabled(s)) {
        iommu_sys_phandle = xiangshan_kmh_fdt_add_iommu_sys(
            s, aplic_s_phandle, imsic_s_phandle, &phandle);
    }

    if (s->dw_pcie) {
        xiangshan_kmh_fdt_add_pcie(s, aplic_s_phandle, imsic_s_phandle,
                                   iommu_sys_phandle);
    }

    if (s->autotest_dtb) {
        xiangshan_kmh_fdt_add_autotest(s);
    }

    if (s->generated_acpi) {
        xiangshan_kmh_fdt_add_acpi_handoff(s);
    }
}

static void xiangshan_kmh_create_iommu_sys(XiangshanKmhState *s)
{
    const MemMapEntry *memmap = xiangshan_kmh_memmap;
    DeviceState *iommu_sys = qdev_new(TYPE_RISCV_IOMMU_SYS);
    XiangshanKmhSoCState *soc = &s->soc;
    PCIBus *bus = NULL;
    Object *iommu_obj;
    RISCVIOMMUState *iommu;

    if (s->dw_pcie) {
        DesignwarePCIEHost *pcie0 = &soc->pcie0;
        PCIHostState *pci_host = PCI_HOST_BRIDGE(pcie0);

        bus = pci_host->bus;
    }

    object_property_set_uint(OBJECT(iommu_sys), "addr",
                             memmap[XIANGSHAN_KMH_IOMMU_SYS].base,
                             &error_fatal);
    object_property_set_uint(OBJECT(iommu_sys), "base-irq",
                             XIANGSHAN_KMH_IOMMU_SYS_IRQ, &error_fatal);
    object_property_set_link(OBJECT(iommu_sys), "irqchip",
                             OBJECT(soc->irqchip), &error_fatal);

    iommu_obj = object_resolve_path_component(OBJECT(iommu_sys), "iommu");
    if (!iommu_obj) {
        error_report("failed to resolve KMH system IOMMU child object");
        exit(1);
    }
    iommu = RISCV_IOMMU(iommu_obj);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(iommu_sys), &error_fatal);

    if (bus && !bus->iommu_ops && !bus->iommu_opaque) {
        riscv_iommu_pci_setup_iommu(iommu, bus, &error_fatal);
    }
}

static void xiangshan_kmh_machine_init(MachineState *machine)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(machine);
    const MemMapEntry *memmap = xiangshan_kmh_memmap;
    MemoryRegion *system_memory = get_system_memory();
    hwaddr start_addr = memmap[XIANGSHAN_KMH_DRAM].base;
    target_ulong firmware_end_addr = start_addr;
    target_ulong kernel_start_addr;
    uint64_t fdt_load_addr = 0;
    uint64_t kernel_entry = 0;
    RISCVBootInfo boot_info;
    bool generate_dtb;
    bool kvm_m_mode = false;
    int fdt_size;
    const char *firmware_name;

#ifndef CONFIG_ACPI
    if (s->generated_acpi) {
        error_report("generated-acpi=on requires CONFIG_ACPI");
        exit(1);
    }
#endif

#ifdef CONFIG_KVM_NESTED
    bool kvm_nested = s->kvm_nested;

    if (kvm_nested && !kvm_enabled()) {
        error_report("'kvm-nested' requires KVM acceleration");
        exit(1);
    }
#endif

#ifdef CONFIG_KVM_M_MODE
    kvm_m_mode = s->kvm_m_mode;
    if (kvm_m_mode && !kvm_enabled()) {
        error_report("'kvm-m-mode' requires KVM acceleration");
        exit(1);
    }
#endif

#ifndef CONFIG_MY_VIRTIO
    if (xiangshan_kmh_my_virtio_requested(s)) {
        error_report("my-virtio support is not compiled in; "
                     "reconfigure QEMU with --enable-my-virtio");
        exit(1);
    }
#endif

    if (kvm_enabled()) {
        if (machine->firmware && strcmp(machine->firmware, "none")) {
            error_report("Machine mode firmware is not supported in "
                         "combination with KVM");
            exit(1);
        }
        if (!machine->firmware) {
            machine->firmware = g_strdup("none");
        }
        if (!machine->kernel_filename) {
            error_report("KVM direct kernel boot requires -kernel");
            exit(1);
        }
    }

    /* Initialize SoC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_XIANGSHAN_KMH_SOC);
    s->soc.dw_pcie = s->dw_pcie;
    s->soc.my_virtio_blk = s->my_virtio_blk;
    s->soc.my_virtio_net = s->my_virtio_net;
    s->soc.my_virtio_console = s->my_virtio_console;
    s->soc.my_virtio_gpu = s->my_virtio_gpu;
    s->soc.my_virtio_keyboard = s->my_virtio_keyboard;
    s->soc.my_virtio_mouse = s->my_virtio_mouse;
    s->soc.my_virtio_tablet = s->my_virtio_tablet;
#ifdef CONFIG_KVM_M_MODE
    s->soc.kvm_m_mode = kvm_m_mode;
#endif
    s->soc.my_virtio_blk_image = s->my_virtio_blk_image;
    s->soc.my_virtio_net_hostfwd = s->my_virtio_net_hostfwd;
    s->soc.my_virtio_net_network = s->my_virtio_net_network;
    s->soc.my_virtio_net_netmask = s->my_virtio_net_netmask;
    s->soc.my_virtio_net_host_ip = s->my_virtio_net_host_ip;
    s->soc.my_virtio_net_dhcp_start = s->my_virtio_net_dhcp_start;
    s->soc.my_virtio_net_dns_ip = s->my_virtio_net_dns_ip;
    s->soc.my_virtio_console_backend = s->my_virtio_console_backend;
    s->soc.my_virtio_console_input_path = s->my_virtio_console_input_path;
    s->soc.my_virtio_console_output_path = s->my_virtio_console_output_path;
    s->soc.my_virtio_keyboard_backend = s->my_virtio_keyboard_backend;
    s->soc.my_virtio_keyboard_evdev_path = s->my_virtio_keyboard_evdev_path;
    s->soc.my_virtio_mouse_backend = s->my_virtio_mouse_backend;
    s->soc.my_virtio_mouse_evdev_path = s->my_virtio_mouse_evdev_path;
    s->soc.my_virtio_tablet_backend = s->my_virtio_tablet_backend;
    s->soc.my_virtio_tablet_evdev_path = s->my_virtio_tablet_evdev_path;
    s->soc.my_virtio_vnc_listen = s->my_virtio_vnc_listen;
    if (s->dw_pcie) {
        object_initialize_child(OBJECT(machine), "pcie0", &s->soc.pcie0,
                                TYPE_DESIGNWARE_PCIE_HOST);
    }
    qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

#ifdef CONFIG_KVM_NESTED
    if (kvm_nested) {
        for (int cpu = 0; cpu < machine->smp.cpus; cpu++) {
            RISCVCPU *vcpu = &s->soc.cpus.harts[cpu];

            /* The actual nested-capable KVM vCPU already exposes H. */
            riscv_cpu_set_misa_ext(&vcpu->env,
                                   vcpu->env.misa_ext | RVH);
        }
    }
#endif

    /* Register RAM */
    memory_region_add_subregion(system_memory,
                                memmap[XIANGSHAN_KMH_DRAM].base,
                                machine->ram);

    generate_dtb = xiangshan_kmh_should_generate_dtb(s);
    if (machine->dtb && (s->generated_dtb == ON_OFF_AUTO_ON ||
                         s->autotest_dtb)) {
        error_report("-dtb cannot be combined with generated-dtb=on or "
                     "autotest-dtb=on");
        exit(1);
    }

    if (s->generated_acpi &&
        (!QEMU_IS_ALIGNED(s->acpi_handoff_addr, 16) ||
         !QEMU_IS_ALIGNED(s->acpi_handoff_size, 16) ||
         !xiangshan_kmh_range_in_dram(s, s->acpi_handoff_addr,
                                      s->acpi_handoff_size))) {
        error_report("acpi-handoff-addr/size must describe a non-empty "
                     "16-byte aligned range inside DDR");
        exit(1);
    }

    if (generate_dtb) {
        xiangshan_kmh_create_fdt(s);
    } else if (machine->dtb) {
        machine->fdt = load_device_tree(machine->dtb, &fdt_size);
        if (!machine->fdt) {
            error_report("load_device_tree() failed");
            exit(1);
        }
        s->fdt_size = fdt_size;
    }

    if (kvm_enabled() && !machine->fdt) {
        error_report("KVM direct kernel boot requires a device tree");
        exit(1);
    }

#ifdef CONFIG_ACPI
    if (s->generated_acpi) {
        xiangshan_kmh_acpi_setup(s);
    }
#endif

    firmware_name = XIANGSHAN_KMH_BIOS_BIN;
    firmware_end_addr = riscv_find_and_load_firmware(machine, firmware_name,
                                                     &start_addr, NULL);

    riscv_boot_info_init(&boot_info, &s->soc.cpus);

    if (machine->kernel_filename) {
        kernel_start_addr = kvm_m_mode ?
            memmap[XIANGSHAN_KMH_DRAM].base :
            riscv_calc_kernel_start_addr(&boot_info, firmware_end_addr);
        riscv_load_kernel(machine, &boot_info, kernel_start_addr,
                          kvm_enabled(), NULL);
        kernel_entry = boot_info.image_low_addr;

        if (machine->firmware && !strcmp(machine->firmware, "none")) {
            start_addr = kernel_entry;
        }
    }

    if (machine->fdt) {
        if (machine->kernel_filename) {
            fdt_load_addr = riscv_compute_fdt_addr(memmap[XIANGSHAN_KMH_DRAM].base,
                                                   memmap[XIANGSHAN_KMH_DRAM].size,
                                                   machine, &boot_info);
        } else {
            fdt_load_addr = xiangshan_kmh_fw_jump_fdt_addr(s);
        }
        riscv_load_fdt(fdt_load_addr, machine->fdt);
    }

    if (kvm_enabled()) {
        riscv_setup_direct_kernel(kernel_entry, fdt_load_addr);
    } else {
        /* ROM reset vector */
        riscv_setup_rom_reset_vec(machine, &s->soc.cpus,
                                  start_addr,
                                  memmap[XIANGSHAN_KMH_ROM].base,
                                  memmap[XIANGSHAN_KMH_ROM].size,
                                  kernel_entry, fdt_load_addr);
    }

    if (kmh_is_iommu_sys_enabled(s)) {
        xiangshan_kmh_create_iommu_sys(s);
    }
}


static void xiangshan_kmh_get_iommu_sys(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);
    OnOffAuto iommu_sys = s->iommu_sys;

    visit_type_OnOffAuto(v, name, &iommu_sys, errp);
}

static void xiangshan_kmh_set_iommu_sys(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &s->iommu_sys, errp);
}

static void xiangshan_kmh_get_generated_dtb(Object *obj, Visitor *v,
                                            const char *name, void *opaque,
                                            Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);
    OnOffAuto generated_dtb = s->generated_dtb;

    visit_type_OnOffAuto(v, name, &generated_dtb, errp);
}

static void xiangshan_kmh_set_generated_dtb(Object *obj, Visitor *v,
                                            const char *name, void *opaque,
                                            Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &s->generated_dtb, errp);
}

#ifdef CONFIG_KVM_M_MODE
static bool xiangshan_kmh_get_kvm_m_mode(Object *obj, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return s->kvm_m_mode;
}

static void xiangshan_kmh_set_kvm_m_mode(Object *obj, bool value,
                                         Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    s->kvm_m_mode = value;
}
#endif

#ifdef CONFIG_KVM_NESTED
static bool xiangshan_kmh_get_kvm_nested(Object *obj, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return s->kvm_nested;
}

static void xiangshan_kmh_set_kvm_nested(Object *obj, bool value,
                                         Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    s->kvm_nested = value;
}
#endif

static bool xiangshan_kmh_get_generated_acpi(Object *obj, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return s->generated_acpi;
}

static void xiangshan_kmh_set_generated_acpi(Object *obj, bool value,
                                             Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    s->generated_acpi = value;
}

static bool xiangshan_kmh_get_autotest_dtb(Object *obj, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return s->autotest_dtb;
}

static void xiangshan_kmh_set_autotest_dtb(Object *obj, bool value, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    s->autotest_dtb = value;
}

static bool xiangshan_kmh_get_dw_pcie(Object *obj, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return s->dw_pcie;
}

static void xiangshan_kmh_set_dw_pcie(Object *obj, bool value, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    s->dw_pcie = value;
}

static bool xiangshan_kmh_get_my_virtio_blk(Object *obj, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return s->my_virtio_blk;
}

static void xiangshan_kmh_set_my_virtio_blk(Object *obj, bool value,
                                            Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    s->my_virtio_blk = value;
}

static bool xiangshan_kmh_get_my_virtio_net(Object *obj, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return s->my_virtio_net;
}

static void xiangshan_kmh_set_my_virtio_net(Object *obj, bool value,
                                            Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    s->my_virtio_net = value;
}

static bool xiangshan_kmh_get_my_virtio_console(Object *obj, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return s->my_virtio_console;
}

static void xiangshan_kmh_set_my_virtio_console(Object *obj, bool value,
                                                Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    s->my_virtio_console = value;
}

static bool xiangshan_kmh_get_my_virtio_gpu(Object *obj, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return s->my_virtio_gpu;
}

static void xiangshan_kmh_set_my_virtio_gpu(Object *obj, bool value,
                                            Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    s->my_virtio_gpu = value;
}

static bool xiangshan_kmh_get_my_virtio_keyboard(Object *obj, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return s->my_virtio_keyboard;
}

static void xiangshan_kmh_set_my_virtio_keyboard(Object *obj, bool value,
                                                 Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    s->my_virtio_keyboard = value;
}

static bool xiangshan_kmh_get_my_virtio_mouse(Object *obj, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return s->my_virtio_mouse;
}

static void xiangshan_kmh_set_my_virtio_mouse(Object *obj, bool value,
                                              Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    s->my_virtio_mouse = value;
}

static bool xiangshan_kmh_get_my_virtio_tablet(Object *obj, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return s->my_virtio_tablet;
}

static void xiangshan_kmh_set_my_virtio_tablet(Object *obj, bool value,
                                               Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    s->my_virtio_tablet = value;
}

static char *xiangshan_kmh_get_my_virtio_blk_image(Object *obj, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return g_strdup(s->my_virtio_blk_image ? s->my_virtio_blk_image : "");
}

static void xiangshan_kmh_set_my_virtio_blk_image(Object *obj,
                                                  const char *value,
                                                  Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    g_free(s->my_virtio_blk_image);
    s->my_virtio_blk_image = g_strdup(value && *value ? value : "disk.img");
}

static char *xiangshan_kmh_get_my_virtio_net_hostfwd(Object *obj, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return g_strdup(s->my_virtio_net_hostfwd ? s->my_virtio_net_hostfwd : "");
}

static void xiangshan_kmh_set_my_virtio_net_hostfwd(Object *obj,
                                                    const char *value,
                                                    Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    g_free(s->my_virtio_net_hostfwd);
    s->my_virtio_net_hostfwd = g_strdup(value ? value : "");
}

#define XIANGSHAN_KMH_NET_STR_PROP_ACCESSORS(_suffix, _field) \
    static char *xiangshan_kmh_get_my_virtio_net_##_suffix(Object *obj, \
                                                           Error **errp) \
    { \
        XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj); \
        return g_strdup(s->_field ? s->_field : ""); \
    } \
    static void xiangshan_kmh_set_my_virtio_net_##_suffix(Object *obj, \
                                                          const char *value, \
                                                          Error **errp) \
    { \
        XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj); \
        g_free(s->_field); \
        s->_field = g_strdup(value ? value : ""); \
    }

XIANGSHAN_KMH_NET_STR_PROP_ACCESSORS(network, my_virtio_net_network)
XIANGSHAN_KMH_NET_STR_PROP_ACCESSORS(netmask, my_virtio_net_netmask)
XIANGSHAN_KMH_NET_STR_PROP_ACCESSORS(host_ip, my_virtio_net_host_ip)
XIANGSHAN_KMH_NET_STR_PROP_ACCESSORS(dhcp_start, my_virtio_net_dhcp_start)
XIANGSHAN_KMH_NET_STR_PROP_ACCESSORS(dns_ip, my_virtio_net_dns_ip)

#undef XIANGSHAN_KMH_NET_STR_PROP_ACCESSORS

static char *xiangshan_kmh_get_my_virtio_console_backend(Object *obj,
                                                         Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return g_strdup(s->my_virtio_console_backend ?
                    s->my_virtio_console_backend : "");
}

static void xiangshan_kmh_set_my_virtio_console_backend(Object *obj,
                                                        const char *value,
                                                        Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    g_free(s->my_virtio_console_backend);
    s->my_virtio_console_backend = g_strdup(value && *value ? value :
                                            "external");
}

static char *xiangshan_kmh_get_my_virtio_console_input_path(Object *obj,
                                                            Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return g_strdup(s->my_virtio_console_input_path ?
                    s->my_virtio_console_input_path : "");
}

static void xiangshan_kmh_set_my_virtio_console_input_path(Object *obj,
                                                           const char *value,
                                                           Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    g_free(s->my_virtio_console_input_path);
    s->my_virtio_console_input_path = g_strdup(value ? value : "");
}

static char *xiangshan_kmh_get_my_virtio_console_output_path(Object *obj,
                                                             Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return g_strdup(s->my_virtio_console_output_path ?
                    s->my_virtio_console_output_path : "");
}

static void xiangshan_kmh_set_my_virtio_console_output_path(Object *obj,
                                                            const char *value,
                                                            Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    g_free(s->my_virtio_console_output_path);
    s->my_virtio_console_output_path = g_strdup(value ? value : "");
}

#define XIANGSHAN_KMH_INPUT_STR_PROP_ACCESSORS(_suffix, _field, _default) \
    static char *xiangshan_kmh_get_my_virtio_##_suffix(Object *obj, \
                                                       Error **errp) \
    { \
        XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj); \
        return g_strdup(s->_field ? s->_field : ""); \
    } \
    static void xiangshan_kmh_set_my_virtio_##_suffix(Object *obj, \
                                                      const char *value, \
                                                      Error **errp) \
    { \
        XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj); \
        g_free(s->_field); \
        s->_field = g_strdup(value && *value ? value : _default); \
    }

XIANGSHAN_KMH_INPUT_STR_PROP_ACCESSORS(keyboard_backend,
                                       my_virtio_keyboard_backend,
                                       "vnc")
XIANGSHAN_KMH_INPUT_STR_PROP_ACCESSORS(keyboard_evdev,
                                       my_virtio_keyboard_evdev_path,
                                       "")
XIANGSHAN_KMH_INPUT_STR_PROP_ACCESSORS(mouse_backend,
                                       my_virtio_mouse_backend,
                                       "vnc")
XIANGSHAN_KMH_INPUT_STR_PROP_ACCESSORS(mouse_evdev,
                                       my_virtio_mouse_evdev_path,
                                       "")
XIANGSHAN_KMH_INPUT_STR_PROP_ACCESSORS(tablet_backend,
                                       my_virtio_tablet_backend,
                                       "vnc")
XIANGSHAN_KMH_INPUT_STR_PROP_ACCESSORS(tablet_evdev,
                                       my_virtio_tablet_evdev_path,
                                       "")
XIANGSHAN_KMH_INPUT_STR_PROP_ACCESSORS(vnc_listen,
                                       my_virtio_vnc_listen,
                                       "127.0.0.1:5915")

#undef XIANGSHAN_KMH_INPUT_STR_PROP_ACCESSORS

static void xiangshan_kmh_get_uint64(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);
    uint64_t value = *(uint64_t *)((char *)s + (uintptr_t)opaque);

    visit_type_uint64(v, name, &value, errp);
}

static void xiangshan_kmh_set_uint64(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    visit_type_uint64(v, name,
                      (uint64_t *)((char *)s + (uintptr_t)opaque), errp);
}

#define XIANGSHAN_KMH_UINT64_PROP(_name, _field, _desc) \
    do { \
        object_class_property_add(klass, _name, "uint64", \
                                  xiangshan_kmh_get_uint64, \
                                  xiangshan_kmh_set_uint64, NULL, \
                                  (void *)offsetof(XiangshanKmhState, _field)); \
        object_class_property_set_description(klass, _name, _desc); \
    } while (0)

static void xiangshan_kmh_machine_instance_init(Object *obj)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    s->iommu_sys = ON_OFF_AUTO_AUTO;
    s->generated_dtb = ON_OFF_AUTO_AUTO;
    s->generated_acpi = false;
    s->autotest_dtb = false;
    s->dw_pcie = false;
    s->my_virtio_blk = false;
    s->my_virtio_net = false;
    s->my_virtio_console = false;
    s->my_virtio_gpu = false;
    s->my_virtio_keyboard = false;
    s->my_virtio_mouse = false;
    s->my_virtio_tablet = false;
#ifdef CONFIG_KVM_M_MODE
    s->kvm_m_mode = false;
#endif
#ifdef CONFIG_KVM_NESTED
    s->kvm_nested = false;
#endif
    s->my_virtio_blk_image = g_strdup("disk.img");
    s->my_virtio_net_hostfwd = g_strdup("");
    s->my_virtio_net_network = g_strdup("");
    s->my_virtio_net_netmask = g_strdup("");
    s->my_virtio_net_host_ip = g_strdup("");
    s->my_virtio_net_dhcp_start = g_strdup("");
    s->my_virtio_net_dns_ip = g_strdup("");
    s->my_virtio_console_backend = g_strdup("external");
    s->my_virtio_console_input_path = g_strdup("");
    s->my_virtio_console_output_path = g_strdup("");
    s->my_virtio_keyboard_backend = g_strdup("vnc");
    s->my_virtio_keyboard_evdev_path = g_strdup("");
    s->my_virtio_mouse_backend = g_strdup("vnc");
    s->my_virtio_mouse_evdev_path = g_strdup("");
    s->my_virtio_tablet_backend = g_strdup("vnc");
    s->my_virtio_tablet_evdev_path = g_strdup("");
    s->my_virtio_vnc_listen = g_strdup("127.0.0.1:5915");
    s->acpi_handoff_addr = XIANGSHAN_KMH_ACPI_HANDOFF_ADDR;
    s->acpi_handoff_size = XIANGSHAN_KMH_ACPI_HANDOFF_SIZE;
    s->fw_jump_fdt_addr = XIANGSHAN_KMH_FW_JUMP_FDT_ADDR;
    s->autotest_image_addr = XIANGSHAN_KMH_AUTOTEST_IMAGE_ADDR;
    s->autotest_rootfs_addr = XIANGSHAN_KMH_AUTOTEST_ROOTFS_ADDR;
    s->autotest_workload_addr = XIANGSHAN_KMH_AUTOTEST_WORKLOAD_ADDR;
    s->autotest_trigger_addr = XIANGSHAN_KMH_AUTOTEST_TRIGGER_ADDR;
    s->autotest_rootfs_size = XIANGSHAN_KMH_AUTOTEST_ROOTFS_SIZE;
    s->autotest_workload_size = XIANGSHAN_KMH_AUTOTEST_WORKLOAD_SIZE;
    s->autotest_trigger_size = XIANGSHAN_KMH_AUTOTEST_TRIGGER_SIZE;
}

static void xiangshan_kmh_machine_class_init(ObjectClass *klass, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(klass);
    static const char *const valid_cpu_types[] = {
        TYPE_RISCV_CPU_XIANGSHAN_KMH,
        TYPE_RISCV_CPU_HOST,
        NULL
    };

    mc->desc = "RISC-V Board compatible with the Xiangshan " \
               "Kunminghu FPGA prototype platform";
    mc->init = xiangshan_kmh_machine_init;
    mc->max_cpus = XIANGSHAN_KMH_MAX_CPUS;
    mc->default_cpu_type = TYPE_RISCV_CPU_XIANGSHAN_KMH;
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_id = "xiangshan.kunminghu.ram";

    object_class_property_add(klass, "iommu-sys", "OnOffAuto",
                                xiangshan_kmh_get_iommu_sys, xiangshan_kmh_set_iommu_sys,
                                NULL, NULL);
    object_class_property_set_description(klass, "iommu-sys",
                                          "Enable IOMMU platform device");

    object_class_property_add(klass, "generated-dtb", "OnOffAuto",
                              xiangshan_kmh_get_generated_dtb,
                              xiangshan_kmh_set_generated_dtb, NULL, NULL);
    object_class_property_set_description(klass, "generated-dtb",
                                          "Use QEMU-generated device tree");

#ifdef CONFIG_KVM_M_MODE
    object_class_property_add_bool(klass, "kvm-m-mode",
                                   xiangshan_kmh_get_kvm_m_mode,
                                   xiangshan_kmh_set_kvm_m_mode);
    object_class_property_set_description(
        klass, "kvm-m-mode",
        "Start KVM VCPUs in software-emulated RISC-V M-mode");
#endif

#ifdef CONFIG_KVM_NESTED
    object_class_property_add_bool(klass, "kvm-nested",
                                   xiangshan_kmh_get_kvm_nested,
                                   xiangshan_kmh_set_kvm_nested);
    object_class_property_set_description(
        klass, "kvm-nested",
        "Expose software-emulated RISC-V H extension to KVM guests");
#endif

    object_class_property_add_bool(klass, "generated-acpi",
                                   xiangshan_kmh_get_generated_acpi,
                                   xiangshan_kmh_set_generated_acpi);
    object_class_property_set_description(klass, "generated-acpi",
                                          "Use QEMU-generated ACPI tables");

    object_class_property_add_bool(klass, "autotest-dtb",
                                   xiangshan_kmh_get_autotest_dtb,
                                   xiangshan_kmh_set_autotest_dtb);
    object_class_property_set_description(klass, "autotest-dtb",
                                          "Add autotest nvdimm and reserved-memory nodes");

    object_class_property_add_bool(klass, "dw-pcie",
                                   xiangshan_kmh_get_dw_pcie,
                                   xiangshan_kmh_set_dw_pcie);
    object_class_property_set_description(klass, "dw-pcie",
                                          "Enable DWC PCIe host controller");

    object_class_property_add_bool(klass, "my-virtio-blk",
                                   xiangshan_kmh_get_my_virtio_blk,
                                   xiangshan_kmh_set_my_virtio_blk);
    object_class_property_set_description(klass, "my-virtio-blk",
                                          "Enable my-virtio block device");

    object_class_property_add_bool(klass, "my-virtio-net",
                                   xiangshan_kmh_get_my_virtio_net,
                                   xiangshan_kmh_set_my_virtio_net);
    object_class_property_set_description(klass, "my-virtio-net",
                                          "Enable my-virtio network device");

    object_class_property_add_bool(klass, "my-virtio-console",
                                   xiangshan_kmh_get_my_virtio_console,
                                   xiangshan_kmh_set_my_virtio_console);
    object_class_property_set_description(klass, "my-virtio-console",
                                          "Enable my-virtio console device");

    object_class_property_add_bool(klass, "my-virtio-gpu",
                                   xiangshan_kmh_get_my_virtio_gpu,
                                   xiangshan_kmh_set_my_virtio_gpu);
    object_class_property_set_description(klass, "my-virtio-gpu",
                                          "Enable my-virtio GPU device");

    object_class_property_add_bool(klass, "my-virtio-keyboard",
                                   xiangshan_kmh_get_my_virtio_keyboard,
                                   xiangshan_kmh_set_my_virtio_keyboard);
    object_class_property_set_description(klass, "my-virtio-keyboard",
                                          "Enable my-virtio keyboard input device");

    object_class_property_add_bool(klass, "my-virtio-mouse",
                                   xiangshan_kmh_get_my_virtio_mouse,
                                   xiangshan_kmh_set_my_virtio_mouse);
    object_class_property_set_description(klass, "my-virtio-mouse",
                                          "Enable my-virtio mouse input device");

    object_class_property_add_bool(klass, "my-virtio-tablet",
                                   xiangshan_kmh_get_my_virtio_tablet,
                                   xiangshan_kmh_set_my_virtio_tablet);
    object_class_property_set_description(klass, "my-virtio-tablet",
                                          "Enable my-virtio absolute tablet input device");

    object_class_property_add_str(klass, "my-virtio-blk-image",
                                  xiangshan_kmh_get_my_virtio_blk_image,
                                  xiangshan_kmh_set_my_virtio_blk_image);
    object_class_property_set_description(klass, "my-virtio-blk-image",
                                          "Disk image path for my-virtio block device");

    object_class_property_add_str(klass, "my-virtio-net-hostfwd",
                                  xiangshan_kmh_get_my_virtio_net_hostfwd,
                                  xiangshan_kmh_set_my_virtio_net_hostfwd);
    object_class_property_set_description(klass, "my-virtio-net-hostfwd",
                                          "libslirp hostfwd rules for my-virtio network device");

    object_class_property_add_str(klass, "my-virtio-net-network",
                                  xiangshan_kmh_get_my_virtio_net_network,
                                  xiangshan_kmh_set_my_virtio_net_network);
    object_class_property_set_description(klass, "my-virtio-net-network",
                                          "IPv4 network for my-virtio libslirp backend");

    object_class_property_add_str(klass, "my-virtio-net-netmask",
                                  xiangshan_kmh_get_my_virtio_net_netmask,
                                  xiangshan_kmh_set_my_virtio_net_netmask);
    object_class_property_set_description(klass, "my-virtio-net-netmask",
                                          "IPv4 netmask for my-virtio libslirp backend");

    object_class_property_add_str(klass, "my-virtio-net-host-ip",
                                  xiangshan_kmh_get_my_virtio_net_host_ip,
                                  xiangshan_kmh_set_my_virtio_net_host_ip);
    object_class_property_set_description(klass, "my-virtio-net-host-ip",
                                          "Host IPv4 address for my-virtio libslirp backend");

    object_class_property_add_str(klass, "my-virtio-net-dhcp-start",
                                  xiangshan_kmh_get_my_virtio_net_dhcp_start,
                                  xiangshan_kmh_set_my_virtio_net_dhcp_start);
    object_class_property_set_description(klass, "my-virtio-net-dhcp-start",
                                          "DHCP start IPv4 address for my-virtio libslirp backend");

    object_class_property_add_str(klass, "my-virtio-net-dns-ip",
                                  xiangshan_kmh_get_my_virtio_net_dns_ip,
                                  xiangshan_kmh_set_my_virtio_net_dns_ip);
    object_class_property_set_description(klass, "my-virtio-net-dns-ip",
                                          "DNS IPv4 address for my-virtio libslirp backend");

    object_class_property_add_str(klass, "my-virtio-console-backend",
                                  xiangshan_kmh_get_my_virtio_console_backend,
                                  xiangshan_kmh_set_my_virtio_console_backend);
    object_class_property_set_description(klass, "my-virtio-console-backend",
                                          "Backend for my-virtio console: external, stdio, fd, or pty");

    object_class_property_add_str(klass, "my-virtio-console-input",
                                  xiangshan_kmh_get_my_virtio_console_input_path,
                                  xiangshan_kmh_set_my_virtio_console_input_path);
    object_class_property_set_description(klass, "my-virtio-console-input",
                                          "Input path for my-virtio console fd backend");

    object_class_property_add_str(klass, "my-virtio-console-output",
                                  xiangshan_kmh_get_my_virtio_console_output_path,
                                  xiangshan_kmh_set_my_virtio_console_output_path);
    object_class_property_set_description(klass, "my-virtio-console-output",
                                          "Output path for my-virtio console fd backend");

    object_class_property_add_str(klass, "my-virtio-keyboard-backend",
                                  xiangshan_kmh_get_my_virtio_keyboard_backend,
                                  xiangshan_kmh_set_my_virtio_keyboard_backend);
    object_class_property_set_description(klass, "my-virtio-keyboard-backend",
                                          "Backend for my-virtio keyboard: evdev, vnc, or ui");

    object_class_property_add_str(klass, "my-virtio-keyboard-evdev",
                                  xiangshan_kmh_get_my_virtio_keyboard_evdev,
                                  xiangshan_kmh_set_my_virtio_keyboard_evdev);
    object_class_property_set_description(klass, "my-virtio-keyboard-evdev",
                                          "Linux evdev event path for my-virtio keyboard evdev backend");

    object_class_property_add_str(klass, "my-virtio-mouse-backend",
                                  xiangshan_kmh_get_my_virtio_mouse_backend,
                                  xiangshan_kmh_set_my_virtio_mouse_backend);
    object_class_property_set_description(klass, "my-virtio-mouse-backend",
                                          "Backend for my-virtio mouse: evdev, vnc, or ui");

    object_class_property_add_str(klass, "my-virtio-mouse-evdev",
                                  xiangshan_kmh_get_my_virtio_mouse_evdev,
                                  xiangshan_kmh_set_my_virtio_mouse_evdev);
    object_class_property_set_description(klass, "my-virtio-mouse-evdev",
                                          "Linux evdev event path for my-virtio mouse evdev backend");

    object_class_property_add_str(klass, "my-virtio-tablet-backend",
                                  xiangshan_kmh_get_my_virtio_tablet_backend,
                                  xiangshan_kmh_set_my_virtio_tablet_backend);
    object_class_property_set_description(klass, "my-virtio-tablet-backend",
                                          "Backend for my-virtio tablet: evdev, vnc, or ui");

    object_class_property_add_str(klass, "my-virtio-tablet-evdev",
                                  xiangshan_kmh_get_my_virtio_tablet_evdev,
                                  xiangshan_kmh_set_my_virtio_tablet_evdev);
    object_class_property_set_description(klass, "my-virtio-tablet-evdev",
                                          "Linux evdev event path for my-virtio tablet evdev backend");

    object_class_property_add_str(klass, "my-virtio-vnc-listen",
                                  xiangshan_kmh_get_my_virtio_vnc_listen,
                                  xiangshan_kmh_set_my_virtio_vnc_listen);
    object_class_property_set_description(klass, "my-virtio-vnc-listen",
                                          "Listen address for backend VNC server, host:port");

    XIANGSHAN_KMH_UINT64_PROP("acpi-handoff-addr", acpi_handoff_addr,
                              "DDR base address for generated ACPI handoff");
    XIANGSHAN_KMH_UINT64_PROP("acpi-handoff-size", acpi_handoff_size,
                              "DDR size for generated ACPI handoff");
    XIANGSHAN_KMH_UINT64_PROP("fw-jump-fdt-addr", fw_jump_fdt_addr,
                              "Generated DTB load address for fw_jump boot");
    XIANGSHAN_KMH_UINT64_PROP("autotest-image-addr", autotest_image_addr,
                              "Autotest Image loader address");
    XIANGSHAN_KMH_UINT64_PROP("autotest-rootfs-addr", autotest_rootfs_addr,
                              "Autotest rootfs pmem loader address");
    XIANGSHAN_KMH_UINT64_PROP("autotest-rootfs-size", autotest_rootfs_size,
                              "Autotest rootfs pmem and reserved-memory size");
    XIANGSHAN_KMH_UINT64_PROP("autotest-workload-addr", autotest_workload_addr,
                              "Autotest workload pmem loader address");
    XIANGSHAN_KMH_UINT64_PROP("autotest-workload-size", autotest_workload_size,
                              "Autotest workload pmem and reserved-memory size");
    XIANGSHAN_KMH_UINT64_PROP("autotest-trigger-addr", autotest_trigger_addr,
                              "Autotest trigger reserved-memory loader address");
    XIANGSHAN_KMH_UINT64_PROP("autotest-trigger-size", autotest_trigger_size,
                              "Autotest trigger reserved-memory size");
}

#undef XIANGSHAN_KMH_UINT64_PROP

static const TypeInfo xiangshan_kmh_machine_info = {
    .name = TYPE_XIANGSHAN_KMH_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(XiangshanKmhState),
    .class_init = xiangshan_kmh_machine_class_init,
    .instance_init = xiangshan_kmh_machine_instance_init,
};

static void xiangshan_kmh_machine_register_types(void)
{
    type_register_static(&xiangshan_kmh_machine_info);
}
type_init(xiangshan_kmh_machine_register_types)
