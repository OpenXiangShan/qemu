#include "io_aplic.h"
#include "io_cmodel_internal.h"

#include <stdlib.h>
#include <string.h>

#define IO_APLIC_MAX_CHILDREN 16

#define IO_APLIC_ISTATE_PENDING           (1u << 0)
#define IO_APLIC_ISTATE_ENABLED           (1u << 1)
#define IO_APLIC_ISTATE_ENPEND            (IO_APLIC_ISTATE_ENABLED | \
                                          IO_APLIC_ISTATE_PENDING)
#define IO_APLIC_ISTATE_INPUT             (1u << 8)

struct IoAplic {
    IoSystem *system;
    IoAplicConfig config;
    IoAplic *parent;
    uint16_t num_children;
    bool msimode;
    bool mmode;

    uint32_t num_irqs;
    uint32_t bitfield_words;
    uint32_t iprio_mask;

    uint32_t domaincfg;
    uint32_t mmsicfgaddr;
    uint32_t mmsicfgaddrH;
    uint32_t smsicfgaddr;
    uint32_t smsicfgaddrH;
    uint32_t genmsi;

    uint32_t *sourcecfg;
    uint32_t *state;
    uint32_t *target;
    uint32_t *idelivery;
    uint32_t *iforce;
    uint32_t *ithreshold;
};

static uint32_t io_aplic_load_u32(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint32_t value = 0;

    for (size_t i = 0; i < size && i < sizeof(value); i++) {
        value |= (uint32_t)bytes[i] << (8 * i);
    }

    return value;
}

static void io_aplic_store_u32(void *data, uint32_t value, size_t size)
{
    uint8_t *bytes = data;

    for (size_t i = 0; i < size && i < sizeof(value); i++) {
        bytes[i] = (uint8_t)(value >> (8 * i));
    }
}

static IoAplic *io_aplic_smsi_owner(IoAplic *aplic)
{
    if (aplic->mmode || !aplic->parent) {
        return aplic;
    }

    return aplic->parent;
}

static bool io_aplic_irq_rectified_val(IoAplic *aplic, uint32_t irq)
{
    uint32_t sourcecfg, sm, raw_input, irq_inverted;

    if (!irq || irq >= aplic->num_irqs) {
        return false;
    }

    sourcecfg = aplic->sourcecfg[irq];
    if (sourcecfg & IO_APLIC_SOURCECFG_D) {
        return false;
    }

    sm = sourcecfg & IO_APLIC_SOURCECFG_SM_MASK;
    if (sm == IO_APLIC_SOURCECFG_SM_INACTIVE) {
        return false;
    }

    raw_input = (aplic->state[irq] & IO_APLIC_ISTATE_INPUT) ? 1u : 0u;
    irq_inverted = (sm == IO_APLIC_SOURCECFG_SM_LEVEL_LOW ||
                    sm == IO_APLIC_SOURCECFG_SM_EDGE_FALL) ? 1u : 0u;

    return !!(raw_input ^ irq_inverted);
}

static uint32_t io_aplic_read_input_word(IoAplic *aplic, uint32_t word)
{
    uint32_t ret = 0;

    for (uint32_t i = 0; i < 32; i++) {
        uint32_t irq = word * 32 + i;

        ret |= (io_aplic_irq_rectified_val(aplic, irq) ? 1u : 0u) << i;
    }

    return ret;
}

static uint32_t io_aplic_read_pending_word(IoAplic *aplic, uint32_t word)
{
    uint32_t ret = 0;

    for (uint32_t i = 0; i < 32; i++) {
        uint32_t irq = word * 32 + i;

        if (!irq || irq >= aplic->num_irqs) {
            continue;
        }

        ret |= ((aplic->state[irq] & IO_APLIC_ISTATE_PENDING) ? 1u : 0u) << i;
    }

    return ret;
}

static uint32_t io_aplic_read_enabled_word(IoAplic *aplic, uint32_t word)
{
    uint32_t ret = 0;

    for (uint32_t i = 0; i < 32; i++) {
        uint32_t irq = word * 32 + i;

        if (!irq || irq >= aplic->num_irqs) {
            continue;
        }

        ret |= ((aplic->state[irq] & IO_APLIC_ISTATE_ENABLED) ? 1u : 0u) << i;
    }

    return ret;
}

