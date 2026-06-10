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
#include "hw/riscv/iommu.h"
#include "hw/riscv/riscv-iommu.h"
#include "hw/riscv/riscv-iommu-bits.h"
#include "qapi/qapi-visit-common.h"
#include "target/riscv/cpu_bits.h"

#define XIANGSHAN_KMH_BIOS_BIN "opensbi-riscv64-xiangshan-kmh-fw_dynamic.bin"
#define XIANGSHAN_KMH_FW_JUMP_FDT_ADDR 0x80200000ULL
#define XIANGSHAN_KMH_AUTOTEST_IMAGE_ADDR 0x80400000ULL
#define XIANGSHAN_KMH_AUTOTEST_ROOTFS_ADDR 0x3c0000000ULL
#define XIANGSHAN_KMH_AUTOTEST_WORKLOAD_ADDR 0x3e0000000ULL
#define XIANGSHAN_KMH_AUTOTEST_TRIGGER_ADDR 0x90000000ULL
#define XIANGSHAN_KMH_AUTOTEST_ROOTFS_SIZE 0x20000000ULL
#define XIANGSHAN_KMH_AUTOTEST_WORKLOAD_SIZE 0x80000000ULL
#define XIANGSHAN_KMH_AUTOTEST_TRIGGER_SIZE 0x200000ULL
#define XIANGSHAN_KMH_UART0_CLOCK 50000000
#define FDT_IRQ_TYPE_EDGE_RISING 4
#define DESIGNWARE_PCIE_IRQ_MSI 4

#define XIANGSHAN_KMH_PCIE0_CFG_BASE 0x67ff0000ULL
#define XIANGSHAN_KMH_PCIE0_CFG_SIZE 0x00010000ULL
#define XIANGSHAN_KMH_PCIE0_LOW_BUS_BASE 0x40000000ULL

