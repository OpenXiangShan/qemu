#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg-gvec-desc.h"
#include "internals.h"

static inline bool require_matrix(DisasContext *s)
{
    /* return s->xmstatus_ms != 0; */
    return true;
}

/* Matrix Configuration translation */
typedef enum MTileSel {
    MTILE_M = 0,
    MTILE_N = 1,
    MTILE_K = 2,
} MTileSel;

static bool do_msettile(DisasContext *s, TCGv val, MTileSel which)
{
    if (!require_matrix(s)) {
        return false;
    }

    TCGv t = temp_new(s);
    /* Conservative mask: immediate is uimm10; rs1-form might supply wider,
     * but architecturally tile sizes are bounded. Keeping low 10 bits is safe.
     */
    tcg_gen_andi_tl(t, val, 0x3ff);

    switch (which) {
    case MTILE_M:
        tcg_gen_mov_tl(mtilem, t);
        gen_helper_msettilem(tcg_env, t);
        break;
    case MTILE_N:
        tcg_gen_mov_tl(mtilen, t);
        gen_helper_msettilen(tcg_env, t);
        break;
    case MTILE_K:
        tcg_gen_mov_tl(mtilek, t);
        gen_helper_msettilek(tcg_env, t);
        break;
    default:
        g_assert_not_reached();
    }

    gen_update_pc(s, s->cur_insn_len);
    lookup_and_goto_ptr(s);
    s->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_msettilem(DisasContext *s, arg_mm_settile *a)
{
    TCGv src1 = get_gpr(s, a->rs1, EXT_NONE);
    return do_msettile(s, src1, MTILE_M);
}

static bool trans_msettilen(DisasContext *s, arg_mm_settile *a)
{
    TCGv src1 = get_gpr(s, a->rs1, EXT_NONE);
    return do_msettile(s, src1, MTILE_N);
}

static bool trans_msettilek(DisasContext *s, arg_mm_settile *a)
{
    TCGv src1 = get_gpr(s, a->rs1, EXT_NONE);
    return do_msettile(s, src1, MTILE_K);
}

static bool do_msettilei(DisasContext *s, uint32_t uimm10, MTileSel which)
{
    TCGv val = tcg_constant_tl((target_ulong)(uimm10 & 0x3ff));
    return do_msettile(s, val, which);
}

static bool trans_msettilemi(DisasContext *s, arg_mm_settile_i *a)
{
    return do_msettilei(s, a->uimm10, MTILE_M);
}

static bool trans_msettileni(DisasContext *s, arg_mm_settile_i *a)
{
    return do_msettilei(s, a->uimm10, MTILE_N);
}

static bool trans_msettileki(DisasContext *s, arg_mm_settile_i *a)
{
    return do_msettilei(s, a->uimm10, MTILE_K);
}

static bool trans_msetcfg(DisasContext *s, arg_r2 *a)
{
    if (!require_matrix(s)) {
        return false;
    }

    if (a->rd >= 8) {
        return false;
    }

    TCGv src = get_gpr(s, a->rs1, EXT_NONE);
    tcg_gen_st_tl(src, tcg_env,
                  offsetof(CPURISCVState, mcfg) + a->rd * sizeof(target_ulong));

    gen_update_pc(s, s->cur_insn_len);
    lookup_and_goto_ptr(s);
    s->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_mgetcfg(DisasContext *s, arg_r2 *a)
{
    if (!require_matrix(s)) {
        return false;
    }

    if (a->rs1 >= 8) {
        return false;
    }

    TCGv dst = dest_gpr(s, a->rd);
    tcg_gen_ld_tl(dst, tcg_env,
                  offsetof(CPURISCVState, mcfg) + a->rs1 * sizeof(target_ulong));
    gen_set_gpr(s, a->rd, dst);
    return true;
}

/* Check */
static inline bool mmext_check_mk(DisasContext *s)
{
    return !s->mill && !s->kill;
}

static inline bool mmext_check_nk(DisasContext *s)
{
    return !s->nill && !s->kill;
}

static inline bool mmext_check_mn(DisasContext *s)
{
    return !s->mill && !s->nill;
}

static inline bool mmext_check_mnk(DisasContext *s)
{
    return !s->mill && !s->nill && !s->kill;
}

static inline bool mmext_check_tile_reg(uint32_t mreg)
{
    return mreg < 4;
}

static inline bool mmext_check_acc_reg(uint32_t mreg)
{
    return mreg >= 4 && mreg < 8;
}

/* matrix register offset from env */
static uint32_t mregs_ofs(DisasContext *s, int mreg_idx)
{
    /* BUG FIX: Correct physical register sizes
     * - Tile registers (tr0-tr3, idx 0-3): mrows × rlenb bytes
     *   = (mreglen/mrowlen) × (mrowlen/8) = mreglen/8 bytes
     * - Accumulator registers (acc0-acc3, idx 4-7): mrows × mrows × 4 bytes
     *   = (mreglen/mrowlen)² × 4 bytes
     * 
     * With mreglen=65536 bits, mrowlen=512 bits:
     * - Tile: 65536/8 = 8192 bytes
     * - Accumulator: (65536/512)² × 4 = 128² × 4 = 65536 bytes
     */
    uint32_t mrows = s->mreglen / s->mrowlen;  // 65536/512 = 128
    uint32_t rlenb = s->mrowlen >> 3;          // 512/8 = 64 bytes
    uint32_t tile_size = mrows * rlenb;        // 128 * 64 = 8192 bytes
    uint32_t acc_size = mrows * mrows * 4;     // 128 * 128 * 4 = 65536 bytes
    
    uint32_t offset = offsetof(CPURISCVState, mreg);
    if (mreg_idx < 4) {
        /* Tile registers tr0-tr3 */
        offset += mreg_idx * tile_size;
    } else {
        /* Accumulator registers acc0-acc3 */
        offset += 4 * tile_size;  // Skip 4 tile registers
        offset += (mreg_idx - 4) * acc_size;
    }
    return offset;
}

/* Matrix Load/Store translation */
/* load & store */
static bool mmext_check_mldsta(DisasContext *s, uint32_t mreg)
{
    return require_matrix(s) && mmext_check_mk(s) && mmext_check_tile_reg(mreg);
}

static bool mmext_check_mldstb(DisasContext *s, uint32_t mreg)
{
    return require_matrix(s) && mmext_check_nk(s) && mmext_check_tile_reg(mreg);
}

static bool mmext_check_mldstc(DisasContext *s, uint32_t mreg)
{
    return require_matrix(s) && mmext_check_mn(s) && mmext_check_acc_reg(mreg);
}

typedef void gen_helper_ldst(TCGv_ptr, TCGv, TCGv, TCGv, TCGv_env);

static bool mm_ldst_op(uint32_t md, TCGv rs1, TCGv rs2,
                              gen_helper_ldst *fn, DisasContext *s)
{
    CPUState *cpu = s->cs;
    CPURISCVState *env = cpu_env(cpu);
    if (env->sync_skip_mode) {
        /* 将指令翻译成sync_skip，跳过执行 */
        gen_helper_sync_skip(tcg_env);
        return true;
    } else {
    TCGv_ptr dest = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, md));
    fn(dest, rs1, rs2, tcg_constant_tl(md), tcg_env);
    return true;
    }
}
#define GEN_MMEXT_LD_TRANS(NAME, CHECK)                               \
static bool trans_##NAME(DisasContext *s, arg_mm_ld *a)              \
{                                                                    \
    if (CHECK(s, a->md)) {                                           \
        return mm_ldst_op(a->md,                                     \
                          get_gpr(s, a->rs1, EXT_ZERO),              \
                          get_gpr(s, a->rs2, EXT_ZERO),              \
                          gen_helper_##NAME, s);                     \
    }                                                                \
    return false;                                                    \
}

#define GEN_MMEXT_ST_TRANS(NAME, CHECK)                               \
static bool trans_##NAME(DisasContext *s, arg_mm_st *a)              \
{                                                                    \
    if (CHECK(s, a->ms3)) {                                          \
        return mm_ldst_op(a->ms3,                                    \
                          get_gpr(s, a->rs1, EXT_ZERO),              \
                          get_gpr(s, a->rs2, EXT_ZERO),              \
                          gen_helper_##NAME, s);                     \
    }                                                                \
    return false;                                                    \
}

GEN_MMEXT_LD_TRANS(mla, mmext_check_mldsta)
GEN_MMEXT_LD_TRANS(mlb, mmext_check_mldstb)
GEN_MMEXT_LD_TRANS(mlc, mmext_check_mldstc)
GEN_MMEXT_ST_TRANS(msa, mmext_check_mldsta)
GEN_MMEXT_ST_TRANS(msb, mmext_check_mldstb)
GEN_MMEXT_ST_TRANS(msc, mmext_check_mldstc)

GEN_MMEXT_LD_TRANS(mlat, mmext_check_mldsta)
GEN_MMEXT_LD_TRANS(mlbt, mmext_check_mldstb)
GEN_MMEXT_LD_TRANS(mlct, mmext_check_mldstc)
GEN_MMEXT_ST_TRANS(msat, mmext_check_mldsta)
GEN_MMEXT_ST_TRANS(msbt, mmext_check_mldstb)
GEN_MMEXT_ST_TRANS(msct, mmext_check_mldstc)


/* whole matrix load & store */
/* Decode uses @mm_ld/@mm_st but rs2 is hard-wired to 0; helper only needs rs1. */
/* Mainly for context save and restore, so no need refer to mcfg type, now all use e64 ld/st */
/* TODO: actually not differ MAT_A/B/C, I think proposal-12 is a bad design for whole ld/st */
static bool mmext_check_ldstm(DisasContext *s, uint32_t mreg)
{
    return require_matrix(s) && (mreg < 8);
}

typedef void gen_helper_ldstm(TCGv_ptr, TCGv, TCGv_env);

static bool mm_ldstm_op(uint32_t md, TCGv rs1,
                                    gen_helper_ldstm *fn,
                                    DisasContext *s)
{
    CPUState *cpu = s->cs;
    CPURISCVState *env = cpu_env(cpu);
    if (env->sync_skip_mode) {
        /* 将指令翻译成sync_skip，跳过执行 */
        gen_helper_sync_skip(tcg_env);
        return true;
    } else {
    TCGv_ptr dest = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, md));
    fn(dest, rs1, tcg_env);
    return true;
    }
}