static void io_aplic_set_pending_raw(IoAplic *aplic, uint32_t irq, bool pending)
{
    if (pending) {
        aplic->state[irq] |= IO_APLIC_ISTATE_PENDING;
    } else {
        aplic->state[irq] &= ~IO_APLIC_ISTATE_PENDING;
    }
}

static void io_aplic_set_enabled_raw(IoAplic *aplic, uint32_t irq, bool enabled)
{
    if (enabled) {
        aplic->state[irq] |= IO_APLIC_ISTATE_ENABLED;
    } else {
        aplic->state[irq] &= ~IO_APLIC_ISTATE_ENABLED;
    }
}

static uint64_t io_aplic_msi_send_addr(IoAplic *aplic, uint32_t hart_idx,
                                       uint32_t guest_idx)
{
    IoAplic *owner = io_aplic_smsi_owner(aplic);
    uint32_t msicfgaddr;
    uint32_t msicfgaddrH;
    uint32_t lhxs, lhxw, hhxs, hhxw, group_idx;
    uint64_t addr = 0;

    if (aplic->mmode) {
        msicfgaddr = owner->mmsicfgaddr;
        msicfgaddrH = owner->mmsicfgaddrH;
    } else {
        msicfgaddr = owner->smsicfgaddr;
        msicfgaddrH = owner->smsicfgaddrH;
    }

    lhxs = (msicfgaddrH >> IO_APLIC_xMSICFGADDRH_LHXS_SHIFT) &
           IO_APLIC_xMSICFGADDRH_LHXS_MASK;
    lhxw = (msicfgaddrH >> IO_APLIC_xMSICFGADDRH_LHXW_SHIFT) &
           IO_APLIC_xMSICFGADDRH_LHXW_MASK;
    hhxs = (msicfgaddrH >> IO_APLIC_xMSICFGADDRH_HHXS_SHIFT) &
           IO_APLIC_xMSICFGADDRH_HHXS_MASK;
    hhxw = (msicfgaddrH >> IO_APLIC_xMSICFGADDRH_HHXW_SHIFT) &
           IO_APLIC_xMSICFGADDRH_HHXW_MASK;
    group_idx = hart_idx >> lhxw;

    addr = msicfgaddr;
    addr |= ((uint64_t)(msicfgaddrH & IO_APLIC_xMSICFGADDRH_BAPPN_MASK)) << 32;
    addr |= ((uint64_t)(group_idx & ((1u << hhxw) - 1u))) <<
             (hhxs + IO_APLIC_xMSICFGADDR_PPN_SHIFT);
    addr |= ((uint64_t)(hart_idx & ((1u << lhxw) - 1u))) << lhxs;
    addr |= (uint64_t)(guest_idx & ((1u << lhxs) - 1u));
    addr <<= IO_APLIC_xMSICFGADDR_PPN_SHIFT;

    return addr;
}

static void io_aplic_send_explicit_msi(IoAplic *aplic, uint32_t hart_idx,
                                       uint32_t guest_idx, uint32_t eiid)
{
    uint64_t addr;
    uint32_t value;
    IoSystemStatus status;

    addr = io_aplic_msi_send_addr(aplic, hart_idx, guest_idx);
    value = eiid;
    status = io_system_guest_memory_write(aplic->system, addr, &value,
                                          sizeof(value));
    (void)status;
}

static void io_aplic_send_msi(IoAplic *aplic, uint32_t irq)
{
    uint32_t hart_idx, guest_idx, eiid;

    if (!aplic->msimode || irq >= aplic->num_irqs ||
        !(aplic->domaincfg & IO_APLIC_DOMAINCFG_IE)) {
        return;
    }

    if ((aplic->state[irq] & IO_APLIC_ISTATE_ENPEND) !=
        IO_APLIC_ISTATE_ENPEND) {
        return;
    }

    io_aplic_set_pending_raw(aplic, irq, false);

    hart_idx = aplic->target[irq] >> IO_APLIC_TARGET_HART_IDX_SHIFT;
    hart_idx &= IO_APLIC_TARGET_HART_IDX_MASK;
    if (aplic->mmode) {
        guest_idx = 0;
    } else {
        guest_idx = aplic->target[irq] >> IO_APLIC_TARGET_GUEST_IDX_SHIFT;
        guest_idx &= IO_APLIC_TARGET_GUEST_IDX_MASK;
    }
    eiid = aplic->target[irq] & IO_APLIC_TARGET_EIID_MASK;

    io_aplic_send_explicit_msi(aplic, hart_idx, guest_idx, eiid);
}

