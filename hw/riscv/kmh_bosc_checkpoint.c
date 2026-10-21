/*
 * Portable KMH BOSC checkpoints, adapted from xiangshan_kmh_checkpoint.c.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/crc32c.h"
#include "qemu/bswap.h"
#include "qemu/cutils.h"
#include "qemu/host-utils.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/units.h"
#include "hw/riscv/kmh_bosc_checkpoint.h"
#include "hw/intc/riscv_aplic.h"
#include "hw/intc/riscv_imsic.h"
#include "hw/intc/riscv_aclint.h"
#include "system/memory.h"
#include "exec/cputlb.h"
#include "system/cpus.h"
#include "system/kvm.h"
#include "system/runstate.h"
#include "system/system.h"
#include "target/riscv/cpu.h"
#include "target/riscv/cpu_bits.h"
#include "target/riscv/internals.h"
#ifdef CONFIG_ZSTD
#include <zstd.h>
#endif

#include "../../third_party/kmh-portable-restore/native-abi.h"

#define KMH_DRAM_BASE              UINT64_C(0x80000000)
#define KMH_CPU_STATE_BASE         KMH_BOSC_CPT_CPU_BASE
#define KMH_CPU_STATE_SIZE         KMH_BOSC_CPT_CPU_SIZE
#define KMH_STUB_BASE              KMH_BOSC_CPT_STUB_BASE
#define KMH_DEVICE_STATE_BASE      KMH_BOSC_CPT_DEVICE_BASE
#define KMH_DEVICE_STATE_SIZE      KMH_BOSC_CPT_DEVICE_SIZE
#define KMH_CPT_MAGIC              UINT64_C(0x31545043484d4b58)
#define KMH_CPT_VERSION            2U
#define KMH_CPT_VERSION_V1         1U
#define KMH_CPT_PLATFORM           UINT32_C(0x43534f42) /* BOSC */
#define KMH_CPT_HEADER_SIZE        128U
#define KMH_CPT_HEADER_SIZE_V1     64U
#define KMH_CPT_ENTRY_SIZE         32U
#define KMH_CPT_SECTION_AIA        1U
#define KMH_CPT_SECTION_UART       4U
#define KMH_UART_HEADER_SIZE       16U
#define KMH_UART_ENTRY_SIZE        16U
#define CPU_MAGIC_OFF              0xECDB0U
#define CPU_PC_OFF                 0xECDB8U
#define CPU_MODE_OFF               0xECDC0U
#define CPU_MTIME_OFF              0xECDC8U
#define CPU_MTIMECMP_OFF           0xECDD0U
#define CPU_MISC_OFF               0xECDE0U
#define CPU_GPR_OFF                0xEDDE0U
#define CPU_FPR_OFF                0xEDEE8U
#define CPU_CSR_OFF                0xEDFF0U
#define CPU_CSR_RESERVED_OFF       0xF5FF8U
#define CPU_VECTOR_OFF             0xFDFF8U
#define CPU_MAGIC                  UINT64_C(0xbeef)
#define KMH_MSIP_ADDR              UINT64_C(0x19000000)
#define KMH_MTIMECMP_ADDR          UINT64_C(0x19004000)
#define KMH_MTIME_ADDR             UINT64_C(0x1900bff8)
#define KMH_CPU_STATE_STRIDE       KMH_BOSC_CPT_CPU_RECORD_SIZE

#ifndef QEMU_ALIGN_PTR_UP
#define QEMU_ALIGN_PTR_UP(p, a) ((typeof(p))QEMU_ALIGN_UP((uintptr_t)(p), (a)))
#endif

static uint32_t kmh_crc32c(const uint8_t *bot, size_t len)
{
    uint32_t crc = 0;
    while (len) {
        unsigned int n = MIN(len, UINT_MAX);
        crc = crc32c(crc, bot, n) ^ UINT32_MAX;
        bot += n;
        len -= n;
    }
    return crc ^ UINT32_MAX;
}

uint64_t kmh_bosc_checkpoint_cpu_state_base(const KmhBoscCheckpoint *s)
{
    MachineState *ms = s->machine;
    uint64_t die_size;
    int last_die = 0;

    if (s->single_soc && s->format_version < KMH_NATIVE_VERSION) {
        return KMH_BOSC_CPT_CPU_BASE;
    }
    for (int die = 0; die < 4; die++) {
        if ((s->die_mask & (UINT32_C(1) << die)) && s->core_mask[die]) {
            last_die = die;
        }
    }
    die_size = ms->ram_size / (s->single_soc ? 1 : 4);
    return KMH_DRAM_BASE + (uint64_t)last_die * UINT64_C(0x800000000) +
           die_size - KMH_BOSC_CPT_CPU_SIZE;
}

static uint8_t *kmh_ram_ptr(KmhBoscCheckpoint *s, uint64_t pa, size_t len,
                            Error **errp)
{
    MachineState *ms = s->machine;
    uint64_t die_size = ms->ram_size / (s->single_soc ? 1 : 4);
    int node = 0;

    for (int die = 0; die < (s->single_soc ? 1 : 4); die++) {
        uint64_t base = KMH_DRAM_BASE + (uint64_t)die * UINT64_C(0x800000000);
        if (!s->single_soc && !(s->die_mask & (UINT32_C(1) << die))) {
            continue;
        }
        if (pa >= base && pa - base <= die_size &&
            len <= die_size - (pa - base)) {
            return memory_region_get_ram_ptr(ms->ram) +
                   (uint64_t)node * die_size + (pa - base);
        }
        node++;
    }
    error_setg(errp, "checkpoint reserved range is outside guest DRAM");
    return NULL;
}

static uint8_t *kmh_cpu_record(KmhBoscCheckpoint *s, unsigned int hartid,
                               Error **errp)
{
    if (hartid >= KMH_BOSC_CPT_CPU_RECORDS) {
        error_setg(errp, "portable checkpoint hart %u is out of range",
                   hartid);
        return NULL;
    }
    return kmh_ram_ptr(s, s->cpu_state_base +
                       (uint64_t)hartid * KMH_CPU_STATE_STRIDE,
                       KMH_BOSC_CPT_CPU_RECORD_SIZE, errp);
}

