#include "io_dwc_dmac.h"
#include "io_cmodel_internal.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define IO_DWC_DMAC_COMMON_SIZE         0x100
#define IO_DWC_DMAC_CHANNEL_STRIDE      0x100
#define IO_DWC_DMAC_LLI_SIZE            64
#define IO_DWC_DMAC_LLI_ADDR_MASK       (~(uint64_t)(IO_DWC_DMAC_LLI_SIZE - 1u))
#define IO_DWC_DMAC_LLI_MAX_CHAIN       1024

typedef struct IoDwcDmacChannel {
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
} IoDwcDmacChannel;

struct IoDwcDmac {
    IoSystem *system;
    IoAplic *irq_parent;
    IoDwcDmacConfig config;
    IoDwcDmacChannel chan[IO_DWC_DMAC_NR_CHANS];
    uint32_t cfg;
    uint32_t chen;
    uint32_t common_intstatus;
    uint32_t common_intstatus_ena;
    uint32_t common_intsignal_ena;
    uint32_t requester_id;
    bool irq_level;
};

typedef struct IoDwcDmacLli {
    uint64_t sar;
    uint64_t dar;
    uint32_t block_ts_lo;
    uint32_t block_ts_hi;
    uint64_t llp;
    uint32_t ctl_lo;
    uint32_t ctl_hi;
    uint32_t sstat;
    uint32_t dstat;
    uint32_t status_lo;
    uint32_t status_hi;
} IoDwcDmacLli;