#define GEN_MMEXT_LDM_TRANS(NAME, CHECK)                    \
static bool trans_##NAME(DisasContext *s, arg_mm_ld *a)     \
{                                                           \
    if (CHECK(s, a->md)) {                                  \
        return mm_ldstm_op(a->md,                           \
                           get_gpr(s, a->rs1, EXT_ZERO),    \
                           gen_helper_mlme64, s);           \
    }                                                       \
    return false;                                           \
}

#define GEN_MMEXT_STM_TRANS(NAME, CHECK)                    \
static bool trans_##NAME(DisasContext *s, arg_mm_st *a)     \
{                                                           \
    if (CHECK(s, a->ms3)) {                                 \
        return mm_ldstm_op(a->ms3,                          \
                           get_gpr(s, a->rs1, EXT_ZERO),    \
                           gen_helper_msme64, s);           \
    }                                                       \
    return false;                                           \
}

GEN_MMEXT_LDM_TRANS(mla_whole, mmext_check_ldstm)
GEN_MMEXT_LDM_TRANS(mlb_whole, mmext_check_ldstm)
GEN_MMEXT_LDM_TRANS(mlc_whole, mmext_check_ldstm)
GEN_MMEXT_STM_TRANS(msa_whole, mmext_check_ldstm)
GEN_MMEXT_STM_TRANS(msb_whole, mmext_check_ldstm)
GEN_MMEXT_STM_TRANS(msc_whole, mmext_check_ldstm)


