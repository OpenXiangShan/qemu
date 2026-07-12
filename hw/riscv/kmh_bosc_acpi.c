/*
 * Support for generating ACPI tables for the BOSC Kunminghu multi-die SoC.
 *
 * Copyright (c) 2026 Beijing Institute of Open Source Chip (BOSC)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/acpi-defs.h"
#include "hw/acpi/aml-build.h"
#include "hw/intc/riscv_imsic.h"
#include "hw/loader.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie_host.h"
#include "hw/riscv/kmh_bosc_acpi.h"
#include "system/address-spaces.h"
#include "target/riscv/cpu.h"

#define KMH_BOSC_ACPI_TABLE_SIZE          0x20000
#define KMH_BOSC_ACPI_RSDP_SIZE           36
#define KMH_BOSC_ACPI_TABLE_ALIGN         64
#define KMH_BOSC_ACPI_TABLE_HEADER_SIZE   36
#define KMH_BOSC_ACPI_OEM_ID              "BOSC  "
#define KMH_BOSC_ACPI_OEM_TABLE_ID        "KMHBOSC "

#define KMH_BOSC_DIES                     4
#define KMH_BOSC_PCIE_PER_DIE             3
#define KMH_BOSC_APP_HARTS_PER_DIE        16
#define KMH_BOSC_APP_HART_MASK            ((1U << KMH_BOSC_APP_HARTS_PER_DIE) - 1U)
#define KMH_BOSC_DIE_SHIFT                44
#define KMH_BOSC_DDR_NODE_BASE            0x0000000080000000ULL
#define KMH_BOSC_DDR_NODE_STRIDE          0x0000000800000000ULL
#define KMH_BOSC_NUMA_LOCAL_DISTANCE      10
#define KMH_BOSC_NUMA_REMOTE_DISTANCE     20

#define KMH_BOSC_UART0_BASE               0x0000000004000000ULL
#define KMH_BOSC_UART0_SIZE               0x10000
#define KMH_BOSC_UART0_IRQ                10
#define KMH_BOSC_UART_CLK_FREQ            50000000
#define KMH_BOSC_UART_REG_SHIFT           2
#define KMH_BOSC_UART_REG_IO_WIDTH        4
#define KMH_BOSC_UART_CURRENT_SPEED       115200

#define KMH_BOSC_APLIC_S_BASE             0x000000001e024000ULL
#define KMH_BOSC_APLIC_S_SIZE             0x4000
#define KMH_BOSC_APLIC_NUM_SOURCES        96
#define KMH_BOSC_IMSIC_S_BASE             0x000000001d000000ULL
#define KMH_BOSC_IMSIC_NUM_IDS            255
#define KMH_BOSC_IMSIC_GUEST_INDEX_BITS   3
#define KMH_BOSC_IMSIC_HART_INDEX_BITS    4
#define KMH_BOSC_IMSIC_GROUP_INDEX_BITS   2
#define KMH_BOSC_CLINT_TIMEBASE_FREQ      1000000

#define KMH_BOSC_PCIE_ECAM_SIZE           PCIE_MMCFG_SIZE_MAX
#define KMH_BOSC_PCIE_MEM_BUS_BASE        0x0000000100000000ULL
#define KMH_BOSC_PCIE_IRQ_STRIDE          6
#define KMH_BOSC_PCIE_INTA_IRQ            14
#define KMH_BOSC_PCIE_MEM32_BUS_BASE      0x0000000050000000ULL
#define KMH_BOSC_PCIE_ROOT_BUS_STRIDE     0x0000000004000000ULL
#define KMH_BOSC_PCIE_WINDOW_SIZE         0x0000000002000000ULL

typedef struct KmhBoscAcpiState {
    MachineState *ms;
    RISCVHartArrayState *cpus;
    uint32_t die_mask;
    uint32_t core_mask[KMH_BOSC_DIES];
    hwaddr handoff_addr;
    uint64_t handoff_size;
    bool dw_pcie;
} KmhBoscAcpiState;

static const uint64_t kmh_bosc_acpi_pcie_ecam_base[KMH_BOSC_PCIE_PER_DIE] = {
    0x0000004800000000ULL,
    0x0000004900000000ULL,
    0x0000004a00000000ULL,
};

static const uint64_t kmh_bosc_acpi_pcie_mem_base[KMH_BOSC_PCIE_PER_DIE] = {
    0x0000048000000000ULL,
    0x000004e000000000ULL,
    0x000004f000000000ULL,
};

static inline hwaddr kmh_bosc_acpi_die_addr(int die, hwaddr offset)
{
    return (((hwaddr)die) << KMH_BOSC_DIE_SHIFT) | offset;
}

static bool kmh_bosc_acpi_die_selected(const KmhBoscAcpiState *s, int die)
{
    return s->die_mask & (1U << die);
}

static bool kmh_bosc_acpi_hart_selected(const KmhBoscAcpiState *s, int die,
                                        int hart)
{
    return kmh_bosc_acpi_die_selected(s, die) &&
           (s->core_mask[die] & (1U << hart));
}

static int kmh_bosc_acpi_hartid(int die, int hart)
{
    return die * KMH_BOSC_APP_HARTS_PER_DIE + hart;
}

static int kmh_bosc_acpi_numa_node_id(const KmhBoscAcpiState *s, int die)
{
    return ctpop32(s->die_mask & ((1U << die) - 1));
}

static int kmh_bosc_acpi_selected_die_count(const KmhBoscAcpiState *s)
{
    return ctpop32(s->die_mask & ((1U << KMH_BOSC_DIES) - 1));
}

static uint64_t kmh_bosc_acpi_ddr_base(int die)
{
    return KMH_BOSC_DDR_NODE_BASE +
           ((uint64_t)die * KMH_BOSC_DDR_NODE_STRIDE);
}

static uint64_t kmh_bosc_acpi_die_ram_size(const KmhBoscAcpiState *s)
{
    return s->ms->ram_size / KMH_BOSC_DIES;
}

static int kmh_bosc_acpi_selected_hart_count(const KmhBoscAcpiState *s)
{
    int count = 0;

    for (int die = 0; die < KMH_BOSC_DIES; die++) {
        if (kmh_bosc_acpi_die_selected(s, die)) {
            count += ctpop32(s->core_mask[die] & KMH_BOSC_APP_HART_MASK);
        }
    }

    return count;
}

static int kmh_bosc_acpi_first_selected_hartid(const KmhBoscAcpiState *s)
{
    for (int die = 0; die < KMH_BOSC_DIES; die++) {
        for (int hart = 0; hart < KMH_BOSC_APP_HARTS_PER_DIE; hart++) {
            if (kmh_bosc_acpi_hart_selected(s, die, hart)) {
                return kmh_bosc_acpi_hartid(die, hart);
            }
        }
    }

    return -1;
}

static uint64_t kmh_bosc_acpi_imsic_s_addr(int die, int hart)
{
    return kmh_bosc_acpi_die_addr(die, KMH_BOSC_IMSIC_S_BASE) +
           hart * IMSIC_HART_SIZE(KMH_BOSC_IMSIC_GUEST_INDEX_BITS);
}

static uint32_t kmh_bosc_acpi_pcie_segment(int die, int port)
{
    return die * KMH_BOSC_PCIE_PER_DIE + port;
}

static uint64_t kmh_bosc_acpi_pcie_ecam_addr(int die, int port)
{
    return kmh_bosc_acpi_die_addr(die,
                                  kmh_bosc_acpi_pcie_ecam_base[port]);
}

static uint64_t kmh_bosc_acpi_pcie_mem32_bus_base(uint32_t segment)
{
    return KMH_BOSC_PCIE_MEM32_BUS_BASE +
           segment * KMH_BOSC_PCIE_ROOT_BUS_STRIDE;
}

static uint64_t kmh_bosc_acpi_pcie_mem_cpu_base(int die, int port,
                                                bool above_4g)
{
    return kmh_bosc_acpi_die_addr(die,
                                  kmh_bosc_acpi_pcie_mem_base[port]) +
           (above_4g ? KMH_BOSC_PCIE_WINDOW_SIZE : 0);
}

static uint32_t kmh_bosc_acpi_pcie_gsi(int die, int port, int pin)
{
    return die * KMH_BOSC_APLIC_NUM_SOURCES +
           KMH_BOSC_PCIE_INTA_IRQ +
           port * KMH_BOSC_PCIE_IRQ_STRIDE +
           pin;
}

static void kmh_bosc_acpi_madt_add_rintc(uint32_t uid, int die, int hart,
                                         GArray *entry)
{
    build_append_int_noprefix(entry, 0x18, 1);       /* Type */
    build_append_int_noprefix(entry, 36, 1);         /* Length */
    build_append_int_noprefix(entry, 1, 1);          /* Version */
    build_append_int_noprefix(entry, 0, 1);          /* Reserved */
    build_append_int_noprefix(entry, 0x1, 4);        /* Flags: enabled */
    build_append_int_noprefix(entry, uid, 8);        /* Hart ID */
    build_append_int_noprefix(entry, uid, 4);        /* ACPI Processor UID */
    build_append_int_noprefix(entry, 0, 4);          /* External INTC ID */
    build_append_int_noprefix(entry,
                              kmh_bosc_acpi_imsic_s_addr(die, hart), 8);
    build_append_int_noprefix(entry,
                              IMSIC_HART_SIZE(KMH_BOSC_IMSIC_GUEST_INDEX_BITS),
                              4);
}