static bool kmh_cpu_save_one(KmhBoscCheckpoint *s, RISCVCPU *cpu,
                             Error **errp)
{
    CPURISCVState *env = &cpu->env;
    uint8_t *p;
    uint64_t mstatus, mepc, val, mtime, mtimecmp;
    uint8_t msip[4];
    unsigned int hartid = env->mhartid;

    if (hartid >= KMH_BOSC_CPT_CPU_RECORDS ||
        !(s->active_hart_mask & (UINT64_C(1) << hartid))) {
        return true;
    }
    if (cpu->cfg.vlenb != 16) {
        error_setg(errp, "portable KMH checkpoint requires VLEN=128 on hart %u",
                   hartid);
        return false;
    }
    p = kmh_cpu_record(s, hartid, errp);
    if (!p) {
        return false;
    }
    memset(p, 0, KMH_BOSC_CPT_CPU_RECORD_SIZE);
    for (unsigned int i = 0; i < 32; i++) {
        stq_le_p(p + CPU_GPR_OFF + i * 8, env->gpr[i]);
        stq_le_p(p + CPU_FPR_OFF + i * 8, env->fpr[i]);
    }
    for (unsigned int i = 0; i < CSR_TABLE_SIZE; i++) {
        val = 0;
        if (csr_ops[i].predicate &&
            riscv_csrrw_debug(env, i, &val, 0, 0) == RISCV_EXCP_NONE) {
            stq_le_p(p + CPU_CSR_OFF + i * 8, val);
        }
    }
    memcpy(p + CPU_VECTOR_OFF, env->vreg, 32 * cpu->cfg.vlenb);
    mstatus = env->mstatus;
    mepc = env->pc;
    mstatus = set_field(mstatus, MSTATUS_MPIE,
                        get_field(mstatus, MSTATUS_MIE));
    mstatus = set_field(mstatus, MSTATUS_MIE, 0);
    mstatus = set_field(mstatus, MSTATUS_MPP, env->priv);
    mstatus = set_field(mstatus, MSTATUS_MPV, env->virt_enabled);
    stq_le_p(p + CPU_CSR_OFF + CSR_MSTATUS * 8, mstatus);
    stq_le_p(p + CPU_CSR_OFF + CSR_MEPC * 8, mepc);
    cpu_physical_memory_read(KMH_MTIME_ADDR, &mtime, sizeof(mtime));
    cpu_physical_memory_read(KMH_MTIMECMP_ADDR + hartid * sizeof(uint64_t),
                             &mtimecmp, sizeof(mtimecmp));
    stq_le_p(p + CPU_MAGIC_OFF, CPU_MAGIC);
    stq_le_p(p + CPU_PC_OFF, env->pc);
    stq_le_p(p + CPU_MODE_OFF, env->priv);
    stq_le_p(p + CPU_MTIME_OFF, mtime);
    stq_le_p(p + CPU_MTIMECMP_OFF, mtimecmp);
    stl_le_p(p + CPU_MISC_OFF, KMH_CPT_VERSION);
    stl_le_p(p + CPU_MISC_OFF + 4, cpu->cfg.vlenb);
    /* Preserve CSRs consumed or changed by the stub's final mret. */
    stq_le_p(p + CPU_MISC_OFF + 8, env->mstatus);
    stq_le_p(p + CPU_MISC_OFF + 16, env->mepc);
    cpu_physical_memory_read(KMH_MSIP_ADDR + hartid * sizeof(uint32_t),
                             msip, sizeof(msip));
    memcpy(p + CPU_MISC_OFF + 24, msip, sizeof(msip));
    stq_le_p(p + CPU_MISC_OFF + 32, env->profiling_insns);
    stq_le_p(p + CPU_MISC_OFF + 40, hartid);
    stq_le_p(p + CPU_CSR_RESERVED_OFF, 1);
    return true;
}

static bool kmh_cpu_save(KmhBoscCheckpoint *s, Error **errp)
{
    CPUState *cs;

    if (!s->active_hart_count) {
        error_setg(errp, "portable checkpoint has no selected application harts");
        return false;
    }
    CPU_FOREACH(cs) {
        RISCVCPU *cpu = RISCV_CPU(cs);
        if (cpu->env.mhartid < KMH_BOSC_CPT_CPU_RECORDS &&
            (s->active_hart_mask & (UINT64_C(1) << cpu->env.mhartid)) &&
            !kmh_cpu_save_one(s, cpu, errp)) {
            return false;
        }
    }
    return true;
}

static size_t aplic_size(RISCVAPLICState *a)
{
    return a ? 48 + a->num_irqs * 12 + a->num_harts * 12 : 0;
}

static uint8_t *put_aplic(uint8_t *p, RISCVAPLICState *a)
{
    stl_le_p(p + 0, a->mmode);
    stl_le_p(p + 4, a->num_irqs);
    stl_le_p(p + 8, a->num_harts);
    stl_le_p(p + 12, a->domaincfg);
    stl_le_p(p + 16, a->mmsicfgaddr);
    stl_le_p(p + 20, a->mmsicfgaddrH);
    stl_le_p(p + 24, a->smsicfgaddr);
    stl_le_p(p + 28, a->smsicfgaddrH);
    stl_le_p(p + 32, a->genmsi);
    stl_le_p(p + 36, a->hartid_base);
    p += 48;
    for (unsigned int i = 0; i < a->num_irqs; i++) {
        stl_le_p(p + i * 12, a->sourcecfg[i]);
        stl_le_p(p + i * 12 + 4, a->state[i]);
        stl_le_p(p + i * 12 + 8, a->target[i]);
    }
    p += a->num_irqs * 12;
    for (unsigned int i = 0; i < a->num_harts; i++) {
        stl_le_p(p + i * 12, a->idelivery[i]);
        stl_le_p(p + i * 12 + 4, a->iforce[i]);
        stl_le_p(p + i * 12 + 8, a->ithreshold[i]);
    }
    return p + a->num_harts * 12;
}

static size_t imsic_size(RISCVIMSICState *i)
{
    return i ? 32 + i->num_pages * 8 + i->num_eistate * 4 : 0;
}

static uint8_t *put_imsic(uint8_t *p, RISCVIMSICState *i)
{
    stl_le_p(p, i->mmode);
    stl_le_p(p + 4, i->hartid);
    stl_le_p(p + 8, i->num_pages);
    stl_le_p(p + 12, i->num_irqs);
    stl_le_p(p + 16, i->num_eistate);
    p += 32;
    for (unsigned int n = 0; n < i->num_pages; n++) {
        stl_le_p(p + n * 8, i->eidelivery[n]);
        stl_le_p(p + n * 8 + 4, i->eithreshold[n]);
    }
    p += i->num_pages * 8;
    for (unsigned int n = 0; n < i->num_eistate; n++) {
        stl_le_p(p + n * 4, i->eistate[n]);
    }
    return p + i->num_eistate * 4;
}

static size_t kmh_aia_size(KmhBoscCheckpoint *s)
{
    size_t n = 32;
    for (unsigned int d = 0; d < s->num_aplic; d++) {
        n += aplic_size(RISCV_APLIC(s->aplic[d]));
    }
    for (unsigned int d = 0; d < s->num_imsic; d++) {
        n += imsic_size(RISCV_IMSIC(s->imsic[d]));
    }
    return n;
}

static void kmh_aia_save(KmhBoscCheckpoint *s, uint8_t *p, size_t len)
{
    memset(p, 0, len);
    stl_le_p(p, 0x31414941U);
    stl_le_p(p + 4, 1);
    stl_le_p(p + 8, len);
    stl_le_p(p + 12, s->num_aplic);
    stl_le_p(p + 16, s->num_imsic);
    p += 32;
    for (unsigned int d = 0; d < s->num_aplic; d++) {
        p = put_aplic(p, RISCV_APLIC(s->aplic[d]));
    }
    for (unsigned int d = 0; d < s->num_imsic; d++) {
        p = put_imsic(p, RISCV_IMSIC(s->imsic[d]));
    }
}