static void io_aplic_update_irq(IoAplic *aplic, uint32_t irq)
{
    if (irq >= aplic->num_irqs) {
        return;
    }

    io_aplic_send_msi(aplic, irq);
}

static void io_aplic_set_pending(IoAplic *aplic, uint32_t irq, bool pending)
{
    uint32_t sourcecfg, sm;

    if (!irq || irq >= aplic->num_irqs) {
        return;
    }

    sourcecfg = aplic->sourcecfg[irq];
    if (sourcecfg & IO_APLIC_SOURCECFG_D) {
        return;
    }

    sm = sourcecfg & IO_APLIC_SOURCECFG_SM_MASK;
    if (sm == IO_APLIC_SOURCECFG_SM_INACTIVE) {
        return;
    }

    if ((sm == IO_APLIC_SOURCECFG_SM_LEVEL_HIGH) ||
        (sm == IO_APLIC_SOURCECFG_SM_LEVEL_LOW)) {
        if (!aplic->msimode) {
            return;
        }
        if (aplic->msimode && !pending) {
            goto write_pending;
        }
        if ((aplic->state[irq] & IO_APLIC_ISTATE_INPUT) &&
            (sm == IO_APLIC_SOURCECFG_SM_LEVEL_LOW)) {
            return;
        }
        if (!(aplic->state[irq] & IO_APLIC_ISTATE_INPUT) &&
            (sm == IO_APLIC_SOURCECFG_SM_LEVEL_HIGH)) {
            return;
        }
    }

write_pending:
    io_aplic_set_pending_raw(aplic, irq, pending);
    io_aplic_update_irq(aplic, irq);
}

static void io_aplic_set_enabled(IoAplic *aplic, uint32_t irq, bool enabled)
{
    uint32_t sourcecfg, sm;

    if (!irq || irq >= aplic->num_irqs) {
        return;
    }

    sourcecfg = aplic->sourcecfg[irq];
    if (sourcecfg & IO_APLIC_SOURCECFG_D) {
        return;
    }

    sm = sourcecfg & IO_APLIC_SOURCECFG_SM_MASK;
    if (sm == IO_APLIC_SOURCECFG_SM_INACTIVE) {
        return;
    }

    io_aplic_set_enabled_raw(aplic, irq, enabled);
    io_aplic_update_irq(aplic, irq);
}

static void io_aplic_set_pending_word(IoAplic *aplic, uint32_t word,
                                      uint32_t value, bool pending)
{
    for (uint32_t i = 0; i < 32; i++) {
        if (value & (1u << i)) {
            io_aplic_set_pending(aplic, word * 32 + i, pending);
        }
    }
}

static void io_aplic_set_enabled_word(IoAplic *aplic, uint32_t word,
                                      uint32_t value, bool enabled)
{
    for (uint32_t i = 0; i < 32; i++) {
        if (value & (1u << i)) {
            io_aplic_set_enabled(aplic, word * 32 + i, enabled);
        }
    }
}

static void io_aplic_request(void *opaque, int irq, int level)
{
    IoAplic *aplic = opaque;
    uint32_t sourcecfg, state;
    bool update = false;

    if (irq <= 0 || (uint32_t)irq >= aplic->num_irqs) {
        return;
    }

    sourcecfg = aplic->sourcecfg[irq];
    if (sourcecfg & IO_APLIC_SOURCECFG_D) {
        return;
    }

    state = aplic->state[irq];
    switch (sourcecfg & IO_APLIC_SOURCECFG_SM_MASK) {
    case IO_APLIC_SOURCECFG_SM_EDGE_RISE:
        if ((level > 0) && !(state & IO_APLIC_ISTATE_INPUT) &&
            !(state & IO_APLIC_ISTATE_PENDING)) {
            io_aplic_set_pending_raw(aplic, irq, true);
            update = true;
        }
        break;
    case IO_APLIC_SOURCECFG_SM_EDGE_FALL:
        if ((level <= 0) && (state & IO_APLIC_ISTATE_INPUT) &&
            !(state & IO_APLIC_ISTATE_PENDING)) {
            io_aplic_set_pending_raw(aplic, irq, true);
            update = true;
        }
        break;
    case IO_APLIC_SOURCECFG_SM_LEVEL_HIGH:
        if ((level > 0) && !(state & IO_APLIC_ISTATE_PENDING)) {
            io_aplic_set_pending_raw(aplic, irq, true);
            update = true;
        }
        break;
    case IO_APLIC_SOURCECFG_SM_LEVEL_LOW:
        if ((level <= 0) && !(state & IO_APLIC_ISTATE_PENDING)) {
            io_aplic_set_pending_raw(aplic, irq, true);
            update = true;
        }
        break;
    default:
        break;
    }

    if (level <= 0) {
        aplic->state[irq] &= ~IO_APLIC_ISTATE_INPUT;
    } else {
        aplic->state[irq] |= IO_APLIC_ISTATE_INPUT;
    }

    if (update) {
        io_aplic_update_irq(aplic, irq);
    }
}