static void kmh_bosc_acpi_dsdt_add_cpus(Aml *scope,
                                        const KmhBoscAcpiState *s)
{
    for (int die = 0; die < KMH_BOSC_DIES; die++) {
        for (int hart = 0; hart < KMH_BOSC_APP_HARTS_PER_DIE; hart++) {
            g_autoptr(GArray) madt_buf = NULL;
            Aml *dev;
            int hartid;

            if (!kmh_bosc_acpi_hart_selected(s, die, hart)) {
                continue;
            }

            hartid = kmh_bosc_acpi_hartid(die, hart);
            madt_buf = g_array_new(false, true, 1);
            dev = aml_device("C%.03X", hartid);

            aml_append(dev, aml_name_decl("_HID", aml_string("ACPI0007")));
            aml_append(dev, aml_name_decl("_UID", aml_int(hartid)));

            kmh_bosc_acpi_madt_add_rintc(hartid, die, hart, madt_buf);
            aml_append(dev, aml_name_decl("_MAT",
                                          aml_buffer(madt_buf->len,
                                          (uint8_t *)madt_buf->data)));
            aml_append(scope, dev);
        }
    }
}

static void kmh_bosc_acpi_dsdt_add_aplics(Aml *scope,
                                          const KmhBoscAcpiState *s)
{
    for (int die = 0; die < KMH_BOSC_DIES; die++) {
        uint64_t aplic_base;
        uint32_t gsi_base;
        Aml *dev;
        Aml *crs;

        if (!kmh_bosc_acpi_die_selected(s, die)) {
            continue;
        }

        aplic_base = kmh_bosc_acpi_die_addr(die, KMH_BOSC_APLIC_S_BASE);
        gsi_base = die * KMH_BOSC_APLIC_NUM_SOURCES;
        dev = aml_device("IC%.02X", die);

        aml_append(dev, aml_name_decl("_HID", aml_string("RSCV0002")));
        aml_append(dev, aml_name_decl("_UID", aml_int(die)));
        aml_append(dev, aml_name_decl("_GSB", aml_int(gsi_base)));

        crs = aml_resource_template();
        aml_append(crs, aml_qword_memory(AML_POS_DECODE, AML_MIN_FIXED,
                                         AML_MAX_FIXED, AML_NON_CACHEABLE,
                                         AML_READ_WRITE, 0, aplic_base,
                                         aplic_base + KMH_BOSC_APLIC_S_SIZE - 1,
                                         0, KMH_BOSC_APLIC_S_SIZE));
        aml_append(dev, aml_name_decl("_CRS", crs));
        aml_append(scope, dev);
    }
}