static const uint8_t *get_aplic(const uint8_t *p, const uint8_t *end,
                                RISCVAPLICState *a, Error **errp)
{
    uint32_t irqs, harts;
    if (!a || end - p < 48) {
        goto bad;
    }
    irqs = ldl_le_p(p + 4);
    harts = ldl_le_p(p + 8);
    if (ldl_le_p(p) != a->mmode || ldl_le_p(p + 36) != a->hartid_base ||
        irqs != a->num_irqs || harts != a->num_harts ||
        end - p < 48 + irqs * 12 + harts * 12) {
        goto bad;
    }
    a->domaincfg = ldl_le_p(p + 12);
    a->mmsicfgaddr = ldl_le_p(p + 16);
    a->mmsicfgaddrH = ldl_le_p(p + 20);
    a->smsicfgaddr = ldl_le_p(p + 24);
    a->smsicfgaddrH = ldl_le_p(p + 28);
    a->genmsi = ldl_le_p(p + 32);
    p += 48;
    for (unsigned int i = 0; i < irqs; i++) {
        a->sourcecfg[i] = ldl_le_p(p + i * 12);
        a->state[i] = ldl_le_p(p + i * 12 + 4);
        a->target[i] = ldl_le_p(p + i * 12 + 8);
    }
    p += irqs * 12;
    for (unsigned int i = 0; i < harts; i++) {
        a->idelivery[i] = ldl_le_p(p + i * 12);
        a->iforce[i] = ldl_le_p(p + i * 12 + 4);
        a->ithreshold[i] = ldl_le_p(p + i * 12 + 8);
    }
    return p + harts * 12;
bad:
    error_setg(errp, "portable APLIC topology mismatch");
    return NULL;
}

static const uint8_t *get_imsic(const uint8_t *p, const uint8_t *end,
                                RISCVIMSICState *i, Error **errp)
{
    uint32_t pages, irqs, states;
    if (!i || end - p < 32) {
        goto bad;
    }
    pages = ldl_le_p(p + 8);
    irqs = ldl_le_p(p + 12);
    states = ldl_le_p(p + 16);
    if (ldl_le_p(p) != i->mmode || ldl_le_p(p + 4) != i->hartid ||
        pages != i->num_pages || irqs != i->num_irqs ||
        states != i->num_eistate || end - p < 32 + pages * 8 + states * 4) {
        goto bad;
    }
    p += 32;
    for (unsigned int n = 0; n < pages; n++) {
        i->eidelivery[n] = ldl_le_p(p + n * 8);
        i->eithreshold[n] = ldl_le_p(p + n * 8 + 4);
    }
    p += pages * 8;
    for (unsigned int n = 0; n < states; n++) {
        i->eistate[n] = ldl_le_p(p + n * 4);
    }
    return p + states * 4;
bad:
    error_setg(errp, "portable IMSIC topology mismatch");
    return NULL;
}

static bool kmh_aia_load(KmhBoscCheckpoint *s, const uint8_t *p, size_t len,
                         Error **errp)
{
    const uint8_t *end = p + len;
    if (len < 32 || ldl_le_p(p) != 0x31414941U || ldl_le_p(p + 4) != 1 ||
        ldl_le_p(p + 8) != len || ldl_le_p(p + 12) != s->num_aplic ||
        ldl_le_p(p + 16) != s->num_imsic) {
        error_setg(errp, "invalid portable BOSC AIA section");
        return false;
    }
    p += 32;
    for (unsigned int d = 0; d < s->num_aplic; d++) {
        p = get_aplic(p, end, RISCV_APLIC(s->aplic[d]), errp);
        if (!p) {
            return false;
        }
    }
    for (unsigned int d = 0; d < s->num_imsic; d++) {
        p = get_imsic(p, end, RISCV_IMSIC(s->imsic[d]), errp);
        if (!p) {
            return false;
        }
    }
    if (p != end) {
        error_setg(errp, "portable AIA section has trailing data");
        return false;
    }
    return true;
}

/* Only idle 16550s are portable: no FIFO contents or host write watches. */
static bool kmh_uart_quiet(KmhBoscCheckpoint *s)
{
    for (unsigned int n = 0; n < s->num_uart; n++) {
        SerialState *u = &s->uart[n]->serial;

        if (!fifo8_is_empty(&u->recv_fifo) ||
            !fifo8_is_empty(&u->xmit_fifo) ||
            (u->lsr & 0x61) != 0x60 || u->tsr_retry ||
            (u->iir & 0xf) != 1 || u->timeout_ipending) {
            return false;
        }
    }
    return true;
}

static size_t kmh_uart_size(KmhBoscCheckpoint *s)
{
    return KMH_UART_HEADER_SIZE + s->num_uart * KMH_UART_ENTRY_SIZE;
}

static void kmh_uart_save(KmhBoscCheckpoint *s, uint8_t *p)
{
    stl_le_p(p, 0x31545255U); /* URT1 */
    stl_le_p(p + 4, 1);
    stl_le_p(p + 8, s->num_uart);
    p += KMH_UART_HEADER_SIZE;
    for (unsigned int n = 0; n < s->num_uart; n++) {
        SerialState *u = &s->uart[n]->serial;

        stl_le_p(p, u->divider);
        p[4] = u->ier;
        p[5] = u->lcr;
        p[6] = u->mcr;
        p[7] = u->scr;
        p[8] = u->fcr;
        p += KMH_UART_ENTRY_SIZE;
    }
}

static bool kmh_uart_load(KmhBoscCheckpoint *s, const uint8_t *p, size_t len,
                          Error **errp)
{
    if (len != kmh_uart_size(s) || ldl_le_p(p) != 0x31545255U ||
        ldl_le_p(p + 4) != 1 || ldl_le_p(p + 8) != s->num_uart) {
        error_setg(errp, "invalid portable BOSC UART section");
        return false;
    }
    p += KMH_UART_HEADER_SIZE;
    for (unsigned int n = 0; n < s->num_uart; n++) {
        SerialState *u = &s->uart[n]->serial;
        uint32_t divider = ldl_le_p(p);

        /* Register writes rebuild baud timing and host serial settings. */
        serial_io_ops.write(u, 1, 0, 1);
        serial_io_ops.write(u, 3, p[5] | 0x80, 1);
        serial_io_ops.write(u, 0, divider & 0xff, 1);
        serial_io_ops.write(u, 1, divider >> 8, 1);
        serial_io_ops.write(u, 3, p[5], 1);
        serial_io_ops.write(u, 4, p[6], 1);
        serial_io_ops.write(u, 7, p[7], 1);
        serial_io_ops.write(u, 2, p[8], 1);
        serial_io_ops.write(u, 1, p[4], 1);
        p += KMH_UART_ENTRY_SIZE;
    }
    return true;
}

static void put_entry(uint8_t *e, uint32_t type, uint64_t off,
                      uint64_t len, uint32_t crc)
{
    stl_le_p(e, type);
    stl_le_p(e + 4, 0);
    stq_le_p(e + 8, off);
    stq_le_p(e + 16, len);
    stl_le_p(e + 24, crc);
}

