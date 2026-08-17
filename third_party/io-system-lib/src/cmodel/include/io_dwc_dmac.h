#ifndef IO_DWC_DMAC_H
#define IO_DWC_DMAC_H

#include <stdint.h>

#include "io_aplic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IO_DWC_DMAC_MMIO_SIZE              0x1000
#define IO_DWC_DMAC_NR_CHANS               2

#define IO_DWC_DMAC_ID                     0x000
#define IO_DWC_DMAC_COMPVER                0x008
#define IO_DWC_DMAC_CFG                    0x010
#define IO_DWC_DMAC_CHEN                   0x018
#define IO_DWC_DMAC_INTSTATUS              0x030
#define IO_DWC_DMAC_COMMON_INTCLEAR        0x038
#define IO_DWC_DMAC_COMMON_INTSTATUS_ENA   0x040
#define IO_DWC_DMAC_COMMON_INTSIGNAL_ENA   0x048
#define IO_DWC_DMAC_COMMON_INTSTATUS       0x050
#define IO_DWC_DMAC_RESET                  0x058

#define IO_DWC_DMAC_CH_SAR                 0x000
#define IO_DWC_DMAC_CH_DAR                 0x008
#define IO_DWC_DMAC_CH_BLOCK_TS            0x010
#define IO_DWC_DMAC_CH_CTL                 0x018
#define IO_DWC_DMAC_CH_CTL_H               0x01c
#define IO_DWC_DMAC_CH_CFG                 0x020
#define IO_DWC_DMAC_CH_CFG_H               0x024
#define IO_DWC_DMAC_CH_LLP                 0x028
#define IO_DWC_DMAC_CH_STATUS              0x030
#define IO_DWC_DMAC_CH_SWHSSRC             0x038
#define IO_DWC_DMAC_CH_SWHSDST             0x040
#define IO_DWC_DMAC_CH_BLK_TFR_RESUME      0x048
#define IO_DWC_DMAC_CH_AXI_ID              0x050
#define IO_DWC_DMAC_CH_AXI_QOS             0x058
#define IO_DWC_DMAC_CH_SSTAT               0x060
#define IO_DWC_DMAC_CH_DSTAT               0x068
#define IO_DWC_DMAC_CH_SSTATAR             0x070
#define IO_DWC_DMAC_CH_DSTATAR             0x078
#define IO_DWC_DMAC_CH_INTSTATUS_ENA       0x080
#define IO_DWC_DMAC_CH_INTSTATUS           0x088
#define IO_DWC_DMAC_CH_INTSIGNAL_ENA       0x090
#define IO_DWC_DMAC_CH_INTCLEAR            0x098

#define IO_DWC_DMAC_CFG_EN                 (1u << 0)
#define IO_DWC_DMAC_CFG_INT_EN             (1u << 1)

#define IO_DWC_DMAC_CHEN_EN_SHIFT          0
#define IO_DWC_DMAC_CHEN_WE_SHIFT          8
#define IO_DWC_DMAC_CHEN_WE_SHIFT2         16

#define IO_DWC_DMAC_CH_CTL_H_LLI_LAST      (1u << 30)
#define IO_DWC_DMAC_CH_CTL_H_LLI_VALID     (1u << 31)

#define IO_DWC_DMAC_CH_CTL_L_DST_MSIZE_POS 18
#define IO_DWC_DMAC_CH_CTL_L_SRC_MSIZE_POS 14
#define IO_DWC_DMAC_CH_CTL_L_DST_WIDTH_POS 11
#define IO_DWC_DMAC_CH_CTL_L_SRC_WIDTH_POS 8
#define IO_DWC_DMAC_CH_CTL_L_DST_INC_POS   6
#define IO_DWC_DMAC_CH_CTL_L_SRC_INC_POS   4

#define IO_DWC_DMAC_CH_CTL_L_INC           0
#define IO_DWC_DMAC_CH_CTL_L_NOINC         1

#define IO_DWC_DMAC_IRQ_BLOCK_TRF          (1u << 0)
#define IO_DWC_DMAC_IRQ_DMA_TRF            (1u << 1)
#define IO_DWC_DMAC_IRQ_SRC_DEC_ERR        (1u << 5)
#define IO_DWC_DMAC_IRQ_DST_DEC_ERR        (1u << 6)
#define IO_DWC_DMAC_IRQ_INVALID_ERR        (1u << 13)
#define IO_DWC_DMAC_CHANNEL_IRQ_MASK       (IO_DWC_DMAC_IRQ_BLOCK_TRF | \
                                            IO_DWC_DMAC_IRQ_DMA_TRF | \
                                            IO_DWC_DMAC_IRQ_SRC_DEC_ERR | \
                                            IO_DWC_DMAC_IRQ_DST_DEC_ERR | \
                                            IO_DWC_DMAC_IRQ_INVALID_ERR)
#define IO_DWC_DMAC_COMMON_IRQ_MASK        ((1u << 0) | (1u << 1) | \
                                            (1u << 2) | (1u << 3) | \
                                            (1u << 8))

typedef struct IoDwcDmac IoDwcDmac;

typedef struct IoDwcDmacConfig {
    const char *name;
    uint64_t base;
    uint64_t size;
    uint32_t irq;
    uint32_t requester_id;
} IoDwcDmacConfig;

IoDwcDmac *io_dwc_dmac_create(IoSystem *system,
                              const IoDwcDmacConfig *config,
                              IoAplic *irq_parent);
void io_dwc_dmac_destroy(IoDwcDmac *dmac);
IoSystemStatus io_dwc_dmac_reset(IoDwcDmac *dmac);

#ifdef __cplusplus
}
#endif

#endif