static void kmh_bosc_acpi_dsdt_add_uart(Aml *scope)
{
    Aml *dev = aml_device("COM0");
    Aml *crs;
    Aml *props;
    Aml *prop;
    Aml *dsd;
    Aml *uuid;
    uint32_t uart_irq = KMH_BOSC_UART0_IRQ;

    aml_append(dev, aml_name_decl("_HID", aml_string("RSCV0003")));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));

    crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(KMH_BOSC_UART0_BASE, KMH_BOSC_UART0_SIZE,
                                       AML_READ_WRITE));
    aml_append(crs,
               aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                             AML_EXCLUSIVE, &uart_irq, 1));
    aml_append(dev, aml_name_decl("_CRS", crs));

    props = aml_package(4);

    prop = aml_package(2);
    aml_append(prop, aml_string("clock-frequency"));
    aml_append(prop, aml_int(KMH_BOSC_UART_CLK_FREQ));
    aml_append(props, prop);

    prop = aml_package(2);
    aml_append(prop, aml_string("reg-shift"));
    aml_append(prop, aml_int(KMH_BOSC_UART_REG_SHIFT));
    aml_append(props, prop);

    prop = aml_package(2);
    aml_append(prop, aml_string("reg-io-width"));
    aml_append(prop, aml_int(KMH_BOSC_UART_REG_IO_WIDTH));
    aml_append(props, prop);

    prop = aml_package(2);
    aml_append(prop, aml_string("current-speed"));
    aml_append(prop, aml_int(KMH_BOSC_UART_CURRENT_SPEED));
    aml_append(props, prop);

    uuid = aml_touuid("DAFFD814-6EBA-4D8C-8A91-BC9BBF4AA301");
    dsd = aml_package(2);
    aml_append(dsd, uuid);
    aml_append(dsd, props);
    aml_append(dev, aml_name_decl("_DSD", dsd));

    aml_append(scope, dev);
}

static Aml *kmh_bosc_acpi_pcie_prt(int die, int port)
{
    Aml *prt = aml_package(PCI_NUM_PINS);

    for (int pin = 0; pin < PCI_NUM_PINS; pin++) {
        Aml *entry = aml_package(4);

        aml_append(entry, aml_int(0x0000ffff));
        aml_append(entry, aml_int(pin));
        aml_append(entry, aml_int(0));
        aml_append(entry, aml_int(kmh_bosc_acpi_pcie_gsi(die, port, pin)));
        aml_append(prt, entry);
    }

    return prt;
}

static void kmh_bosc_acpi_dsdt_add_pcie_ecam_reservation(Aml *parent,
                                                         uint32_t segment,
                                                         uint64_t ecam_base)
{
    Aml *dev = aml_device("E%.03X", segment);
    Aml *crs = aml_resource_template();

    aml_append(dev, aml_name_decl("_HID", aml_string("PNP0C02")));
    aml_append(dev, aml_name_decl("_UID", aml_int(segment)));
    aml_append(crs, aml_qword_memory(AML_POS_DECODE, AML_MIN_FIXED,
                                     AML_MAX_FIXED, AML_NON_CACHEABLE,
                                     AML_READ_WRITE, 0, ecam_base,
                                     ecam_base + KMH_BOSC_PCIE_ECAM_SIZE - 1,
                                     0, KMH_BOSC_PCIE_ECAM_SIZE));
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(parent, dev);
}