static const uint8_t *find_section(uint8_t *dir, uint32_t type,
                                   size_t *len, Error **errp);
#include "kmh_bosc_checkpoint_native.c.inc"

static bool kmh_prepare_directory(KmhBoscCheckpoint *s, Error **errp)
{
    if (s->format_version == KMH_NATIVE_VERSION) {
        return kmh_prepare_native_directory(s, errp);
    }
    MachineState *ms = s->machine;
    uint8_t *dir, *entry, *p;
    size_t aia_len;
    unsigned int sections = 2;
    dir = kmh_ram_ptr(s, KMH_DEVICE_STATE_BASE, KMH_DEVICE_STATE_SIZE, errp);
    if (!dir) {
        return false;
    }
    memset(dir, 0, KMH_DEVICE_STATE_SIZE);
    aia_len = kmh_aia_size(s);
    p = QEMU_ALIGN_PTR_UP(dir + KMH_CPT_HEADER_SIZE +
                          sections * KMH_CPT_ENTRY_SIZE, 64);
    entry = dir + KMH_CPT_HEADER_SIZE;
    kmh_aia_save(s, p, aia_len);
    put_entry(entry, KMH_CPT_SECTION_AIA, p - dir, aia_len,
              kmh_crc32c(p, aia_len));
    entry += KMH_CPT_ENTRY_SIZE;
    p = QEMU_ALIGN_PTR_UP(p + aia_len, 64);
    kmh_uart_save(s, p);
    put_entry(entry, KMH_CPT_SECTION_UART, p - dir, kmh_uart_size(s),
              kmh_crc32c(p, kmh_uart_size(s)));
    p += kmh_uart_size(s);
    if (p > dir + KMH_DEVICE_STATE_SIZE) {
        error_setg(errp, "portable device directory overflow");
        return false;
    }
    stq_le_p(dir, KMH_CPT_MAGIC);
    stl_le_p(dir + 8, KMH_CPT_VERSION);
    stl_le_p(dir + 12, KMH_CPT_HEADER_SIZE);
    stl_le_p(dir + 16, KMH_CPT_PLATFORM);
    stl_le_p(dir + 20, 1);
    stl_le_p(dir + 24, sections);
    stl_le_p(dir + 28, p - dir);
    stq_le_p(dir + 32, ms->ram_size);
    stq_le_p(dir + 40, s->cpu ? s->cpu->env.profiling_insns : 0);
    stl_le_p(dir + 52, s->single_soc);
    stl_le_p(dir + 56, s->die_mask);
    stl_le_p(dir + 60, s->machine->smp.cpus);
    stl_le_p(dir + 64, s->active_hart_count);
    stl_le_p(dir + 68, s->trigger_hartid);
    stq_le_p(dir + 72, s->active_hart_mask);
    for (unsigned int i = 0; i < ARRAY_SIZE(s->core_mask); i++) {
        stl_le_p(dir + 80 + i * sizeof(uint32_t), s->core_mask[i]);
    }
    stq_le_p(dir + 96, s->cpu_state_base);
    stq_le_p(dir + 104, KMH_CPU_STATE_STRIDE);
    stq_le_p(dir + 112, KMH_BOSC_CPT_CPU_SIZE);
    stl_le_p(dir + 48, 0);
    stl_le_p(dir + 48, kmh_crc32c(dir, p - dir));
    return true;
}

static bool kmh_validate_directory(KmhBoscCheckpoint *s, uint8_t **out,
                                   Error **errp)
{
    MachineState *ms = s->machine;
    uint8_t *dir = kmh_ram_ptr(s, KMH_DEVICE_STATE_BASE,
                                KMH_DEVICE_STATE_SIZE, errp);
    uint32_t total, expected, stored;
    if (!dir) {
        return false;
    }
    if (ldq_le_p(dir) != KMH_CPT_MAGIC ||
        ldl_le_p(dir + 16) != KMH_CPT_PLATFORM ||
        ldl_le_p(dir + 20) != 1 || ldq_le_p(dir + 32) != ms->ram_size ||
        ldl_le_p(dir + 52) != s->single_soc ||
        ldl_le_p(dir + 56) != s->die_mask ||
        ldl_le_p(dir + 60) != ms->smp.cpus) {
        error_setg(errp, "checkpoint is not a portable BOSC image with mismatched platform topology");
        return false;
    }
    s->format_version = ldl_le_p(dir + 8);
    s->cpu_state_base = kmh_bosc_checkpoint_cpu_state_base(s);
    if (s->format_version == KMH_CPT_VERSION_V1) {
        if (ldl_le_p(dir + 12) != KMH_CPT_HEADER_SIZE_V1 ||
            s->active_hart_count != 1 || s->active_hart_mask != 1) {
            error_setg(errp, "portable BOSC v1 checkpoint is single-hart only");
            return false;
        }
    } else if (s->format_version == KMH_CPT_VERSION ||
               s->format_version == KMH_NATIVE_VERSION) {
        if (ldl_le_p(dir + 12) != (s->format_version == KMH_NATIVE_VERSION ?
                KMH_NATIVE_HEADER_SIZE : KMH_CPT_HEADER_SIZE) ||
            ldl_le_p(dir + 64) != s->active_hart_count ||
            ldl_le_p(dir + 68) != s->trigger_hartid ||
            ldq_le_p(dir + 72) != s->active_hart_mask ||
            ldq_le_p(dir + 96) != s->cpu_state_base ||
            ldq_le_p(dir + 104) != KMH_CPU_STATE_STRIDE ||
            ldq_le_p(dir + 112) != KMH_BOSC_CPT_CPU_SIZE) {
            error_setg(errp, "portable BOSC v2 checkpoint has mismatched hart layout");
            return false;
        }
        for (unsigned int i = 0; i < ARRAY_SIZE(s->core_mask); i++) {
            if (ldl_le_p(dir + 80 + i * sizeof(uint32_t)) != s->core_mask[i]) {
                error_setg(errp, "portable BOSC v2 checkpoint core mask mismatch");
                return false;
            }
        }
    } else {
        error_setg(errp, "unsupported portable BOSC checkpoint version %u",
                   s->format_version);
        return false;
    }
    total = ldl_le_p(dir + 28);
    if (total < ldl_le_p(dir + 12) +
        (s->format_version == KMH_NATIVE_VERSION ? 1 : 2) * KMH_CPT_ENTRY_SIZE ||
        total > KMH_DEVICE_STATE_SIZE || ldl_le_p(dir + 24) !=
        (s->format_version == KMH_NATIVE_VERSION ? 1 : 2)) {
        error_setg(errp, "invalid portable checkpoint directory size");
        return false;
    }
    stored = ldl_le_p(dir + 48);
    stl_le_p(dir + 48, 0);
    expected = kmh_crc32c(dir, total);
    stl_le_p(dir + 48, stored);
    if (stored != expected) {
        error_setg(errp, "portable checkpoint directory checksum mismatch");
        return false;
    }
    *out = dir;
    return true;
}

