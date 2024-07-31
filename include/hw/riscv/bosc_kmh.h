/*
 * BOSC Kunminghu SoC emulation
 *
 * Copyright (c) 2024 Beijing Institute of Open Source Chip (BOSC)
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

#ifndef HW_BOSC_KMH_H
#define HW_BOSC_KMH_H

#include "hw/riscv/riscv_hart.h"
#include "hw/boards.h"
#include "hw/char/serial.h"
#include "hw/pci-host/xilinx-pcie.h"


#define TYPE_RISCV_KMH_SOC "riscv.bosc.kmh.soc"
#define RISCV_KMH_SOC(obj) \
    OBJECT_CHECK(BoscKmhSoCState, (obj), TYPE_RISCV_KMH_SOC)

typedef struct BoscKmhSoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *plic;
    XilinxPCIEHost pcie;
    MemoryRegion rom;

} BoscKmhSoCState;

#define TYPE_RISCV_KMH_MACHINE MACHINE_TYPE_NAME("bosc-kmh")

typedef struct BoscKmhMachineState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    BoscKmhSoCState soc;
} BoscKmhMachineState;

enum {
    BOSC_KMH_DEV_DEBUG,
    BOSC_KMH_DEV_MROM,
    BOSC_KMH_DEV_FLASH,
    BOSC_KMH_DEV_VIRTIO,
    BOSC_KMH_DEV_FW_CFG,
    BOSC_KMH_DEV_UART0,
    BOSC_KMH_DEV_CLINT,
    BOSC_KMH_DEV_PLIC,
    BOSC_KMH_DEV_PCIE_CFG,
    BOSC_KMH_DEV_PCIE_MMIO,
    BOSC_KMH_DEV_DRAM
};

enum {
    BOSC_KMH_UART0_IRQ = 40,
    BOSC_KMH_PCIE0_IRQ0 = 51,
    BOSC_KMH_PCIE0_IRQ1 = 52,
    BOSC_KMH_PCIE0_IRQ2 = 53,
};


#define BOSC_KMH_PLIC_HART_CONFIG "MS"
/* Including Interrupt ID 0 (no interrupt)*/
#define BOSC_KMH_PLIC_NUM_SOURCES 28
/* Excluding Priority 0 */
#define BOSC_KMH_PLIC_NUM_PRIORITIES 2
#define BOSC_KMH_PLIC_PRIORITY_BASE 0x00
#define BOSC_KMH_PLIC_PENDING_BASE 0x1000
#define BOSC_KMH_PLIC_ENABLE_BASE 0x2000
#define BOSC_KMH_PLIC_ENABLE_STRIDE 0x80
#define BOSC_KMH_PLIC_CONTEXT_BASE 0x200000
#define BOSC_KMH_PLIC_CONTEXT_STRIDE 0x1000

#endif