static void kmh_bosc_acpi_dsdt_add_pcie(Aml *scope,
                                        const KmhBoscAcpiState *s)
{
    for (int die = 0; die < KMH_BOSC_DIES; die++) {
        uint32_t pxm;

        if (!kmh_bosc_acpi_die_selected(s, die)) {
            continue;
        }

        pxm = kmh_bosc_acpi_numa_node_id(s, die);
        for (int port = 0; port < KMH_BOSC_PCIE_PER_DIE; port++) {
            uint32_t segment = kmh_bosc_acpi_pcie_segment(die, port);
            uint64_t ecam_base = kmh_bosc_acpi_pcie_ecam_addr(die, port);
            uint64_t mem32_bus_base =
                kmh_bosc_acpi_pcie_mem32_bus_base(segment);
            uint64_t mem32_bus_limit =
                mem32_bus_base + KMH_BOSC_PCIE_WINDOW_SIZE - 1;
            uint64_t mem32_cpu_base =
                kmh_bosc_acpi_pcie_mem_cpu_base(die, port, false);
            uint64_t mem64_bus_limit = KMH_BOSC_PCIE_MEM_BUS_BASE +
                                       KMH_BOSC_PCIE_WINDOW_SIZE - 1;
            uint64_t mem64_cpu_base =
                kmh_bosc_acpi_pcie_mem_cpu_base(die, port, true);
            Aml *dev = aml_device("P%.03X", segment);
            Aml *cba;
            Aml *crs;

            aml_append(dev, aml_name_decl("_HID", aml_string("PNP0A08")));
            aml_append(dev, aml_name_decl("_CID", aml_string("PNP0A03")));
            aml_append(dev, aml_name_decl("_SEG", aml_int(segment)));
            aml_append(dev, aml_name_decl("_BBN", aml_int(0)));
            aml_append(dev, aml_name_decl("_UID", aml_int(segment)));
            aml_append(dev, aml_name_decl("_CCA", aml_int(1)));
            aml_append(dev, aml_name_decl("_PXM", aml_int(pxm)));

            cba = aml_method("_CBA", 0, AML_SERIALIZED);
            aml_append(cba, aml_return(aml_int(ecam_base)));
            aml_append(dev, cba);

            aml_append(dev, aml_name_decl("_PRT",
                                          kmh_bosc_acpi_pcie_prt(die, port)));

            crs = aml_resource_template();
            aml_append(crs, aml_word_bus_number(AML_MIN_FIXED, AML_MAX_FIXED,
                                                AML_POS_DECODE, 0, 0, 0xff,
                                                0, 0x100));
            aml_append(crs, aml_qword_memory(AML_POS_DECODE, AML_MIN_FIXED,
                                             AML_MAX_FIXED, AML_NON_CACHEABLE,
                                             AML_READ_WRITE, 0,
                                             mem32_bus_base, mem32_bus_limit,
                                             mem32_cpu_base - mem32_bus_base,
                                             KMH_BOSC_PCIE_WINDOW_SIZE));
            aml_append(crs, aml_qword_memory(AML_POS_DECODE, AML_MIN_FIXED,
                                             AML_MAX_FIXED, AML_NON_CACHEABLE,
                                             AML_READ_WRITE, 0,
                                             KMH_BOSC_PCIE_MEM_BUS_BASE,
                                             mem64_bus_limit,
                                             mem64_cpu_base -
                                             KMH_BOSC_PCIE_MEM_BUS_BASE,
                                             KMH_BOSC_PCIE_WINDOW_SIZE));
            aml_append(dev, aml_name_decl("_CRS", crs));

            kmh_bosc_acpi_dsdt_add_pcie_ecam_reservation(dev, segment,
                                                         ecam_base);
            aml_append(scope, dev);
        }
    }
}

static void kmh_bosc_acpi_build_dsdt(GArray *table_data, BIOSLinker *linker,
                                     const KmhBoscAcpiState *s)
{
    AcpiTable table = {
        .sig = "DSDT",
        .rev = 2,
        .oem_id = KMH_BOSC_ACPI_OEM_ID,
        .oem_table_id = KMH_BOSC_ACPI_OEM_TABLE_ID,
    };
    Aml *dsdt;
    Aml *scope;

    acpi_table_begin(&table, table_data);
    dsdt = init_aml_allocator();

    scope = aml_scope("\\_SB");
    kmh_bosc_acpi_dsdt_add_cpus(scope, s);
    kmh_bosc_acpi_dsdt_add_aplics(scope, s);
    kmh_bosc_acpi_dsdt_add_uart(scope);
    if (s->dw_pcie) {
        kmh_bosc_acpi_dsdt_add_pcie(scope, s);
    }
    aml_append(dsdt, scope);

    g_array_append_vals(table_data, dsdt->buf->data, dsdt->buf->len);

    acpi_table_end(linker, &table);
    free_aml_allocator();
}

static void kmh_bosc_acpi_build_fadt(GArray *table_data, BIOSLinker *linker,
                                     unsigned dsdt_tbl_offset)
{
    AcpiFadtData fadt = {
        .rev = 6,
        .minor_ver = 6,
        .flags = 1 << ACPI_FADT_F_HW_REDUCED_ACPI,
        .dsdt_tbl_offset = &dsdt_tbl_offset,
        .xdsdt_tbl_offset = &dsdt_tbl_offset,
    };

    build_fadt(table_data, linker, &fadt, KMH_BOSC_ACPI_OEM_ID,
               KMH_BOSC_ACPI_OEM_TABLE_ID);
}

