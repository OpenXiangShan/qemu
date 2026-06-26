/*
 * Synopsys DesignWare AXI DMAC
 *
 * Copyright (c) 2026 Beijing Institute of Open Source Chip (BOSC)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/dma/dw-axi-dmac.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/module.h"
#include "system/address-spaces.h"
#include "system/memory.h"

#define DW_AXI_DMAC_COMMON_SIZE         0x100
#define DW_AXI_DMAC_CHANNEL_STRIDE      0x100

#define DW_AXI_DMAC_ID                  0x000
#define DW_AXI_DMAC_COMPVER             0x008
#define DW_AXI_DMAC_CFG                 0x010
#define DW_AXI_DMAC_CHEN                0x018
#define DW_AXI_DMAC_INTSTATUS           0x030
#define DW_AXI_DMAC_COMMON_INTCLEAR     0x038
#define DW_AXI_DMAC_COMMON_INTSTATUS_EN 0x040
#define DW_AXI_DMAC_COMMON_INTSIGNAL_EN 0x048
#define DW_AXI_DMAC_COMMON_INTSTATUS    0x050
#define DW_AXI_DMAC_RESET               0x058

#define DW_AXI_DMAC_CH_SAR              0x000
#define DW_AXI_DMAC_CH_DAR              0x008
#define DW_AXI_DMAC_CH_BLOCK_TS         0x010
#define DW_AXI_DMAC_CH_CTL              0x018
#define DW_AXI_DMAC_CH_CTL_H            0x01c
#define DW_AXI_DMAC_CH_CFG              0x020
#define DW_AXI_DMAC_CH_CFG_H            0x024
#define DW_AXI_DMAC_CH_LLP              0x028
#define DW_AXI_DMAC_CH_STATUS           0x030
#define DW_AXI_DMAC_CH_SWHSSRC          0x038
#define DW_AXI_DMAC_CH_SWHSDST          0x040
#define DW_AXI_DMAC_CH_BLK_TFR_RESUME   0x048
#define DW_AXI_DMAC_CH_AXI_ID           0x050
#define DW_AXI_DMAC_CH_AXI_QOS          0x058
#define DW_AXI_DMAC_CH_SSTAT            0x060
#define DW_AXI_DMAC_CH_DSTAT            0x068
#define DW_AXI_DMAC_CH_SSTATAR          0x070
#define DW_AXI_DMAC_CH_DSTATAR          0x078
#define DW_AXI_DMAC_CH_INTSTATUS_ENA    0x080
#define DW_AXI_DMAC_CH_INTSTATUS        0x088
#define DW_AXI_DMAC_CH_INTSIGNAL_ENA    0x090
#define DW_AXI_DMAC_CH_INTCLEAR         0x098

#define DW_AXI_DMAC_CFG_EN              BIT(0)
#define DW_AXI_DMAC_CFG_INT_EN          BIT(1)

#define DW_AXI_DMAC_CH_CTL_SRC_INC_POS  4
#define DW_AXI_DMAC_CH_CTL_DST_INC_POS  6
#define DW_AXI_DMAC_CH_CTL_SRC_W_POS    8
#define DW_AXI_DMAC_CH_CTL_DST_W_POS    11

#define DW_AXI_DMAC_CH_INC_NOINC        1

#define DW_AXI_DMAC_IRQ_BLOCK_TRF       BIT(0)
#define DW_AXI_DMAC_IRQ_DMA_TRF         BIT(1)
#define DW_AXI_DMAC_IRQ_SRC_DEC_ERR     BIT(5)
#define DW_AXI_DMAC_IRQ_DST_DEC_ERR     BIT(6)
#define DW_AXI_DMAC_IRQ_INVALID_ERR     BIT(13)

#define DW_AXI_DMAC_ID_VALUE            0x44574158
#define DW_AXI_DMAC_COMPVER_VALUE       0x31303161
#define DW_AXI_DMAC_COMMON_IRQ_SUMMARY  BIT(16)
#define DW_AXI_DMAC_COMMON_IRQ_MASK     (BIT(0) | BIT(1) | BIT(2) | \
                                         BIT(3) | BIT(8))
#define DW_AXI_DMAC_CHANNEL_IRQ_MASK    (DW_AXI_DMAC_IRQ_BLOCK_TRF | \
                                         DW_AXI_DMAC_IRQ_DMA_TRF | \
                                         DW_AXI_DMAC_IRQ_SRC_DEC_ERR | \
                                         DW_AXI_DMAC_IRQ_DST_DEC_ERR | \
                                         DW_AXI_DMAC_IRQ_INVALID_ERR)

static uint32_t dw_axi_dmac_common_irq_status(DWAxiDMACState *s)
{
    uint32_t status = 0;
    int i;

    for (i = 0; i < DW_AXI_DMAC_NR_CHANS; i++) {
        if (s->chan[i].intstatus) {
            status |= BIT(i);
        }
    }

    if (s->common_intstatus) {
        status |= DW_AXI_DMAC_COMMON_IRQ_SUMMARY;
    }

    return status;
}

static void dw_axi_dmac_update_irq(DWAxiDMACState *s)
{
    bool level = false;
    int i;

    if (s->cfg & DW_AXI_DMAC_CFG_INT_EN) {
        for (i = 0; i < DW_AXI_DMAC_NR_CHANS; i++) {
            if (s->chan[i].intstatus & s->chan[i].intsignal_ena) {
                level = true;
                break;
            }
        }

        if (!level && (s->common_intstatus & s->common_intsignal_ena)) {
            level = true;
        }
    }

    qemu_set_irq(s->irq, level);
    s->irq_level = level;
}

static void dw_axi_dmac_channel_clear(DWAxiDMACState *s, unsigned int chan,
                                      uint32_t mask)
{
    s->chan[chan].intstatus &= ~mask;
    dw_axi_dmac_update_irq(s);
}

static void dw_axi_dmac_common_clear(DWAxiDMACState *s, uint32_t mask)
{
    s->common_intstatus &= ~mask;
    dw_axi_dmac_update_irq(s);
}

static void dw_axi_dmac_channel_raise(DWAxiDMACState *s, unsigned int chan,
                                      uint32_t mask)
{
    s->chan[chan].intstatus |= mask & s->chan[chan].intstatus_ena;
    dw_axi_dmac_update_irq(s);
}

static void dw_axi_dmac_channel_complete(DWAxiDMACState *s, unsigned int chan)
{
    s->chen &= ~BIT(chan);
    s->chan[chan].status = 0;
    dw_axi_dmac_channel_raise(s, chan, DW_AXI_DMAC_IRQ_BLOCK_TRF |
                                       DW_AXI_DMAC_IRQ_DMA_TRF);
}

static void dw_axi_dmac_channel_error(DWAxiDMACState *s, unsigned int chan,
                                      uint32_t irq_bits)
{
    s->chen &= ~BIT(chan);
    s->chan[chan].status = 0;
    dw_axi_dmac_channel_raise(s, chan, irq_bits);
}

static unsigned int dw_axi_dmac_width_bytes(uint32_t ctl_lo, unsigned int pos)
{
    uint32_t width = extract32(ctl_lo, pos, 3);

    if (width > 6) {
        return 0;
    }

    return 1U << width;
}

static MemTxResult dw_axi_dmac_dma_rw(DWAxiDMACState *s, hwaddr addr,
                                      void *buf, hwaddr len, bool is_write)
{
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;

    attrs.unspecified = 0;
    attrs.requester_id = s->requester_id;
    attrs.pid = 0;
    attrs.secure = 0;

    return address_space_rw(&address_space_memory, addr, attrs, buf, len,
                            is_write);
}

static uint32_t dw_axi_dmac_memcpy_chan(DWAxiDMACState *s,
                                        DWAxiDMACChannelState *ch)
{
    uint8_t data[64];
    uint64_t src = ch->sar;
    uint64_t dst = ch->dar;
    uint64_t items = (uint64_t)ch->block_ts + 1;
    unsigned int src_bytes = dw_axi_dmac_width_bytes(ch->ctl_lo,
                                                     DW_AXI_DMAC_CH_CTL_SRC_W_POS);
    unsigned int dst_bytes = dw_axi_dmac_width_bytes(ch->ctl_lo,
                                                     DW_AXI_DMAC_CH_CTL_DST_W_POS);
    unsigned int unit_bytes;
    bool src_noinc;
    bool dst_noinc;
    uint64_t i;
    MemTxResult result;

    if (!src_bytes || !dst_bytes || src_bytes != dst_bytes) {
        return DW_AXI_DMAC_IRQ_INVALID_ERR;
    }

    unit_bytes = src_bytes;
    src_noinc = extract32(ch->ctl_lo, DW_AXI_DMAC_CH_CTL_SRC_INC_POS, 1) ==
                DW_AXI_DMAC_CH_INC_NOINC;
    dst_noinc = extract32(ch->ctl_lo, DW_AXI_DMAC_CH_CTL_DST_INC_POS, 1) ==
                DW_AXI_DMAC_CH_INC_NOINC;

    for (i = 0; i < items; i++) {
        result = dw_axi_dmac_dma_rw(s, src, data, unit_bytes, false);
        if (result != MEMTX_OK) {
            return result == MEMTX_DECODE_ERROR ? DW_AXI_DMAC_IRQ_SRC_DEC_ERR :
                                                  DW_AXI_DMAC_IRQ_INVALID_ERR;
        }

        result = dw_axi_dmac_dma_rw(s, dst, data, unit_bytes, true);
        if (result != MEMTX_OK) {
            return result == MEMTX_DECODE_ERROR ? DW_AXI_DMAC_IRQ_DST_DEC_ERR :
                                                  DW_AXI_DMAC_IRQ_INVALID_ERR;
        }

        if (!src_noinc) {
            src += unit_bytes;
        }
        if (!dst_noinc) {
            dst += unit_bytes;
        }
    }

    ch->sar = src;
    ch->dar = dst;
    return 0;
}

static void dw_axi_dmac_kick_channel(DWAxiDMACState *s, unsigned int chan)
{
    DWAxiDMACChannelState *ch = &s->chan[chan];
    uint32_t error_bits;

    if (!(s->cfg & DW_AXI_DMAC_CFG_EN) || !(s->chen & BIT(chan))) {
        return;
    }

    ch->status = 1;
    ch->intstatus &= ~(DW_AXI_DMAC_IRQ_BLOCK_TRF | DW_AXI_DMAC_IRQ_DMA_TRF);
    dw_axi_dmac_update_irq(s);

    error_bits = dw_axi_dmac_memcpy_chan(s, ch);
    if (!error_bits) {
        dw_axi_dmac_channel_complete(s, chan);
    } else {
        dw_axi_dmac_channel_error(s, chan, error_bits);
    }
}

static void dw_axi_dmac_kick_ready_channels(DWAxiDMACState *s)
{
    int i;

    if (!(s->cfg & DW_AXI_DMAC_CFG_EN)) {
        return;
    }

    for (i = 0; i < DW_AXI_DMAC_NR_CHANS; i++) {
        if (s->chen & BIT(i)) {
            dw_axi_dmac_kick_channel(s, i);
        }
    }
}

static uint32_t dw_axi_dmac_read_u64(uint64_t value, hwaddr addr)
{
    return (addr & 0x4) ? extract64(value, 32, 32) : extract64(value, 0, 32);
}

static void dw_axi_dmac_write_u64(uint64_t *value, hwaddr addr, uint32_t val)
{
    if (addr & 0x4) {
        *value = deposit64(*value, 32, 32, val);
    } else {
        *value = deposit64(*value, 0, 32, val);
    }
}

static uint64_t dw_axi_dmac_channel_read(DWAxiDMACState *s, unsigned int chan,
                                         hwaddr addr)
{
    DWAxiDMACChannelState *ch = &s->chan[chan];

    switch (addr) {
    case DW_AXI_DMAC_CH_SAR:
    case DW_AXI_DMAC_CH_SAR + 4:
        return dw_axi_dmac_read_u64(ch->sar, addr);
    case DW_AXI_DMAC_CH_DAR:
    case DW_AXI_DMAC_CH_DAR + 4:
        return dw_axi_dmac_read_u64(ch->dar, addr);
    case DW_AXI_DMAC_CH_BLOCK_TS:
        return ch->block_ts;
    case DW_AXI_DMAC_CH_CTL:
        return ch->ctl_lo;
    case DW_AXI_DMAC_CH_CTL_H:
        return ch->ctl_hi;
    case DW_AXI_DMAC_CH_CFG:
        return ch->cfg_lo;
    case DW_AXI_DMAC_CH_CFG_H:
        return ch->cfg_hi;
    case DW_AXI_DMAC_CH_LLP:
    case DW_AXI_DMAC_CH_LLP + 4:
        return dw_axi_dmac_read_u64(ch->llp, addr);
    case DW_AXI_DMAC_CH_STATUS:
        return ch->status;
    case DW_AXI_DMAC_CH_SWHSSRC:
        return ch->swhssrc;
    case DW_AXI_DMAC_CH_SWHSDST:
        return ch->swhsdst;
    case DW_AXI_DMAC_CH_BLK_TFR_RESUME:
        return ch->blk_tfr_resume_req;
    case DW_AXI_DMAC_CH_AXI_ID:
        return ch->axi_id;
    case DW_AXI_DMAC_CH_AXI_QOS:
        return ch->axi_qos;
    case DW_AXI_DMAC_CH_SSTAT:
        return ch->sstat;
    case DW_AXI_DMAC_CH_DSTAT:
        return ch->dstat;
    case DW_AXI_DMAC_CH_SSTATAR:
    case DW_AXI_DMAC_CH_SSTATAR + 4:
        return dw_axi_dmac_read_u64(ch->sstatar, addr);
    case DW_AXI_DMAC_CH_DSTATAR:
    case DW_AXI_DMAC_CH_DSTATAR + 4:
        return dw_axi_dmac_read_u64(ch->dstatar, addr);
    case DW_AXI_DMAC_CH_INTSTATUS_ENA:
        return ch->intstatus_ena;
    case DW_AXI_DMAC_CH_INTSTATUS:
        return ch->intstatus;
    case DW_AXI_DMAC_CH_INTSIGNAL_ENA:
        return ch->intsignal_ena;
    default:
        return 0;
    }
}

static void dw_axi_dmac_channel_write(DWAxiDMACState *s, unsigned int chan,
                                      hwaddr addr, uint32_t val)
{
    DWAxiDMACChannelState *ch = &s->chan[chan];

    switch (addr) {
    case DW_AXI_DMAC_CH_SAR:
    case DW_AXI_DMAC_CH_SAR + 4:
        dw_axi_dmac_write_u64(&ch->sar, addr, val);
        break;
    case DW_AXI_DMAC_CH_DAR:
    case DW_AXI_DMAC_CH_DAR + 4:
        dw_axi_dmac_write_u64(&ch->dar, addr, val);
        break;
    case DW_AXI_DMAC_CH_BLOCK_TS:
        ch->block_ts = val;
        break;
    case DW_AXI_DMAC_CH_CTL:
        ch->ctl_lo = val;
        break;
    case DW_AXI_DMAC_CH_CTL_H:
        ch->ctl_hi = val;
        break;
    case DW_AXI_DMAC_CH_CFG:
        ch->cfg_lo = val;
        break;
    case DW_AXI_DMAC_CH_CFG_H:
        ch->cfg_hi = val;
        break;
    case DW_AXI_DMAC_CH_LLP:
    case DW_AXI_DMAC_CH_LLP + 4:
        dw_axi_dmac_write_u64(&ch->llp, addr, val);
        break;
    case DW_AXI_DMAC_CH_SWHSSRC:
        ch->swhssrc = val;
        break;
    case DW_AXI_DMAC_CH_SWHSDST:
        ch->swhsdst = val;
        break;
    case DW_AXI_DMAC_CH_BLK_TFR_RESUME:
        ch->blk_tfr_resume_req = val;
        break;
    case DW_AXI_DMAC_CH_AXI_ID:
        ch->axi_id = val;
        break;
    case DW_AXI_DMAC_CH_AXI_QOS:
        ch->axi_qos = val;
        break;
    case DW_AXI_DMAC_CH_SSTATAR:
    case DW_AXI_DMAC_CH_SSTATAR + 4:
        dw_axi_dmac_write_u64(&ch->sstatar, addr, val);
        break;
    case DW_AXI_DMAC_CH_DSTATAR:
    case DW_AXI_DMAC_CH_DSTATAR + 4:
        dw_axi_dmac_write_u64(&ch->dstatar, addr, val);
        break;
    case DW_AXI_DMAC_CH_INTSTATUS_ENA:
        ch->intstatus_ena = val & DW_AXI_DMAC_CHANNEL_IRQ_MASK;
        break;
    case DW_AXI_DMAC_CH_INTSIGNAL_ENA:
        ch->intsignal_ena = val & DW_AXI_DMAC_CHANNEL_IRQ_MASK;
        dw_axi_dmac_update_irq(s);
        break;
    case DW_AXI_DMAC_CH_INTCLEAR:
        dw_axi_dmac_channel_clear(s, chan, val);
        break;
    default:
        break;
    }
}

static uint64_t dw_axi_dmac_read(void *opaque, hwaddr addr, unsigned int size)
{
    DWAxiDMACState *s = opaque;

    if (addr < DW_AXI_DMAC_COMMON_SIZE) {
        switch (addr) {
        case DW_AXI_DMAC_ID:
            return DW_AXI_DMAC_ID_VALUE;
        case DW_AXI_DMAC_COMPVER:
            return DW_AXI_DMAC_COMPVER_VALUE;
        case DW_AXI_DMAC_CFG:
            return s->cfg;
        case DW_AXI_DMAC_CHEN:
            return (s->cfg & DW_AXI_DMAC_CFG_EN) ? s->chen : 0;
        case DW_AXI_DMAC_INTSTATUS:
            return dw_axi_dmac_common_irq_status(s);
        case DW_AXI_DMAC_COMMON_INTSTATUS:
            return s->common_intstatus;
        case DW_AXI_DMAC_COMMON_INTSTATUS_EN:
            return s->common_intstatus_ena;
        case DW_AXI_DMAC_COMMON_INTSIGNAL_EN:
            return s->common_intsignal_ena;
        case DW_AXI_DMAC_RESET:
            return 0;
        default:
            return 0;
        }
    }

    if (addr >= DW_AXI_DMAC_COMMON_SIZE &&
        addr < DW_AXI_DMAC_COMMON_SIZE +
               DW_AXI_DMAC_NR_CHANS * DW_AXI_DMAC_CHANNEL_STRIDE) {
        unsigned int chan = addr / DW_AXI_DMAC_CHANNEL_STRIDE - 1;
        hwaddr chan_addr = addr % DW_AXI_DMAC_CHANNEL_STRIDE;

        return dw_axi_dmac_channel_read(s, chan, chan_addr);
    }

    return 0;
}

static void dw_axi_dmac_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned int size)
{
    DWAxiDMACState *s = opaque;

    if (addr < DW_AXI_DMAC_COMMON_SIZE) {
        switch (addr) {
        case DW_AXI_DMAC_CFG:
            s->cfg = val & (DW_AXI_DMAC_CFG_EN | DW_AXI_DMAC_CFG_INT_EN);
            if (!(s->cfg & DW_AXI_DMAC_CFG_EN)) {
                int i;

                s->chen = 0;
                for (i = 0; i < DW_AXI_DMAC_NR_CHANS; i++) {
                    s->chan[i].status = 0;
                }
            }
            dw_axi_dmac_update_irq(s);
            dw_axi_dmac_kick_ready_channels(s);
            return;
        case DW_AXI_DMAC_CHEN:
        {
            uint32_t value = val;
            uint32_t we_mask = extract32(value, 8, DW_AXI_DMAC_NR_CHANS) |
                               extract32(value, 16, DW_AXI_DMAC_NR_CHANS);
            uint32_t enable_mask = extract32(value, 0, DW_AXI_DMAC_NR_CHANS);
            int i;

            if (!(s->cfg & DW_AXI_DMAC_CFG_EN)) {
                return;
            }

            for (i = 0; i < DW_AXI_DMAC_NR_CHANS; i++) {
                if (we_mask & BIT(i)) {
                    if (enable_mask & BIT(i)) {
                        s->chen |= BIT(i);
                    } else {
                        s->chen &= ~BIT(i);
                        s->chan[i].status = 0;
                    }
                }
            }
            dw_axi_dmac_kick_ready_channels(s);
            return;
        }
        case DW_AXI_DMAC_COMMON_INTCLEAR:
            dw_axi_dmac_common_clear(s, val & DW_AXI_DMAC_COMMON_IRQ_MASK);
            return;
        case DW_AXI_DMAC_COMMON_INTSTATUS_EN:
            s->common_intstatus_ena = val & DW_AXI_DMAC_COMMON_IRQ_MASK;
            return;
        case DW_AXI_DMAC_COMMON_INTSIGNAL_EN:
            s->common_intsignal_ena = val & DW_AXI_DMAC_COMMON_IRQ_MASK;
            dw_axi_dmac_update_irq(s);
            return;
        case DW_AXI_DMAC_RESET:
            if (val & BIT(0)) {
                device_cold_reset(DEVICE(s));
            }
            return;
        default:
            return;
        }
    }

    if (addr >= DW_AXI_DMAC_COMMON_SIZE &&
        addr < DW_AXI_DMAC_COMMON_SIZE +
               DW_AXI_DMAC_NR_CHANS * DW_AXI_DMAC_CHANNEL_STRIDE) {
        unsigned int chan = addr / DW_AXI_DMAC_CHANNEL_STRIDE - 1;
        hwaddr chan_addr = addr % DW_AXI_DMAC_CHANNEL_STRIDE;

        dw_axi_dmac_channel_write(s, chan, chan_addr, val);
    }
}

static const MemoryRegionOps dw_axi_dmac_ops = {
    .read = dw_axi_dmac_read,
    .write = dw_axi_dmac_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void dw_axi_dmac_realize(DeviceState *dev, Error **errp)
{
    DWAxiDMACState *s = DW_AXI_DMAC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (s->requester_id > UINT16_MAX) {
        error_setg(errp, "requester-id must fit in 16 bits");
        return;
    }

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, OBJECT(s), &dw_axi_dmac_ops, s,
                          TYPE_DW_AXI_DMAC, DW_AXI_DMAC_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void dw_axi_dmac_reset(DeviceState *dev)
{
    DWAxiDMACState *s = DW_AXI_DMAC(dev);

    memset(s->chan, 0, sizeof(s->chan));
    s->cfg = 0;
    s->chen = 0;
    s->common_intstatus = 0;
    s->common_intstatus_ena = DW_AXI_DMAC_COMMON_IRQ_MASK;
    s->common_intsignal_ena = DW_AXI_DMAC_COMMON_IRQ_MASK;
    for (int i = 0; i < DW_AXI_DMAC_NR_CHANS; i++) {
        s->chan[i].intstatus_ena = DW_AXI_DMAC_CHANNEL_IRQ_MASK;
        s->chan[i].intsignal_ena = DW_AXI_DMAC_CHANNEL_IRQ_MASK;
    }
    qemu_set_irq(s->irq, 0);
    s->irq_level = 0;
}

static const VMStateDescription dw_axi_dmac_channel_vmstate = {
    .name = TYPE_DW_AXI_DMAC "/channel",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(sar, DWAxiDMACChannelState),
        VMSTATE_UINT64(dar, DWAxiDMACChannelState),
        VMSTATE_UINT64(llp, DWAxiDMACChannelState),
        VMSTATE_UINT32(block_ts, DWAxiDMACChannelState),
        VMSTATE_UINT32(ctl_lo, DWAxiDMACChannelState),
        VMSTATE_UINT32(ctl_hi, DWAxiDMACChannelState),
        VMSTATE_UINT32(cfg_lo, DWAxiDMACChannelState),
        VMSTATE_UINT32(cfg_hi, DWAxiDMACChannelState),
        VMSTATE_UINT32(status, DWAxiDMACChannelState),
        VMSTATE_UINT32(swhssrc, DWAxiDMACChannelState),
        VMSTATE_UINT32(swhsdst, DWAxiDMACChannelState),
        VMSTATE_UINT32(blk_tfr_resume_req, DWAxiDMACChannelState),
        VMSTATE_UINT32(axi_id, DWAxiDMACChannelState),
        VMSTATE_UINT32(axi_qos, DWAxiDMACChannelState),
        VMSTATE_UINT32(sstat, DWAxiDMACChannelState),
        VMSTATE_UINT32(dstat, DWAxiDMACChannelState),
        VMSTATE_UINT64(sstatar, DWAxiDMACChannelState),
        VMSTATE_UINT64(dstatar, DWAxiDMACChannelState),
        VMSTATE_UINT32(intstatus_ena, DWAxiDMACChannelState),
        VMSTATE_UINT32(intstatus, DWAxiDMACChannelState),
        VMSTATE_UINT32(intsignal_ena, DWAxiDMACChannelState),
        VMSTATE_END_OF_LIST(),
    },
};

static const VMStateDescription dw_axi_dmac_vmstate = {
    .name = TYPE_DW_AXI_DMAC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cfg, DWAxiDMACState),
        VMSTATE_UINT32(chen, DWAxiDMACState),
        VMSTATE_UINT32(common_intstatus, DWAxiDMACState),
        VMSTATE_UINT32(common_intstatus_ena, DWAxiDMACState),
        VMSTATE_UINT32(common_intsignal_ena, DWAxiDMACState),
        VMSTATE_UINT32(requester_id, DWAxiDMACState),
        VMSTATE_UINT8(irq_level, DWAxiDMACState),
        VMSTATE_STRUCT_ARRAY(chan, DWAxiDMACState, DW_AXI_DMAC_NR_CHANS, 1,
                             dw_axi_dmac_channel_vmstate,
                             DWAxiDMACChannelState),
        VMSTATE_END_OF_LIST(),
    },
};

static const Property dw_axi_dmac_properties[] = {
    DEFINE_PROP_UINT32("requester-id", DWAxiDMACState, requester_id, 0x8),
};

static void dw_axi_dmac_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Synopsys DesignWare AXI DMAC";
    dc->realize = dw_axi_dmac_realize;
    device_class_set_legacy_reset(dc, dw_axi_dmac_reset);
    dc->vmsd = &dw_axi_dmac_vmstate;
    device_class_set_props(dc, dw_axi_dmac_properties);
}

static const TypeInfo dw_axi_dmac_info = {
    .name = TYPE_DW_AXI_DMAC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DWAxiDMACState),
    .class_init = dw_axi_dmac_class_init,
};

static void dw_axi_dmac_register_types(void)
{
    type_register_static(&dw_axi_dmac_info);
}

type_init(dw_axi_dmac_register_types)
