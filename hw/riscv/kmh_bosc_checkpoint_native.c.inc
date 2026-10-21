/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native v3 wire encoding. Only the producer reads QEMU device structures. */
#include "../../third_party/kmh-portable-restore/native-abi.h"
#include "../../third_party/kmh-portable-restore/native-resources.inc"

static uint64_t native_peripheral(KmhBoscCheckpoint *s, unsigned die,
                                  uint64_t offset)
{
    return s->single_soc ? offset : ((uint64_t)(die + 1) << 44) | offset;
}

static void native_op(GByteArray *ops, uint64_t address, uint64_t value,
                      uint32_t hart, uint16_t kind, uint16_t phase)
{
    uint8_t p[KMH_NATIVE_OP_SIZE] = { 0 };

    stq_le_p(p, address);
    stq_le_p(p + 8, value);
    stl_le_p(p + 16, hart);
    stw_le_p(p + 20, kind);
    stw_le_p(p + 22, phase);
    g_byte_array_append(ops, p, sizeof(p));
}

static bool kmh_native_program(KmhBoscCheckpoint *s, GByteArray *ops,
                               Error **errp)
{
    for (unsigned n = 0; n < s->num_aplic; n++) {
        RISCVAPLICState *a = RISCV_APLIC(s->aplic[n]);
        unsigned die = n / 2;
        uint64_t base = native_peripheral(s, die,
                                           a->mmode ? 0x1e020000 : 0x1e024000);

        if (!(s->die_mask & (1U << die))) {
            continue;
        }
        native_op(ops, base, 0, KMH_NATIVE_GLOBAL, KMH_NATIVE_MMIO32, 0);
        if (a->mmode) {
            native_op(ops, base + 0x1bc0, a->mmsicfgaddr,
                      KMH_NATIVE_GLOBAL, KMH_NATIVE_MMIO32, 0);
            native_op(ops, base + 0x1bc8, a->smsicfgaddr,
                      KMH_NATIVE_GLOBAL, KMH_NATIVE_MMIO32, 0);
            native_op(ops, base + 0x1bcc, a->smsicfgaddrH,
                      KMH_NATIVE_GLOBAL, KMH_NATIVE_MMIO32, 0);
            /* Program the lock-bearing register last. */
            native_op(ops, base + 0x1bc4, a->mmsicfgaddrH,
                      KMH_NATIVE_GLOBAL, KMH_NATIVE_MMIO32, 0);
        }
        for (unsigned irq = 1; irq < a->num_irqs; irq++) {
            uint32_t state = a->state[irq];

            if (state & ~3U) {
                error_setg(errp, "native checkpoint requires quiescent "
                           "external IRQ inputs (die %u source %u)",
                           die, irq);
                return false;
            }
            native_op(ops, base + irq * 4, a->sourcecfg[irq],
                      KMH_NATIVE_GLOBAL, KMH_NATIVE_MMIO32, 0);
            native_op(ops, base + 0x3000 + irq * 4, a->target[irq],
                      KMH_NATIVE_GLOBAL, KMH_NATIVE_MMIO32, 0);
            if (state & 1) {
                native_op(ops, base + 0x1cdc, irq,
                          KMH_NATIVE_GLOBAL, KMH_NATIVE_MMIO32, 0);
            }
            if (state & 2) {
                native_op(ops, base + 0x1edc, irq,
                          KMH_NATIVE_GLOBAL, KMH_NATIVE_MMIO32, 0);
            }
        }
        native_op(ops, base, a->domaincfg, KMH_NATIVE_GLOBAL,
                  KMH_NATIVE_MMIO32, 1);
    }
    for (unsigned n = 0; n < s->num_uart; n++) {
        SerialState *u = &s->uart[n]->serial;
        uint64_t base = native_peripheral(s, n, 0x04000000);
        const uint8_t regs[] = { 1, 3, 0, 1, 3, 4, 7, 2 };
        uint8_t values[] = { 0, u->lcr | 0x80, u->divider,
                             u->divider >> 8, u->lcr, u->mcr, u->scr, u->fcr };

        if (!(s->die_mask & (1U << n))) {
            continue;
        }
        for (unsigned j = 0; j < ARRAY_SIZE(regs); j++) {
            native_op(ops, base + regs[j] * 4, values[j],
                      KMH_NATIVE_GLOBAL, KMH_NATIVE_MMIO32, 0);
        }
        native_op(ops, base + 4, u->ier, KMH_NATIVE_GLOBAL,
                  KMH_NATIVE_MMIO32, 1);
    }
    for (unsigned n = 0; n < s->num_imsic; n++) {
        RISCVIMSICState *i = RISCV_IMSIC(s->imsic[n]);
        unsigned hart = i->hartid;
        unsigned kind = i->mmode ? KMH_NATIVE_MIREG : KMH_NATIVE_SIREG;

        if (!(s->active_hart_mask & (UINT64_C(1) << hart))) {
            continue;
        }
        /* VS interrupt files are outside the native nonvirtualized ABI. */
        for (unsigned page = 1; page < i->num_pages; page++) {
            if (i->eidelivery[page] || i->eithreshold[page]) {
                goto active_vs;
            }
            for (unsigned id = 1; id < i->num_irqs; id++) {
                if (i->eistate[page * i->num_irqs + id] & 3) {
                    goto active_vs;
                }
            }
        }
        native_op(ops, 0x70, 0, hart, kind, 0);
        for (unsigned first = 0; first < i->num_irqs; first += 64) {
            uint64_t pending = 0, enabled = 0;
            for (unsigned bit = 0; bit < 64 && first + bit < i->num_irqs; bit++) {
                if (!first && !bit) {
                    continue;
                }
                uint32_t state = i->eistate[first + bit];
                pending |= (uint64_t)(state & 1) << bit;
                enabled |= (uint64_t)((state >> 1) & 1) << bit;
            }
            native_op(ops, 0x80 + first / 32, pending, hart, kind, 0);
            native_op(ops, 0xc0 + first / 32, enabled, hart, kind, 0);
        }
        native_op(ops, 0x72, i->eithreshold[0], hart, kind, 0);
        native_op(ops, 0x70, i->eidelivery[0], hart, kind, 1);
    }
    return true;
active_vs:
    error_setg(errp, "native checkpoint does not support active VS interrupt files");
    return false;
}