static IoSystemStatus io_aplic_read(void *opaque, uint64_t addr,
                                    void *data, size_t size)
{
    IoAplic *aplic = opaque;
    uint32_t irq, word;
    uint32_t value = 0;

    if (!data || !size || size > sizeof(uint32_t) || (addr & 0x3)) {
        return IO_SYSTEM_ERR_INVALID;
    }

    if (addr < aplic->config.base) {
        return IO_SYSTEM_ERR_UNMAPPED;
    }
    addr -= aplic->config.base;

    if (addr == IO_APLIC_DOMAINCFG) {
        value = IO_APLIC_DOMAINCFG_RDONLY | aplic->domaincfg |
                (aplic->msimode ? IO_APLIC_DOMAINCFG_DM : 0);
    } else if ((IO_APLIC_SOURCECFG_BASE <= addr) &&
               (addr < (IO_APLIC_SOURCECFG_BASE +
                        (aplic->num_irqs - 1) * 4))) {
        irq = ((addr - IO_APLIC_SOURCECFG_BASE) >> 2) + 1;
        value = aplic->sourcecfg[irq];
    } else if (addr == IO_APLIC_MMSICFGADDR) {
        value = aplic->mmsicfgaddr;
    } else if (addr == IO_APLIC_MMSICFGADDRH) {
        value = aplic->mmsicfgaddrH;
    } else if (aplic->mmode && (addr == IO_APLIC_SMSICFGADDR)) {
        value = aplic->num_children ? aplic->smsicfgaddr : 0;
    } else if (aplic->mmode && (addr == IO_APLIC_SMSICFGADDRH)) {
        value = aplic->num_children ? aplic->smsicfgaddrH : 0;
    } else if ((IO_APLIC_SETIP_BASE <= addr) &&
               (addr < (IO_APLIC_SETIP_BASE + aplic->bitfield_words * 4))) {
        word = (addr - IO_APLIC_SETIP_BASE) >> 2;
        value = io_aplic_read_pending_word(aplic, word);
    } else if (addr == IO_APLIC_SETIPNUM) {
        value = 0;
    } else if ((IO_APLIC_CLRIP_BASE <= addr) &&
               (addr < (IO_APLIC_CLRIP_BASE + aplic->bitfield_words * 4))) {
        word = (addr - IO_APLIC_CLRIP_BASE) >> 2;
        value = io_aplic_read_input_word(aplic, word);
    } else if (addr == IO_APLIC_CLRIPNUM) {
        value = 0;
    } else if ((IO_APLIC_SETIE_BASE <= addr) &&
               (addr < (IO_APLIC_SETIE_BASE + aplic->bitfield_words * 4))) {
        word = (addr - IO_APLIC_SETIE_BASE) >> 2;
        value = io_aplic_read_enabled_word(aplic, word);
    } else if (addr == IO_APLIC_SETIENUM) {
        value = 0;
    } else if ((IO_APLIC_CLRIE_BASE <= addr) &&
               (addr < (IO_APLIC_CLRIE_BASE + aplic->bitfield_words * 4))) {
        value = 0;
    } else if (addr == IO_APLIC_CLRIENUM) {
        value = 0;
    } else if (addr == IO_APLIC_SETIPNUM_LE || addr == IO_APLIC_SETIPNUM_BE) {
        value = 0;
    } else if (addr == IO_APLIC_GENMSI) {
        value = aplic->msimode ? aplic->genmsi : 0;
    } else if ((IO_APLIC_TARGET_BASE <= addr) &&
               (addr < (IO_APLIC_TARGET_BASE +
                        (aplic->num_irqs - 1) * 4))) {
        irq = ((addr - IO_APLIC_TARGET_BASE) >> 2) + 1;
        if ((aplic->sourcecfg[irq] & IO_APLIC_SOURCECFG_SM_MASK) ==
            IO_APLIC_SOURCECFG_SM_INACTIVE) {
            value = 0;
        } else {
            value = aplic->target[irq];
        }
    } else if (!aplic->msimode && (IO_APLIC_IDC_BASE <= addr) &&
               (addr < (IO_APLIC_IDC_BASE +
                        aplic->config.num_harts * IO_APLIC_IDC_SIZE))) {
        uint32_t idc = (addr - IO_APLIC_IDC_BASE) / IO_APLIC_IDC_SIZE;

        switch (addr - (IO_APLIC_IDC_BASE + idc * IO_APLIC_IDC_SIZE)) {
        case 0x00:
            value = aplic->idelivery[idc];
            break;
        case 0x04:
            value = aplic->iforce[idc];
            break;
        case 0x08:
            value = aplic->ithreshold[idc];
            break;
        default:
            value = 0;
            break;
        }
    } else {
        return IO_SYSTEM_ERR_UNMAPPED;
    }

    io_aplic_store_u32(data, value, size);
    return IO_SYSTEM_OK;
}

