/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU RISC-V Board Compatible with the Xiangshan Nanhu V5
 * FPGA prototype platform
 *
 * Copyright (c) 2025 Beijing Institute of Open Source Chip (BOSC)
 *
 */

#ifndef HW_XIANGSHAN_NHV5_H
#define HW_XIANGSHAN_NHV5_H

#include "hw/boards.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/pci-host/designware.h"
#include "qemu/units.h"

#define XIANGSHAN_NHV5_CPUS_MAX_BITS             4
#define XIANGSHAN_NHV5_MAX_CPUS                  \
    (1U << XIANGSHAN_NHV5_CPUS_MAX_BITS)

#define XIANGSHAN_NHV5_PLIC_NUM_SOURCES          256
#define XIANGSHAN_NHV5_PLIC_NUM_PRIORITIES       7
#define XIANGSHAN_NHV5_PLIC_PRIORITY_BASE        0x00
#define XIANGSHAN_NHV5_PLIC_PENDING_BASE         0x1000
#define XIANGSHAN_NHV5_PLIC_ENABLE_BASE          0x2000
#define XIANGSHAN_NHV5_PLIC_ENABLE_STRIDE        0x80
#define XIANGSHAN_NHV5_PLIC_CONTEXT_BASE         0x200000
#define XIANGSHAN_NHV5_PLIC_CONTEXT_STRIDE       0x1000

/* Indicating Timebase-freq (1MHZ) */
#define RISCV_ACLINT_NHV5_TIMEBASE_FREQ      1000000

typedef struct XiangshanNhv5SoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *plic;
    MemoryRegion rom;
    DesignwarePCIEHost pcie0;
} XiangshanNhv5SoCState;

#define TYPE_XIANGSHAN_NHV5_SOC "xiangshan.nanhuv5.soc"
DECLARE_INSTANCE_CHECKER(XiangshanNhv5SoCState, XIANGSHAN_NHV5_SOC,
                         TYPE_XIANGSHAN_NHV5_SOC)

typedef struct XiangshanNhv5State {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    XiangshanNhv5SoCState soc;
} XiangshanNhv5State;

#define TYPE_XIANGSHAN_NHV5_MACHINE MACHINE_TYPE_NAME("xiangshan-nanhuv5")
DECLARE_INSTANCE_CHECKER(XiangshanNhv5State, XIANGSHAN_NHV5_MACHINE,
                         TYPE_XIANGSHAN_NHV5_MACHINE)

enum {
    XIANGSHAN_NHV5_DEBUG,
    XIANGSHAN_NHV5_ROM,
    XIANGSHAN_NHV5_FLASH,
    XIANGSHAN_NHV5_VIRTIO,
    XIANGSHAN_NHV5_FW_CFG,
    XIANGSHAN_NHV5_UART0,
    XIANGSHAN_NHV5_CLINT,
    XIANGSHAN_NHV5_PLIC,
    XIANGSHAN_NHV5_PLIC_M,
    XIANGSHAN_NHV5_APLIC_S,
    XIANGSHAN_NHV5_UART1,
    XIANGSHAN_NHV5_DRAM
};

enum {
    XIANGSHAN_NHV5_UART0_IRQ = 40,
    XIANGSHAN_NHV5_UART1_IRQ = 41,
    XIANGSHAN_NHV5_RC0_MSI_IRQ = 52,
    XIANGSHAN_NHV5_RC0_HP_IRQ = 53,
};
#endif