static void kmh_bosc_acpi_build_madt(GArray *table_data, BIOSLinker *linker,
                                     const KmhBoscAcpiState *s)
{
    AcpiTable table = {
        .sig = "APIC",
        .rev = 7,
        .oem_id = KMH_BOSC_ACPI_OEM_ID,
        .oem_table_id = KMH_BOSC_ACPI_OEM_TABLE_ID,
    };

    acpi_table_begin(&table, table_data);
    build_append_int_noprefix(table_data, 0, 4);
    build_append_int_noprefix(table_data, 0, 4);

    for (int die = 0; die < KMH_BOSC_DIES; die++) {
        for (int hart = 0; hart < KMH_BOSC_APP_HARTS_PER_DIE; hart++) {
            int hartid;

            if (!kmh_bosc_acpi_hart_selected(s, die, hart)) {
                continue;
            }

            hartid = kmh_bosc_acpi_hartid(die, hart);
            kmh_bosc_acpi_madt_add_rintc(hartid, die, hart, table_data);
        }
    }

    build_append_int_noprefix(table_data, 0x19, 1); /* IMSIC */
    build_append_int_noprefix(table_data, 16, 1);
    build_append_int_noprefix(table_data, 1, 1);
    build_append_int_noprefix(table_data, 0, 1);
    build_append_int_noprefix(table_data, 0, 4);
    build_append_int_noprefix(table_data, KMH_BOSC_IMSIC_NUM_IDS, 2);
    build_append_int_noprefix(table_data, KMH_BOSC_IMSIC_NUM_IDS, 2);
    build_append_int_noprefix(table_data, KMH_BOSC_IMSIC_GUEST_INDEX_BITS, 1);
    build_append_int_noprefix(table_data, KMH_BOSC_IMSIC_HART_INDEX_BITS, 1);
    build_append_int_noprefix(table_data, KMH_BOSC_IMSIC_GROUP_INDEX_BITS, 1);
    build_append_int_noprefix(table_data, KMH_BOSC_DIE_SHIFT, 1);

    for (int die = 0; die < KMH_BOSC_DIES; die++) {
        uint64_t aplic_base;
        uint32_t gsi_base;

        if (!kmh_bosc_acpi_die_selected(s, die)) {
            continue;
        }

        aplic_base = kmh_bosc_acpi_die_addr(die, KMH_BOSC_APLIC_S_BASE);
        gsi_base = die * KMH_BOSC_APLIC_NUM_SOURCES;

        build_append_int_noprefix(table_data, 0x1A, 1); /* APLIC */
        build_append_int_noprefix(table_data, 36, 1);
        build_append_int_noprefix(table_data, 1, 1);
        build_append_int_noprefix(table_data, die, 1);
        build_append_int_noprefix(table_data, 0, 4);
        build_append_int_noprefix(table_data, 0, 8);
        build_append_int_noprefix(table_data, 0, 2);
        build_append_int_noprefix(table_data, KMH_BOSC_APLIC_NUM_SOURCES, 2);
        build_append_int_noprefix(table_data, gsi_base, 4);
        build_append_int_noprefix(table_data, aplic_base, 8);
        build_append_int_noprefix(table_data, KMH_BOSC_APLIC_S_SIZE, 4);
    }

    acpi_table_end(linker, &table);
}

#define RHCT_NODE_ARRAY_OFFSET 56

static void kmh_bosc_acpi_build_rhct(GArray *table_data, BIOSLinker *linker,
                                     const KmhBoscAcpiState *s)
{
    int first_hartid = kmh_bosc_acpi_first_selected_hartid(s);
    RISCVCPU *cpu = &s->cpus->harts[first_hartid];
    bool rv32 = riscv_cpu_is_32bit(cpu);
    g_autofree char *isa = riscv_isa_string(cpu);
    int selected_harts = kmh_bosc_acpi_selected_hart_count(s);
    size_t len, aligned_len;
    uint32_t isa_offset;
    uint32_t cmo_offset = 0;
    uint32_t mmu_offset = 0;
    uint32_t num_rhct_nodes = 1 + selected_harts;
    AcpiTable table = {
        .sig = "RHCT",
        .rev = 1,
        .oem_id = KMH_BOSC_ACPI_OEM_ID,
        .oem_table_id = KMH_BOSC_ACPI_OEM_TABLE_ID,
    };

    if (cpu->cfg.ext_zicbom || cpu->cfg.ext_zicboz) {
        num_rhct_nodes++;
    }
    if (!rv32 && cpu->cfg.max_satp_mode >= VM_1_10_SV39) {
        num_rhct_nodes++;
    }

    acpi_table_begin(&table, table_data);
    build_append_int_noprefix(table_data, 0, 4);
    build_append_int_noprefix(table_data, KMH_BOSC_CLINT_TIMEBASE_FREQ, 8);
    build_append_int_noprefix(table_data, num_rhct_nodes, 4);
    build_append_int_noprefix(table_data, RHCT_NODE_ARRAY_OFFSET, 4);

    isa_offset = table_data->len - table.table_offset;
    build_append_int_noprefix(table_data, 0, 2);
    len = 8 + strlen(isa) + 1;
    aligned_len = ROUND_UP(len, 2);
    build_append_int_noprefix(table_data, aligned_len, 2);
    build_append_int_noprefix(table_data, 1, 2);
    build_append_int_noprefix(table_data, strlen(isa) + 1, 2);
    g_array_append_vals(table_data, isa, strlen(isa) + 1);
    if (aligned_len != len) {
        build_append_int_noprefix(table_data, 0, 1);
    }

    if (cpu->cfg.ext_zicbom || cpu->cfg.ext_zicboz) {
        cmo_offset = table_data->len - table.table_offset;
        build_append_int_noprefix(table_data, 1, 2);
        build_append_int_noprefix(table_data, 10, 2);
        build_append_int_noprefix(table_data, 1, 2);
        build_append_int_noprefix(table_data, 0, 1);
        build_append_int_noprefix(table_data,
                                  cpu->cfg.cbom_blocksize ?
                                  __builtin_ctz(cpu->cfg.cbom_blocksize) : 0,
                                  1);
        build_append_int_noprefix(table_data, 0, 1);
        build_append_int_noprefix(table_data,
                                  cpu->cfg.cboz_blocksize ?
                                  __builtin_ctz(cpu->cfg.cboz_blocksize) : 0,
                                  1);
    }

    if (!rv32 && cpu->cfg.max_satp_mode >= VM_1_10_SV39) {
        mmu_offset = table_data->len - table.table_offset;
        build_append_int_noprefix(table_data, 2, 2);
        build_append_int_noprefix(table_data, 8, 2);
        build_append_int_noprefix(table_data, 1, 2);
        build_append_int_noprefix(table_data, 0, 1);
        if (cpu->cfg.max_satp_mode == VM_1_10_SV57) {
            build_append_int_noprefix(table_data, 2, 1);
        } else if (cpu->cfg.max_satp_mode == VM_1_10_SV48) {
            build_append_int_noprefix(table_data, 1, 1);
        } else {
            build_append_int_noprefix(table_data, 0, 1);
        }
    }

    for (int die = 0; die < KMH_BOSC_DIES; die++) {
        for (int hart = 0; hart < KMH_BOSC_APP_HARTS_PER_DIE; hart++) {
            int hartid;
            int node_len = 16;
            int num_offsets = 1;

            if (!kmh_bosc_acpi_hart_selected(s, die, hart)) {
                continue;
            }

            if (cmo_offset) {
                node_len += 4;
                num_offsets++;
            }
            if (mmu_offset) {
                node_len += 4;
                num_offsets++;
            }

            hartid = kmh_bosc_acpi_hartid(die, hart);
            build_append_int_noprefix(table_data, 0xFFFF, 2);
            build_append_int_noprefix(table_data, node_len, 2);
            build_append_int_noprefix(table_data, 1, 2);
            build_append_int_noprefix(table_data, num_offsets, 2);
            build_append_int_noprefix(table_data, hartid, 4);
            build_append_int_noprefix(table_data, isa_offset, 4);
            if (cmo_offset) {
                build_append_int_noprefix(table_data, cmo_offset, 4);
            }
            if (mmu_offset) {
                build_append_int_noprefix(table_data, mmu_offset, 4);
            }
        }
    }

    acpi_table_end(linker, &table);
}