static IoSystemStatus io_aplic_write(void *opaque, uint64_t addr,
                                     const void *data, size_t size)
{
    IoAplic *aplic = opaque;
    uint32_t irq, word, value;

    if (!data || !size || size > sizeof(uint32_t) || (addr & 0x3)) {
        return IO_SYSTEM_ERR_INVALID;
    }

    if (addr < aplic->config.base) {
        return IO_SYSTEM_ERR_UNMAPPED;
    }
    addr -= aplic->config.base;

    value = io_aplic_load_u32(data, size);

    if (addr == IO_APLIC_DOMAINCFG) {
        aplic->domaincfg = value & IO_APLIC_DOMAINCFG_IE;
    } else if ((IO_APLIC_SOURCECFG_BASE <= addr) &&
               (addr < (IO_APLIC_SOURCECFG_BASE +
                        (aplic->num_irqs - 1) * 4))) {
        irq = ((addr - IO_APLIC_SOURCECFG_BASE) >> 2) + 1;
        if (!aplic->num_children && (value & IO_APLIC_SOURCECFG_D)) {
            value = 0;
        }
        if (value & IO_APLIC_SOURCECFG_D) {
            value &= IO_APLIC_SOURCECFG_D | IO_APLIC_SOURCECFG_CHILDIDX_MASK;
        } else {
            value &= IO_APLIC_SOURCECFG_D | IO_APLIC_SOURCECFG_SM_MASK;
        }
        aplic->sourcecfg[irq] = value;
        if ((aplic->sourcecfg[irq] & IO_APLIC_SOURCECFG_D) ||
            aplic->sourcecfg[irq] == 0) {
            io_aplic_set_pending_raw(aplic, irq, false);
            io_aplic_set_enabled_raw(aplic, irq, false);
        } else if (io_aplic_irq_rectified_val(aplic, irq)) {
            io_aplic_set_pending_raw(aplic, irq, true);
        }
        io_aplic_update_irq(aplic, irq);
    } else if (addr == IO_APLIC_MMSICFGADDR) {
        if (!(aplic->mmsicfgaddrH & IO_APLIC_xMSICFGADDRH_L)) {
            aplic->mmsicfgaddr = value;
        }
    } else if (addr == IO_APLIC_MMSICFGADDRH) {
        if (!(aplic->mmsicfgaddrH & IO_APLIC_xMSICFGADDRH_L)) {
            aplic->mmsicfgaddrH = value & IO_APLIC_MMSICFGADDRH_VALID_MASK;
        }
    } else if (aplic->mmode && (addr == IO_APLIC_SMSICFGADDR)) {
        if (aplic->num_children &&
            !(aplic->mmsicfgaddrH & IO_APLIC_xMSICFGADDRH_L)) {
            aplic->smsicfgaddr = value;
        }
    } else if (aplic->mmode && (addr == IO_APLIC_SMSICFGADDRH)) {
        if (aplic->num_children &&
            !(aplic->mmsicfgaddrH & IO_APLIC_xMSICFGADDRH_L)) {
            aplic->smsicfgaddrH = value & IO_APLIC_SMSICFGADDRH_VALID_MASK;
        }
    } else if ((IO_APLIC_SETIP_BASE <= addr) &&
               (addr < (IO_APLIC_SETIP_BASE + aplic->bitfield_words * 4))) {
        word = (addr - IO_APLIC_SETIP_BASE) >> 2;
        io_aplic_set_pending_word(aplic, word, value, true);
    } else if (addr == IO_APLIC_SETIPNUM) {
        io_aplic_set_pending(aplic, value, true);
    } else if ((IO_APLIC_CLRIP_BASE <= addr) &&
               (addr < (IO_APLIC_CLRIP_BASE + aplic->bitfield_words * 4))) {
        word = (addr - IO_APLIC_CLRIP_BASE) >> 2;
        io_aplic_set_pending_word(aplic, word, value, false);
    } else if (addr == IO_APLIC_CLRIPNUM) {
        io_aplic_set_pending(aplic, value, false);
    } else if ((IO_APLIC_SETIE_BASE <= addr) &&
               (addr < (IO_APLIC_SETIE_BASE + aplic->bitfield_words * 4))) {
        word = (addr - IO_APLIC_SETIE_BASE) >> 2;
        io_aplic_set_enabled_word(aplic, word, value, true);
    } else if (addr == IO_APLIC_SETIENUM) {
        io_aplic_set_enabled(aplic, value, true);
    } else if ((IO_APLIC_CLRIE_BASE <= addr) &&
               (addr < (IO_APLIC_CLRIE_BASE + aplic->bitfield_words * 4))) {
        word = (addr - IO_APLIC_CLRIE_BASE) >> 2;
        io_aplic_set_enabled_word(aplic, word, value, false);
    } else if (addr == IO_APLIC_CLRIENUM) {
        io_aplic_set_enabled(aplic, value, false);
    } else if (addr == IO_APLIC_SETIPNUM_LE) {
        io_aplic_set_pending(aplic, value, true);
    } else if (addr == IO_APLIC_SETIPNUM_BE) {
        io_aplic_set_pending(aplic,
                             ((value & 0x000000ffu) << 24) |
                             ((value & 0x0000ff00u) << 8) |
                             ((value & 0x00ff0000u) >> 8) |
                             ((value & 0xff000000u) >> 24),
                             true);
    } else if (addr == IO_APLIC_GENMSI) {
        if (aplic->msimode) {
            aplic->genmsi = value & ~(IO_APLIC_TARGET_GUEST_IDX_MASK <<
                                      IO_APLIC_TARGET_GUEST_IDX_SHIFT);
            io_aplic_send_explicit_msi(aplic,
                                       value >> IO_APLIC_TARGET_HART_IDX_SHIFT,
                                       0,
                                       value & IO_APLIC_TARGET_EIID_MASK);
        }
    } else if ((IO_APLIC_TARGET_BASE <= addr) &&
               (addr < (IO_APLIC_TARGET_BASE +
                        (aplic->num_irqs - 1) * 4))) {
        irq = ((addr - IO_APLIC_TARGET_BASE) >> 2) + 1;
        if (aplic->msimode) {
            aplic->target[irq] = value;
        } else {
            aplic->target[irq] = (value & ~IO_APLIC_TARGET_IPRIO_MASK) |
                                 ((value & aplic->iprio_mask) ?
                                  (value & aplic->iprio_mask) : 1u);
        }
        io_aplic_update_irq(aplic, irq);
    } else if (!aplic->msimode && (IO_APLIC_IDC_BASE <= addr) &&
               (addr < (IO_APLIC_IDC_BASE +
                        aplic->config.num_harts * IO_APLIC_IDC_SIZE))) {
        uint32_t idc = (addr - IO_APLIC_IDC_BASE) / IO_APLIC_IDC_SIZE;
        uint32_t offset = addr - (IO_APLIC_IDC_BASE + idc * IO_APLIC_IDC_SIZE);

        switch (offset) {
        case IO_APLIC_IDC_IDELIVERY:
            aplic->idelivery[idc] = value & 0x1;
            break;
        case IO_APLIC_IDC_IFORCE:
            aplic->iforce[idc] = value & 0x1;
            break;
        case IO_APLIC_IDC_ITHRESHOLD:
            aplic->ithreshold[idc] = value;
            break;
        case IO_APLIC_IDC_CLAIMI:
            break;
        default:
            return IO_SYSTEM_ERR_UNMAPPED;
        }
    } else {
        return IO_SYSTEM_ERR_UNMAPPED;
    }

    return IO_SYSTEM_OK;
}

