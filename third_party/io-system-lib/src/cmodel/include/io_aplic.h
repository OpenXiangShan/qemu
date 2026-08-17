#ifndef IO_APLIC_H
#define IO_APLIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "io_system.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IO_APLIC_DOMAINCFG                0x0000
#define IO_APLIC_DOMAINCFG_RDONLY         0x80000000u
#define IO_APLIC_DOMAINCFG_IE             (1u << 8)
#define IO_APLIC_DOMAINCFG_DM             (1u << 2)
#define IO_APLIC_DOMAINCFG_BE             (1u << 0)

#define IO_APLIC_SOURCECFG_BASE           0x0004
#define IO_APLIC_SOURCECFG_D              (1u << 10)
#define IO_APLIC_SOURCECFG_CHILDIDX_MASK  0x000003ffu
#define IO_APLIC_SOURCECFG_SM_MASK        0x00000007u
#define IO_APLIC_SOURCECFG_SM_INACTIVE    0x0
#define IO_APLIC_SOURCECFG_SM_DETACH      0x1
#define IO_APLIC_SOURCECFG_SM_EDGE_RISE   0x4
#define IO_APLIC_SOURCECFG_SM_EDGE_FALL   0x5
#define IO_APLIC_SOURCECFG_SM_LEVEL_HIGH  0x6
#define IO_APLIC_SOURCECFG_SM_LEVEL_LOW   0x7

#define IO_APLIC_MMSICFGADDR              0x1bc0
#define IO_APLIC_MMSICFGADDRH             0x1bc4
#define IO_APLIC_SMSICFGADDR              0x1bc8
#define IO_APLIC_SMSICFGADDRH             0x1bcc

#define IO_APLIC_xMSICFGADDRH_L           (1u << 31)
#define IO_APLIC_xMSICFGADDRH_HHXS_MASK   0x1fu
#define IO_APLIC_xMSICFGADDRH_HHXS_SHIFT  24
#define IO_APLIC_xMSICFGADDRH_LHXS_MASK   0x7u
#define IO_APLIC_xMSICFGADDRH_LHXS_SHIFT  20
#define IO_APLIC_xMSICFGADDRH_HHXW_MASK   0x7u
#define IO_APLIC_xMSICFGADDRH_HHXW_SHIFT  16
#define IO_APLIC_xMSICFGADDRH_LHXW_MASK   0xfu
#define IO_APLIC_xMSICFGADDRH_LHXW_SHIFT  12
#define IO_APLIC_xMSICFGADDRH_BAPPN_MASK  0xfffu

#define IO_APLIC_xMSICFGADDR_PPN_SHIFT    12

#define IO_APLIC_MMSICFGADDRH_VALID_MASK   \
    (IO_APLIC_xMSICFGADDRH_L | \
     (IO_APLIC_xMSICFGADDRH_HHXS_MASK << IO_APLIC_xMSICFGADDRH_HHXS_SHIFT) | \
     (IO_APLIC_xMSICFGADDRH_LHXS_MASK << IO_APLIC_xMSICFGADDRH_LHXS_SHIFT) | \
     (IO_APLIC_xMSICFGADDRH_HHXW_MASK << IO_APLIC_xMSICFGADDRH_HHXW_SHIFT) | \
     (IO_APLIC_xMSICFGADDRH_LHXW_MASK << IO_APLIC_xMSICFGADDRH_LHXW_SHIFT) | \
     IO_APLIC_xMSICFGADDRH_BAPPN_MASK)

#define IO_APLIC_SMSICFGADDRH_VALID_MASK   \
    ((IO_APLIC_xMSICFGADDRH_LHXS_MASK << IO_APLIC_xMSICFGADDRH_LHXS_SHIFT) | \
     IO_APLIC_xMSICFGADDRH_BAPPN_MASK)

#define IO_APLIC_SETIP_BASE               0x1c00
#define IO_APLIC_SETIPNUM                 0x1cdc

#define IO_APLIC_CLRIP_BASE               0x1d00
#define IO_APLIC_CLRIPNUM                 0x1ddc

#define IO_APLIC_SETIE_BASE               0x1e00
#define IO_APLIC_SETIENUM                 0x1edc

#define IO_APLIC_CLRIE_BASE               0x1f00
#define IO_APLIC_CLRIENUM                 0x1fdc

#define IO_APLIC_SETIPNUM_LE              0x2000
#define IO_APLIC_SETIPNUM_BE              0x2004

#define IO_APLIC_GENMSI                   0x3000

#define IO_APLIC_TARGET_BASE              0x3004
#define IO_APLIC_TARGET_HART_IDX_SHIFT    18
#define IO_APLIC_TARGET_HART_IDX_MASK     0x3fffu
#define IO_APLIC_TARGET_GUEST_IDX_SHIFT   12
#define IO_APLIC_TARGET_GUEST_IDX_MASK    0x3fu
#define IO_APLIC_TARGET_IPRIO_MASK        0xffu
#define IO_APLIC_TARGET_EIID_MASK         0x7ffu

#define IO_APLIC_IDC_BASE                 0x4000
#define IO_APLIC_IDC_SIZE                 32
#define IO_APLIC_IDC_IDELIVERY            0x00
#define IO_APLIC_IDC_IFORCE               0x04
#define IO_APLIC_IDC_ITHRESHOLD           0x08
#define IO_APLIC_IDC_TOPI                 0x18
#define IO_APLIC_IDC_TOPI_ID_SHIFT        16
#define IO_APLIC_IDC_TOPI_ID_MASK         0x3ffu
#define IO_APLIC_IDC_TOPI_PRIO_MASK       0xffu
#define IO_APLIC_IDC_CLAIMI               0x1c

typedef struct IoAplic IoAplic;

typedef struct IoAplicConfig {
    const char *name;
    uint64_t base;
    uint64_t size;
    uint32_t num_sources;
    uint32_t num_harts;
    uint32_t iprio_bits;
    bool msimode;
    bool mmode;
} IoAplicConfig;

IoAplic *io_aplic_create(IoSystem *system, const IoAplicConfig *config,
                         IoAplic *parent);
void io_aplic_destroy(IoAplic *aplic);
IoSystemStatus io_aplic_reset(IoAplic *aplic);
void io_aplic_irq_line_set(IoAplic *aplic, uint32_t source, bool level);

#ifdef __cplusplus
}
#endif

#endif