static void kmh_bosc_acpi_build_srat(GArray *table_data, BIOSLinker *linker,
                                     const KmhBoscAcpiState *s)
{
    AcpiTable table = {
        .sig = "SRAT",
        .rev = 3,
        .oem_id = KMH_BOSC_ACPI_OEM_ID,
        .oem_table_id = KMH_BOSC_ACPI_OEM_TABLE_ID,
    };
    uint64_t die_ram_size = kmh_bosc_acpi_die_ram_size(s);

    acpi_table_begin(&table, table_data);
    build_append_int_noprefix(table_data, 1, 4); /* Reserved */
    build_append_int_noprefix(table_data, 0, 8); /* Reserved */

    for (int die = 0; die < KMH_BOSC_DIES; die++) {
        uint32_t nodeid;

        if (!kmh_bosc_acpi_die_selected(s, die)) {
            continue;
        }

        nodeid = kmh_bosc_acpi_numa_node_id(s, die);
        for (int hart = 0; hart < KMH_BOSC_APP_HARTS_PER_DIE; hart++) {
            uint32_t uid;

            if (!kmh_bosc_acpi_hart_selected(s, die, hart)) {
                continue;
            }

            uid = kmh_bosc_acpi_hartid(die, hart);

            build_append_int_noprefix(table_data, 7, 1);  /* RINTC */
            build_append_int_noprefix(table_data, 20, 1);
            build_append_int_noprefix(table_data, 0, 2);  /* Reserved */
            build_append_int_noprefix(table_data, nodeid, 4);
            build_append_int_noprefix(table_data, uid, 4);
            build_append_int_noprefix(table_data, 1, 4);  /* Enabled */
            build_append_int_noprefix(table_data, 0, 4);  /* Clock domain */
        }

        build_srat_memory(table_data, kmh_bosc_acpi_ddr_base(die),
                          die_ram_size, nodeid, MEM_AFFINITY_ENABLED);
    }

    acpi_table_end(linker, &table);
}

static void kmh_bosc_acpi_build_slit(GArray *table_data, BIOSLinker *linker,
                                     const KmhBoscAcpiState *s)
{
    AcpiTable table = {
        .sig = "SLIT",
        .rev = 1,
        .oem_id = KMH_BOSC_ACPI_OEM_ID,
        .oem_table_id = KMH_BOSC_ACPI_OEM_TABLE_ID,
    };
    uint64_t selected_dies = kmh_bosc_acpi_selected_die_count(s);

    acpi_table_begin(&table, table_data);
    build_append_int_noprefix(table_data, selected_dies, 8);

    for (int die = 0; die < KMH_BOSC_DIES; die++) {
        if (!kmh_bosc_acpi_die_selected(s, die)) {
            continue;
        }

        for (int other = 0; other < KMH_BOSC_DIES; other++) {
            if (!kmh_bosc_acpi_die_selected(s, other)) {
                continue;
            }

            build_append_int_noprefix(table_data,
                                      die == other ?
                                      KMH_BOSC_NUMA_LOCAL_DISTANCE :
                                      KMH_BOSC_NUMA_REMOTE_DISTANCE,
                                      1);
        }
    }

    acpi_table_end(linker, &table);
}

