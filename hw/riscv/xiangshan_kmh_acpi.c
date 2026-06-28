/*
 * Support for generating ACPI tables and passing them to guests.
 *
 * RISC-V Xiangshan Kunminghu ACPI generation
 *
 * Copyright (c) 2026 Beijing Institute of Open Source Chip (BOSC)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/acpi-defs.h"
#include "hw/acpi/aml-build.h"
#include "hw/intc/riscv_imsic.h"
#include "hw/loader.h"
#include "hw/riscv/xiangshan_kmh.h"
#include "system/address-spaces.h"
#include "target/riscv/cpu.h"

#define XIANGSHAN_KMH_ACPI_TABLE_SIZE 0x20000
#define XIANGSHAN_KMH_ACPI_RSDP_SIZE 36
#define XIANGSHAN_KMH_ACPI_TABLE_ALIGN 64
#define XIANGSHAN_KMH_ACPI_TABLE_HEADER_SIZE 36
#define XIANGSHAN_KMH_ACPI_OEM_ID "BOSC  "
#define XIANGSHAN_KMH_ACPI_OEM_TABLE_ID "KMH     "
#define XIANGSHAN_KMH_ACPI_UART_CLK_FREQ 50000000
#define XIANGSHAN_KMH_ACPI_UART_REG_SHIFT 2
#define XIANGSHAN_KMH_ACPI_UART_REG_IO_WIDTH 4
#define XIANGSHAN_KMH_ACPI_UART_CURRENT_SPEED 115200

static const MemMapEntry xiangshan_kmh_acpi_memmap[] = {
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

static uint32_t xiangshan_kmh_imsic_num_bits(uint32_t count)
{
    uint32_t ret = 0;

    while (BIT(ret) < count) {
        ret++;
    }

    return ret;
}

static void xiangshan_kmh_acpi_madt_add_rintc(uint32_t uid, GArray *entry)
{
    uint64_t imsic_addr = xiangshan_kmh_acpi_memmap[XIANGSHAN_KMH_IMSIC_S].base +
                          uid * IMSIC_HART_SIZE(XIANGSHAN_KMH_IMSIC_GUEST_BITS);

    build_append_int_noprefix(entry, 0x18, 1);       /* Type */
    build_append_int_noprefix(entry, 36, 1);         /* Length */
    build_append_int_noprefix(entry, 1, 1);          /* Version */
    build_append_int_noprefix(entry, 0, 1);          /* Reserved */
    build_append_int_noprefix(entry, 0x1, 4);        /* Flags: enabled */
    build_append_int_noprefix(entry, uid, 8);        /* Hart ID */
    build_append_int_noprefix(entry, uid, 4);        /* ACPI Processor UID */
    build_append_int_noprefix(entry, 0, 4);          /* External INTC ID */
    build_append_int_noprefix(entry, imsic_addr, 8); /* IMSIC base */
    build_append_int_noprefix(entry,
                              IMSIC_HART_SIZE(XIANGSHAN_KMH_IMSIC_GUEST_BITS),
                              4);
}

static void xiangshan_kmh_acpi_dsdt_add_cpus(Aml *scope, MachineState *ms)
{
    for (int i = 0; i < ms->smp.cpus; i++) {
        g_autoptr(GArray) madt_buf = g_array_new(false, true, 1);
        Aml *dev = aml_device("C%.03X", i);

        aml_append(dev, aml_name_decl("_HID", aml_string("ACPI0007")));
        aml_append(dev, aml_name_decl("_UID", aml_int(i)));

        xiangshan_kmh_acpi_madt_add_rintc(i, madt_buf);
        aml_append(dev, aml_name_decl("_MAT",
                                      aml_buffer(madt_buf->len,
                                      (uint8_t *)madt_buf->data)));
        aml_append(scope, dev);
    }
}

static void xiangshan_kmh_acpi_dsdt_add_aplic(Aml *scope)
{
    const MemMapEntry *memmap = xiangshan_kmh_acpi_memmap;
    Aml *dev = aml_device("IC00");
    Aml *crs;

    aml_append(dev, aml_name_decl("_HID", aml_string("RSCV0002")));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));
    aml_append(dev, aml_name_decl("_GSB", aml_int(0)));

    crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(memmap[XIANGSHAN_KMH_APLIC_S].base,
                                       memmap[XIANGSHAN_KMH_APLIC_S].size,
                                       AML_READ_WRITE));
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);
}