static const uint8_t *find_section(uint8_t *dir, uint32_t type,
                                   size_t *len, Error **errp)
{
    uint32_t count = ldl_le_p(dir + 24), total = ldl_le_p(dir + 28);
    uint32_t header = ldl_le_p(dir + 12);
    for (unsigned int i = 0; i < count; i++) {
        uint8_t *e = dir + header + i * KMH_CPT_ENTRY_SIZE;
        uint64_t off = ldq_le_p(e + 8), n = ldq_le_p(e + 16);
        if (off > total || n > total - off) {
            error_setg(errp, "portable checkpoint section is out of bounds");
            return NULL;
        }
        if (ldl_le_p(e) == type) {
            if (ldl_le_p(e + 24) != kmh_crc32c(dir + off, n)) {
                error_setg(errp, "portable checkpoint section checksum "
                           "mismatch");
                return NULL;
            }
            *len = n;
            return dir + off;
        }
    }
    error_setg(errp, "portable checkpoint section %u is missing", type);
    return NULL;
}

static bool kmh_restore_devices(KmhBoscCheckpoint *s, uint8_t *dir,
                                 Error **errp)
{
    const uint8_t *aia, *uart;
    size_t aia_len, uart_len;

    aia = find_section(dir, KMH_CPT_SECTION_AIA, &aia_len, errp);
    if (!aia || !kmh_aia_load(s, aia, aia_len, errp)) {
        return false;
    }
    uart = find_section(dir, KMH_CPT_SECTION_UART, &uart_len, errp);
    return uart && kmh_uart_load(s, uart, uart_len, errp);
}

static bool kmh_quiesce(KmhBoscCheckpoint *s, Error **errp)
{
    if (!kmh_uart_quiet(s)) {
        error_setg(errp, "checkpoint busy: UART has pending I/O or interrupts");
        return false;
    }
    /* APLIC/IMSIC pending bits are serialized and restored exactly. */
    info_report("checkpoint: CPU stopped; serializing APLIC/IMSIC state");
    return true;
}

#ifdef CONFIG_ZSTD
static bool kmh_zstd_save(KmhBoscCheckpoint *s, const char *path, Error **errp)
{
    MachineState *ms = s->machine;
    uint8_t *ram = memory_region_get_ram_ptr(ms->ram);
    g_autofree uint8_t *native_chunk = s->format_version == KMH_NATIVE_VERSION ?
                                       g_malloc(64 * MiB) : NULL;
    g_autofree char *tmp = g_strdup_printf("%s.tmp", path);
    g_autofree char *dirname = g_path_get_dirname(path);
    FILE *fp = NULL;
    ZSTD_CCtx *cctx = NULL;
    void *out = NULL;
    size_t out_size, pos = 0;
    bool ok = false;
    if (g_mkdir_with_parents(dirname, 0755) < 0) {
        error_setg_errno(errp, errno, "cannot create checkpoint directory");
        return false;
    }
    fp = fopen(tmp, "wb");
    cctx = ZSTD_createCCtx();
    out_size = ZSTD_CStreamOutSize();
    out = g_malloc(out_size);
    if (!fp || !cctx ||
        ZSTD_isError(ZSTD_CCtx_setParameter(
            cctx, ZSTD_c_compressionLevel, 1)) ||
        ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1)) ||
        ZSTD_isError(ZSTD_CCtx_setPledgedSrcSize(cctx, ms->ram_size))) {
        error_setg(errp, "failed to initialize portable checkpoint writer");
        goto out;
    }
    while (pos < ms->ram_size) {
        size_t n = MIN(ms->ram_size - pos, 64 * MiB);
        uint8_t *source = ram + pos;
        if (native_chunk) {
            const uint64_t starts[] = { 0, KMH_NATIVE_WORK - KMH_DRAM_BASE };
            const uint64_t sizes[] = { KMH_NATIVE_ENTRY_SIZE, KMH_NATIVE_WORK_SIZE };
            const uint8_t *blobs[] = { kmh_native_entry, kmh_native_firmware };
            const size_t lengths[] = { sizeof(kmh_native_entry), sizeof(kmh_native_firmware) };
            for (unsigned j = 0; j < 2; j++) {
                if (pos < starts[j] + sizes[j] && pos + n > starts[j]) {
                    if (source == ram + pos) {
                        memcpy(native_chunk, source, n);
                        source = native_chunk;
                    }
                    uint64_t lo = MAX(pos, starts[j]);
                    uint64_t hi = MIN(pos + n, starts[j] + sizes[j]);
                    memset(source + lo - pos, 0, hi - lo);
                    uint64_t blob_hi = MIN(hi, starts[j] + lengths[j]);
                    if (blob_hi > lo) {
                        memcpy(source + lo - pos, blobs[j] + lo - starts[j], blob_hi - lo);
                    }
                }
            }
        }
        ZSTD_inBuffer in = { source, n, 0 };
        while (in.pos < in.size) {
            ZSTD_outBuffer ob = { out, out_size, 0 };
            size_t ret = ZSTD_compressStream2(cctx, &ob, &in, ZSTD_e_continue);
            if (ZSTD_isError(ret) ||
                (ob.pos && fwrite(out, 1, ob.pos, fp) != ob.pos)) {
                error_setg(errp, "portable checkpoint compression/write "
                           "failed");
                goto out;
            }
        }
        pos += n;
    }
    for (;;) {
        ZSTD_inBuffer in = { NULL, 0, 0 };
        ZSTD_outBuffer ob = { out, out_size, 0 };
        size_t ret = ZSTD_compressStream2(cctx, &ob, &in, ZSTD_e_end);
        if (ZSTD_isError(ret) ||
            (ob.pos && fwrite(out, 1, ob.pos, fp) != ob.pos)) {
            error_setg(errp, "failed to finish portable checkpoint");
            goto out;
        }
        if (!ret) {
            break;
        }
    }
    if (fflush(fp) || fsync(fileno(fp))) {
        error_setg_errno(errp, errno, "failed to flush portable checkpoint");
        goto out;
    }
    if (fclose(fp)) {
        fp = NULL;
        error_setg_errno(errp, errno, "failed to close portable checkpoint");
        goto out;
    }
    fp = NULL;
    if (rename(tmp, path)) {
        error_setg_errno(errp, errno, "failed to commit portable checkpoint");
        goto out;
    }
    info_report("checkpoint: portable checkpoint committed: %s", path);
    ok = true;
out:
    if (fp) {
        fclose(fp);
    }
    if (!ok) {
        unlink(tmp);
    }
    ZSTD_freeCCtx(cctx);
    g_free(out);
    return ok;
}