static bool kmh_prepare_native_directory(KmhBoscCheckpoint *s, Error **errp)
{
    g_autoptr(GByteArray) ops = g_byte_array_new();
    uint8_t *dir, *p, *segment;
    uint8_t *entry = kmh_ram_ptr(s, KMH_NATIVE_ENTRY, KMH_NATIVE_ENTRY_SIZE, errp);
    size_t native_len;
    unsigned segments = ctpop32(s->die_mask);
    uint64_t die_size = s->machine->ram_size / (s->single_soc ? 1 : 4);

    if (!entry || !kmh_native_program(s, ops, errp)) {
        return false;
    }
    native_len = KMH_NATIVE_OPS_OFFSET + ops->len +
                 segments * KMH_NATIVE_SEGMENT_SIZE;
    if (native_len > KMH_DEVICE_STATE_SIZE - KMH_NATIVE_HEADER_SIZE - 32) {
        error_setg(errp, "native checkpoint operation directory overflow");
        return false;
    }
    dir = kmh_ram_ptr(s, KMH_DEVICE_STATE_BASE, KMH_DEVICE_STATE_SIZE, errp);
    if (!dir) {
        return false;
    }
    memset(dir, 0, KMH_DEVICE_STATE_SIZE);
    p = dir + KMH_NATIVE_HEADER_SIZE + KMH_CPT_ENTRY_SIZE;
    stl_le_p(p, KMH_NATIVE_MAGIC);
    stl_le_p(p + 4, ops->len / KMH_NATIVE_OP_SIZE);
    stq_le_p(p + 8, native_peripheral(s, 0, 0x04000000));
    stl_le_p(p + 16, segments);
    memcpy(p + KMH_NATIVE_BACKUP_OFFSET, entry, KMH_NATIVE_ENTRY_SIZE);
    memcpy(p + KMH_NATIVE_OPS_OFFSET, ops->data, ops->len);
    segment = p + KMH_NATIVE_OPS_OFFSET + ops->len;
    unsigned node = 0;
    for (unsigned die = 0; die < (s->single_soc ? 1 : 4); die++) {
        if (!(s->die_mask & (1U << die))) {
            continue;
        }
        stq_le_p(segment, KMH_DRAM_BASE + (uint64_t)die * UINT64_C(0x800000000));
        stq_le_p(segment + 8, (uint64_t)node++ * die_size);
        stq_le_p(segment + 16, die_size);
        segment += KMH_NATIVE_SEGMENT_SIZE;
    }
    put_entry(dir + KMH_NATIVE_HEADER_SIZE, KMH_NATIVE_SECTION,
              p - dir, native_len, kmh_crc32c(p, native_len));
    stq_le_p(dir, KMH_CPT_MAGIC);
    stl_le_p(dir + 8, KMH_NATIVE_VERSION);
    stl_le_p(dir + 12, KMH_NATIVE_HEADER_SIZE);
    stl_le_p(dir + 16, KMH_CPT_PLATFORM);
    stl_le_p(dir + 20, 1);
    stl_le_p(dir + 24, 1);
    stl_le_p(dir + 28, segment - dir);
    stq_le_p(dir + 32, s->machine->ram_size);
    stq_le_p(dir + 40, s->cpu->env.profiling_insns);
    stl_le_p(dir + 52, s->single_soc);
    stl_le_p(dir + 56, s->die_mask);
    stl_le_p(dir + 60, s->machine->smp.cpus);
    stl_le_p(dir + 64, s->active_hart_count);
    stl_le_p(dir + 68, s->trigger_hartid);
    stq_le_p(dir + 72, s->active_hart_mask);
    for (unsigned i = 0; i < 4; i++) {
        stl_le_p(dir + 80 + i * 4, s->core_mask[i]);
    }
    stq_le_p(dir + 96, s->cpu_state_base);
    stq_le_p(dir + 104, KMH_CPU_STATE_STRIDE);
    stq_le_p(dir + 112, KMH_BOSC_CPT_CPU_SIZE);
    stq_le_p(dir + 128, KMH_NATIVE_ENTRY);
    stq_le_p(dir + 136, KMH_NATIVE_ENTRY_SIZE);
    stq_le_p(dir + 144, KMH_DEVICE_STATE_BASE + (p - dir) + KMH_NATIVE_BACKUP_OFFSET);
    stq_le_p(dir + 152, KMH_NATIVE_WORK);
    stq_le_p(dir + 160, sizeof(kmh_native_firmware));
    stl_le_p(dir + 168, kmh_crc32c(kmh_native_firmware, sizeof(kmh_native_firmware)));
    stl_le_p(dir + 172, KMH_NATIVE_CPU_ABI);
    stq_le_p(dir + 176, KMH_DEVICE_STATE_BASE + (p - dir));
    stq_le_p(dir + 184, native_len);
    stl_le_p(dir + 48, kmh_crc32c(dir, segment - dir));
    return true;
}