IoAplic *io_aplic_create(IoSystem *system, const IoAplicConfig *config,
                         IoAplic *parent)
{
    IoAplic *aplic;
    const char *name;

    if (!system || !config || !config->base || !config->size ||
        !config->num_sources || !config->num_harts) {
        return NULL;
    }

    aplic = calloc(1, sizeof(*aplic));
    if (!aplic) {
        return NULL;
    }

    aplic->system = system;
    aplic->config = *config;
    aplic->parent = parent;
    aplic->msimode = config->msimode;
    aplic->mmode = config->mmode;
    aplic->num_irqs = config->num_sources + 1u;
    aplic->bitfield_words = (aplic->num_irqs + 31u) / 32u;
    aplic->iprio_mask = config->iprio_bits >= 32 ? 0xffffffffu :
                        ((1u << config->iprio_bits) - 1u);

    aplic->sourcecfg = calloc(aplic->num_irqs, sizeof(*aplic->sourcecfg));
    aplic->state = calloc(aplic->num_irqs, sizeof(*aplic->state));
    aplic->target = calloc(aplic->num_irqs, sizeof(*aplic->target));
    aplic->idelivery = calloc(config->num_harts, sizeof(*aplic->idelivery));
    aplic->iforce = calloc(config->num_harts, sizeof(*aplic->iforce));
    aplic->ithreshold = calloc(config->num_harts, sizeof(*aplic->ithreshold));
    if (!aplic->sourcecfg || !aplic->state || !aplic->target ||
        !aplic->idelivery || !aplic->iforce || !aplic->ithreshold) {
        io_aplic_destroy(aplic);
        return NULL;
    }

    io_aplic_reset(aplic);

    name = config->name ? config->name : "aplic";
    if (io_cmodel_register_mmio_window(system, name, config->base, config->size,
                                       io_aplic_read, io_aplic_write, aplic) !=
        IO_SYSTEM_OK) {
        io_aplic_destroy(aplic);
        return NULL;
    }

    if (parent) {
        parent->num_children++;
    }

    return aplic;
}