static bool kmh_zstd_load(KmhBoscCheckpoint *s, const char *path, Error **errp)
{
    MachineState *ms = s->machine;
    uint8_t *ram = memory_region_get_ram_ptr(ms->ram);
    FILE *fp = fopen(path, "rb");
    ZSTD_DCtx *dctx = ZSTD_createDCtx();
    size_t is = ZSTD_DStreamInSize(), os = ZSTD_DStreamOutSize(), pos = 0;
    uint8_t *ib = g_malloc(is), *ob = g_malloc(os);
    bool done = false, ok = false;
    if (!fp || !dctx) {
        error_setg_errno(errp, errno, "cannot open portable checkpoint '%s'",
                         path);
        goto out;
    }
    while (!done) {
        size_t nr = fread(ib, 1, is, fp);
        ZSTD_inBuffer in = { ib, nr, 0 };
        if (!nr) {
            error_setg(errp, "truncated portable checkpoint");
            goto out;
        }
        while (in.pos < in.size) {
            ZSTD_outBuffer output = { ob, os, 0 };
            size_t ret = ZSTD_decompressStream(dctx, &output, &in);
            if (ZSTD_isError(ret) || output.pos > ms->ram_size - pos) {
                error_setg(errp, "invalid portable checkpoint stream");
                goto out;
            }
            /* Preserve zero pages without allocating untouched DDR slots. */
            if (!buffer_is_zero(ob, output.pos) ||
                !buffer_is_zero(ram + pos, output.pos)) {
                memcpy(ram + pos, ob, output.pos);
            }
            pos += output.pos;
            if (!ret) {
                done = true;
                if (in.pos != in.size) {
                    error_setg(errp, "portable checkpoint has trailing frames");
                    goto out;
                }
            }
        }
    }
    if (pos != ms->ram_size || fgetc(fp) != EOF) {
        error_setg(errp, "portable checkpoint DRAM size mismatch");
        goto out;
    }
    ok = true;
out:
    if (fp) {
        fclose(fp);
    }
    ZSTD_freeDCtx(dctx);
    g_free(ib);
    g_free(ob);
    return ok;
}

static bool kmh_raw_load(KmhBoscCheckpoint *s, const char *path, Error **errp)
{
    MachineState *ms = s->machine;
    uint8_t *ram = memory_region_get_ram_ptr(ms->ram);
    uint8_t *buf = g_malloc(16 * MiB);
    FILE *fp = fopen(path, "rb");
    struct stat st;
    uint64_t pos = 0;
    off_t data, hole;
    bool ok = false;

    if (!fp || fstat(fileno(fp), &st) < 0 ||
        (uint64_t)st.st_size != ms->ram_size) {
        error_setg(errp, "raw checkpoint must be exactly %" PRIu64
                   " bytes (the configured RAM size)", ms->ram_size);
        goto out;
    }
    data = lseek(fileno(fp), 0, SEEK_DATA);
    if (data == (off_t)-1 && errno == EINVAL) {
        /* Filesystems without sparse-file extent support: use a full scan. */
        data = 0;
        while (pos < ms->ram_size) {
            size_t want = MIN((uint64_t)(16 * MiB), ms->ram_size - pos);
            size_t nr = fread(buf, 1, want, fp);
            if (nr != want) {
                error_setg(errp, "short read from raw checkpoint at offset %" PRIu64,
                           pos);
                goto out;
            }
            if (!buffer_is_zero(buf, nr)) {
                memcpy(ram + pos, buf, nr);
            }
            pos += nr;
        }
    } else {
        while (data != (off_t)-1 && (uint64_t)data < ms->ram_size) {
            hole = lseek(fileno(fp), data, SEEK_HOLE);
            if (hole == (off_t)-1) {
                error_setg_errno(errp, errno, "cannot find raw checkpoint hole");
                goto out;
            }
            pos = (uint64_t)data;
            if (fseek(fp, data, SEEK_SET) != 0) {
                error_setg_errno(errp, errno, "cannot seek raw checkpoint data");
                goto out;
            }
            while (pos < (uint64_t)hole) {
                size_t want = MIN((uint64_t)(16 * MiB),
                                  (uint64_t)hole - pos);
                size_t nr = fread(buf, 1, want, fp);
                if (nr != want) {
                    error_setg(errp, "short read from raw checkpoint at offset %" PRIu64,
                               pos);
                    goto out;
                }
                if (!buffer_is_zero(buf, nr)) {
                    memcpy(ram + pos, buf, nr);
                }
                pos += nr;
            }
            data = lseek(fileno(fp), hole, SEEK_DATA);
            if (data == (off_t)-1 && errno == ENXIO) {
                break;
            }
        }
    }
    ok = true;
out:
    if (fp) {
        fclose(fp);
    }
    g_free(buf);
    return ok;
}

#else
static bool kmh_zstd_save(KmhBoscCheckpoint *s, const char *path, Error **errp)
{
    error_setg(errp, "checkpoint requires a QEMU build with zstd support");
    return false;
}

static bool kmh_zstd_load(KmhBoscCheckpoint *s, const char *path, Error **errp)
{
    return kmh_zstd_save(s, path, errp);
}
#endif

static bool kmh_load_stub(KmhBoscCheckpoint *s, Error **errp)
{
    g_autofree uint8_t *data = NULL;
    gsize size = 0;
    uint8_t *dst;
    if (!s->checkpoint_stub || !*s->checkpoint_stub) {
        error_setg(errp, "checkpoint-stub is required for portable restore");
        return false;
    }
    if (!g_file_get_contents(s->checkpoint_stub, (char **)&data, &size, NULL) ||
        !size || size > CPU_MAGIC_OFF) {
        error_setg(errp, "invalid KMH checkpoint restore stub '%s'",
                   s->checkpoint_stub);
        return false;
    }
    dst = kmh_ram_ptr(s, KMH_STUB_BASE, size, errp);
    if (!dst) {
        return false;
    }
    memcpy(dst, data, size);
    return true;
}

static void kmh_prepare_timer_for_stub(KmhBoscCheckpoint *s)
{
    uint8_t *p;
    uint64_t mtime;
    unsigned int hartid;

    p = kmh_cpu_record(s, s->trigger_hartid, &error_abort);
    mtime = ldq_le_p(p + CPU_MTIME_OFF);
    if (s->mtimer) {
        riscv_aclint_mtimer_set_time(s->mtimer, mtime);
    }
    for (hartid = 0; hartid < KMH_BOSC_CPT_CPU_RECORDS; hartid++) {
        uint64_t mtimecmp;
        if (!(s->active_hart_mask & (UINT64_C(1) << hartid))) {
            continue;
        }
        p = kmh_cpu_record(s, hartid, &error_abort);
        mtimecmp = ldq_le_p(p + CPU_MTIMECMP_OFF);
        cpu_physical_memory_write(KMH_MTIMECMP_ADDR +
                                  hartid * sizeof(uint64_t), &mtimecmp,
                                  sizeof(mtimecmp));
        cpu_physical_memory_write(KMH_MSIP_ADDR +
                                  hartid * sizeof(uint32_t),
                                  p + CPU_MISC_OFF + 24, sizeof(uint32_t));
    }
}