static void kmh_bosc_acpi_build_spcr(GArray *table_data, BIOSLinker *linker)
{
    const char name[] = ".";
    AcpiSpcrData serial = {
        .interface_type = 0x12,
        .base_addr.id = AML_AS_SYSTEM_MEMORY,
        .base_addr.width = 32,
        .base_addr.offset = 0,
        .base_addr.size = 1,
        .base_addr.addr = KMH_BOSC_UART0_BASE,
        .interrupt_type = 1 << 4,
        .pc_interrupt = 0,
        .interrupt = KMH_BOSC_UART0_IRQ,
        .baud_rate = 7,
        .parity = 0,
        .stop_bits = 1,
        .flow_control = 0,
        .terminal_type = 3,
        .language = 0,
        .pci_device_id = 0xffff,
        .pci_vendor_id = 0xffff,
        .pci_bus = 0,
        .pci_device = 0,
        .pci_function = 0,
        .pci_flags = 0,
        .pci_segment = 0,
        .uart_clk_freq = 0,
        .precise_baudrate = 0,
        .namespace_string_length = sizeof(name),
        .namespace_string_offset = 88,
    };

    build_spcr(table_data, linker, &serial, 4, KMH_BOSC_ACPI_OEM_ID,
               KMH_BOSC_ACPI_OEM_TABLE_ID, name);
}

static void kmh_bosc_acpi_build_mcfg(GArray *table_data, BIOSLinker *linker,
                                     const KmhBoscAcpiState *s)
{
    AcpiTable table = {
        .sig = "MCFG",
        .rev = 1,
        .oem_id = KMH_BOSC_ACPI_OEM_ID,
        .oem_table_id = KMH_BOSC_ACPI_OEM_TABLE_ID,
    };

    acpi_table_begin(&table, table_data);
    build_append_int_noprefix(table_data, 0, 8);

    for (int die = 0; die < KMH_BOSC_DIES; die++) {
        if (!kmh_bosc_acpi_die_selected(s, die)) {
            continue;
        }

        for (int port = 0; port < KMH_BOSC_PCIE_PER_DIE; port++) {
            build_append_int_noprefix(
                table_data,
                kmh_bosc_acpi_pcie_ecam_addr(die, port), 8);
            build_append_int_noprefix(
                table_data,
                kmh_bosc_acpi_pcie_segment(die, port), 2);
            build_append_int_noprefix(table_data, 0, 1);
            build_append_int_noprefix(
                table_data,
                PCIE_MMCFG_BUS(KMH_BOSC_PCIE_ECAM_SIZE - 1), 1);
            build_append_int_noprefix(table_data, 0, 4);
        }
    }

    acpi_table_end(linker, &table);
}

static uint8_t kmh_bosc_acpi_checksum(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;

    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }

    return 0 - sum;
}

static void kmh_bosc_acpi_update_table_checksum(GArray *blob,
                                                unsigned table_offset)
{
    uint8_t *table = (uint8_t *)blob->data + table_offset;
    uint32_t len = ldl_le_p(table + 4);

    assert(table_offset + len <= blob->len);
    table[9] = 0;
    table[9] = kmh_bosc_acpi_checksum(table, len);
}

static void kmh_bosc_acpi_build_xsdt(GArray *blob, BIOSLinker *linker,
                                     GArray *table_offsets)
{
    AcpiTable table = {
        .sig = "XSDT",
        .rev = 1,
        .oem_id = KMH_BOSC_ACPI_OEM_ID,
        .oem_table_id = KMH_BOSC_ACPI_OEM_TABLE_ID,
    };

    acpi_table_begin(&table, blob);
    for (int i = 0; i < table_offsets->len; i++) {
        build_append_int_noprefix(blob, 0, 8);
    }
    acpi_table_end(linker, &table);
}

static void kmh_bosc_acpi_patch_table_pointers(GArray *blob,
                                               GArray *table_offsets,
                                               hwaddr blob_base,
                                               unsigned dsdt_offset,
                                               unsigned xsdt_offset)
{
    unsigned xsdt_entry_offset = xsdt_offset +
                                 KMH_BOSC_ACPI_TABLE_HEADER_SIZE;
    hwaddr dsdt_addr = blob_base + dsdt_offset;

    for (int i = 0; i < table_offsets->len; i++) {
        uint32_t table_offset = g_array_index(table_offsets, uint32_t, i);
        uint8_t *table = (uint8_t *)blob->data + table_offset;

        stq_le_p(blob->data + xsdt_entry_offset + i * sizeof(uint64_t),
                 blob_base + table_offset);

        if (!memcmp(table, "FACP", 4)) {
            if (dsdt_addr > UINT32_MAX) {
                error_report("ACPI DSDT address 0x%"HWADDR_PRIx" exceeds "
                             "32-bit FADT DSDT field", dsdt_addr);
                exit(1);
            }
            stl_le_p(table + 40, dsdt_addr);
            stq_le_p(table + 140, dsdt_addr);
        }
    }

    kmh_bosc_acpi_update_table_checksum(blob, xsdt_offset);
    kmh_bosc_acpi_update_table_checksum(blob, dsdt_offset);
    for (int i = 0; i < table_offsets->len; i++) {
        uint32_t table_offset = g_array_index(table_offsets, uint32_t, i);

        kmh_bosc_acpi_update_table_checksum(blob, table_offset);
    }
}