static uint32_t io_dwc_dmac_load_u32(const void *data)
{
    const uint8_t *bytes = data;

    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint64_t io_dwc_dmac_load_u64_le(const void *data)
{
    const uint8_t *bytes = data;

    return (uint64_t)io_dwc_dmac_load_u32(bytes) |
           ((uint64_t)io_dwc_dmac_load_u32(bytes + 4) << 32);
}

static void io_dwc_dmac_store_u32(void *data, uint32_t value)
{
    uint8_t *bytes = data;

    bytes[0] = (uint8_t)(value);
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void io_dwc_dmac_store_u64_le(void *data, uint64_t value)
{
    uint8_t *bytes = data;

    io_dwc_dmac_store_u32(bytes, (uint32_t)value);
    io_dwc_dmac_store_u32(bytes + 4, (uint32_t)(value >> 32));
}

static uint64_t io_dwc_dmac_load_u64(const IoDwcDmac *dmac, uint64_t value,
                                     uint64_t addr)
{
    (void)dmac;

    return (addr & 0x4) ? (value >> 32) : (value & 0xffffffffu);
}

static void io_dwc_dmac_store_u64(uint64_t *value, uint64_t addr,
                                  uint32_t part)
{
    if (addr & 0x4) {
        *value = (*value & 0xffffffffu) | ((uint64_t)part << 32);
    } else {
        *value = (*value & ~0xffffffffULL) | (uint64_t)part;
    }
}

static void io_dwc_dmac_update_irq(IoDwcDmac *dmac)
{
    bool level = false;

    if (dmac->cfg & IO_DWC_DMAC_CFG_INT_EN) {
        for (size_t i = 0; i < IO_DWC_DMAC_NR_CHANS; i++) {
            if (dmac->chan[i].intstatus & dmac->chan[i].intsignal_ena) {
                level = true;
                break;
            }
        }

        if (!level && (dmac->common_intstatus & dmac->common_intsignal_ena)) {
            level = true;
        }
    }

    if (level != dmac->irq_level) {
        dmac->irq_level = level;
        if (dmac->irq_parent) {
            io_aplic_irq_line_set(dmac->irq_parent, dmac->config.irq, level);
        }
    }
}

static void io_dwc_dmac_channel_clear(IoDwcDmac *dmac, unsigned int chan,
                                      uint32_t mask)
{
    dmac->chan[chan].intstatus &= ~mask;
    io_dwc_dmac_update_irq(dmac);
}

static void io_dwc_dmac_common_clear(IoDwcDmac *dmac, uint32_t mask)
{
    dmac->common_intstatus &= ~mask;
    io_dwc_dmac_update_irq(dmac);
}

static void io_dwc_dmac_channel_raise(IoDwcDmac *dmac, unsigned int chan,
                                      uint32_t mask)
{
    dmac->chan[chan].intstatus |= mask & dmac->chan[chan].intstatus_ena;
    io_dwc_dmac_update_irq(dmac);
}

static unsigned int io_dwc_dmac_width_bytes(uint32_t ctl_lo, unsigned int pos)
{
    uint32_t width = (ctl_lo >> pos) & 0x7u;

    if (width > 6) {
        return 0;
    }

    return 1u << width;
}

static IoSystemStatus io_dwc_dmac_dma_rw(IoDwcDmac *dmac, uint64_t addr,
                                         void *buf, size_t len, bool write)
{
    IoSystemDmaAttrs attrs = {
        .requester_id = dmac->requester_id,
    };

    if (write) {
        return io_system_dma_write(dmac->system, &attrs, addr, buf, len);
    }

    return io_system_dma_read(dmac->system, &attrs, addr, buf, len);
}

static uint64_t io_dwc_dmac_lli_addr(uint64_t llp)
{
    return llp & IO_DWC_DMAC_LLI_ADDR_MASK;
}

static uint32_t io_dwc_dmac_fetch_lli(IoDwcDmac *dmac, uint64_t llp,
                                      IoDwcDmacLli *lli)
{
    uint8_t raw[IO_DWC_DMAC_LLI_SIZE];
    uint64_t addr = io_dwc_dmac_lli_addr(llp);
    IoSystemStatus status;

    if (!addr || !lli) {
        return IO_DWC_DMAC_IRQ_INVALID_ERR;
    }

    status = io_dwc_dmac_dma_rw(dmac, addr, raw, sizeof(raw), false);
    if (status != IO_SYSTEM_OK) {
        return IO_DWC_DMAC_IRQ_SRC_DEC_ERR;
    }

    memset(lli, 0, sizeof(*lli));
    lli->sar = io_dwc_dmac_load_u64_le(raw + 0x00);
    lli->dar = io_dwc_dmac_load_u64_le(raw + 0x08);
    lli->block_ts_lo = io_dwc_dmac_load_u32(raw + 0x10);
    lli->block_ts_hi = io_dwc_dmac_load_u32(raw + 0x14);
    lli->llp = io_dwc_dmac_load_u64_le(raw + 0x18);
    lli->ctl_lo = io_dwc_dmac_load_u32(raw + 0x20);
    lli->ctl_hi = io_dwc_dmac_load_u32(raw + 0x24);
    lli->sstat = io_dwc_dmac_load_u32(raw + 0x28);
    lli->dstat = io_dwc_dmac_load_u32(raw + 0x2c);
    lli->status_lo = io_dwc_dmac_load_u32(raw + 0x30);
    lli->status_hi = io_dwc_dmac_load_u32(raw + 0x34);

    return 0;
}

static uint32_t io_dwc_dmac_memcpy_chan(IoDwcDmac *dmac, IoDwcDmacChannel *ch)
{
    uint8_t data[64];
    uint64_t src = ch->sar;
    uint64_t dst = ch->dar;
    uint64_t items = (uint64_t)ch->block_ts + 1u;
    unsigned int src_bytes = io_dwc_dmac_width_bytes(ch->ctl_lo,
                                                      IO_DWC_DMAC_CH_CTL_L_SRC_WIDTH_POS);
    unsigned int dst_bytes = io_dwc_dmac_width_bytes(ch->ctl_lo,
                                                      IO_DWC_DMAC_CH_CTL_L_DST_WIDTH_POS);
    bool src_noinc;
    bool dst_noinc;

    if (!src_bytes || !dst_bytes || src_bytes != dst_bytes ||
        src_bytes > sizeof(data)) {
        return IO_DWC_DMAC_IRQ_INVALID_ERR;
    }

    src_noinc = ((ch->ctl_lo >> IO_DWC_DMAC_CH_CTL_L_SRC_INC_POS) & 0x1u) ==
                IO_DWC_DMAC_CH_CTL_L_NOINC;
    dst_noinc = ((ch->ctl_lo >> IO_DWC_DMAC_CH_CTL_L_DST_INC_POS) & 0x1u) ==
                IO_DWC_DMAC_CH_CTL_L_NOINC;

    for (uint64_t i = 0; i < items; i++) {
        IoSystemStatus status;

        status = io_dwc_dmac_dma_rw(dmac, src, data, src_bytes, false);
        if (status != IO_SYSTEM_OK) {
            return IO_DWC_DMAC_IRQ_SRC_DEC_ERR;
        }

        status = io_dwc_dmac_dma_rw(dmac, dst, data, dst_bytes, true);
        if (status != IO_SYSTEM_OK) {
            return IO_DWC_DMAC_IRQ_DST_DEC_ERR;
        }

        if (!src_noinc) {
            src += src_bytes;
        }
        if (!dst_noinc) {
            dst += dst_bytes;
        }
    }

    ch->sar = src;
    ch->dar = dst;
    return 0;
}

static uint32_t io_dwc_dmac_memcpy_lli_chain(IoDwcDmac *dmac,
                                             IoDwcDmacChannel *ch)
{
    uint64_t llp = ch->llp;

    for (unsigned int i = 0; i < IO_DWC_DMAC_LLI_MAX_CHAIN; i++) {
        IoDwcDmacLli lli;
        uint32_t error_bits;
        bool last;
        uint64_t next_llp;

        error_bits = io_dwc_dmac_fetch_lli(dmac, llp, &lli);
        if (error_bits) {
            return error_bits;
        }

        if (!(lli.ctl_hi & IO_DWC_DMAC_CH_CTL_H_LLI_VALID)) {
            return IO_DWC_DMAC_IRQ_INVALID_ERR;
        }

        if (lli.block_ts_hi) {
            return IO_DWC_DMAC_IRQ_INVALID_ERR;
        }

        ch->sar = lli.sar;
        ch->dar = lli.dar;
        ch->block_ts = lli.block_ts_lo;
        ch->llp = lli.llp;
        ch->ctl_lo = lli.ctl_lo;
        ch->ctl_hi = lli.ctl_hi;
        ch->sstat = lli.sstat;
        ch->dstat = lli.dstat;

        error_bits = io_dwc_dmac_memcpy_chan(dmac, ch);
        if (error_bits) {
            return error_bits;
        }

        last = (lli.ctl_hi & IO_DWC_DMAC_CH_CTL_H_LLI_LAST) != 0;
        next_llp = io_dwc_dmac_lli_addr(lli.llp);
        if (last || !next_llp) {
            ch->llp = lli.llp;
            return 0;
        }

        if (next_llp == io_dwc_dmac_lli_addr(llp)) {
            return IO_DWC_DMAC_IRQ_INVALID_ERR;
        }
        llp = lli.llp;
    }

    return IO_DWC_DMAC_IRQ_INVALID_ERR;
}

static void io_dwc_dmac_channel_complete(IoDwcDmac *dmac, unsigned int chan)
{
    dmac->chen &= ~((uint32_t)1u << chan);
    dmac->chan[chan].status = 0;
    io_dwc_dmac_channel_raise(dmac, chan,
                              IO_DWC_DMAC_IRQ_BLOCK_TRF |
                              IO_DWC_DMAC_IRQ_DMA_TRF);
}

static void io_dwc_dmac_channel_error(IoDwcDmac *dmac, unsigned int chan,
                                      uint32_t irq_bits)
{
    dmac->chen &= ~((uint32_t)1u << chan);
    dmac->chan[chan].status = 0;
    io_dwc_dmac_channel_raise(dmac, chan, irq_bits);
}

static void io_dwc_dmac_kick_channel(IoDwcDmac *dmac, unsigned int chan)
{
    IoDwcDmacChannel *ch = &dmac->chan[chan];
    uint32_t error_bits;

    if (!(dmac->cfg & IO_DWC_DMAC_CFG_EN) || !(dmac->chen & ((uint32_t)1u << chan))) {
        return;
    }

    ch->status = 1;
    ch->intstatus &= ~(IO_DWC_DMAC_IRQ_BLOCK_TRF | IO_DWC_DMAC_IRQ_DMA_TRF);
    io_dwc_dmac_update_irq(dmac);

    if (io_dwc_dmac_lli_addr(ch->llp)) {
        error_bits = io_dwc_dmac_memcpy_lli_chain(dmac, ch);
    } else {
        error_bits = io_dwc_dmac_memcpy_chan(dmac, ch);
    }
    if (!error_bits) {
        io_dwc_dmac_channel_complete(dmac, chan);
    } else {
        io_dwc_dmac_channel_error(dmac, chan, error_bits);
    }
}

static void io_dwc_dmac_kick_ready_channels(IoDwcDmac *dmac)
{
    for (unsigned int i = 0; i < IO_DWC_DMAC_NR_CHANS; i++) {
        if (dmac->chen & ((uint32_t)1u << i)) {
            io_dwc_dmac_kick_channel(dmac, i);
        }
    }
}

static uint32_t io_dwc_dmac_read_u32_reg(const IoDwcDmac *dmac, uint64_t addr)
{
    switch (addr) {
    case IO_DWC_DMAC_ID:
        return 0x44574158u;
    case IO_DWC_DMAC_COMPVER:
        return 0x31303161u;
    case IO_DWC_DMAC_CFG:
        return dmac->cfg;
    case IO_DWC_DMAC_CHEN:
        return (dmac->cfg & IO_DWC_DMAC_CFG_EN) ? dmac->chen : 0u;
    case IO_DWC_DMAC_INTSTATUS:
    {
        uint32_t status = 0;

        for (size_t i = 0; i < IO_DWC_DMAC_NR_CHANS; i++) {
            if (dmac->chan[i].intstatus) {
                status |= (uint32_t)1u << i;
            }
        }
        if (dmac->common_intstatus) {
            status |= (uint32_t)1u << 16;
        }
        return status;
    }
    case IO_DWC_DMAC_COMMON_INTSTATUS:
        return dmac->common_intstatus;
    case IO_DWC_DMAC_COMMON_INTSTATUS_ENA:
        return dmac->common_intstatus_ena;
    case IO_DWC_DMAC_COMMON_INTSIGNAL_ENA:
        return dmac->common_intsignal_ena;
    case IO_DWC_DMAC_RESET:
        return 0;
    default:
        return 0;
    }
}

static uint32_t io_dwc_dmac_channel_read(const IoDwcDmac *dmac,
                                         unsigned int chan,
                                         uint64_t addr)
{
    const IoDwcDmacChannel *ch = &dmac->chan[chan];

    switch (addr) {
    case IO_DWC_DMAC_CH_SAR:
    case IO_DWC_DMAC_CH_SAR + 4:
        return (uint32_t)io_dwc_dmac_load_u64(dmac, ch->sar, addr);
    case IO_DWC_DMAC_CH_DAR:
    case IO_DWC_DMAC_CH_DAR + 4:
        return (uint32_t)io_dwc_dmac_load_u64(dmac, ch->dar, addr);
    case IO_DWC_DMAC_CH_BLOCK_TS:
        return ch->block_ts;
    case IO_DWC_DMAC_CH_CTL:
        return ch->ctl_lo;
    case IO_DWC_DMAC_CH_CTL_H:
        return ch->ctl_hi;
    case IO_DWC_DMAC_CH_CFG:
        return ch->cfg_lo;
    case IO_DWC_DMAC_CH_CFG_H:
        return ch->cfg_hi;
    case IO_DWC_DMAC_CH_LLP:
    case IO_DWC_DMAC_CH_LLP + 4:
        return (uint32_t)io_dwc_dmac_load_u64(dmac, ch->llp, addr);
    case IO_DWC_DMAC_CH_STATUS:
        return ch->status;
    case IO_DWC_DMAC_CH_SWHSSRC:
        return ch->swhssrc;
    case IO_DWC_DMAC_CH_SWHSDST:
        return ch->swhsdst;
    case IO_DWC_DMAC_CH_BLK_TFR_RESUME:
        return ch->blk_tfr_resume_req;
    case IO_DWC_DMAC_CH_AXI_ID:
        return ch->axi_id;
    case IO_DWC_DMAC_CH_AXI_QOS:
        return ch->axi_qos;
    case IO_DWC_DMAC_CH_SSTAT:
        return ch->sstat;
    case IO_DWC_DMAC_CH_DSTAT:
        return ch->dstat;
    case IO_DWC_DMAC_CH_SSTATAR:
    case IO_DWC_DMAC_CH_SSTATAR + 4:
        return (uint32_t)io_dwc_dmac_load_u64(dmac, ch->sstatar, addr);
    case IO_DWC_DMAC_CH_DSTATAR:
    case IO_DWC_DMAC_CH_DSTATAR + 4:
        return (uint32_t)io_dwc_dmac_load_u64(dmac, ch->dstatar, addr);
    case IO_DWC_DMAC_CH_INTSTATUS_ENA:
        return ch->intstatus_ena;
    case IO_DWC_DMAC_CH_INTSTATUS:
        return ch->intstatus;
    case IO_DWC_DMAC_CH_INTSIGNAL_ENA:
        return ch->intsignal_ena;
    default:
        return 0;
    }
}

static void io_dwc_dmac_channel_write(IoDwcDmac *dmac, unsigned int chan,
                                      uint64_t addr, uint32_t value)
{
    IoDwcDmacChannel *ch = &dmac->chan[chan];

    switch (addr) {
    case IO_DWC_DMAC_CH_SAR:
    case IO_DWC_DMAC_CH_SAR + 4:
        io_dwc_dmac_store_u64(&ch->sar, addr, value);
        break;
    case IO_DWC_DMAC_CH_DAR:
    case IO_DWC_DMAC_CH_DAR + 4:
        io_dwc_dmac_store_u64(&ch->dar, addr, value);
        break;
    case IO_DWC_DMAC_CH_BLOCK_TS:
        ch->block_ts = value;
        break;
    case IO_DWC_DMAC_CH_CTL:
        ch->ctl_lo = value;
        break;
    case IO_DWC_DMAC_CH_CTL_H:
        ch->ctl_hi = value;
        break;
    case IO_DWC_DMAC_CH_CFG:
        ch->cfg_lo = value;
        break;
    case IO_DWC_DMAC_CH_CFG_H:
        ch->cfg_hi = value;
        break;
    case IO_DWC_DMAC_CH_LLP:
    case IO_DWC_DMAC_CH_LLP + 4:
        io_dwc_dmac_store_u64(&ch->llp, addr, value);
        break;
    case IO_DWC_DMAC_CH_SWHSSRC:
        ch->swhssrc = value;
        break;
    case IO_DWC_DMAC_CH_SWHSDST:
        ch->swhsdst = value;
        break;
    case IO_DWC_DMAC_CH_BLK_TFR_RESUME:
        ch->blk_tfr_resume_req = value;
        break;
    case IO_DWC_DMAC_CH_AXI_ID:
        ch->axi_id = value;
        break;
    case IO_DWC_DMAC_CH_AXI_QOS:
        ch->axi_qos = value;
        break;
    case IO_DWC_DMAC_CH_SSTATAR:
    case IO_DWC_DMAC_CH_SSTATAR + 4:
        io_dwc_dmac_store_u64(&ch->sstatar, addr, value);
        break;
    case IO_DWC_DMAC_CH_DSTATAR:
    case IO_DWC_DMAC_CH_DSTATAR + 4:
        io_dwc_dmac_store_u64(&ch->dstatar, addr, value);
        break;
    case IO_DWC_DMAC_CH_INTSTATUS_ENA:
        ch->intstatus_ena = value & IO_DWC_DMAC_CHANNEL_IRQ_MASK;
        break;
    case IO_DWC_DMAC_CH_INTSIGNAL_ENA:
        ch->intsignal_ena = value & IO_DWC_DMAC_CHANNEL_IRQ_MASK;
        io_dwc_dmac_update_irq(dmac);
        break;
    case IO_DWC_DMAC_CH_INTCLEAR:
        io_dwc_dmac_channel_clear(dmac, chan,
                                  value & IO_DWC_DMAC_CHANNEL_IRQ_MASK);
        break;
    default:
        break;
    }
}

static IoSystemStatus io_dwc_dmac_read_u32_at(IoDwcDmac *dmac, uint64_t addr,
                                              uint32_t *value)
{
    uint64_t offset;

    if (!dmac || !value || (addr & 0x3)) {
        return IO_SYSTEM_ERR_INVALID;
    }
    if (addr < dmac->config.base || addr >= dmac->config.base + dmac->config.size) {
        return IO_SYSTEM_ERR_UNMAPPED;
    }

    offset = addr - dmac->config.base;
    if (offset < IO_DWC_DMAC_COMMON_SIZE) {
        *value = io_dwc_dmac_read_u32_reg(dmac, offset);
    } else {
        unsigned int chan = (unsigned int)(offset / IO_DWC_DMAC_CHANNEL_STRIDE) - 1u;
        uint64_t chan_addr = offset % IO_DWC_DMAC_CHANNEL_STRIDE;

        if (chan >= IO_DWC_DMAC_NR_CHANS) {
            return IO_SYSTEM_ERR_UNMAPPED;
        }
        *value = io_dwc_dmac_channel_read(dmac, chan, chan_addr);
    }

    return IO_SYSTEM_OK;
}

static IoSystemStatus io_dwc_dmac_read(void *opaque, uint64_t addr,
                                       void *data, size_t size)
{
    IoDwcDmac *dmac = opaque;
    uint32_t low;
    IoSystemStatus status;

    if (!dmac || !data || (addr & 0x3) ||
        (size != sizeof(uint32_t) && size != sizeof(uint64_t))) {
        return IO_SYSTEM_ERR_INVALID;
    }

    status = io_dwc_dmac_read_u32_at(dmac, addr, &low);
    if (status != IO_SYSTEM_OK) {
        return status;
    }
    if (size == sizeof(uint32_t)) {
        io_dwc_dmac_store_u32(data, low);
    } else {
        uint32_t high;

        status = io_dwc_dmac_read_u32_at(dmac, addr + 4, &high);
        if (status != IO_SYSTEM_OK) {
            return status;
        }
        io_dwc_dmac_store_u64_le(data, (uint64_t)low | ((uint64_t)high << 32));
    }

    return IO_SYSTEM_OK;
}

static IoSystemStatus io_dwc_dmac_write_u32_at(IoDwcDmac *dmac, uint64_t addr,
                                               uint32_t value)
{
    uint64_t offset;

    if (!dmac || (addr & 0x3)) {
        return IO_SYSTEM_ERR_INVALID;
    }
    if (addr < dmac->config.base || addr >= dmac->config.base + dmac->config.size) {
        return IO_SYSTEM_ERR_UNMAPPED;
    }

    offset = addr - dmac->config.base;
    if (offset < IO_DWC_DMAC_COMMON_SIZE) {
        switch (offset) {
        case IO_DWC_DMAC_CFG:
            dmac->cfg = value & (IO_DWC_DMAC_CFG_EN | IO_DWC_DMAC_CFG_INT_EN);
            if (!(dmac->cfg & IO_DWC_DMAC_CFG_EN)) {
                for (size_t i = 0; i < IO_DWC_DMAC_NR_CHANS; i++) {
                    dmac->chan[i].status = 0;
                }
                dmac->chen = 0;
            }
            io_dwc_dmac_update_irq(dmac);
            io_dwc_dmac_kick_ready_channels(dmac);
            break;
        case IO_DWC_DMAC_CHEN:
        {
            uint32_t mask = (uint32_t)((1u << IO_DWC_DMAC_NR_CHANS) - 1u);
            uint32_t we_mask = ((value >> IO_DWC_DMAC_CHEN_WE_SHIFT) & mask) |
                               ((value >> IO_DWC_DMAC_CHEN_WE_SHIFT2) & mask);
            uint32_t enable_mask = value & mask;

            if (!(dmac->cfg & IO_DWC_DMAC_CFG_EN)) {
                break;
            }
            for (size_t i = 0; i < IO_DWC_DMAC_NR_CHANS; i++) {
                if (we_mask & ((uint32_t)1u << i)) {
                    if (enable_mask & ((uint32_t)1u << i)) {
                        dmac->chen |= (uint32_t)1u << i;
                    } else {
                        dmac->chen &= ~((uint32_t)1u << i);
                        dmac->chan[i].status = 0;
                    }
                }
            }
            io_dwc_dmac_kick_ready_channels(dmac);
            break;
        }
        case IO_DWC_DMAC_COMMON_INTCLEAR:
            io_dwc_dmac_common_clear(dmac,
                                     value & IO_DWC_DMAC_COMMON_IRQ_MASK);
            break;
        case IO_DWC_DMAC_COMMON_INTSTATUS_ENA:
            dmac->common_intstatus_ena = value & IO_DWC_DMAC_COMMON_IRQ_MASK;
            break;
        case IO_DWC_DMAC_COMMON_INTSIGNAL_ENA:
            dmac->common_intsignal_ena = value & IO_DWC_DMAC_COMMON_IRQ_MASK;
            io_dwc_dmac_update_irq(dmac);
            break;
        case IO_DWC_DMAC_RESET:
            if (value & 1u) {
                io_dwc_dmac_reset(dmac);
            }
            break;
        default:
            break;
        }
    } else {
        unsigned int chan = (unsigned int)(offset / IO_DWC_DMAC_CHANNEL_STRIDE) - 1u;
        uint64_t chan_addr = offset % IO_DWC_DMAC_CHANNEL_STRIDE;

        if (chan >= IO_DWC_DMAC_NR_CHANS) {
            return IO_SYSTEM_ERR_UNMAPPED;
        }
        io_dwc_dmac_channel_write(dmac, chan, chan_addr, value);
    }

    return IO_SYSTEM_OK;
}

static IoSystemStatus io_dwc_dmac_write(void *opaque, uint64_t addr,
                                        const void *data, size_t size)
{
    IoDwcDmac *dmac = opaque;
    IoSystemStatus status;

    if (!dmac || !data || (addr & 0x3) ||
        (size != sizeof(uint32_t) && size != sizeof(uint64_t))) {
        return IO_SYSTEM_ERR_INVALID;
    }

    status = io_dwc_dmac_write_u32_at(dmac, addr, io_dwc_dmac_load_u32(data));
    if (status != IO_SYSTEM_OK || size == sizeof(uint32_t)) {
        return status;
    }

    return io_dwc_dmac_write_u32_at(dmac, addr + 4,
                                    io_dwc_dmac_load_u32((const uint8_t *)data + 4));
}

static void io_dwc_dmac_reset_state(IoDwcDmac *dmac)
{
    memset(dmac->chan, 0, sizeof(dmac->chan));
    dmac->cfg = 0;
    dmac->chen = 0;
    dmac->common_intstatus = 0;
    dmac->common_intstatus_ena = IO_DWC_DMAC_COMMON_IRQ_MASK;
    dmac->common_intsignal_ena = IO_DWC_DMAC_COMMON_IRQ_MASK;
    dmac->irq_level = false;

    for (size_t i = 0; i < IO_DWC_DMAC_NR_CHANS; i++) {
        dmac->chan[i].intstatus_ena = IO_DWC_DMAC_CHANNEL_IRQ_MASK;
        dmac->chan[i].intsignal_ena = IO_DWC_DMAC_CHANNEL_IRQ_MASK;
    }
}

IoSystemStatus io_dwc_dmac_reset(IoDwcDmac *dmac)
{
    if (!dmac) {
        return IO_SYSTEM_ERR_INVALID;
    }

    if (dmac->irq_level && dmac->irq_parent) {
        io_aplic_irq_line_set(dmac->irq_parent, dmac->config.irq, false);
    }
    io_dwc_dmac_reset_state(dmac);
    io_dwc_dmac_update_irq(dmac);
    return IO_SYSTEM_OK;
}

IoDwcDmac *io_dwc_dmac_create(IoSystem *system,
                              const IoDwcDmacConfig *config,
                              IoAplic *irq_parent)
{
    IoDwcDmac *dmac;
    const char *name;

    if (!system || !config || !config->base || !config->size || !config->irq) {
        return NULL;
    }
    if (config->requester_id > 0xffffu) {
        return NULL;
    }

    dmac = calloc(1, sizeof(*dmac));
    if (!dmac) {
        return NULL;
    }

    dmac->system = system;
    dmac->irq_parent = irq_parent;
    dmac->config = *config;
    dmac->requester_id = config->requester_id;
    io_dwc_dmac_reset_state(dmac);

    name = config->name ? config->name : "dw-axi-dmac";
    if (io_cmodel_register_mmio_window(system, name, config->base,
                                       config->size, io_dwc_dmac_read,
                                       io_dwc_dmac_write, dmac) !=
        IO_SYSTEM_OK) {
        free(dmac);
        return NULL;
    }

    return dmac;
}

void io_dwc_dmac_destroy(IoDwcDmac *dmac)
{
    if (!dmac) {
        return;
    }

    if (dmac->irq_level && dmac->irq_parent) {
        io_aplic_irq_line_set(dmac->irq_parent, dmac->config.irq, false);
    }
    free(dmac);
}