bool kmh_bosc_checkpoint_save(KmhBoscCheckpoint *s, const char *path,
                                   Error **errp)
{
    bool ok;
    CPUState *other;

    if (kvm_enabled() || !s->active_hart_count || !path || !*path) {
        error_setg(errp, "portable checkpoint requires TCG and a path");
        return false;
    }
    CPU_FOREACH(other) {
        RISCVCPU *cpu = RISCV_CPU(other);
        unsigned int hartid = cpu->env.mhartid;
        if (hartid >= KMH_BOSC_CPT_CPU_RECORDS) {
            continue;
        }
        if (s->active_hart_mask & (UINT64_C(1) << hartid)) {
            if (other->start_powered_off) {
                error_setg(errp, "checkpoint hart %u is powered off", hartid);
                return false;
            }
        } else if (!other->start_powered_off && !other->halted) {
            error_setg(errp, "unselected hart %u is running", hartid);
            return false;
        }
    }
    if (s->restore_pending || s->restoring_harts) {
        error_setg(errp, "checkpoint restore is still in progress");
        return false;
    }
    CPU_FOREACH(other) {
        RISCVCPU *cpu = RISCV_CPU(other);
        if (cpu->env.mhartid < KMH_BOSC_CPT_CPU_RECORDS &&
            (s->active_hart_mask & (UINT64_C(1) << cpu->env.mhartid)) &&
            cpu->env.virt_enabled) {
            error_setg(errp, "portable BOSC checkpoint does not support virtualized harts");
            return false;
        }
    }
    if (s->saving) {
        error_setg(errp, "checkpoint save already in progress");
        return false;
    }
    if (s->save_version == KMH_NATIVE_VERSION) {
        CPU_FOREACH(other) {
            RISCVCPU *cpu = RISCV_CPU(other);
            unsigned hart = cpu->env.mhartid;
            if (hart < 64 && (s->active_hart_mask & (UINT64_C(1) << hart)) &&
                cpu->env.priv > PRV_S) {
                error_setg(errp, "native v3 checkpoint requires U/S-mode "
                           "harts (hart %u is M-mode); use "
                           "checkpoint-format=v2 for legacy snapshots", hart);
                return false;
            }
        }
    }
    s->format_version = s->save_version;
    s->cpu_state_base = kmh_bosc_checkpoint_cpu_state_base(s);
    s->saving = true;
    ok = kmh_quiesce(s, errp) && kmh_cpu_save(s, errp) &&
         kmh_prepare_directory(s, errp) && kmh_zstd_save(s, path, errp);
    s->saving = false;
    return ok;
}

bool kmh_bosc_checkpoint_load(KmhBoscCheckpoint *s, const char *path,
                                   Error **errp)
{
    FILE *fp;
    if (kvm_enabled() || !s->active_hart_count) {
        error_setg(errp, "portable checkpoint restore requires TCG and selected harts");
        return false;
    }
    fp = fopen(path, "rb");
    if (!fp) {
        error_setg_errno(errp, errno, "cannot open checkpoint '%s'", path);
        return false;
    }
    fclose(fp);
    g_free(s->restore_path);
    s->restore_path = g_strdup(path);
    s->restore_pending = true;
    s->restored = false;
    return true;
}

static void kmh_restore_before_start(void *opaque, bool running, RunState state)
{
    KmhBoscCheckpoint *s = opaque;
    CPUState *cs;
    uint8_t *dir;
    Error *err = NULL;
    if (!running || !s->restore_pending || s->restored) {
        return;
    }
    if (!(s->raw_checkpoint ? kmh_raw_load(s, s->restore_path, &err) :
          kmh_zstd_load(s, s->restore_path, &err)) ||
        !kmh_validate_directory(s, &dir, &err)) {
        error_report_err(err);
        exit(EXIT_FAILURE);
    }
    if (s->format_version == KMH_NATIVE_VERSION) {
        if (s->checkpoint_stub || !kmh_validate_native(s, dir, &err)) {
            if (!err) {
                error_setg(&err, "native v3 checkpoint uses its embedded "
                           "firmware; remove checkpoint-stub");
            }
            error_report_err(err);
            exit(EXIT_FAILURE);
        }
        CPU_FOREACH(cs) {
            RISCVCPU *cpu = RISCV_CPU(cs);
            unsigned hart = cpu->env.mhartid;
            if (hart < 64 && (s->active_hart_mask & (UINT64_C(1) << hart))) {
                uint8_t *p = kmh_cpu_record(s, hart, &error_abort);
                if (ldq_le_p(p + CPU_MAGIC_OFF) != CPU_MAGIC ||
                    ldl_le_p(p + CPU_MISC_OFF) != KMH_NATIVE_CPU_ABI ||
                    ldl_le_p(p + CPU_MISC_OFF + 4) != 16 ||
                    ldq_le_p(p + CPU_MODE_OFF) > PRV_S) {
                    error_report("invalid native CPU record for hart %u", hart);
                    exit(EXIT_FAILURE);
                }
                cpu->env.resetvec = KMH_NATIVE_ENTRY;
                cpu->env.pc = KMH_NATIVE_ENTRY;
                cpu->checkpoint_restoring = false;
                cpu->env.profiling_insns = ldq_le_p(p + CPU_MISC_OFF + 32);
            }
        }
        s->native_pending_mask = s->active_hart_mask;
        s->restoring_harts = s->active_hart_count;
        s->restore_pending = false;
        s->restored = true;
        s->checkpoint_next = s->cpu->env.profiling_insns + s->checkpoint_interval;
        info_report("checkpoint: native v3 RAM loaded; all %u harts reset "
                    "at 0x80000000; CPU/devices restored by guest firmware",
                    s->active_hart_count);
        return;
    }
    if (s->bios_checkpoint || !kmh_restore_devices(s, dir, &err) ||
        !kmh_load_stub(s, &err)) {
        if (!err) {
            error_setg(&err, "-bios checkpoint requires a self-contained v3 image");
        }
        error_report_err(err);
        exit(EXIT_FAILURE);
    }
    s->resume_insns = ldq_le_p(dir + 40);
    kmh_prepare_timer_for_stub(s);
    s->restoring_harts = 0;
    CPU_FOREACH(cs) {
        RISCVCPU *cpu = RISCV_CPU(cs);
        unsigned int hartid = cpu->env.mhartid;
        uint8_t *p;
        uint32_t record_version;

        if (hartid >= KMH_BOSC_CPT_CPU_RECORDS ||
            !(s->active_hart_mask & (UINT64_C(1) << hartid))) {
            continue;
        }
        p = kmh_cpu_record(s, hartid, &error_abort);
        record_version = ldl_le_p(p + CPU_MISC_OFF);
        if (ldq_le_p(p + CPU_MAGIC_OFF) != CPU_MAGIC ||
            record_version != s->format_version ||
            ldl_le_p(p + CPU_MISC_OFF + 4) != cpu->cfg.vlenb) {
            error_report("invalid portable BOSC CPU state block for hart %u",
                         hartid);
            exit(EXIT_FAILURE);
        }
        cpu->checkpoint_restoring = true;
        cpu->env.profiling_insns = record_version >= KMH_CPT_VERSION ?
            ldq_le_p(p + CPU_MISC_OFF + 32) : s->resume_insns;
        CPU(cpu)->halted = false;
        CPU(cpu)->exception_index = -1;
        cpu->env.pc = KMH_STUB_BASE;
        cpu->env.priv = PRV_M;
        cpu->env.virt_enabled = false;
        /* The portable stub receives the per-machine record base in t5. */
        cpu->env.gpr[30] = s->cpu_state_base;
        /* Keep reset-only WARL fields while the stub writes mstatus. */
        cpu->env.xl = riscv_cpu_mxl(&cpu->env);
        cpu->env.load_res = -1;
        cpu_resume(CPU(cpu));
        tlb_flush(CPU(cpu));
        s->restoring_harts++;
    }
    if (!s->restoring_harts) {
        error_report("portable BOSC checkpoint contains no restorable harts");
        exit(EXIT_FAILURE);
    }
    s->checkpoint_next = s->resume_insns + s->checkpoint_interval;
    s->restored = true;
    s->restore_pending = false;
    info_report("checkpoint: portable BOSC CPU/AIA state imported; "
                "restore stub entered for %u harts", s->restoring_harts);
}

