/*
 * Synopsys DesignWare AXI DMAC
 *
 * Copyright (c) 2026 Beijing Institute of Open Source Chip (BOSC)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_DMA_DW_AXI_DMAC_H
#define HW_DMA_DW_AXI_DMAC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_DW_AXI_DMAC "dw-axi-dmac"
OBJECT_DECLARE_SIMPLE_TYPE(DWAxiDMACState, DW_AXI_DMAC)

#define DW_AXI_DMAC_NR_CHANS 2
#define DW_AXI_DMAC_MMIO_SIZE 0x1000

typedef struct DWAxiDMACChannelState {
    uint64_t sar;
    uint64_t dar;
    uint64_t llp;
    uint32_t block_ts;
    uint32_t ctl_lo;
    uint32_t ctl_hi;
    uint32_t cfg_lo;
    uint32_t cfg_hi;
    uint32_t status;
    uint32_t swhssrc;
    uint32_t swhsdst;
    uint32_t blk_tfr_resume_req;
    uint32_t axi_id;
    uint32_t axi_qos;
    uint32_t sstat;
    uint32_t dstat;
    uint64_t sstatar;
    uint64_t dstatar;
    uint32_t intstatus_ena;
    uint32_t intstatus;
    uint32_t intsignal_ena;
} DWAxiDMACChannelState;

struct DWAxiDMACState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t cfg;
    uint32_t chen;
    uint32_t common_intstatus;
    uint32_t common_intstatus_ena;
    uint32_t common_intsignal_ena;
    uint32_t requester_id;
    uint8_t irq_level;
    DWAxiDMACChannelState chan[DW_AXI_DMAC_NR_CHANS];
};

#endif /* HW_DMA_DW_AXI_DMAC_H */