static void xiangshan_kmh_acpi_dsdt_add_uart(Aml *scope)
{
    const MemMapEntry *memmap = xiangshan_kmh_acpi_memmap;
    Aml *dev = aml_device("COM0");
    Aml *crs;
    Aml *props;
    Aml *prop;
    Aml *dsd;
    Aml *uuid;

    aml_append(dev, aml_name_decl("_HID", aml_string("RSCV0003")));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));

    crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(memmap[XIANGSHAN_KMH_UART0].base,
                                       memmap[XIANGSHAN_KMH_UART0].size,
                                       AML_READ_WRITE));
    {
        uint32_t uart_irq = XIANGSHAN_KMH_UART0_IRQ;

        aml_append(crs,
                   aml_interrupt(AML_CONSUMER, AML_EDGE, AML_ACTIVE_HIGH,
                                 AML_EXCLUSIVE, &uart_irq, 1));
    }
    aml_append(dev, aml_name_decl("_CRS", crs));

    props = aml_package(4);

    prop = aml_package(2);
    aml_append(prop, aml_string("clock-frequency"));
    aml_append(prop, aml_int(XIANGSHAN_KMH_ACPI_UART_CLK_FREQ));
    aml_append(props, prop);

    prop = aml_package(2);
    aml_append(prop, aml_string("reg-shift"));
    aml_append(prop, aml_int(XIANGSHAN_KMH_ACPI_UART_REG_SHIFT));
    aml_append(props, prop);

    prop = aml_package(2);
    aml_append(prop, aml_string("reg-io-width"));
    aml_append(prop, aml_int(XIANGSHAN_KMH_ACPI_UART_REG_IO_WIDTH));
    aml_append(props, prop);

    prop = aml_package(2);
    aml_append(prop, aml_string("current-speed"));
    aml_append(prop, aml_int(XIANGSHAN_KMH_ACPI_UART_CURRENT_SPEED));
    aml_append(props, prop);

    uuid = aml_touuid("DAFFD814-6EBA-4D8C-8A91-BC9BBF4AA301");
    dsd = aml_package(2);
    aml_append(dsd, uuid);
    aml_append(dsd, props);
    aml_append(dev, aml_name_decl("_DSD", dsd));

    aml_append(scope, dev);
}

static void xiangshan_kmh_acpi_build_dsdt(GArray *table_data,
                                          BIOSLinker *linker,
                                          XiangshanKmhState *s)
{
    MachineState *ms = MACHINE(s);
    AcpiTable table = {
        .sig = "DSDT",
        .rev = 2,
        .oem_id = XIANGSHAN_KMH_ACPI_OEM_ID,
        .oem_table_id = XIANGSHAN_KMH_ACPI_OEM_TABLE_ID,
    };
    Aml *dsdt;
    Aml *scope;

    acpi_table_begin(&table, table_data);
    dsdt = init_aml_allocator();

    scope = aml_scope("\\_SB");
    xiangshan_kmh_acpi_dsdt_add_cpus(scope, ms);
    xiangshan_kmh_acpi_dsdt_add_aplic(scope);
    xiangshan_kmh_acpi_dsdt_add_uart(scope);
    aml_append(dsdt, scope);

    g_array_append_vals(table_data, dsdt->buf->data, dsdt->buf->len);

    acpi_table_end(linker, &table);
    free_aml_allocator();
}

static void xiangshan_kmh_acpi_build_fadt(GArray *table_data,
                                          BIOSLinker *linker,
                                          unsigned dsdt_tbl_offset)
{
    AcpiFadtData fadt = {
        .rev = 6,
        .minor_ver = 6,
        .flags = 1 << ACPI_FADT_F_HW_REDUCED_ACPI,
        .dsdt_tbl_offset = &dsdt_tbl_offset,
        .xdsdt_tbl_offset = &dsdt_tbl_offset,
    };

    build_fadt(table_data, linker, &fadt, XIANGSHAN_KMH_ACPI_OEM_ID,
               XIANGSHAN_KMH_ACPI_OEM_TABLE_ID);
}