static void kmh_save_bh(void *opaque)
{
    KmhBoscCheckpoint *s = opaque;
    g_autofree char *path = g_steal_pointer(&s->save_path);
    Error *err = NULL;
    bool ok;
    s->save_bh_scheduled = false;
    s->save_pending = false;
    ok = kmh_bosc_checkpoint_save(s, path, &err);
    if (!ok) {
        error_report_err(err);
    }
    if (ok && s->checkpoint_exit) {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    } else if (ok) {
        vm_start();
    } else {
        qemu_system_shutdown_request_with_code(
            SHUTDOWN_CAUSE_HOST_ERROR, EXIT_FAILURE);
    }
}

static void kmh_vm_state_change(void *opaque, bool running, RunState state)
{
    KmhBoscCheckpoint *s = opaque;

    if (running || state != RUN_STATE_SAVE_VM || !s->save_pending ||
        s->save_bh_scheduled) {
        return;
    }
    s->save_bh_scheduled = true;
    aio_bh_schedule_oneshot(qemu_get_aio_context(), kmh_save_bh, opaque);
}

static bool kmh_schedule_save(KmhBoscCheckpoint *s, const char *path)
{
    if (s->saving || s->save_pending || !path || !*path) {
        return false;
    }
    g_free(s->save_path);
    s->save_path = g_strdup(path);
    s->save_pending = true;
    vm_stop(RUN_STATE_SAVE_VM);
    return true;
}

static void kmh_bosc_checkpoint_machine_done(Notifier *notifier, void *data)
{
    KmhBoscCheckpoint *s = container_of(notifier, KmhBoscCheckpoint,
                                       machine_done);
    qemu_add_vm_change_state_handler_prio_full(
        kmh_vm_state_change, kmh_restore_before_start, NULL, s, INT_MIN);
}

static bool kmh_bosc_checkpoint_marker(RISCVCPU *cpu)
{
    KmhBoscCheckpoint *s = cpu->checkpoint_opaque;
    CPURISCVState *env = &cpu->env;
    if (!s || !s->checkpoint_marker || !s->checkpoint_path || s->saving) {
        return false;
    }
    env->pc += 4;
    if (!kmh_schedule_save(s, s->checkpoint_path)) {
        env->pc -= 4;
        return false;
    }
    return true;
}

static bool kmh_bosc_checkpoint_interval(RISCVCPU *cpu, target_ulong pc)
{
    KmhBoscCheckpoint *s = cpu->checkpoint_opaque;
    CPURISCVState *env = &cpu->env;
    g_autofree char *path = NULL;

    if (s && s->format_version == KMH_NATIVE_VERSION &&
        (s->native_pending_mask & (UINT64_C(1) << env->mhartid))) {
        if ((pc >= KMH_NATIVE_WORK && pc < KMH_NATIVE_WORK + KMH_NATIVE_WORK_SIZE) ||
            (pc >= KMH_NATIVE_ENTRY && pc < KMH_NATIVE_ENTRY + KMH_NATIVE_ENTRY_SIZE)) {
            return false;
        }
        /* Observation only: no architectural state or IRQ changes. */
        uint8_t *p = kmh_cpu_record(s, env->mhartid, &error_abort);
        env->profiling_insns = ldq_le_p(p + CPU_MISC_OFF + 32);
        s->native_pending_mask &= ~(UINT64_C(1) << env->mhartid);
        if (!--s->restoring_harts) {
            info_report("checkpoint: native firmware returned to all "
                        "selected harts; no host CSR/device fixups");
        }
        return false;
    }

    /* The stub rebuilds the return frame; never slice it or count its code. */
    if (cpu->checkpoint_restoring) {
        if (pc >= KMH_STUB_BASE && pc < KMH_STUB_BASE + CPU_MAGIC_OFF) {
            return false;
        }
        uint8_t *p = kmh_cpu_record(s, env->mhartid, &error_abort);

        if (s->format_version >= KMH_CPT_VERSION) {
            env->profiling_insns = ldq_le_p(p + CPU_MISC_OFF + 32);
        } else {
            env->profiling_insns = s->resume_insns;
        }
        env->mstatus = ldq_le_p(p + CPU_MISC_OFF + 8);
        env->mepc = ldq_le_p(p + CPU_MISC_OFF + 16);
        env->pc = pc;
        cpu->checkpoint_restoring = false;
        if (s->restoring_harts) {
            s->restoring_harts--;
            if (!s->restoring_harts) {
                info_report("checkpoint: restore stubs completed for all selected harts");
            }
        }
        if (!s->checkpoint_interval && !s->restoring_harts) {
            cpu->checkpoint_interval = NULL;
        }
        /* Rebuild TB flags after restoring the original mstatus. */
        return true;
    }
    if (!s || !s->checkpoint_interval || !s->checkpoint_path ||
        cpu->env.mhartid != s->trigger_hartid ||
        env->profiling_insns < s->checkpoint_next || s->saving ||
        s->restore_pending || s->restoring_harts) {
        return false;
    }
    env->pc = pc;
    path = s->checkpoint_exit ? g_strdup(s->checkpoint_path) :
        g_strdup_printf("%s.%" PRIu64 ".zst", s->checkpoint_path,
                        env->profiling_insns);
    s->checkpoint_next += s->checkpoint_interval;
    return kmh_schedule_save(s, path);
}

void kmh_bosc_checkpoint_init(KmhBoscCheckpoint *s)
{
    CPUState *cs;

    CPU_FOREACH(cs) {
        RISCVCPU *cpu = RISCV_CPU(cs);
        unsigned int hartid = cpu->env.mhartid;

        if (hartid >= KMH_BOSC_CPT_CPU_RECORDS ||
            !(s->active_hart_mask & (UINT64_C(1) << hartid))) {
            continue;
        }
        cpu->checkpoint_opaque = s;
        cpu->checkpoint_marker = kmh_bosc_checkpoint_marker;
        cpu->checkpoint_count = s->checkpoint_interval != 0;
        if (s->checkpoint_interval || s->checkpoint_load) {
            cpu->checkpoint_interval = kmh_bosc_checkpoint_interval;
        }
    }
    s->machine_done.notify = kmh_bosc_checkpoint_machine_done;
    qemu_add_machine_init_done_notifier(&s->machine_done);
}