static const MemMapEntry xiangshan_kmh_memmap[] = {
    [XIANGSHAN_KMH_ROM]      =        {     0x1000,       0x40000 },
    [XIANGSHAN_KMH_FLASH]    =        { 0x10000000,     0x4000000 },
    [XIANGSHAN_KMH_UART0]    =        { 0x310B0000,       0x10000 },
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

static bool kmh_is_iommu_sys_enabled(XiangshanKmhState *s)
{
    return s->iommu_sys == ON_OFF_AUTO_ON;
}

static bool xiangshan_kmh_should_generate_dtb(XiangshanKmhState *s)
{
    MachineState *machine = MACHINE(s);

    return s->generated_dtb == ON_OFF_AUTO_ON ||
           (s->generated_dtb == ON_OFF_AUTO_AUTO && !machine->dtb &&
            (machine->kernel_filename || s->autotest_dtb || s->pcie_dtb ||
             kmh_is_iommu_sys_enabled(s)));
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

typedef struct XiangshanKmhMemRange {
    hwaddr start;
    hwaddr end;
} XiangshanKmhMemRange;

static hwaddr xiangshan_kmh_range_end(hwaddr start, uint64_t size)
{
    hwaddr end = start + size;

    return end < start ? HWADDR_MAX : end;
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
                                               XIANGSHAN_KMH_AUTOTEST_ROOTFS_SIZE),
            }, {
                .start = s->autotest_workload_addr,
                .end = xiangshan_kmh_range_end(s->autotest_workload_addr,
                                               XIANGSHAN_KMH_AUTOTEST_WORKLOAD_SIZE),
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

    qemu_fdt_add_subnode(fdt, "/reserved-memory");
    qemu_fdt_setprop_cell(fdt, "/reserved-memory", "#address-cells", 2);
    qemu_fdt_setprop_cell(fdt, "/reserved-memory", "#size-cells", 2);
    qemu_fdt_setprop(fdt, "/reserved-memory", "ranges", NULL, 0);

    {
        g_autofree char *name = g_strdup_printf(
            "/reserved-memory/region@%"HWADDR_PRIx, rootfs_addr);

        xiangshan_kmh_fdt_add_reg_node(fdt,
                                       "/reserved-memory/region@%"HWADDR_PRIx,
                                       rootfs_addr,
                                       XIANGSHAN_KMH_AUTOTEST_ROOTFS_SIZE,
                                       NULL, NULL);
        qemu_fdt_setprop(fdt, name, "no-map", NULL, 0);
    }

    {
        g_autofree char *name = g_strdup_printf(
            "/reserved-memory/region@%"HWADDR_PRIx, workload_addr);

        xiangshan_kmh_fdt_add_reg_node(fdt,
                                       "/reserved-memory/region@%"HWADDR_PRIx,
                                       workload_addr,
                                       XIANGSHAN_KMH_AUTOTEST_WORKLOAD_SIZE,
                                       NULL, NULL);
        qemu_fdt_setprop(fdt, name, "no-map", NULL, 0);
    }

    xiangshan_kmh_fdt_add_reg_node(fdt,
                                   "/reserved-memory/my_reserved_buffer@%"HWADDR_PRIx,
                                   trigger_addr,
                                   XIANGSHAN_KMH_AUTOTEST_TRIGGER_SIZE,
                                   "my,reserved-mem", "okay");

    /*
     * qemu_fdt_add_subnode() prepends children. Add workload first so rootfs
     * remains before workload in the DT and is normally probed as /dev/pmem0.
     */
    xiangshan_kmh_fdt_add_pmem(fdt, workload_addr,
                               XIANGSHAN_KMH_AUTOTEST_WORKLOAD_SIZE);
    xiangshan_kmh_fdt_add_pmem(fdt, rootfs_addr,
                               XIANGSHAN_KMH_AUTOTEST_ROOTFS_SIZE);
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

static void xiangshan_kmh_create_fdt(XiangshanKmhState *s)
{
    MachineState *ms = MACHINE(s);
    const MemMapEntry *memmap = xiangshan_kmh_memmap;
    uint32_t phandle = 1;
    uint32_t imsic_m_phandle, imsic_s_phandle;
    uint32_t aplic_m_phandle, aplic_s_phandle;
    uint32_t iommu_sys_phandle = 0;
    g_autofree uint32_t *intc_phandles = g_new0(uint32_t, ms->smp.cpus);
    g_autofree uint32_t *clint_cells = g_new0(uint32_t, ms->smp.cpus * 4);
    g_autofree uint32_t *imsic_m_cells = g_new0(uint32_t, ms->smp.cpus * 2);
    g_autofree uint32_t *imsic_s_cells = g_new0(uint32_t, ms->smp.cpus * 2);
    void *fdt;
    int cpu;
    static const char * const cpu_compat[2] = {
        "bosc,kmh-v2", "riscv"
    };
    static const char * const soc_compat[2] = {
        "bosc,kmh-v2-soc", "simple-bus"
    };

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
    if (ms->kernel_cmdline && *ms->kernel_cmdline) {
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
                                ms->kernel_cmdline);
    }

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 1);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
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

    for (cpu = 0; cpu < ms->smp.cpus; cpu++) {
        clint_cells[cpu * 4 + 0] = cpu_to_be32(intc_phandles[cpu]);
        clint_cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_SOFT);
        clint_cells[cpu * 4 + 2] = cpu_to_be32(intc_phandles[cpu]);
        clint_cells[cpu * 4 + 3] = cpu_to_be32(IRQ_M_TIMER);
        imsic_m_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        imsic_m_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_M_EXT);
        imsic_s_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        imsic_s_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_S_EXT);
    }

    {
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

    imsic_m_phandle = phandle++;
    imsic_s_phandle = phandle++;
    {
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
    aplic_m_phandle = phandle++;
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
    {
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

    if (kmh_is_iommu_sys_enabled(s)) {
        iommu_sys_phandle = xiangshan_kmh_fdt_add_iommu_sys(
            s, aplic_s_phandle, imsic_s_phandle, &phandle);
    }

    if (s->pcie_dtb) {
        xiangshan_kmh_fdt_add_pcie(s, aplic_s_phandle, imsic_s_phandle,
                                   iommu_sys_phandle);
    }

    if (s->autotest_dtb) {
        xiangshan_kmh_fdt_add_autotest(s);
    }
}

static void xiangshan_kmh_create_iommu_sys(XiangshanKmhState *s)
{
    const MemMapEntry *memmap = xiangshan_kmh_memmap;
    DeviceState *iommu_sys = qdev_new(TYPE_RISCV_IOMMU_SYS);
    XiangshanKmhSoCState *soc = &s->soc;
    DesignwarePCIEHost *pcie0 = &soc->pcie0;
    PCIHostState *pci_host = PCI_HOST_BRIDGE(pcie0);
    PCIBus *bus = pci_host->bus;
    Object *iommu_obj;
    RISCVIOMMUState *iommu;

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

    if (!bus->iommu_ops && !bus->iommu_opaque) {
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
    int fdt_size;
    const char *firmware_name;

    /* Initialize SoC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_XIANGSHAN_KMH_SOC);
    qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

    /* Register RAM */
    memory_region_add_subregion(system_memory,
                                memmap[XIANGSHAN_KMH_DRAM].base,
                                machine->ram);

    generate_dtb = xiangshan_kmh_should_generate_dtb(s);
    if (machine->dtb && (s->generated_dtb == ON_OFF_AUTO_ON ||
                         s->autotest_dtb || s->pcie_dtb)) {
        error_report("-dtb cannot be combined with generated-dtb=on or "
                     "autotest-dtb=on or pcie-dtb=on");
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

    firmware_name = XIANGSHAN_KMH_BIOS_BIN;
    firmware_end_addr = riscv_find_and_load_firmware(machine, firmware_name,
                                                     &start_addr, NULL);

    riscv_boot_info_init(&boot_info, &s->soc.cpus);

    if (machine->kernel_filename) {
        kernel_start_addr = riscv_calc_kernel_start_addr(&boot_info,
                                                         firmware_end_addr);
        riscv_load_kernel(machine, &boot_info, kernel_start_addr,
                          false, NULL);
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

    /* ROM reset vector */
    riscv_setup_rom_reset_vec(machine, &s->soc.cpus,
                              start_addr,
                              memmap[XIANGSHAN_KMH_ROM].base,
                              memmap[XIANGSHAN_KMH_ROM].size,
                              kernel_entry, fdt_load_addr);

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

static bool xiangshan_kmh_get_pcie_dtb(Object *obj, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    return s->pcie_dtb;
}

static void xiangshan_kmh_set_pcie_dtb(Object *obj, bool value, Error **errp)
{
    XiangshanKmhState *s = XIANGSHAN_KMH_MACHINE(obj);

    s->pcie_dtb = value;
}

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
    s->autotest_dtb = false;
    s->pcie_dtb = false;
    s->fw_jump_fdt_addr = XIANGSHAN_KMH_FW_JUMP_FDT_ADDR;
    s->autotest_image_addr = XIANGSHAN_KMH_AUTOTEST_IMAGE_ADDR;
    s->autotest_rootfs_addr = XIANGSHAN_KMH_AUTOTEST_ROOTFS_ADDR;
    s->autotest_workload_addr = XIANGSHAN_KMH_AUTOTEST_WORKLOAD_ADDR;
    s->autotest_trigger_addr = XIANGSHAN_KMH_AUTOTEST_TRIGGER_ADDR;
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

    object_class_property_add_bool(klass, "autotest-dtb",
                                   xiangshan_kmh_get_autotest_dtb,
                                   xiangshan_kmh_set_autotest_dtb);
    object_class_property_set_description(klass, "autotest-dtb",
                                          "Add autotest nvdimm and reserved-memory nodes");

    object_class_property_add_bool(klass, "pcie-dtb",
                                   xiangshan_kmh_get_pcie_dtb,
                                   xiangshan_kmh_set_pcie_dtb);
    object_class_property_set_description(klass, "pcie-dtb",
                                          "Add DWC PCIe host node to the generated device tree");

    XIANGSHAN_KMH_UINT64_PROP("fw-jump-fdt-addr", fw_jump_fdt_addr,
                              "Generated DTB load address for fw_jump boot");
    XIANGSHAN_KMH_UINT64_PROP("autotest-image-addr", autotest_image_addr,
                              "Autotest Image loader address");
    XIANGSHAN_KMH_UINT64_PROP("autotest-rootfs-addr", autotest_rootfs_addr,
                              "Autotest rootfs pmem loader address");
    XIANGSHAN_KMH_UINT64_PROP("autotest-workload-addr", autotest_workload_addr,
                              "Autotest workload pmem loader address");
    XIANGSHAN_KMH_UINT64_PROP("autotest-trigger-addr", autotest_trigger_addr,
                              "Autotest trigger reserved-memory loader address");
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
