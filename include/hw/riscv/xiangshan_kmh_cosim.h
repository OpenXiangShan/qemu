/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * QEMU RISC-V Board Compatible with the Xiangshan Kunminghu
 * FPGA prototype platform
 *
 * Copyright (c) 2025 Beijing Institute of Open Source Chip (BOSC)
 *
 */

#ifndef HW_XIANGSHAN_KMH_COSIM_H
#define HW_XIANGSHAN_KMH_COSIM_H

#include <stdbool.h>

#include "hw/boards.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/pci-host/designware.h"
#include "qemu/units.h"

#define XIANGSHAN_KMH_MAX_CPUS 16

typedef struct XiangshanKmhCosimSoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *irqchip;
    MemoryRegion rom;
    MemoryRegion sram;
    MemoryRegion flash;
    DesignwarePCIEHost pcie0;
} XiangshanKmhCosimSoCState;

#define TYPE_XIANGSHAN_KMH_COSIM_SOC "xiangshan.kunminghu.cosim.soc"
DECLARE_INSTANCE_CHECKER(XiangshanKmhCosimSoCState, XIANGSHAN_KMH_COSIM_SOC,
                         TYPE_XIANGSHAN_KMH_COSIM_SOC)

typedef struct XiangshanKmhCosimState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    XiangshanKmhCosimSoCState soc;
    char *devproxy_mode;
    char *devproxy_mmio_socket;
    char *devproxy_dma_socket;
    char *devproxy_dma_shm;
    char *devproxy_irq_socket;
    char *devproxy_dmac_irq_socket;
    char *devproxy_msi_socket;
    uint32_t devproxy_dmac_requester_id;
} XiangshanKmhCosimState;

#define TYPE_XIANGSHAN_KMH_COSIM_MACHINE \
    MACHINE_TYPE_NAME("xiangshan-kunminghu-cosim")
DECLARE_INSTANCE_CHECKER(XiangshanKmhCosimState, XIANGSHAN_KMH_COSIM_MACHINE,
                         TYPE_XIANGSHAN_KMH_COSIM_MACHINE)

enum {
    XIANGSHAN_KMH_ROM,
    XIANGSHAN_KMH_FLASH,
    XIANGSHAN_KMH_DMAC,
    XIANGSHAN_KMH_UART0,
    XIANGSHAN_KMH_PCIE_ECAM,
    XIANGSHAN_KMH_PCIE_MMIO,
    XIANGSHAN_KMH_CLINT,
    XIANGSHAN_KMH_APLIC_M,
    XIANGSHAN_KMH_APLIC_S,
    XIANGSHAN_KMH_SRAM,
    XIANGSHAN_KMH_IMSIC_M,
    XIANGSHAN_KMH_IMSIC_S,
    XIANGSHAN_KMH_UART1,
    XIANGSHAN_KMH_DRAM,
};

enum {
    XIANGSHAN_KMH_UART0_IRQ = 10,
    XIANGSHAN_KMH_UART1_IRQ = 11,
    XIANGSHAN_KMH_RC_MSI0_IRQ = 12,
    XIANGSHAN_KMH_RC_HP_IRQ = 13,
    XIANGSHAN_KMH_PCIE_IRQ = 15,
    XIANGSHAN_KMH_DMAC_IRQ = 18,
};

/* Indicating Timebase-freq (1MHZ) */
#define XIANGSHAN_KMH_CLINT_TIMEBASE_FREQ 1000000

#define XIANGSHAN_KMH_IMSIC_NUM_IDS 255
#define XIANGSHAN_KMH_IMSIC_NUM_GUESTS 7
#define XIANGSHAN_KMH_IMSIC_GUEST_BITS 3

#define XIANGSHAN_KMH_APLIC_NUM_SOURCES 96

#endif