static void xiangshan_kmh_acpi_build_madt(GArray *table_data,
                                          BIOSLinker *linker,
                                          XiangshanKmhState *s)
{
    MachineState *ms = MACHINE(s);
    uint8_t hart_index_bits = xiangshan_kmh_imsic_num_bits(ms->smp.cpus);
    uint8_t guest_index_bits = XIANGSHAN_KMH_IMSIC_GUEST_BITS;
    const MemMapEntry *memmap = xiangshan_kmh_acpi_memmap;
    AcpiTable table = {
        .sig = "APIC",
        .rev = 7,
        .oem_id = XIANGSHAN_KMH_ACPI_OEM_ID,
        .oem_table_id = XIANGSHAN_KMH_ACPI_OEM_TABLE_ID,
    };

    acpi_table_begin(&table, table_data);
    build_append_int_noprefix(table_data, 0, 4);
    build_append_int_noprefix(table_data, 0, 4);

    for (int i = 0; i < ms->smp.cpus; i++) {
        xiangshan_kmh_acpi_madt_add_rintc(i, table_data);
    }

    build_append_int_noprefix(table_data, 0x19, 1); /* IMSIC */
    build_append_int_noprefix(table_data, 16, 1);
    build_append_int_noprefix(table_data, 1, 1);
    build_append_int_noprefix(table_data, 0, 1);
    build_append_int_noprefix(table_data, 0, 4);
    build_append_int_noprefix(table_data, XIANGSHAN_KMH_IMSIC_NUM_IDS, 2);
    build_append_int_noprefix(table_data, XIANGSHAN_KMH_IMSIC_NUM_IDS, 2);
    build_append_int_noprefix(table_data, guest_index_bits, 1);
    build_append_int_noprefix(table_data, hart_index_bits, 1);
    build_append_int_noprefix(table_data, 0, 1);
    build_append_int_noprefix(table_data, IMSIC_MMIO_GROUP_MIN_SHIFT, 1);

    build_append_int_noprefix(table_data, 0x1A, 1); /* APLIC */
    build_append_int_noprefix(table_data, 36, 1);
    build_append_int_noprefix(table_data, 1, 1);
    build_append_int_noprefix(table_data, 0, 1);
    build_append_int_noprefix(table_data, 0, 4);
    build_append_int_noprefix(table_data, 0, 8);
    build_append_int_noprefix(table_data, 0, 2);
    build_append_int_noprefix(table_data, XIANGSHAN_KMH_APLIC_NUM_SOURCES, 2);
    build_append_int_noprefix(table_data, 0, 4);
    build_append_int_noprefix(table_data, memmap[XIANGSHAN_KMH_APLIC_S].base,
                              8);
    build_append_int_noprefix(table_data, memmap[XIANGSHAN_KMH_APLIC_S].size,
                              4);

    acpi_table_end(linker, &table);
}

#define RHCT_NODE_ARRAY_OFFSET 56

static void xiangshan_kmh_acpi_build_rhct(GArray *table_data,
                                          BIOSLinker *linker,
                                          XiangshanKmhState *s)
{
    MachineState *ms = MACHINE(s);
    RISCVCPU *cpu = &s->soc.cpus.harts[0];
    bool rv32 = riscv_cpu_is_32bit(cpu);
    g_autofree char *isa = riscv_isa_string(cpu);
    size_t len, aligned_len;
    uint32_t isa_offset;
    uint32_t cmo_offset = 0;
    uint32_t mmu_offset = 0;
    uint32_t num_rhct_nodes = 1 + ms->smp.cpus;
    AcpiTable table = {
        .sig = "RHCT",
        .rev = 1,
        .oem_id = XIANGSHAN_KMH_ACPI_OEM_ID,
        .oem_table_id = XIANGSHAN_KMH_ACPI_OEM_TABLE_ID,
    };

    if (cpu->cfg.ext_zicbom || cpu->cfg.ext_zicboz) {
        num_rhct_nodes++;
    }
    if (!rv32 && cpu->cfg.max_satp_mode >= VM_1_10_SV39) {
        num_rhct_nodes++;
    }

    acpi_table_begin(&table, table_data);
    build_append_int_noprefix(table_data, 0, 4);
    build_append_int_noprefix(table_data, XIANGSHAN_KMH_CLINT_TIMEBASE_FREQ, 8);
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

    for (int i = 0; i < ms->smp.cpus; i++) {
        int node_len = 16;
        int num_offsets = 1;

        if (cmo_offset) {
            node_len += 4;
            num_offsets++;
        }
        if (mmu_offset) {
            node_len += 4;
            num_offsets++;
        }

        build_append_int_noprefix(table_data, 0xFFFF, 2);
        build_append_int_noprefix(table_data, node_len, 2);
        build_append_int_noprefix(table_data, 1, 2);
        build_append_int_noprefix(table_data, num_offsets, 2);
        build_append_int_noprefix(table_data, i, 4);
        build_append_int_noprefix(table_data, isa_offset, 4);
        if (cmo_offset) {
            build_append_int_noprefix(table_data, cmo_offset, 4);
        }
        if (mmu_offset) {
            build_append_int_noprefix(table_data, mmu_offset, 4);
        }
    }

    acpi_table_end(linker, &table);
}