void io_aplic_destroy(IoAplic *aplic)
{
    if (!aplic) {
        return;
    }

    if (aplic->parent && aplic->parent->num_children > 0) {
        aplic->parent->num_children--;
    }

    free(aplic->sourcecfg);
    free(aplic->state);
    free(aplic->target);
    free(aplic->idelivery);
    free(aplic->iforce);
    free(aplic->ithreshold);
    free(aplic);
}

IoSystemStatus io_aplic_reset(IoAplic *aplic)
{
    if (!aplic) {
        return IO_SYSTEM_ERR_INVALID;
    }

    memset(aplic->sourcecfg, 0, aplic->num_irqs * sizeof(uint32_t));
    memset(aplic->state, 0, aplic->num_irqs * sizeof(uint32_t));
    memset(aplic->target, 0, aplic->num_irqs * sizeof(uint32_t));
    memset(aplic->idelivery, 0, aplic->config.num_harts * sizeof(uint32_t));
    memset(aplic->iforce, 0, aplic->config.num_harts * sizeof(uint32_t));
    memset(aplic->ithreshold, 0, aplic->config.num_harts * sizeof(uint32_t));
    aplic->domaincfg = 0;
    aplic->mmsicfgaddr = 0;
    aplic->mmsicfgaddrH = 0;
    aplic->smsicfgaddr = 0;
    aplic->smsicfgaddrH = 0;
    aplic->genmsi = 0;

    return IO_SYSTEM_OK;
}

void io_aplic_irq_line_set(IoAplic *aplic, uint32_t source, bool level)
{
    if (!aplic) {
        return;
    }

    io_aplic_request(aplic, (int)source, level ? 1 : 0);
}