static void kmh_bosc_acpi_build_tables(const KmhBoscAcpiState *s,
                                       GArray *blob, BIOSLinker *linker,
                                       GArray *table_offsets,
                                       unsigned *xsdt_offset)
{
    unsigned dsdt;

    bios_linker_loader_alloc(linker, ACPI_BUILD_TABLE_FILE, blob,
                             KMH_BOSC_ACPI_TABLE_ALIGN, false);

    dsdt = blob->len;
    kmh_bosc_acpi_build_dsdt(blob, linker, s);

    acpi_add_table(table_offsets, blob);
    kmh_bosc_acpi_build_fadt(blob, linker, dsdt);

    acpi_add_table(table_offsets, blob);
    kmh_bosc_acpi_build_madt(blob, linker, s);

    acpi_add_table(table_offsets, blob);
    kmh_bosc_acpi_build_rhct(blob, linker, s);

    acpi_add_table(table_offsets, blob);
    kmh_bosc_acpi_build_spcr(blob, linker);

    if (s->dw_pcie) {
        acpi_add_table(table_offsets, blob);
        kmh_bosc_acpi_build_mcfg(blob, linker, s);
    }

    acpi_add_table(table_offsets, blob);
    kmh_bosc_acpi_build_srat(blob, linker, s);

    acpi_add_table(table_offsets, blob);
    kmh_bosc_acpi_build_slit(blob, linker, s);

    *xsdt_offset = blob->len;
    kmh_bosc_acpi_build_xsdt(blob, linker, table_offsets);
}

static void kmh_bosc_acpi_build_rsdp(GArray *blob, hwaddr xsdt_addr)
{
    GArray *rsdp = g_array_new(false, true, 1);

    g_array_append_vals(rsdp, "RSD PTR ", 8);
    build_append_int_noprefix(rsdp, 0, 1);
    g_array_append_vals(rsdp, KMH_BOSC_ACPI_OEM_ID, 6);
    build_append_int_noprefix(rsdp, 2, 1);
    build_append_int_noprefix(rsdp, 0, 4);
    build_append_int_noprefix(rsdp, KMH_BOSC_ACPI_RSDP_SIZE, 4);
    build_append_int_noprefix(rsdp, xsdt_addr, 8);
    build_append_int_noprefix(rsdp, 0, 1);
    build_append_int_noprefix(rsdp, 0, 3);

    assert(rsdp->len == KMH_BOSC_ACPI_RSDP_SIZE);
    rsdp->data[8] = kmh_bosc_acpi_checksum((uint8_t *)rsdp->data, 20);
    rsdp->data[32] = kmh_bosc_acpi_checksum((uint8_t *)rsdp->data,
                                            rsdp->len);

    assert(blob->len >= rsdp->len);
    memcpy(blob->data, rsdp->data, rsdp->len);
    g_array_free(rsdp, true);
}

void kmh_bosc_acpi_setup(MachineState *ms, RISCVHartArrayState *cpus,
                         uint32_t die_mask, const uint32_t core_mask[4],
                         hwaddr handoff_addr, uint64_t handoff_size,
                         bool dw_pcie)
{
    KmhBoscAcpiState state = {
        .ms = ms,
        .cpus = cpus,
        .die_mask = die_mask,
        .handoff_addr = handoff_addr,
        .handoff_size = handoff_size,
        .dw_pcie = dw_pcie,
    };
    GArray *handoff = g_array_new(false, true, 1);
    GArray *table_offsets = g_array_new(false, true, sizeof(uint32_t));
    BIOSLinker *linker = bios_linker_loader_init();
    unsigned tables_offset;
    unsigned xsdt_offset;
    uint32_t handoff_len;

    memcpy(state.core_mask, core_mask, sizeof(state.core_mask));

    g_array_set_size(handoff, KMH_BOSC_ACPI_RSDP_SIZE);
    tables_offset = ROUND_UP(handoff->len, KMH_BOSC_ACPI_TABLE_ALIGN);
    g_array_set_size(handoff, tables_offset);

    kmh_bosc_acpi_build_tables(&state, handoff, linker, table_offsets,
                               &xsdt_offset);
    kmh_bosc_acpi_patch_table_pointers(handoff, table_offsets, handoff_addr,
                                       tables_offset, xsdt_offset);
    kmh_bosc_acpi_build_rsdp(handoff, handoff_addr + xsdt_offset);
    g_array_set_size(handoff, ROUND_UP(acpi_data_len(handoff),
                                       KMH_BOSC_ACPI_TABLE_ALIGN));

    handoff_len = acpi_data_len(handoff);
    if (handoff_len > handoff_size) {
        error_report("generated ACPI handoff size 0x%x exceeds configured "
                     "range 0x%"PRIx64, handoff_len, handoff_size);
        exit(1);
    }
    if (handoff_len > KMH_BOSC_ACPI_TABLE_SIZE) {
        warn_report("ACPI handoff size %u exceeds %d bytes",
                    handoff_len, KMH_BOSC_ACPI_TABLE_SIZE);
    }

    rom_add_blob_fixed_as("kmh-bosc-soc.acpi-handoff", handoff->data,
                          handoff_len, handoff_addr, &address_space_memory);

    bios_linker_loader_cleanup(linker);
    g_array_free(table_offsets, true);
    g_array_free(handoff, true);
}