static void xiangshan_kmh_acpi_build_spcr(GArray *table_data,
                                          BIOSLinker *linker)
{
    const char name[] = ".";
    const MemMapEntry *memmap = xiangshan_kmh_acpi_memmap;
    AcpiSpcrData serial = {
        .interface_type = 0x12,
        .base_addr.id = AML_AS_SYSTEM_MEMORY,
        .base_addr.width = 32,
        .base_addr.offset = 0,
        .base_addr.size = 1,
        .base_addr.addr = memmap[XIANGSHAN_KMH_UART0].base,
        .interrupt_type = 1 << 4,
        .pc_interrupt = 0,
        .interrupt = XIANGSHAN_KMH_UART0_IRQ,
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

    build_spcr(table_data, linker, &serial, 4, XIANGSHAN_KMH_ACPI_OEM_ID,
               XIANGSHAN_KMH_ACPI_OEM_TABLE_ID, name);
}

static uint8_t xiangshan_kmh_acpi_checksum(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;

    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }

    return 0 - sum;
}

static void xiangshan_kmh_acpi_update_table_checksum(GArray *blob,
                                                     unsigned table_offset)
{
    uint8_t *table = (uint8_t *)blob->data + table_offset;
    uint32_t len = ldl_le_p(table + 4);

    assert(table_offset + len <= blob->len);
    table[9] = 0;
    table[9] = xiangshan_kmh_acpi_checksum(table, len);
}

static void xiangshan_kmh_acpi_build_xsdt(GArray *blob,
                                          BIOSLinker *linker,
                                          GArray *table_offsets)
{
    AcpiTable table = {
        .sig = "XSDT",
        .rev = 1,
        .oem_id = XIANGSHAN_KMH_ACPI_OEM_ID,
        .oem_table_id = XIANGSHAN_KMH_ACPI_OEM_TABLE_ID,
    };

    acpi_table_begin(&table, blob);
    for (int i = 0; i < table_offsets->len; i++) {
        build_append_int_noprefix(blob, 0, 8);
    }
    acpi_table_end(linker, &table);
}

static void xiangshan_kmh_acpi_patch_table_pointers(GArray *blob,
                                                    GArray *table_offsets,
                                                    hwaddr blob_base,
                                                    unsigned dsdt_offset,
                                                    unsigned xsdt_offset)
{
    unsigned xsdt_entry_offset = xsdt_offset +
                                 XIANGSHAN_KMH_ACPI_TABLE_HEADER_SIZE;
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

    xiangshan_kmh_acpi_update_table_checksum(blob, xsdt_offset);
    xiangshan_kmh_acpi_update_table_checksum(blob, dsdt_offset);
    for (int i = 0; i < table_offsets->len; i++) {
        uint32_t table_offset = g_array_index(table_offsets, uint32_t, i);

        xiangshan_kmh_acpi_update_table_checksum(blob, table_offset);
    }
}