/* Matrix Multiplication translation */

static inline bool mmext_check_macc_common(DisasContext *s, arg_mm_rmm *a)
{
    return require_matrix(s)
        && mmext_check_mnk(s)
        && mmext_check_acc_reg(a->md)
        && mmext_check_tile_reg(a->ms1)
        && mmext_check_tile_reg(a->ms2);
}
/* TODO: check for misa, matrix multiplication support type - mmf8f32, ..., mmi8i32 */

typedef void gen_helper_opimmrmm(TCGv_ptr, TCGv_ptr, TCGv_ptr,
                                 TCGv, TCGv, TCGv, TCGv_env);

static bool mm_oprmm_trans(uint32_t md, uint32_t ms1, uint32_t ms2,
                           gen_helper_opimmrmm *fn, DisasContext *s)
{
    TCGv_ptr dest, src1, src2;

    dest = tcg_temp_new_ptr();
    src1 = tcg_temp_new_ptr();
    src2 = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, md));
    tcg_gen_addi_ptr(src1, tcg_env, mregs_ofs(s, ms1));
    tcg_gen_addi_ptr(src2, tcg_env, mregs_ofs(s, ms2));

    CPUState *cpu = s->cs;
    CPURISCVState *env = cpu_env(cpu);
    if (env->sync_skip_mode) {
        gen_helper_sync_skip(tcg_env);
    } else {
        fn(dest, src1, src2,
            tcg_constant_tl(md), tcg_constant_tl(ms1), tcg_constant_tl(ms2),
            tcg_env);
    }

    /*tcg_temp_free_ptr(dest);
    tcg_temp_free_ptr(src1);
    tcg_temp_free_ptr(src2);*/

    return true;
}