static bool native_operation_valid(KmhBoscCheckpoint *s, const uint8_t *p)
{
    uint64_t addr = ldq_le_p(p), value = ldq_le_p(p + 8);
    uint32_t hart = ldl_le_p(p + 16);
    unsigned kind = lduw_le_p(p + 20), phase = lduw_le_p(p + 22);

    if (phase > 1) {
        return false;
    }
    if (hart != KMH_NATIVE_GLOBAL) {
        if (hart >= 64 || !(s->active_hart_mask & (UINT64_C(1) << hart)) ||
            (kind != KMH_NATIVE_MIREG && kind != KMH_NATIVE_SIREG)) {
            return false;
        }
        return addr == 0x70 || addr == 0x72 ||
               ((addr >= 0x80 && addr <= 0x86 && !(addr & 1)) ||
                (addr >= 0xc0 && addr <= 0xc6 && !(addr & 1)));
    }
    for (unsigned die = 0; die < (s->single_soc ? 1 : 4); die++) {
        if (!(s->die_mask & (1U << die))) {
            continue;
        }
        uint64_t uart = native_peripheral(s, die, 0x04000000);
        if (kind == KMH_NATIVE_MMIO32 && addr >= uart &&
            addr <= uart + 7 * 4 && !((addr - uart) & 3) && value <= 255) {
            return true;
        }
        for (unsigned level = 0; level < 2; level++) {
            uint64_t base = native_peripheral(s, die, 0x1e020000 + level * 0x4000);
            uint64_t off = addr - base;
            if (kind == KMH_NATIVE_MMIO32 && value <= UINT32_MAX &&
                !(off & 3) && (off == 0 ||
                (off >= 4 && off <= 0x1000) ||
                (off >= 0x1bc0 && off <= 0x1bcc) ||
                off == 0x1cdc || off == 0x1edc ||
                (off >= 0x3004 && off < 0x4000))) {
                return true;
            }
        }
    }
    return false;
}