static void xiangshan_kmh_acpi_build_tables(XiangshanKmhState *s, GArray *blob,
                                            BIOSLinker *linker,
                                            GArray *table_offsets,
                                            unsigned *xsdt_offset)
{
    unsigned dsdt;

    bios_linker_loader_alloc(linker, ACPI_BUILD_TABLE_FILE, blob,
                             XIANGSHAN_KMH_ACPI_TABLE_ALIGN, false);

    dsdt = blob->len;
    xiangshan_kmh_acpi_build_dsdt(blob, linker, s);

    acpi_add_table(table_offsets, blob);
    xiangshan_kmh_acpi_build_fadt(blob, linker, dsdt);

    acpi_add_table(table_offsets, blob);
    xiangshan_kmh_acpi_build_madt(blob, linker, s);

    acpi_add_table(table_offsets, blob);
    xiangshan_kmh_acpi_build_rhct(blob, linker, s);

    acpi_add_table(table_offsets, blob);
    xiangshan_kmh_acpi_build_spcr(blob, linker);

    *xsdt_offset = blob->len;
    xiangshan_kmh_acpi_build_xsdt(blob, linker, table_offsets);
}

static void xiangshan_kmh_acpi_build_rsdp(GArray *blob, hwaddr xsdt_addr)
{
    GArray *rsdp = g_array_new(false, true, 1);

    g_array_append_vals(rsdp, "RSD PTR ", 8);
    build_append_int_noprefix(rsdp, 0, 1);
    g_array_append_vals(rsdp, XIANGSHAN_KMH_ACPI_OEM_ID, 6);
    build_append_int_noprefix(rsdp, 2, 1);
    build_append_int_noprefix(rsdp, 0, 4);
    build_append_int_noprefix(rsdp, XIANGSHAN_KMH_ACPI_RSDP_SIZE, 4);
    build_append_int_noprefix(rsdp, xsdt_addr, 8);
    build_append_int_noprefix(rsdp, 0, 1);
    build_append_int_noprefix(rsdp, 0, 3);

    assert(rsdp->len == XIANGSHAN_KMH_ACPI_RSDP_SIZE);
    rsdp->data[8] = xiangshan_kmh_acpi_checksum((uint8_t *)rsdp->data, 20);
    rsdp->data[32] = xiangshan_kmh_acpi_checksum((uint8_t *)rsdp->data,
                                                 rsdp->len);

    assert(blob->len >= rsdp->len);
    memcpy(blob->data, rsdp->data, rsdp->len);
    g_array_free(rsdp, true);
}

void xiangshan_kmh_acpi_setup(XiangshanKmhState *s)
{
    GArray *handoff = g_array_new(false, true, 1);
    GArray *table_offsets = g_array_new(false, true, sizeof(uint32_t));
    BIOSLinker *linker = bios_linker_loader_init();
    unsigned tables_offset;
    unsigned xsdt_offset;
    hwaddr handoff_addr = s->acpi_handoff_addr;
    uint32_t handoff_len;

    g_array_set_size(handoff, XIANGSHAN_KMH_ACPI_RSDP_SIZE);
    tables_offset = ROUND_UP(handoff->len, XIANGSHAN_KMH_ACPI_TABLE_ALIGN);
    g_array_set_size(handoff, tables_offset);

    xiangshan_kmh_acpi_build_tables(s, handoff, linker, table_offsets,
                                    &xsdt_offset);
    xiangshan_kmh_acpi_patch_table_pointers(handoff, table_offsets,
                                            handoff_addr, tables_offset,
                                            xsdt_offset);
    xiangshan_kmh_acpi_build_rsdp(handoff, handoff_addr + xsdt_offset);
    g_array_set_size(handoff, ROUND_UP(acpi_data_len(handoff),
                                       XIANGSHAN_KMH_ACPI_TABLE_ALIGN));

    handoff_len = acpi_data_len(handoff);
    if (handoff_len > s->acpi_handoff_size) {
        error_report("generated ACPI handoff size 0x%x exceeds configured "
                     "range 0x%"PRIx64, handoff_len, s->acpi_handoff_size);
        exit(1);
    }
    if (handoff_len > XIANGSHAN_KMH_ACPI_TABLE_SIZE) {
        warn_report("ACPI handoff size %u exceeds %d bytes",
                    handoff_len, XIANGSHAN_KMH_ACPI_TABLE_SIZE);
    }

    rom_add_blob_fixed_as("xiangshan.kunminghu.acpi-handoff",
                          handoff->data, handoff_len, handoff_addr,
                          &address_space_memory);

    bios_linker_loader_cleanup(linker);
    g_array_free(table_offsets, true);
    g_array_free(handoff, true);
}