static bool trans_mmacc(DisasContext *s, arg_mm_rmm *a)
{
    if (!mmext_check_macc_common(s, a)) {
        return false;
    }
    return mm_oprmm_trans(a->md, a->ms1, a->ms2, gen_helper_mmacc, s);
}

/* Matrix MISC translation */
/* only support zero 1 matrix register now */
static inline uint32_t get_rows(DisasContext *s)
{
    return s->mreglen / s->mrowlen;
}
static bool trans_mzero(DisasContext *s, arg_mm_zero *a)
{
    if (!require_matrix(s)) {
        return false;
    }

    uint32_t len = 0;
    if (a->md < 4) {
        /* Tile registers */
        len = get_rows(s) * s->mrowlen / 8;
    } else {
        /* Acc registers */
        len = get_rows(s) * get_rows(s) * 4;
    }

    tcg_gen_gvec_dup_imm(MO_64, mregs_ofs(s, a->md), len, len, 0);
    return true;
}

static bool trans_minit(DisasContext *s, arg_empty *a)
{
    if (!require_matrix(s)) {
        return false;
    }

#ifndef CONFIG_USER_ONLY
    if (s->xmstatus_ms == 1) {
        return true;
    }
    s->xmstatus_ms = 1;
    // TCGv tmp = tcg_temp_new();
    // Mrelease Instruction sets MS to Initial state
    // tcg_gen_ld_tl(tmp, tcg_env, offsetof(CPURISCVState, mxstatus));
    // tcg_gen_deposit_tl(tmp, tmp, tcg_constant_tl(1), 0, 2);
    // tcg_gen_st_tl(tmp, tcg_env, offsetof(CPURISCVState, mxstatus));
    // tcg_temp_free(tmp);
#endif
    return true;
}

/* TODO: checkpoints should be generated outside async insts bound */
static bool trans_msyncregreset(DisasContext *s, arg_mm_async *a)
{
    if (!require_matrix(s)) {
        return false;
    }
    return true;
}

static bool trans_mrelease(DisasContext *s, arg_mm_async *a)
{
    if (!require_matrix(s)) {
        return false;
    }
    return true;
}

static bool trans_macquire(DisasContext *s, arg_mm_async *a)
{
    if (!require_matrix(s)) {
        return false;
    }
    return true;
}

static bool trans_mfence(DisasContext *s, arg_empty *a)
{
    if (!require_matrix(s)) {
        return false;
    }

    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
    return true;
}