static bool kmh_validate_native(KmhBoscCheckpoint *s, uint8_t *dir, Error **errp)
{
    size_t size;
    const uint8_t *p = find_section(dir, KMH_NATIVE_SECTION, &size, errp);
    uint64_t stage_size = ldq_le_p(dir + 160);
    uint8_t *entry = kmh_ram_ptr(s, KMH_NATIVE_ENTRY, KMH_NATIVE_ENTRY_SIZE, errp);
    uint8_t *work = kmh_ram_ptr(s, KMH_NATIVE_WORK, KMH_NATIVE_WORK_SIZE, errp);
    uint64_t count, segments;

    if (!p || !entry || !work) {
        return false;
    }
    if (s->machine->ram_size / (s->single_soc ? 1 : 4) < 512 * MiB ||
        size < KMH_NATIVE_OPS_OFFSET || ldl_le_p(p) != KMH_NATIVE_MAGIC ||
        ldq_le_p(dir + 128) != KMH_NATIVE_ENTRY ||
        ldq_le_p(dir + 136) != KMH_NATIVE_ENTRY_SIZE ||
        ldq_le_p(dir + 152) != KMH_NATIVE_WORK ||
        !stage_size || stage_size > KMH_NATIVE_WORK_SIZE ||
        ldl_le_p(dir + 168) != kmh_crc32c(work, stage_size) ||
        ldl_le_p(dir + 172) != KMH_NATIVE_CPU_ABI ||
        ldq_le_p(dir + 176) != KMH_DEVICE_STATE_BASE + (p - dir) ||
        ldq_le_p(dir + 184) != size ||
        ldq_le_p(dir + 144) != ldq_le_p(dir + 176) + KMH_NATIVE_BACKUP_OFFSET ||
        memcmp(entry, kmh_native_entry, KMH_NATIVE_ENTRY_SIZE) ||
        ldq_le_p(p + 8) != native_peripheral(s, 0, 0x04000000)) {
        goto bad;
    }
    count = ldl_le_p(p + 4);
    segments = ldl_le_p(p + 16);
    if (KMH_NATIVE_OPS_OFFSET + count * KMH_NATIVE_OP_SIZE +
        segments * KMH_NATIVE_SEGMENT_SIZE != size || segments != ctpop32(s->die_mask)) {
        goto bad;
    }
    for (unsigned i = 0; i < count; i++) {
        if (!native_operation_valid(s, p + KMH_NATIVE_OPS_OFFSET + i * KMH_NATIVE_OP_SIZE)) {
            goto bad;
        }
    }
    const uint8_t *seg = p + KMH_NATIVE_OPS_OFFSET + count * KMH_NATIVE_OP_SIZE;
    unsigned node = 0;
    uint64_t die_size = s->machine->ram_size / (s->single_soc ? 1 : 4);
    for (unsigned die = 0; die < (s->single_soc ? 1 : 4); die++) {
        if (!(s->die_mask & (1U << die))) {
            continue;
        }
        if (ldq_le_p(seg) != KMH_DRAM_BASE + (uint64_t)die * UINT64_C(0x800000000) ||
            ldq_le_p(seg + 8) != (uint64_t)node++ * die_size ||
            ldq_le_p(seg + 16) != die_size) {
            goto bad;
        }
        seg += KMH_NATIVE_SEGMENT_SIZE;
    }
    return true;
bad:
    error_setg(errp, "invalid native checkpoint entry, firmware or operations");
    return false;
}
