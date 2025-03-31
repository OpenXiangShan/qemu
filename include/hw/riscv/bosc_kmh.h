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
#include "hw/intc/riscv_imsic.h"

#define BOSC_KMH_CPUS_MAX_BITS             4
#define BOSC_KMH_CPUS_MAX                  (1 << BOSC_KMH_CPUS_MAX_BITS)
#define BOSC_KMH_SOCKETS_MAX_BITS          2
#define BOSC_KMH_SOCKETS_MAX               (1 << BOSC_KMH_SOCKETS_MAX_BITS)

#define TYPE_RISCV_KMH_SOC "riscv.bosc.kmh.soc"
#define RISCV_KMH_SOC(obj) \
    OBJECT_CHECK(BoscKmhSoCState, (obj), TYPE_RISCV_KMH_SOC)

typedef enum RISCVKmhAIAType {
    BOSC_KMH_AIA_TYPE_NONE = 0,
    BOSC_KMH_AIA_TYPE_APLIC,
    BOSC_KMH_AIA_TYPE_APLIC_IMSIC,
} RISCVKmhAIAType;

typedef struct BoscKmhSoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *irqchip;
    MemoryRegion rom;

    RISCVKmhAIAType aia_type;

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
    BOSC_KMH_APLIC_M,
    BOSC_KMH_APLIC_S,
    BOSC_KMH_IMSIC_M,
    BOSC_KMH_IMSIC_S,
    BOSC_KMH_DEV_UART1,
    BOSC_KMH_DEV_DRAM
};

enum {
    BOSC_KMH_UART0_IRQ = 10,
    BOSC_KMH_UART1_IRQ = 11,
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

/* Indicating Timebase-freq (1MHZ) */
#define RISCV_ACLINT_KMH_TIMEBASE_FREQ 1000000

#define BOSC_KMH_IRQCHIP_NUM_MSIS 255
#define BOSC_KMH_IRQCHIP_NUM_SOURCES 96
#define BOSC_KMH_IRQCHIP_NUM_PRIO_BITS 3
#define BOSC_KMH_IRQCHIP_MAX_GUESTS_BITS 3
#define BOSC_KMH_IRQCHIP_MAX_GUESTS ((1U << BOSC_KMH_IRQCHIP_MAX_GUESTS_BITS) - 1U)
/*
 * The bosc-kmh machine physical address space used by some of the devices
 * namely ACLINT, PLIC, APLIC, and IMSIC depend on number of Sockets,
 * number of CPUs, and number of IMSIC guest files.
 *
 * Various limits defined by BOSC_KMH_SOCKETS_MAX_BITS, BOSC_KMH_CPUS_MAX_BITS,
 * and BOSC_KMH_IRQCHIP_MAX_GUESTS_BITS are tuned for maximum utilization
 * of bosc-kmh machine physical address space.
 */

#define BOSC_KMH_IMSIC_GROUP_MAX_SIZE      (1U << IMSIC_MMIO_GROUP_MIN_SHIFT)
#if BOSC_KMH_IMSIC_GROUP_MAX_SIZE < \
    IMSIC_GROUP_SIZE(BOSC_KMH_CPUS_MAX_BITS, BOSC_KMH_IRQCHIP_MAX_GUESTS_BITS)
#error "Can't accommodate single IMSIC group in address space"
#endif

#define BOSC_KMH_IMSIC_MAX_SIZE            (BOSC_KMH_SOCKETS_MAX * \
                                        BOSC_KMH_IMSIC_GROUP_MAX_SIZE)
#if 0x4000000 < BOSC_KMH_IMSIC_MAX_SIZE
#error "Can't accommodate all IMSIC groups in address space"
#endif
#endif
