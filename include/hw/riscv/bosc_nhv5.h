/*
 * BOSC NanHuV5 SoC emulation
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

#ifndef HW_BOSC_NHV5_H
#define HW_BOSC_NHV5_H

#include "hw/riscv/riscv_hart.h"
#include "hw/boards.h"
#include "hw/char/serial.h"
#include "hw/intc/riscv_imsic.h"
#include "hw/pci-host/designware.h"
#include "qemu/units.h"

#define BOSC_NHV5_CPUS_MAX_BITS             4
#define BOSC_NHV5_CPUS_MAX                  (1 << BOSC_NHV5_CPUS_MAX_BITS)

#define TYPE_RISCV_NHV5_SOC     "riscv.bosc.nhv5.soc"
#define RISCV_NHV5_SOC(obj) \
    OBJECT_CHECK(BoscNhv5SoCState, (obj), TYPE_RISCV_NHV5_SOC)


#define BOSC_NHV5_PLIC_NUM_SOURCES          256
#define BOSC_NHV5_PLIC_NUM_PRIORITIES       7
#define BOSC_NHV5_PLIC_PRIORITY_BASE        0x00
#define BOSC_NHV5_PLIC_PENDING_BASE         0x1000
#define BOSC_NHV5_PLIC_ENABLE_BASE          0x2000
#define BOSC_NHV5_PLIC_ENABLE_STRIDE        0x80
#define BOSC_NHV5_PLIC_CONTEXT_BASE         0x200000
#define BOSC_NHV5_PLIC_CONTEXT_STRIDE       0x1000

/* Indicating Timebase-freq (1MHZ) */
#define RISCV_ACLINT_NHV5_TIMEBASE_FREQ      1000000

/*
typedef enum RISCVNHAIAType {
    BOSC_NHV5_AIA_TYPE_NONE = 0,
    BOSC_NHV5_AIA_TYPE_APLIC,
} RISCVNHAIAType;
*/

typedef struct BoscNhv5SoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *plic;
    MemoryRegion rom;

//    RISCVNHAIAType aia_type;
    DesignwarePCIEHost pcie0;
} BoscNhv5SoCState;

typedef struct BoscNhv5MachineState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    BoscNhv5SoCState soc;
} BoscNhv5MachineState;

#define TYPE_RISCV_NHV5_MACHINE MACHINE_TYPE_NAME("bosc-nhv5")

enum {
    BOSC_NHV5_DEV_DEBUG,
    BOSC_NHV5_DEV_MROM,
    BOSC_NHV5_DEV_FLASH,
    BOSC_NHV5_DEV_VIRTIO,
    BOSC_NHV5_DEV_FW_CFG,
    BOSC_NHV5_DEV_UART0,
    BOSC_NHV5_DEV_CLINT,
    BOSC_NHV5_DEV_PLIC,
    BOSC_NHV5_APLIC_M,
    BOSC_NHV5_APLIC_S,
    BOSC_NHV5_DEV_UART1,
    BOSC_NHV5_DEV_DRAM
};

enum {
    BOSC_NHV5_UART0_IRQ = 40,
    BOSC_NHV5_UART1_IRQ = 41,
    BOSC_KMH_RC0_MSI_IRQ = 52,
    BOSC_KMH_RC0_HP_IRQ = 53,
};



#endif
