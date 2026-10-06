/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "compose-internal.h"

void kmh_profile_init_defaults(KmhProfile *p)
{
    *p = (KmhProfile) {
        .soc = { "Host-assembled Xiangshan Kunminghu", "bosc,kmh-v2-dev" },
        .dram = { "dram", 0x80000000, UINT64_C(16) << 30, LIBQEMU_RAM, true },
        .rom = { "rom", 0x1000, 0x40000, LIBQEMU_ROM, false },
        .sram = { "sram", 0x37f00000, 0x100000, LIBQEMU_RAM, false },
        .flash = { "flash", 0x10000000, 0x4000000, LIBQEMU_RAM, false },
        .cpu = { "cpu0", "xiangshan-kunminghu-riscv-cpu", 0x1000,
                 "riscv,sv48", 64, 64 },
        .aia = {
            .name = "aia", .imsic_m_base = 0x3a800000,
            .imsic_s_base = 0x3b000000, .aplic_m_base = 0x31100000,
            .aplic_s_base = 0x31120000, .num_sources = 96,
            .num_ids = 255, .guest_files = 5,
        },
        .aclint = { "aclint", 0x38000000, 1000000 },
        .uart = { "uart0", 0x310b0000, 10, "uart0", true },
        .blk = { .name = "blk", .base = 0x310a0000, .irq = 15 },
        .pcie = {
            .name = "pcie0", .dbi_base = 0x32000000, .dbi_size = 0x1000000,
            .config_base = 0x67ff0000, .config_size = 0x10000,
            .cpu_mem_base = 0x60000000, .pci_mem_base = 0x40000000,
            .mem_size = 0x07ff0000, .msi_irq = 12, .hp_irq = 13,
        },
        .nvme = { .name = "nvme0", .serial = "LIBQEMU-KMH-NVME" },
        .finisher = { "finisher", 0x100000 },
        .boot = {
            .firmware_base = 0x80000000, .kernel_base = 0x80200000,
            .fdt_base = 0xfe000000,
            .bootargs = "console=ttyS0,115200 earlycon=sbi loglevel=8 rdinit=/bin/sh",
        },
        .uart_enabled = true, .blk_enabled = true, .pcie_enabled = true,
        .nvme_enabled = true, .finisher_enabled = true,
    };
}

int kmh_soc_build(LibQemuCompose *c, const KmhProfile *p, LibQemuSoc **out,
                  char **errp)
{
    LibQemuSoc *s;
    LibQemuCpu *cpu;
    LibQemuAia *aia;
    LibQemuPcie *pcie;

    if (out) {
        *out = NULL;
    }
    if (!c || !p || !out || (p->nvme_enabled && !p->pcie_enabled)) {
        return compose_error(errp,
                             "compose:profile:invalid config or NVMe without RC");
    }
    if (libqemu_soc_new(c, &p->soc, &s, errp) ||
        libqemu_soc_add_memory(s, &p->dram, errp) ||
        libqemu_soc_add_memory(s, &p->rom, errp) ||
        libqemu_soc_add_memory(s, &p->sram, errp) ||
        libqemu_soc_add_memory(s, &p->flash, errp) ||
        libqemu_riscv_cpu_create(s, &p->cpu, &cpu, errp) ||
        libqemu_riscv_aia_create(s, cpu, &p->aia, &aia, errp) ||
        libqemu_riscv_aclint_create(s, cpu, &p->aclint, errp) ||
        (p->uart_enabled && libqemu_uart_create(s, &p->uart, aia, errp)) ||
        (p->blk_enabled && libqemu_my_virtio_blk_create(s, &p->blk, aia, errp)) ||
        (p->pcie_enabled && libqemu_dwc_create(s, &p->pcie, aia, &pcie, errp)) ||
        (p->nvme_enabled && libqemu_nvme_attach(pcie, &p->nvme, errp)) ||
        (p->finisher_enabled && libqemu_finisher_create(s, &p->finisher, errp))) {
        return -1;
    }
    *out = s;
    return 0;
}
