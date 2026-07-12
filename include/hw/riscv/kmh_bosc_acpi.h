/*
 * QEMU-generated ACPI handoff for the BOSC Kunminghu multi-die SoC.
 *
 * Copyright (c) 2026 Beijing Institute of Open Source Chip (BOSC)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_KMH_BOSC_ACPI_H
#define HW_RISCV_KMH_BOSC_ACPI_H

#include "exec/hwaddr.h"
#include "hw/boards.h"
#include "hw/riscv/riscv_hart.h"

void kmh_bosc_acpi_setup(MachineState *ms, RISCVHartArrayState *cpus,
                         uint32_t die_mask, const uint32_t core_mask[4],
                         hwaddr handoff_addr, uint64_t handoff_size,
                         bool dw_pcie);

#endif
