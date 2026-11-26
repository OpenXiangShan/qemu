#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/host-utils.h"
#include "qemu/bitops.h"
#include "cpu.h"
#include "exec/memop.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"
#include "tcg/tcg-gvec-desc.h"
#include "internals.h"

/* -----------------------------------------------------------------------
 * Matrix instruction tracer — toggle MMAT_TRACE to enable.
 * Prints FNV-1a-32 hashes of active tile/accumulator regions after
 * mlae8, mlbe8, mlce32, and mmacc.w.b (mqma) instructions.
 * ----------------------------------------------------------------------- */
#define MMAT_TRACE 0
#if MMAT_TRACE
#include <stdio.h>
static uint64_t _qemu_mmat_cnt;
#define MMAT_TRACE_LIMIT 500

/* Hash active int8 region of MAT_A or MAT_B register.
 * Element layout: md[i * rlenb + j] (row stride = rlenb bytes). */
static void _qemu_mtrace_ab(const char *tag, void *md,
                            uint32_t rows, uint32_t cols, uint32_t rlenb,
                            uint64_t base_addr) {
    if (_qemu_mmat_cnt++ >= MMAT_TRACE_LIMIT) return;
    uint32_t h = 0x811c9dc5u;
    for (uint32_t i = 0; i < rows; i++)
        for (uint32_t j = 0; j < cols; j++) {
            uint8_t b = ((const uint8_t *)md)[i * rlenb + j];
            h = (h ^ b) * 0x01000193u;
        }
    fprintf(stderr, "[mtrace] qemu  %-12s M=%-4u K/N=%-4u addr=0x%016" PRIx64 "  fnv1a32=0x%08x\n",
            tag, rows, cols, base_addr, h);
    fflush(stderr);
}

/* Hash active int32 region of MAT_C (accumulator) register.
 * Element layout: md[i * mrows + j] (row stride = mrows int32 values). */
static void _qemu_mtrace_acc32(const char *tag, void *md,
                               uint32_t rows, uint32_t cols, uint32_t mrows,
                               uint64_t base_addr) {
    if (_qemu_mmat_cnt++ >= MMAT_TRACE_LIMIT) return;
    uint32_t h = 0x811c9dc5u;
    for (uint32_t i = 0; i < rows; i++) {
        for (uint32_t j = 0; j < cols; j++) {
            uint32_t v = (uint32_t)((const int32_t *)md)[i * mrows + j];
            h = (h ^ (v         & 0xff)) * 0x01000193u;
            h = (h ^ ((v >>  8) & 0xff)) * 0x01000193u;
            h = (h ^ ((v >> 16) & 0xff)) * 0x01000193u;
            h = (h ^ ((v >> 24) & 0xff)) * 0x01000193u;
        }
    }
    fprintf(stderr, "[mtrace] qemu  %-12s M=%-4u K/N=%-4u addr=0x%016" PRIx64 "  fnv1a32=0x%08x\n",
            tag, rows, cols, base_addr, h);
    fflush(stderr);
}
#endif /* MMAT_TRACE */

/* Matrix Config helpers */
void helper_msettilek(CPURISCVState *env, target_ulong s1)
{
    env->mtilek = s1;
}

void helper_msettilem(CPURISCVState *env, target_ulong s1)
{
    env->mtilem = s1;
}

void helper_msettilen(CPURISCVState *env, target_ulong s1)
{
    env->mtilen = s1;
}

void HELPER(minit)(CPURISCVState *env)
{
#ifndef CONFIG_USER_ONLY
    /* Zames MS is mstatus[26:25]; the translator's local state is not a CSR. */
    env->mstatus = deposit64(env->mstatus, 25, 2, 1);
#endif
    for (int i = 0; i < ARRAY_SIZE(env->mscale_reg_generation); i++) {
        if (env->mscale_reg_generation[i] == UINT32_MAX) {
            riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        }
    }
    memset(env->mscale_valid, 0, sizeof(env->mscale_valid));
    memset(env->mscale_rows, 0, sizeof(env->mscale_rows));
    memset(env->mscale_blocks, 0, sizeof(env->mscale_blocks));
    memset(env->mscale_mode, 0, sizeof(env->mscale_mode));
    for (int i = 0; i < ARRAY_SIZE(env->mscale_reg_generation); i++) {
        env->mscale_reg_generation[i]++;
    }
}

/* Matrix Load/Store helpers */
/*
 * The v0.6 proposal defines 3 tile shapes (based on C = A x B^T):
 *   - A tile  : mtilem x mtilek  (mtilem rows, mtilek elements per row)
 *   - B tile  : mtilen x mtilek
 *   - C tile  : mtilem x mtilen
 *
 * rs1 base address, rs2 row byte stride. Whole-matrix load/store ignores tile
 * sizes and uses the full physical register shape (get_mrows x get_rlenb).
 */

static void probe_pages(CPURISCVState *env, target_ulong addr,
                        target_ulong len, uintptr_t ra,
                        MMUAccessType access_type)
{
    target_ulong pagelen = -(addr | TARGET_PAGE_MASK);
    target_ulong curlen = MIN(pagelen, len);
    int mmu_index = riscv_env_mmu_index(env, false);

    probe_access(env, addr, curlen, access_type,
                 mmu_index, ra);
    if (len > curlen) {
        addr += curlen;
        curlen = len - curlen;
        probe_access(env, addr, curlen, access_type,
                     mmu_index, ra);
    }
}

static void mmext_scale_load(CPURISCVState *env, target_ulong base,
                             target_ulong stride, uint32_t scale_reg,
                             bool is_b_side)
{
    uint32_t rows, blocks, row, block, side;
    uint8_t staged[RV_TROWNUM_MAX][RV_MX_MAX_SCALE_BLOCKS] = { 0 };
    uintptr_t ra = GETPC();

    if (scale_reg > 3 ||
        env->mtilek == 0) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }

    rows = (uint32_t)(is_b_side ? env->mtilen : env->mtilem);
    blocks = (uint32_t)((env->mtilek + RV_MX_BLOCK_SIZE - 1) /
                        RV_MX_BLOCK_SIZE);
    if ((is_b_side ? env->mtilen : env->mtilem) > get_mrows(env) ||
        blocks > RV_MX_MAX_SCALE_BLOCKS ||
        stride < blocks) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }

    for (row = 0; row < rows; row++) {
        probe_pages(env, base + (target_ulong)row * stride, blocks, ra,
                    MMU_DATA_LOAD);
    }

    side = is_b_side ? 4 : 0;
    if (env->mscale_reg_generation[side + scale_reg] == UINT32_MAX) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }
    for (row = 0; row < rows; row++) {
        for (block = 0; block < blocks; block++) {
            target_ulong addr = base + (target_ulong)row * stride + block;
            staged[row][block] = cpu_ldub_data_ra(env, addr, ra);
        }
    }
    memcpy(env->mscale[side + scale_reg], staged, sizeof(staged));
    env->mscale_rows[side + scale_reg] = rows;
    env->mscale_blocks[side + scale_reg] = blocks;
    env->mscale_mode[side + scale_reg] =
        (uint8_t)(env->mcfg[scale_reg] & 0xff);
    env->mscale_reg_generation[side + scale_reg]++;
    env->mscale_valid[side + scale_reg] = 1;
}

void HELPER(mlascale)(CPURISCVState *env, target_ulong base,
                      target_ulong stride, target_ulong scale_reg)
{
    mmext_scale_load(env, base, stride, (uint32_t)scale_reg, false);
}

void HELPER(mlbscale)(CPURISCVState *env, target_ulong base,
                      target_ulong stride, target_ulong scale_reg)
{
    mmext_scale_load(env, base, stride, (uint32_t)scale_reg, true);
}

#define MMEXT_LD_ELEM(NAME, LDSUF)                                         \
static int64_t NAME(CPURISCVState *env, target_ulong addr,                 \
                    uintptr_t retaddr){                                    \
    return cpu_##LDSUF##_data_ra(env, addr, retaddr);                      \
}

MMEXT_LD_ELEM(ld_b, ldsb)
MMEXT_LD_ELEM(ld_h, ldsw)
MMEXT_LD_ELEM(ld_w, ldl)
MMEXT_LD_ELEM(ld_d, ldq)

typedef int64_t mmext_ld_fn(CPURISCVState *env, target_ulong addr,
                            uintptr_t retaddr);

#define MMEXT_ST_ELEM(NAME, STSUF)                                      \
static void NAME(CPURISCVState *env, target_ulong addr, uint64_t val,   \
                 uintptr_t retaddr){                                    \
    cpu_##STSUF##_data_ra(env, addr, val, retaddr);                     \
}

MMEXT_ST_ELEM(st_b, stb)
MMEXT_ST_ELEM(st_h, stw)
MMEXT_ST_ELEM(st_w, stl)
MMEXT_ST_ELEM(st_d, stq)

typedef void mmext_st_fn(CPURISCVState *env, target_ulong addr, uint64_t val,
                         uintptr_t retaddr);                            

static inline int64_t get_elem_b(void *md, uint32_t i, uint32_t j,
                                 CPURISCVState *env){
    uint32_t idx = i * get_rlenb(env) + j;
    return ((int8_t *)md)[idx];
}

static inline void set_elem_b(void *md, uint32_t i, uint32_t j,
                              CPURISCVState *env, int64_t val){
    uint32_t idx = i * get_rlenb(env) + j;
    ((int8_t *) md)[idx] = (int8_t) val;
}

static inline int64_t get_elem_h(void *md, uint32_t i, uint32_t j,
                                 CPURISCVState *env){
    uint32_t idx = i * (get_rlenb(env) >> 1) + j;
    return ((int16_t *)md)[idx];
}

static inline void set_elem_h(void *md, uint32_t i, uint32_t j,
                              CPURISCVState *env, int64_t val){
    uint32_t idx = i * (get_rlenb(env) >> 1) + j;
    ((int16_t *) md)[idx] = (int16_t) val;
}

static inline int64_t get_elem_s(void* md, uint32_t i, uint32_t j,
                                 CPURISCVState* env){
    /* BUG FIX: Accumulator stride should be mrows (128), not rlenb>>2 (16).
     * Accumulator is mrows × mrows int32 matrix. */
    uint32_t idx = i * get_mrows(env) + j;
    return ((int32_t *)md)[idx];
}

static inline void set_elem_s(void* md, uint32_t i, uint32_t j,
                              CPURISCVState* env, int64_t val){
    /* BUG FIX: Accumulator stride should be mrows (128), not rlenb>>2 (16). */
    uint32_t idx = i * get_mrows(env) + j;
    ((int32_t *) md)[idx] = (int32_t) val;
}

static inline int64_t get_elem_d(void* md, uint32_t i, uint32_t j,
                                 CPURISCVState* env){
    uint32_t idx = i * (get_rlenb(env) >> 3) + j;
    return ((int64_t *)md)[idx];
}

static inline void set_elem_d(void* md, uint32_t i, uint32_t j,
                              CPURISCVState* env, int64_t val){
    uint32_t idx = i * (get_rlenb(env) >> 3) + j;
    ((int64_t *) md)[idx] = val;
}

static inline void set_elem_nibble(void *md, uint32_t i, uint32_t j,
                                   CPURISCVState *env, int64_t val)
{
    uint32_t idx = i * get_rlenb(env) + (j >> 1);
    uint8_t *packed = &((uint8_t *)md)[idx];
    uint8_t mask = (j & 1) ? 0x0f : 0xf0;
    uint8_t shifted = (j & 1) ? ((uint8_t)val << 4) : ((uint8_t)val & 0xf);

    *packed = (*packed & mask) | shifted;
}

static inline uint8_t get_elem_nibble(void *md, uint32_t i, uint32_t j,
                                      CPURISCVState *env)
{
    uint8_t packed = ((uint8_t *)md)[i * get_rlenb(env) + (j >> 1)];
    return (j & 1) ? (packed >> 4) : (packed & 0xf);
}

typedef int64_t mmext_get_elem(void*, uint32_t, uint32_t, CPURISCVState*);
typedef void mmext_set_elem(void*, uint32_t, uint32_t, CPURISCVState*, int64_t);

typedef enum {
    MAT_A,
    MAT_B,
    MAT_C,
} mmext_mat_kind;

/* rows for the requested logical tile */
static inline uint32_t mmext_rows(CPURISCVState *env, mmext_mat_kind kind)
{
    return (kind == MAT_B) ? (uint32_t)env->mtilen : (uint32_t)env->mtilem;
}

/* elements per row for the requested logical tile */
static inline uint32_t mmext_cols(CPURISCVState *env, mmext_mat_kind kind)
{
    return (kind == MAT_C) ? (uint32_t)env->mtilen : (uint32_t)env->mtilek;
}

static void mmext_mload_tile(void *md, target_ulong rs1, target_ulong stride,
                             mmext_ld_fn *ld_elem, mmext_set_elem *set_elem,
                             CPURISCVState *env, uint8_t esz, uintptr_t ra,
                             mmext_mat_kind kind, bool transposed)
{
    uint32_t i, j;
    uint32_t rows_lim = mmext_rows(env, kind);
    uint32_t cols_lim = mmext_cols(env, kind);
    uint32_t rows_phys = get_mrows(env);
    uint32_t cols_phys;
    bool packed_nibbles = esz == UINT8_MAX;

    if (kind == MAT_C) {
        cols_phys = get_mrows(env);
    } else if (packed_nibbles) {
        cols_phys = (uint32_t)get_rlenb(env) * 2;
    } else {
        cols_phys = (uint32_t)(get_rlenb(env) >> esz);
    }
    target_ulong addr;
    if (kind != MAT_C && cols_lim > cols_phys) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }

    /*
     * Probe pages conservatively for the actually-touched memory region.
     * Note: when transposed, memory is accessed by "cols" as rows.
     */
    if (!transposed) {
        for (i = 0; i < rows_lim; i++) {
            probe_pages(env, rs1 + (target_ulong)i * stride,
                        packed_nibbles ?
                        (cols_lim + 1) >> 1 : (target_ulong)cols_lim << esz,
                        ra, MMU_DATA_LOAD);
        }
    } else {
        for (j = 0; j < cols_lim; j++) {
            probe_pages(env, rs1 + (target_ulong)j * stride,
                        packed_nibbles ?
                        (rows_lim + 1) >> 1 : (target_ulong)rows_lim << esz,
                        ra, MMU_DATA_LOAD);
        }
    }

    /* Fill the full physical register; out-of-tile region is zeroed. */
    for (i = 0; i < rows_phys; i++) {
        for (j = 0; j < cols_phys; j++) {
            if (i < rows_lim && j < cols_lim) {
                if (!transposed) {
                    addr = rs1 + (target_ulong)i * stride +
                           (packed_nibbles ? ((target_ulong)j >> 1) :
                            ((target_ulong)j << esz));
                } else {
                    addr = rs1 + (target_ulong)j * stride +
                           (packed_nibbles ? ((target_ulong)i >> 1) :
                            ((target_ulong)i << esz));
                }
                if (packed_nibbles) {
                    uint8_t packed = (uint8_t)ld_elem(env, addr, ra);
                    set_elem_nibble(md, i, j, env,
                                    (j & 1) ? (packed >> 4) : (packed & 0xf));
                } else {
                    set_elem(md, i, j, env, ld_elem(env, addr, ra));
                }
            } else {
                if (packed_nibbles) {
                    set_elem_nibble(md, i, j, env, 0);
                } else {
                    set_elem(md, i, j, env, 0);
                }
            }
        }
    }
#if MMAT_TRACE
    /* Only trace the int8-source loads and int32-acc load that we care about */
    if (esz == 0 && kind == MAT_A)
        _qemu_mtrace_ab("mlae8", md, rows_lim, cols_lim, get_rlenb(env), (uint64_t)rs1);
    else if (esz == 0 && kind == MAT_B)
        _qemu_mtrace_ab("mlbe8", md, rows_lim, cols_lim, get_rlenb(env), (uint64_t)rs1);
    else if (esz == 2 && kind == MAT_C)
        _qemu_mtrace_acc32("mlce32", md, rows_lim, cols_lim, get_mrows(env), (uint64_t)rs1);
#endif /* MMAT_TRACE */
}

static void mmext_mstore_tile(void *ms3, target_ulong rs1, target_ulong stride,
                              mmext_st_fn *st_elem, mmext_get_elem *get_elem,
                              CPURISCVState *env, uint8_t esz, uintptr_t ra,
                              mmext_mat_kind kind, bool transposed)
{
    uint32_t i, j;
    uint32_t rows_lim = mmext_rows(env, kind);
    uint32_t cols_lim = mmext_cols(env, kind);
    uint32_t cols_phys = kind == MAT_C ? get_mrows(env) :
        (esz == UINT8_MAX ? (uint32_t)get_rlenb(env) * 2 :
         (uint32_t)(get_rlenb(env) >> esz));
    target_ulong addr;

    if (kind != MAT_C && cols_lim > cols_phys) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }
    if (!transposed) {
        for (i = 0; i < rows_lim; i++) {
            probe_pages(env, rs1 + (target_ulong)i * stride,
                        (target_ulong)cols_lim << esz, ra, MMU_DATA_STORE);
        }
    } else {
        for (j = 0; j < cols_lim; j++) {
            probe_pages(env, rs1 + (target_ulong)j * stride,
                        (target_ulong)rows_lim << esz, ra, MMU_DATA_STORE);
        }
    }

    for (i = 0; i < rows_lim; i++) {
        for (j = 0; j < cols_lim; j++) {
            if (!transposed) {
                addr = rs1 + (target_ulong)i * stride + ((target_ulong)j << esz);
            } else {
                addr = rs1 + (target_ulong)j * stride + ((target_ulong)i << esz);
            }
            st_elem(env, addr, (uint64_t)get_elem(ms3, i, j, env), ra);
        }
    }
}

#define GEN_MMEXT_LD_HELPER(insn, ld_elem, set_elem, ESZ, kind, T)        \
static void mmext_##insn(void *md, target_ulong rs1, target_ulong rs2,    \
                         CPURISCVState *env)                              \
{                                                                         \
    mmext_mload_tile(md, rs1, rs2, ld_elem, set_elem, env, ESZ, GETPC(),  \
                     kind, T);                                            \
}

#define GEN_MMEXT_ST_HELPER(insn, st_elem, get_elem, ESZ, kind, T)        \
static void mmext_##insn(void *ms3, target_ulong rs1, target_ulong rs2,   \
                         CPURISCVState *env)                              \
{                                                                         \
    mmext_mstore_tile(ms3, rs1, rs2, st_elem, get_elem, env, ESZ, GETPC(),\
                      kind, T);                                           \
}

GEN_MMEXT_LD_HELPER(mlae8,  ld_b, set_elem_b, 0, MAT_A, false)
GEN_MMEXT_LD_HELPER(mlae16, ld_h, set_elem_h, 1, MAT_A, false)
GEN_MMEXT_LD_HELPER(mlae32, ld_w, set_elem_s, 2, MAT_A, false)
GEN_MMEXT_LD_HELPER(mlae64, ld_d, set_elem_d, 3, MAT_A, false)

GEN_MMEXT_LD_HELPER(mlbe8,  ld_b, set_elem_b, 0, MAT_B, false)
GEN_MMEXT_LD_HELPER(mlbe16, ld_h, set_elem_h, 1, MAT_B, false)
GEN_MMEXT_LD_HELPER(mlbe32, ld_w, set_elem_s, 2, MAT_B, false)
GEN_MMEXT_LD_HELPER(mlbe64, ld_d, set_elem_d, 3, MAT_B, false)

GEN_MMEXT_LD_HELPER(mlce8,  ld_b, set_elem_b, 0, MAT_C, false)
GEN_MMEXT_LD_HELPER(mlce16, ld_h, set_elem_h, 1, MAT_C, false)
GEN_MMEXT_LD_HELPER(mlce32, ld_w, set_elem_s, 2, MAT_C, false)
GEN_MMEXT_LD_HELPER(mlce64, ld_d, set_elem_d, 3, MAT_C, false)

GEN_MMEXT_ST_HELPER(msae8,  st_b, get_elem_b, 0, MAT_A, false)
GEN_MMEXT_ST_HELPER(msae16, st_h, get_elem_h, 1, MAT_A, false)
GEN_MMEXT_ST_HELPER(msae32, st_w, get_elem_s, 2, MAT_A, false)
GEN_MMEXT_ST_HELPER(msae64, st_d, get_elem_d, 3, MAT_A, false)

GEN_MMEXT_ST_HELPER(msbe8,  st_b, get_elem_b, 0, MAT_B, false)
GEN_MMEXT_ST_HELPER(msbe16, st_h, get_elem_h, 1, MAT_B, false)
GEN_MMEXT_ST_HELPER(msbe32, st_w, get_elem_s, 2, MAT_B, false)
GEN_MMEXT_ST_HELPER(msbe64, st_d, get_elem_d, 3, MAT_B, false)

GEN_MMEXT_ST_HELPER(msce8,  st_b, get_elem_b, 0, MAT_C, false)
GEN_MMEXT_ST_HELPER(msce16, st_h, get_elem_h, 1, MAT_C, false)
GEN_MMEXT_ST_HELPER(msce32, st_w, get_elem_s, 2, MAT_C, false)
GEN_MMEXT_ST_HELPER(msce64, st_d, get_elem_d, 3, MAT_C, false)

/* ---- Transposed variants ---- */
GEN_MMEXT_LD_HELPER(mlate8,  ld_b, set_elem_b, 0, MAT_A, true)
GEN_MMEXT_LD_HELPER(mlate16, ld_h, set_elem_h, 1, MAT_A, true)
GEN_MMEXT_LD_HELPER(mlate32, ld_w, set_elem_s, 2, MAT_A, true)
GEN_MMEXT_LD_HELPER(mlate64, ld_d, set_elem_d, 3, MAT_A, true)

GEN_MMEXT_LD_HELPER(mlbte8,  ld_b, set_elem_b, 0, MAT_B, true)
GEN_MMEXT_LD_HELPER(mlbte16, ld_h, set_elem_h, 1, MAT_B, true)
GEN_MMEXT_LD_HELPER(mlbte32, ld_w, set_elem_s, 2, MAT_B, true)
GEN_MMEXT_LD_HELPER(mlbte64, ld_d, set_elem_d, 3, MAT_B, true)

GEN_MMEXT_LD_HELPER(mlcte8,  ld_b, set_elem_b, 0, MAT_C, true)
GEN_MMEXT_LD_HELPER(mlcte16, ld_h, set_elem_h, 1, MAT_C, true)
GEN_MMEXT_LD_HELPER(mlcte32, ld_w, set_elem_s, 2, MAT_C, true)
GEN_MMEXT_LD_HELPER(mlcte64, ld_d, set_elem_d, 3, MAT_C, true)

GEN_MMEXT_ST_HELPER(msate8,  st_b, get_elem_b, 0, MAT_A, true)
GEN_MMEXT_ST_HELPER(msate16, st_h, get_elem_h, 1, MAT_A, true)
GEN_MMEXT_ST_HELPER(msate32, st_w, get_elem_s, 2, MAT_A, true)
GEN_MMEXT_ST_HELPER(msate64, st_d, get_elem_d, 3, MAT_A, true)

GEN_MMEXT_ST_HELPER(msbte8,  st_b, get_elem_b, 0, MAT_B, true)
GEN_MMEXT_ST_HELPER(msbte16, st_h, get_elem_h, 1, MAT_B, true)
GEN_MMEXT_ST_HELPER(msbte32, st_w, get_elem_s, 2, MAT_B, true)
GEN_MMEXT_ST_HELPER(msbte64, st_d, get_elem_d, 3, MAT_B, true)

GEN_MMEXT_ST_HELPER(mscte8,  st_b, get_elem_b, 0, MAT_C, true)
GEN_MMEXT_ST_HELPER(mscte16, st_h, get_elem_h, 1, MAT_C, true)
GEN_MMEXT_ST_HELPER(mscte32, st_w, get_elem_s, 2, MAT_C, true)
GEN_MMEXT_ST_HELPER(mscte64, st_d, get_elem_d, 3, MAT_C, true)

/* ----------- Unified matrix load/store for XSAI proposal-12 ----------- */
typedef enum McfgTypeCode {
    MCFG_TC_INT4      = 0x0,
    MCFG_TC_UINT4     = 0x1,
    MCFG_TC_INT8      = 0x2,
    MCFG_TC_UINT8     = 0x3,
    MCFG_TC_INT32     = 0x4,
    MCFG_TC_NVFP4     = 0x5,
    MCFG_TC_MXFP4     = 0x6,
    MCFG_TC_FP8E5M2   = 0x7,
    MCFG_TC_FP8E4M3   = 0x8,
    MCFG_TC_FP16      = 0x9,
    MCFG_TC_BF16      = 0xa,
    MCFG_TC_TF32      = 0xb,
    MCFG_TC_FP32      = 0xc,
    MCFG_TC_FP2PACK4  = 0xd,
    MCFG_TC_FP2PACK5  = 0xe,
    MCFG_TC_RESERVED  = 0xf,
} McfgTypeCode;

static bool mmext_decode_mcfg(CPURISCVState *env, uint32_t mreg_idx,
                              uint32_t *type_code, uint32_t *table_sel)
{
    uint32_t cfg = (uint32_t)env->mcfg[mreg_idx];

    *type_code = cfg & 0xf;
    *table_sel = (cfg >> 4) & 0xf;
    return *type_code != MCFG_TC_RESERVED && *table_sel <= 1;
}

static bool mmext_type_to_esz(uint32_t type_code, uint32_t *esz)
{
    switch (type_code) {
    case MCFG_TC_INT8:
        *esz = 0;
        return true;
    case MCFG_TC_FP16:
    case MCFG_TC_BF16:
        *esz = 1;
        return true;
    case MCFG_TC_INT32:
    case MCFG_TC_FP32:
        *esz = 2;
        return true;
    default:
        return false;
    }
}

static bool mmext_mreg_get_esz(CPURISCVState *env, uint32_t mreg_idx, uint32_t *esz)
{
    uint32_t type_code, table_sel;
    if (!mmext_decode_mcfg(env, mreg_idx, &type_code, &table_sel)) {
        return false;
    }
    if (table_sel == 1) {
        switch (type_code) {
        case MCFG_TC_MXFP4:
            /* Marker for the two-nibbles-per-byte MXFP4 representation. */
            *esz = UINT8_MAX;
            return true;
        case MCFG_TC_FP8E5M2:
        case MCFG_TC_FP8E4M3:
            *esz = 0;
            return true;
        default:
            return false;
        }
    }
    if (table_sel != 0) {
        return false;
    }
    return mmext_type_to_esz(type_code, esz);
}

#define GEN_MMEXT_PACKED_LD_HELPER(insn, kind, T)                               \
static void mmext_##insn(void *md, target_ulong rs1, target_ulong rs2,          \
                         CPURISCVState *env)                                    \
{                                                                               \
    mmext_mload_tile(md, rs1, rs2, ld_b, set_elem_nibble, env, UINT8_MAX,       \
                     GETPC(), kind, T);                                         \
}

GEN_MMEXT_PACKED_LD_HELPER(mlae4, MAT_A, false)
GEN_MMEXT_PACKED_LD_HELPER(mlbe4, MAT_B, false)
GEN_MMEXT_PACKED_LD_HELPER(mlate4, MAT_A, true)
GEN_MMEXT_PACKED_LD_HELPER(mlbte4, MAT_B, true)

#define GEN_UNIFIED_LDST_HELPER(NAME, H4, H8, H16, H32, H64)                    \
void helper_##NAME(void *mreg_ptr, target_ulong rs1, target_ulong rs2,          \
                   target_ulong mreg_idx, CPURISCVState *env)                   \
{                                                                               \
    uint32_t esz;                                                               \
    if (!mmext_mreg_get_esz(env, (uint32_t)mreg_idx, &esz)) {                   \
        return;                                                                 \
    }                                                                           \
    if (esz == UINT8_MAX) {                                                     \
        mmext_##H4(mreg_ptr, rs1, rs2, env);                                   \
        return;                                                                 \
    }                                                                           \
    switch (esz) {                                                              \
    case 0:                                                                     \
        mmext_##H8(mreg_ptr, rs1, rs2, env);                                    \
        return;                                                                 \
    case 1:                                                                     \
        mmext_##H16(mreg_ptr, rs1, rs2, env);                                   \
        return;                                                                 \
    case 2:                                                                     \
        mmext_##H32(mreg_ptr, rs1, rs2, env);                                   \
        return;                                                                 \
    default:                                                                    \
        mmext_##H64(mreg_ptr, rs1, rs2, env);                                   \
        return;                                                                 \
    }                                                                           \
}

GEN_UNIFIED_LDST_HELPER(mla,  mlae4,  mlae8,  mlae16,  mlae32, mlae64)
GEN_UNIFIED_LDST_HELPER(mlb,  mlbe4,  mlbe8,  mlbe16,  mlbe32, mlbe64)
GEN_UNIFIED_LDST_HELPER(mlc,  mlce8,  mlce8,  mlce16,  mlce32, mlce64)
GEN_UNIFIED_LDST_HELPER(msa,  msae8,  msae8,  msae16,  msae32, msae64)
GEN_UNIFIED_LDST_HELPER(msb,  msbe8,  msbe8,  msbe16,  msbe32, msbe64)
GEN_UNIFIED_LDST_HELPER(msc,  msce8,  msce8,  msce16,  msce32, msce64)

GEN_UNIFIED_LDST_HELPER(mlat, mlate4, mlate8, mlate16, mlate32, mlate64)
GEN_UNIFIED_LDST_HELPER(mlbt, mlbte4, mlbte8, mlbte16, mlbte32, mlbte64)
GEN_UNIFIED_LDST_HELPER(mlct, mlcte8, mlcte8, mlcte16, mlcte32, mlcte64)
GEN_UNIFIED_LDST_HELPER(msat, msate8, msate8, msate16, msate32, msate64)
GEN_UNIFIED_LDST_HELPER(msbt, msbte8, msbte8, msbte16, msbte32, msbte64)
GEN_UNIFIED_LDST_HELPER(msct, mscte8, mscte8, mscte16, mscte32, mscte64)

/* Whole Matrix load/store */
static void mmext_mload_whole(void *md, target_ulong rs1,
                             mmext_ld_fn *ld_elem, mmext_set_elem *set_elem,
                             CPURISCVState *env, uint8_t esz, uintptr_t ra)
{
    uint32_t i, j;
    uint32_t rows = get_mrows(env);
    uint32_t cols = (uint32_t)(get_rlenb(env) >> esz);
    target_ulong addr;

    for (i = 0; i < rows; i++) {
        probe_pages(env, rs1 + (target_ulong)i * get_rlenb(env),
                    get_rlenb(env), ra, MMU_DATA_LOAD);
    }

    for (i = 0; i < rows; i++) {
        for (j = 0; j < cols; j++) {
            addr = rs1 + (target_ulong)i * get_rlenb(env) + ((target_ulong)j << esz);
            set_elem(md, i, j, env, ld_elem(env, addr, ra));
        }
    }
}

static void mmext_mstore_whole(void *ms3, target_ulong rs1,
                              mmext_st_fn *st_elem, mmext_get_elem *get_elem,
                              CPURISCVState *env, uint8_t esz, uintptr_t ra)
{
    uint32_t i, j;
    uint32_t rows = get_mrows(env);
    uint32_t cols = (uint32_t)(get_rlenb(env) >> esz);
    target_ulong addr;

    for (i = 0; i < rows; i++) {
        probe_pages(env, rs1 + (target_ulong)i * get_rlenb(env),
                    get_rlenb(env), ra, MMU_DATA_STORE);
    }

    for (i = 0; i < rows; i++) {
        for (j = 0; j < cols; j++) {
            addr = rs1 + (target_ulong)i * get_rlenb(env) + ((target_ulong)j << esz);
            st_elem(env, addr, (uint64_t)get_elem(ms3, i, j, env), ra);
        }
    }
}

#define GEN_MMEXT_LD_WHOLE(insn, ld_elem, set_elem, ESZ)               \
void HELPER(insn)(void *md, target_ulong rs1, CPURISCVState *env)      \
{                                                                      \
    mmext_mload_whole(md, rs1, ld_elem, set_elem, env, ESZ, GETPC());  \
}

#define GEN_MMEXT_ST_WHOLE(insn, st_elem, get_elem, ESZ)               \
void HELPER(insn)(void *ms3, target_ulong rs1, CPURISCVState *env)     \
{                                                                      \
    mmext_mstore_whole(ms3, rs1, st_elem, get_elem, env, ESZ, GETPC());\
}

GEN_MMEXT_LD_WHOLE(mlme8,  ld_b, set_elem_b, 0)
GEN_MMEXT_LD_WHOLE(mlme16, ld_h, set_elem_h, 1)
GEN_MMEXT_LD_WHOLE(mlme32, ld_w, set_elem_s, 2)
GEN_MMEXT_LD_WHOLE(mlme64, ld_d, set_elem_d, 3)

GEN_MMEXT_ST_WHOLE(msme8,  st_b, get_elem_b, 0)
GEN_MMEXT_ST_WHOLE(msme16, st_h, get_elem_h, 1)
GEN_MMEXT_ST_WHOLE(msme32, st_w, get_elem_s, 2)
GEN_MMEXT_ST_WHOLE(msme64, st_d, get_elem_d, 3)


/* Matrix Multiplication helpers */
/* ============================================================
 * New ISA (v0.6) naming: mfmacc/mmacc family
 * We keep the existing internal implementations (mmaqa/fmmacc/fwmacc)
 * and provide wrappers with the new helper names.
 * ============================================================ */

/* mmaqa instructions */
/* byte oprands accumulate to single word */
static inline int32_t macc_b_ss_s(int8_t a, int8_t b, int32_t sum)
{
    return sum + a * b;
}

static inline int32_t macc_b_su_s(int8_t a, int8_t b, int32_t sum)
{
    return sum + a * (uint8_t) b;
}

static inline int32_t macc_b_us_s(int8_t a, int8_t b, int32_t sum)
{
    return sum + (uint8_t) a * b;
}

static inline int32_t macc_b_uu_s(int8_t a, int8_t b, int32_t sum)
{
    return sum + (uint8_t) a * (uint8_t) b;
}

typedef int32_t macc_fn_b(int8_t, int8_t, int32_t);

static void mmext_mmaqa_b(void *md, void *ms1, void *ms2, CPURISCVState *env,
                          macc_fn_b *macc){
    uint32_t i, j, k;
    int32_t temp, psum;
    int8_t oprd_a, oprd_b;
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < env->mtilek; k++) {
                oprd_a = get_elem_b(ms1, i, k, env);
                oprd_b = get_elem_b(ms2, j, k, env);
                temp = macc(oprd_a, oprd_b, temp);
            }
            if (i < env->mtilem && j < env->mtilen) {
                psum = get_elem_s(md, i, j, env);
                psum += temp;
                set_elem_s(md, i, j, env, psum);
            } else {
                set_elem_s(md, i, j, env, 0);
            }
        }
    }
#if MMAT_TRACE
    _qemu_mtrace_acc32("mqma", md,
                       (uint32_t)env->mtilem, (uint32_t)env->mtilen,
                       get_mrows(env), 0);
#endif /* MMAT_TRACE */
}

/* half byte oprands accumulate to single word */
static inline int32_t macc_p_ss_s(int8_t a, int8_t b, int32_t sum,
                                  uint32_t start, uint32_t length){
    return sum + (int32_t) (sextract32(a, start, length) * sextract32(b, start, length));
}

static inline int32_t macc_p_su_s(int8_t a, int8_t b, int32_t sum,
                                  uint32_t start, uint32_t length){
    return sum + (int32_t) (sextract32(a, start, length) * extract32(b, start, length));
}

static inline int32_t macc_p_us_s(int8_t a, int8_t b, int32_t sum,
                                  uint32_t start, uint32_t length){
    return sum + (int32_t) (extract32(a, start, length) * sextract32(b, start, length));
}

static inline int32_t macc_p_uu_s(int8_t a, int8_t b, int32_t sum,
                                  uint32_t start, uint32_t length){
    return sum + (int32_t) (extract32(a, start, length) * extract32(b, start, length));
}

typedef int32_t macc_fn_p(int8_t, int8_t, int32_t, uint32_t, uint32_t);

static void mmext_mmaqa_p(void *md, void *ms1, void *ms2, CPURISCVState *env,
                          macc_fn_p *macc){
    uint32_t i, j, k;
    int32_t temp, psum;
    int8_t oprd_a, oprd_b;
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < env->mtilek; k++) {
                oprd_a = get_elem_b(ms1, i, k, env);
                oprd_b = get_elem_b(ms2, j, k, env);
                temp = macc(oprd_a, oprd_b, temp, 0, 4);
                temp = macc(oprd_a, oprd_b, temp, 4, 4);
            }
            if (i < env->mtilem && j < env->mtilen) {
                psum = get_elem_s(md, i, j, env);
                psum += temp;
                set_elem_s(md, i, j, env, psum);
            } else {
                set_elem_s(md, i, j, env, 0);
            }
        }
    }
}

/* half word oprands accumulate to double words */
// XSAI proposal-12 do not support widening macc for 64b now.
/* static inline int64_t macc_h_ss_d(int16_t a, int16_t b, int64_t sum)
{
    return sum + a * b;
}

static inline int64_t macc_h_su_d(int16_t a, int16_t b, int64_t sum)
{
    return sum + a * (uint16_t) b;
}

static inline int64_t macc_h_us_d(int16_t a, int16_t b, int64_t sum)
{
    return sum + (uint16_t) a * b;
}

static inline int64_t macc_h_uu_d(int16_t a, int16_t b, int64_t sum)
{
    return sum + (uint16_t) a * (uint16_t) b;
}

typedef int64_t macc_fn_h(int16_t, int16_t, int64_t);

static void mmext_mmaqa_h(void *md, void *ms1, void *ms2, CPURISCVState *env,
                          macc_fn_h *macc){
    uint32_t i, j, k;
    int64_t temp, psum;
    int16_t oprd_a, oprd_b;
    void *md_pair_1 = md;
    void *md_pair_2 = (void *) (((int8_t *) md) + get_mlenb(env));

    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < (env->mtilek >> 1); k++) {
                oprd_a = get_elem_h(ms1, i, k, env);
                oprd_b = get_elem_h(ms2, j, k, env);
                temp = macc(oprd_a, oprd_b, temp);
            }
            if (j >= (get_mrows(env) >> 1)) {
                if (i < env->mtilem && j < env->mtilen) {
                    psum = get_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                                      env);
                    psum += temp;
                    set_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                               env, psum);
                } else {
                    set_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                               env, 0);
                }
            } else {
                if (i < env->mtilem && j < env->mtilen) {
                    psum = get_elem_d(md_pair_1, i, j, env);
                    psum += temp;
                    set_elem_d(md_pair_1, i, j, env, psum);
                } else {
                    set_elem_d(md_pair_1, i, j, env, 0);
                }
            }
        }
    }
} */

/* Integer: mmacc*.w.b map to mmaqa*.b */
#define GEN_MMACC_B_HELPER(insn, macc_fn_b)                   \
static void mmext_##insn(void *md, void *ms1, void *ms2,      \
                         CPURISCVState *env){                  \
    mmext_mmaqa_b(md, ms1, ms2, env, macc_fn_b);              \
}

GEN_MMACC_B_HELPER(mmacc_w_b,   macc_b_ss_s)
GEN_MMACC_B_HELPER(mmaccu_w_b,  macc_b_uu_s)
GEN_MMACC_B_HELPER(mmaccus_w_b, macc_b_us_s)
GEN_MMACC_B_HELPER(mmaccsu_w_b, macc_b_su_s)

/* Packed int4: pmmacc*.w.b map to pmmaqa*.b */
#define GEN_MMACC_P_HELPER(insn, macc_fn_p)                   \
static void mmext_##insn(void *md, void *ms1, void *ms2,      \
                         CPURISCVState *env){                  \
    mmext_mmaqa_p(md, ms1, ms2, env, (macc_fn_p));            \
}

GEN_MMACC_P_HELPER(pmmacc_w_b,   macc_p_ss_s)
GEN_MMACC_P_HELPER(pmmaccu_w_b,  macc_p_uu_s)
GEN_MMACC_P_HELPER(pmmaccus_w_b, macc_p_us_s)
GEN_MMACC_P_HELPER(pmmaccsu_w_b, macc_p_su_s)

/* Optional 16b->64b integer macc: map to mmaqa*.h */
/* #define GEN_MMACC_H_HELPER(insn, macc_fn_h)                   \
static void mmext_##insn(void *md, void *ms1, void *ms2,      \
                         CPURISCVState *env){                  \
    mmext_mmaqa_h(md, ms1, ms2, env, (macc_fn_h));            \
}

GEN_MMACC_H_HELPER(mmacc_d_h,   macc_h_ss_d)
GEN_MMACC_H_HELPER(mmaccu_d_h,  macc_h_uu_d)
GEN_MMACC_H_HELPER(mmaccus_d_h, macc_h_us_d)
GEN_MMACC_H_HELPER(mmaccsu_d_h, macc_h_su_d) */

/* Optional packed formats (bp). For now, reuse the packed int4 path. */
// GEN_MMACC_P_HELPER(mmacc_w_bp,  macc_p_ss_s)
// GEN_MMACC_P_HELPER(mmaccu_w_bp, macc_p_uu_s)

/* fmmacc instructions */
static uint16_t fmacc16(uint16_t a, uint16_t b, uint16_t d, float_status * s)
{
    return float16_muladd(a, b, d, 0, s);
}

static uint16_t fmaccbf16(uint16_t a, uint16_t b, uint16_t d, float_status *s)
{
    return bfloat16_muladd(a, b, d, 0, s);
}

static uint32_t fmacc32(uint32_t a, uint32_t b, uint32_t d, float_status *s)
{
    return float32_muladd(a, b, d, 0, s);
}

/* static uint64_t fmacc64(uint64_t a, uint64_t b, uint64_t d, float_status *s)
{
    return float64_muladd(a, b, d, 0, s);
} */


static void fmmacc_h_impl(void *md, void *ms1, void *ms2,
                     CPURISCVState *env, uint32_t use_bf16){
    uint32_t i, j, k;
    uint16_t temp, psum;
    uint16_t oprd_a, oprd_b;
    void *ms2_pair_1 = ms2;
    void *ms2_pair_2 = (void *) (((int8_t *) ms2) + get_mlenb(env));
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env) * 2; j++) {
            temp = 0;
            for (k = 0; k < (env->mtilek >> 1); k++) {
                oprd_a = get_elem_h(ms1, i, k, env);
                if (j >= get_mrows(env)) {
                    oprd_b = get_elem_h(ms2_pair_2, j % (get_mrows(env)),
                                        k, env);
                } else {
                    oprd_b = get_elem_h(ms2_pair_1, j, k, env);
                }
                if (use_bf16) {
                    temp = fmaccbf16(oprd_a, oprd_b, temp, &env->fp_status);
                } else {
                    temp = fmacc16(oprd_a, oprd_b, temp, &env->fp_status);
                }
            }
            if (i < env->mtilem && j < env->mtilen) {
                psum = get_elem_h(md, i, j, env);
                if (use_bf16) {
                    psum = bfloat16_add(psum, temp, &env->fp_status);
                } else {
                    psum = float16_add(psum, temp, &env->fp_status);
                }
                set_elem_h(md, i, j, env, psum);
            } else {
                set_elem_h(md, i, j, env, 0);
            }
        }
    }
}

static void fmmacc_s_impl(void *md, void *ms1, void *ms2,
                     CPURISCVState *env){
    uint32_t i, j, k;
    uint32_t temp, psum;
    uint32_t oprd_a, oprd_b;
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < (env->mtilek >> 2); k++) {
                oprd_a = get_elem_s(ms1, i, k, env);
                oprd_b = get_elem_s(ms2, j, k, env);
                temp = fmacc32(oprd_a, oprd_b, temp, &env->fp_status);
            }
            if (i < env->mtilem && j < env->mtilen) {
                psum = get_elem_s(md, i, j, env);
                psum = float32_add(psum, temp, &env->fp_status);
                set_elem_s(md, i, j, env, psum);
            } else {
                set_elem_s(md, i, j, env, 0);
            }
        }
    }
}

/* static void fmmacc_d_impl(void *md, void *ms1, void *ms2,
                     CPURISCVState *env){
    uint32_t i, j, k;
    uint64_t temp, psum;
    uint64_t oprd_a, oprd_b;
    void *md_pair_1 = md;
    void *md_pair_2 = (void *) (((int8_t *) md) + get_mlenb(env));
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < (env->mtilek >> 3); k++) {
                oprd_a = get_elem_d(ms1, i, k, env);
                oprd_b = get_elem_d(ms2, j, k, env);
                temp = fmacc64(oprd_a, oprd_b, temp, &env->fp_status);
            }
            if (j >= (get_mrows(env) >> 1)) {
                if (i <= env->mtilem && j <= env->mtilen) {
                    psum = get_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1), env);
                    psum = float64_add(psum, temp, &env->fp_status);
                    set_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                               env, psum);
                } else {
                    set_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                               env, 0);
                }
            } else {
                if (i < env->mtilem && j < env->mtilen) {
                    psum = get_elem_d(md_pair_1, i, j, env);
                    psum = float64_add(psum, temp, &env->fp_status);
                    set_elem_d(md, i, j, env, psum);
                } else {
                    set_elem_d(md, i, j, env, 0);
                }
            }
        }
    }
} */

/* fwmacc instructions */
static uint32_t fwmacc16(uint16_t a, uint16_t b, uint32_t d, float_status *s)
{
    return float32_muladd(float16_to_float32(a, true, s),
                          float16_to_float32(b, true, s), d, 0, s);
}

/* static uint64_t fwmacc32(uint32_t a, uint32_t b, uint64_t d, float_status *s)
{
    return float64_muladd(float32_to_float64(a, s),
                          float32_to_float64(b, s), d, 0, s);
} */

static void fwmmacc_h_impl(void *md, void *ms1, void *ms2,
                      CPURISCVState *env){
    uint32_t i, j, k;
    uint32_t temp, psum;
    uint16_t oprd_a, oprd_b;
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < (env->mtilek >> 1); k++) {
                oprd_a = get_elem_h(ms1, i, k, env);
                oprd_b = get_elem_h(ms2, j, k, env);
                temp = fwmacc16(oprd_a, oprd_b, temp, &env->fp_status);
            }
            if (i < env->mtilem && j < env->mtilen) {
                psum = get_elem_s(md, i, j, env);
                psum = float32_add(psum, temp, &env->fp_status);
                set_elem_s(md, i, j, env, psum);
            } else {
                set_elem_s(md, i, j, env, 0);
            }
        }
    }
}

/* static void fwmmacc_s_impl(void *md, void *ms1, void *ms2,
                      CPURISCVState *env){
    uint32_t i, j, k;
    uint64_t temp, psum;
    uint32_t oprd_a, oprd_b;
    void *md_pair_1 = md;
    void *md_pair_2 = (void *) (((int8_t *) md) + get_mlenb(env));
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < (env->mtilek >> 2); k++) {
                oprd_a = get_elem_s(ms1, i, k, env);
                oprd_b = get_elem_s(ms2, j, k, env);
                temp = fwmacc32(oprd_a, oprd_b, temp, &env->fp_status);
            }
            if (j >= (get_mrows(env) >> 1)) {
                if (i < env->mtilem && j < env->mtilen) {
                    psum = get_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1), env);
                    psum = float64_add(psum, temp, &env->fp_status);
                    set_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                               env, psum);
                } else {
                    set_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                               env, 0);
                }
            } else {
                if (i < env->mtilem && j < env->mtilen) {
                    psum = get_elem_d(md_pair_1, i, j, env);
                    psum = float64_add(psum, temp, &env->fp_status);
                    set_elem_d(md_pair_1, i, j, env, psum);
                } else {
                    set_elem_d(md_pair_1, i, j, env, 0);
                }
            }
        }
    }
} */

/* Floating-point: mfmacc.* wrappers.
 *
 * Notes:
 *  - Existing fmmacc helpers implement fp16/fp32/fp64 (non-widen) and use env->fp_status.
 *  - Existing fwmmacc helpers implement widening fp16->fp32 and fp32->fp64.
 *  - XSAI proposal-12 do not support widening macc for 64b now.
 */
static void mmext_mfmacc_h(void *md, void *ms1, void *ms2, CPURISCVState *env)
{
    fmmacc_h_impl(md, ms1, ms2, env, 0);
}

static void mmext_mfmacc_s(void *md, void *ms1, void *ms2, CPURISCVState *env)
{
    fmmacc_s_impl(md, ms1, ms2, env);
}

// static void mmext_mfmacc_d(void *md, void *ms1, void *ms2, CPURISCVState *env)
// {
//     fmmacc_d_impl(md, ms1, ms2, env);
// }

static void mmext_mfmacc_s_h(void *md, void *ms1, void *ms2, CPURISCVState *env)
{
    fwmmacc_h_impl(md, ms1, ms2, env);
}

// static void mmext_mfmacc_d_s(void *md, void *ms1, void *ms2, CPURISCVState *env)
// {
//     fwmmacc_s_impl(md, ms1, ms2, env);
// }

/* BF16 inputs, FP32 accumulator: md = md + bf16(ms1) * bf16(ms2) */
static void mmext_mfmacc_s_bf16(void *md, void *ms1, void *ms2, CPURISCVState *env)
{
    uint32_t i, j, k;
    uint32_t temp, psum;
    uint16_t a16, b16;
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < (env->mtilek >> 1); k++) {
                a16 = (uint16_t)get_elem_h(ms1, i, k, env);
                b16 = (uint16_t)get_elem_h(ms2, j, k, env);
                /* Promote bf16->f32 then FMA in f32 */
                temp = float32_muladd(bfloat16_to_float32(a16, &env->fp_status),
                                      bfloat16_to_float32(b16, &env->fp_status),
                                      temp, 0, &env->fp_status);
            }
            if (i < env->mtilem && j < env->mtilen) {
                psum = (uint32_t)get_elem_s(md, i, j, env);
                psum = float32_add(psum, temp, &env->fp_status);
                set_elem_s(md, i, j, env, psum);
            } else {
                set_elem_s(md, i, j, env, 0);
            }
        }
    }
}

/* TF32 inputs, FP32 accumulator: like fp32, but truncate mantissa to 10 bits. */
static inline uint32_t tf32_trunc(uint32_t f32bits)
{
    /* Keep sign+exp+top-10 mantissa bits (23-10 = 13 truncated). */
    return f32bits & 0xFFFFE000u;
}

static void mmext_mfmacc_s_tf32(void *md, void *ms1, void *ms2, CPURISCVState *env)
{
    uint32_t i, j, k;
    uint32_t temp, psum;
    uint32_t a32, b32;
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < (env->mtilek >> 2); k++) {
                a32 = (uint32_t)get_elem_s(ms1, i, k, env);
                b32 = (uint32_t)get_elem_s(ms2, j, k, env);
                a32 = tf32_trunc(a32);
                b32 = tf32_trunc(b32);
                temp = float32_muladd(a32, b32, temp, 0, &env->fp_status);
            }
            if (i < env->mtilem && j < env->mtilen) {
                psum = (uint32_t)get_elem_s(md, i, j, env);
                psum = float32_add(psum, temp, &env->fp_status);
                set_elem_s(md, i, j, env, psum);
            } else {
                set_elem_s(md, i, j, env, 0);
            }
        }
    }
}

typedef enum MxfpFormat {
    MXFP_FORMAT_E4M3,
    MXFP_FORMAT_E5M2,
    MXFP_FORMAT_E2M1,
} MxfpFormat;

static bool mxfp_encode_param_from_mcfg(uint32_t type_code, uint32_t table,
                                        bool *fp4, bool *e5m2)
{
    if (table != 1) {
        return false;
    }
    switch (type_code) {
    case MCFG_TC_MXFP4:
        *fp4 = true;
        *e5m2 = false;
        return true;
    case MCFG_TC_FP8E5M2:
        *fp4 = false;
        *e5m2 = true;
        return true;
    case MCFG_TC_FP8E4M3:
        *fp4 = false;
        *e5m2 = false;
        return true;
    default:
        return false;
    }
}

static inline float128 mxfp_pow2_f128(int exponent, float_status *st)
{
    return float128_scalbn(make_float128(0x3fff000000000000ULL, 0),
                           exponent, st);
}

static inline uint32_t mxfp_decode_bits(uint8_t bits, MxfpFormat format);
static inline float128 mxfp_decode_f128(uint8_t bits, MxfpFormat format,
                                        float_status *st);

static uint8_t mxfp_encode_value(float128 value, uint8_t scale,
                                 MxfpFormat format, bool *invalid)
{
    float_status st = { 0 };
    const bool fp4 = format == MXFP_FORMAT_E2M1;
    const bool e5m2 = format == MXFP_FORMAT_E5M2;
    const uint8_t max_code = fp4 ? 7 : (e5m2 ? 123 : 126);
    const uint8_t sign_bit = fp4 ? 8 : 128;
    float128 magnitude = float128_abs(value);
    float128 d;
    float128 admission;
    float128 previous = float128_zero;
    uint8_t previous_code = 0;

    set_float_rounding_mode(float_round_nearest_even, &st);
    set_float_detect_tininess(float_tininess_after_rounding, &st);
    if (fp4 && scale > 251) {
        *invalid = true;
        return 0;
    }
    if (!fp4 && scale > 254) {
        *invalid = true;
        return 0;
    }
    if (float128_is_any_nan(value) ||
        float128_is_infinity(value)) {
        *invalid = true;
        return 0;
    }
    d = mxfp_pow2_f128((int)scale - 127, &st);
    admission = fp4 ?
        float128_mul(mxfp_pow2_f128((int)scale - 128, &st),
                     int32_to_float128(16, &st), &st) :
        float128_mul(d, int32_to_float128(e5m2 ? 57344 : 448, &st), &st);
    if (float128_lt_quiet(admission, magnitude, &st)) {
        *invalid = true;
        return 0;
    }
    if (float128_is_zero(magnitude)) {
        return !fp4 && float128_is_neg(value) ? sign_bit : 0;
    }
    for (uint8_t code = 1; code <= max_code; code++) {
        float128 candidate = float128_mul(
            mxfp_decode_f128(code, format, &st), d, &st);
        if (float128_eq_quiet(candidate, magnitude, &st)) {
            return code | (float128_is_neg(value) ? sign_bit : 0);
        }
        if (float128_lt_quiet(magnitude, candidate, &st)) {
            float128 sum = float128_add(previous, candidate, &st);
            float128 midpoint = float128_scalbn(sum, -1, &st);
            uint8_t best;

            if (float128_lt_quiet(magnitude, midpoint, &st)) {
                best = previous_code;
            } else if (float128_lt_quiet(midpoint, magnitude, &st)) {
                best = code;
            } else {
                best = fp4 ? previous_code :
                    ((previous_code & 1) ? code : previous_code);
            }
            if (best || !fp4) {
                best |= float128_is_neg(value) ? sign_bit : 0;
            }
            return best;
        }
        previous = candidate;
        previous_code = code;
    }
    return max_code | (float128_is_neg(value) ? sign_bit : 0);
}

uint32_t xsai_mxfp4_encode_f32_lane(uint32_t bits, uint32_t e, bool *invalid)
{
    float_status st = { 0 };
    float128 value;

    *invalid = false;
    if (e > UINT8_MAX) {
        *invalid = true;
        return 0;
    }

    set_float_rounding_mode(float_round_nearest_even, &st);
    set_float_detect_tininess(float_tininess_after_rounding, &st);
    value = float32_to_float128(bits, &st);
    return mxfp_encode_value(value, (uint8_t)e, MXFP_FORMAT_E2M1, invalid);
}

/*
 * Decode through integer fields so host floating-point rounding, tininess and
 * NaN canonicalization cannot affect the architectural result. Every finite
 * unscaled MX element is exactly representable as binary32.
 */
static inline uint32_t mxfp_decode_bits(uint8_t bits, MxfpFormat format)
{
    const bool fp4 = format == MXFP_FORMAT_E2M1;
    const bool e5m2 = format == MXFP_FORMAT_E5M2;
    const unsigned mant_bits = fp4 ? 1 : (e5m2 ? 2 : 3);
    const unsigned exp_mask = fp4 ? 3 : (e5m2 ? 31 : 15);
    const unsigned exp = (bits >> mant_bits) & exp_mask;
    unsigned sig = bits & ((1u << mant_bits) - 1);
    const uint32_t sign =
        ((uint32_t)bits >> (fp4 ? 3 : 7) & 1) << 31;
    const int bias = fp4 ? 1 : (e5m2 ? 15 : 7);
    int power;

    if (!fp4 && exp == exp_mask) {
        if (!e5m2 && sig == 7) {
            return sign | 0x7fc00000u;
        }
        if (e5m2) {
            return sign | 0x7f800000u | (sig << 21);
        }
    }

    power = (exp ? (int)exp : 1) - bias - (int)mant_bits;
    if (exp) {
        sig |= 1u << mant_bits;
    }
    if (!sig) {
        return sign;
    }
    while (sig < 0x800000u) {
        sig <<= 1;
        power--;
    }
    return sign | (uint32_t)(power + 23 + 127) << 23 |
           (sig & 0x7fffffu);
}

static inline float128 mxfp_decode_f128(uint8_t bits, MxfpFormat format,
                                        float_status *st)
{
    return float32_to_float128(mxfp_decode_bits(bits, format), st);
}

static bool mxfp_convert_scale_ready(CPURISCVState *env, uint32_t scale_reg,
                                     bool is_b_side, uint32_t rows,
                                     uint32_t type_code, uint32_t table)
{
    uint32_t side = is_b_side ? 4 : 0;
    uint32_t index = side + scale_reg;
    return env->mscale_valid[index] &&
           env->mscale_reg_generation[index] != 0 &&
           env->mscale_rows[index] == rows &&
           env->mscale_blocks[index] == 2 &&
           env->mscale_mode[index] == (uint8_t)((table << 4) | type_code);
}

/*
 * Guarded K32 scale selection on the original retained raw source. This is
 * the frozen integer admission domain, not a general log2/libm replacement.
 * Scalar NX admission belongs to the caller; no scalar FP state is modified.
 */
static bool mxfp_dynamic_scale(const uint32_t *source, uint8_t *scale)
{
    uint32_t maximum = 0;
    bool finite = true;
    uint32_t exponent, fraction;

    *scale = 0;
    for (uint32_t lane = 0; lane < RV_MX_BLOCK_SIZE; lane++) {
        uint32_t magnitude = source[lane] & 0x7fffffffu;
        finite &= (magnitude >> 23) != 255;
        maximum = MAX(maximum, magnitude);
    }
    if (!finite) {
        return false;
    }
    if (maximum == 0) {
        return true;
    }
    exponent = maximum >> 23;
    fraction = maximum & 0x7fffffu;
    if (exponent < 2 || exponent > 253 ||
        (fraction != 0 && (fraction < 0x400 || fraction > 0x7ffc00))) {
        return false;
    }
    *scale = exponent - 2;
    return true;
}

static void mmext_mxfp_convert_load(void *md, target_ulong base,
                                    target_ulong stride,
                                    uint32_t mreg_idx, CPURISCVState *env,
                                    bool is_b_side, bool dynamic)
{
    uint32_t type_code, table;
    uint32_t rows = (uint32_t)(is_b_side ? env->mtilen : env->mtilem);
    uint32_t blocks, row, block, lane;
    bool fp4, e5m2;
    uint8_t side = is_b_side ? 4 : 0;
    float_status st = env->fp_status;
    uint8_t packed_bytes;
    uint8_t scale_snapshot[RV_TROWNUM_MAX][RV_MX_MAX_SCALE_BLOCKS];
    uint32_t source[64][64];
    uintptr_t ra = GETPC();

    if (
#ifndef CONFIG_USER_ONLY
        env->priv != PRV_M ||
#endif
        mreg_idx > 3 || rows == 0 || rows > 64 ||
        rows > get_mrows(env) || env->mtilek != 64 ||
        stride < 256 || (stride & 3) != 0) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }
    if (!mmext_decode_mcfg(env, mreg_idx, &type_code, &table) ||
        !mxfp_encode_param_from_mcfg(type_code, table, &fp4, &e5m2)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }

    if (dynamic) {
        RISCVCPU *cpu = env_archcpu(env);

        if (!cpu->cfg.ext_matrix || !cpu->cfg.ext_mxfp_convert_load ||
            !cpu->cfg.ext_mxfp_dynamic_quant || !fp4 || rows != 8 ||
            get_rlenb(env) < 32 ||
            env->mscale_reg_generation[side + mreg_idx] == UINT32_MAX) {
            riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
        }
    }
    if ((base & 3) != 0) {
#ifdef CONFIG_USER_ONLY
        cpu_loop_exit_sigbus(env_cpu(env), base, MMU_DATA_LOAD, ra);
#else
        riscv_cpu_do_unaligned_access(env_cpu(env), base, MMU_DATA_LOAD,
                                     riscv_env_mmu_index(env, false), ra);
#endif
    }

    if (!dynamic && !mxfp_convert_scale_ready(env, mreg_idx, is_b_side, rows,
                                             type_code, table)) {
        error_report("mxfp convert-load missing/mismatched published %s "
                     "scale register %u (rows=%u valid=%u stored_rows=%u "
                     "stored_blocks=%u stored_mode=0x%02x wanted=0x%02x "
                     "generation=%u)",
                     is_b_side ? "B" : "A", mreg_idx, rows,
                     env->mscale_valid[side + mreg_idx],
                     env->mscale_rows[side + mreg_idx],
                     env->mscale_blocks[side + mreg_idx],
                     env->mscale_mode[side + mreg_idx],
                     (unsigned)((table << 4) | type_code),
                     env->mscale_reg_generation[side + mreg_idx]);
        exit(2);
    }

    packed_bytes = fp4 ? 32 : 64;
    if (packed_bytes > get_rlenb(env)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }
    bool raisedNv = false;
    for (row = 0; row < rows; row++) {
        probe_pages(env, base + row * stride, 256, ra, MMU_DATA_LOAD);
    }
    for (row = 0; row < rows; row++) {
        for (lane = 0; lane < 64; lane++) {
            source[row][lane] = cpu_ldl_data_ra(
                env, base + row * stride + lane * 4, ra);
        }
    }
    if (dynamic) {
        /* Same logical SF state, sixteen live bytes and E8M0-127 padding. */
        memset(scale_snapshot, 127, sizeof(scale_snapshot));
    } else {
        memcpy(scale_snapshot, env->mscale[side + mreg_idx],
               sizeof(scale_snapshot));
    }

    set_float_exception_flags(0, &st);
    set_float_rounding_mode(float_round_nearest_even, &st);
    set_float_detect_tininess(float_tininess_after_rounding, &st);
    blocks = (env->mtilek + RV_MX_BLOCK_SIZE - 1) / RV_MX_BLOCK_SIZE;
    for (row = 0; row < rows; row++) {
        for (block = 0; block < blocks; block++) {
            uint8_t scale;
            bool invalid = false;
            uint8_t encoded[32];
            bool scaleReject;

            if (dynamic) {
                scaleReject = !mxfp_dynamic_scale(
                    &source[row][block * RV_MX_BLOCK_SIZE], &scale);
            } else {
                scale = scale_snapshot[row][block];
                scaleReject = fp4 ? scale > 251 : scale > 254;
            }
            if (scaleReject) {
                invalid = true;
            } else {
                for (lane = 0; lane < RV_MX_BLOCK_SIZE; lane++) {
                    uint32_t bits =
                        source[row][block * RV_MX_BLOCK_SIZE + lane];
                    float128 value = float32_to_float128(bits, &st);
                    encoded[lane] = mxfp_encode_value(
                        value, scale, fp4 ? MXFP_FORMAT_E2M1 :
                        (type_code == MCFG_TC_FP8E5M2 ?
                         MXFP_FORMAT_E5M2 : MXFP_FORMAT_E4M3), &invalid);
                }
            }
            if (invalid || scaleReject) {
                memset(encoded, 0, sizeof(encoded));
                raisedNv = true;
            }
            if (dynamic) {
                scale_snapshot[row][block] = invalid || scaleReject ? 0 : scale;
            }
            for (lane = 0; lane < RV_MX_BLOCK_SIZE; lane++) {
                if (fp4) {
                    set_elem_nibble(md, row, block * 32 + lane, env,
                                    encoded[lane]);
                } else {
                    ((uint8_t *)md)[row * get_rlenb(env) +
                                    block * 32 + lane] = encoded[lane];
                }
            }
        }
    }
    if (dynamic) {
        /* Publish only after all raw reads and original packed TR writes.
         * Numeric rejection still owns an attempt/generation, never valid SF.
         */
        memcpy(env->mscale[side + mreg_idx], scale_snapshot,
               sizeof(scale_snapshot));
        env->mscale_rows[side + mreg_idx] = rows;
        env->mscale_blocks[side + mreg_idx] = blocks;
        env->mscale_mode[side + mreg_idx] =
            (uint8_t)((table << 4) | type_code);
        env->mscale_reg_generation[side + mreg_idx]++;
        env->mscale_valid[side + mreg_idx] = !raisedNv;
    }
    if (raisedNv) {
        env->xmfflags |= 0x10;
    }
    env->mcsr = (env->mcsr & ~(target_ulong)MCSR_FFLAGS) |
        ((env->xmfflags << 3) & MCSR_FFLAGS);
}

void HELPER(mxfp_convert_a)(void *md, target_ulong rs1, target_ulong rs2,
                            target_ulong mreg_idx, CPURISCVState *env)
{
    mmext_mxfp_convert_load(md, rs1, rs2, mreg_idx, env, false, false);
}

void HELPER(mxfp_convert_b)(void *md, target_ulong rs1, target_ulong rs2,
                            target_ulong mreg_idx, CPURISCVState *env)
{
    mmext_mxfp_convert_load(md, rs1, rs2, mreg_idx, env, true, false);
}

void HELPER(mxfp_dynamic_a)(void *md, target_ulong rs1, target_ulong rs2,
                            target_ulong mreg_idx, CPURISCVState *env)
{
    mmext_mxfp_convert_load(md, rs1, rs2, mreg_idx, env, false, true);
}

void HELPER(mxfp_dynamic_b)(void *md, target_ulong rs1, target_ulong rs2,
                            target_ulong mreg_idx, CPURISCVState *env)
{
    mmext_mxfp_convert_load(md, rs1, rs2, mreg_idx, env, true, true);
}

static inline bool mmext_is_mx_type(uint32_t type_code)
{
    return type_code == MCFG_TC_MXFP4 ||
           type_code == MCFG_TC_FP8E5M2 ||
           type_code == MCFG_TC_FP8E4M3;
}

static void mmext_mxfp_macc(void *md, void *ms1, void *ms2,
                            CPURISCVState *env, MxfpFormat format,
                            uint32_t a_scale_reg, uint32_t b_scale_reg)
{
    uint32_t i, j, k;
    float_status st = env->fp_status;

    /*
     * Matrix rollback arithmetic is independent of the scalar FP environment.
     * Keep all intermediate rounding and flags local, then publish only the
     * architecturally visible FP32 flags to xmfflags.
     */
    set_float_exception_flags(0, &st);
    set_float_rounding_mode(float_round_nearest_even, &st);
    set_float_detect_tininess(float_tininess_after_rounding, &st);

    for (i = 0; i < env->mtilem; i++) {
        for (j = 0; j < env->mtilen; j++) {
            uint32_t acc_bits = (uint32_t)get_elem_s(md, i, j, env);

            for (uint32_t base = 0; base < env->mtilek;
                 base += RV_MX_BLOCK_SIZE) {
                const uint32_t block = base / RV_MX_BLOCK_SIZE;
                const uint32_t lanes =
                    MIN(env->mtilek - base, RV_MX_BLOCK_SIZE);
                float128 dot;
                float128 acc;
                float128 sum;
                uint8_t scale_a = env->mscale[a_scale_reg][i][block];
                uint8_t scale_b = env->mscale[b_scale_reg][j][block];

                if (scale_a == 0xff || scale_b == 0xff) {
                    /*
                     * OCP MX v1.0: an E8M0=0xFF block has no dot product.
                     * Do not decode or multiply any element, so element
                     * sNaNs and Inf*0 cannot raise NV. The block's canonical
                     * qNaN still participates in the ordered C addition.
                     */
                    dot = make_float128(0x7fff800000000000ULL, 0);
                } else {
                    /*
                     * E5M2 is the widest case: <=32 products need at most 69
                     * significant bits. Binary128 therefore retains the entire
                     * finite unscaled block sum exactly.
                     */
                    for (k = 0; k < lanes; k++) {
                        float128 a;
                        float128 b;
                        float128 product;
                        uint8_t a_bits;
                        uint8_t b_bits;

                        if (format == MXFP_FORMAT_E2M1) {
                            a_bits = get_elem_nibble(ms1, i, base + k, env);
                            b_bits = get_elem_nibble(ms2, j, base + k, env);
                        } else {
                            a_bits = get_elem_b(ms1, i, base + k, env);
                            b_bits = get_elem_b(ms2, j, base + k, env);
                        }
                        a = mxfp_decode_f128(a_bits, format, &st);
                        b = mxfp_decode_f128(b_bits, format, &st);
                        product = float128_mul(a, b, &st);
                        if (k == 0) {
                            dot = product;
                        } else {
                            dot = float128_add(dot, product, &st);
                        }
                    }
                    dot = float128_scalbn(
                        dot, (int)scale_a + (int)scale_b - 254, &st);
                }

                acc = float32_to_float128(acc_bits, &st);
                set_float_rounding_mode(float_round_to_odd, &st);
                sum = float128_add(acc, dot, &st);
                set_float_rounding_mode(float_round_nearest_even, &st);
                acc_bits = float128_to_float32(sum, &st);
            }
            set_elem_s(md, i, j, env, acc_bits);
        }
    }

    env->xmfflags |= riscv_cpu_get_fflags_from_softfloat(
        get_float_exception_flags(&st));
}

/* ------------ Unified mmacc for XSAI proposal-12 ------------ */

void helper_mmacc(void *md, void *ms1, void *ms2,
                  target_ulong md_idx, target_ulong ms1_idx,
                  target_ulong ms2_idx, CPURISCVState *env)
{
    uint32_t type_a, type_b, type_c;
    uint32_t table_a, table_b, table_c;

    if (!mmext_decode_mcfg(env, (uint32_t)ms1_idx, &type_a, &table_a) ||
        !mmext_decode_mcfg(env, (uint32_t)ms2_idx, &type_b, &table_b) ||
        !mmext_decode_mcfg(env, (uint32_t)md_idx, &type_c, &table_c)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    if (table_a == 1 || table_b == 1) {
        uint32_t blocks =
            (env->mtilek + RV_MX_BLOCK_SIZE - 1) / RV_MX_BLOCK_SIZE;
        uint32_t a_scale_reg = ((uint32_t)ms1_idx & 0x3);
        uint32_t b_scale_reg = 4 + ((uint32_t)ms2_idx & 0x3);
        MxfpFormat format;

        if (table_a != 1 || table_b != 1 ||
            table_c != 0 ||
            type_a != type_b ||
            !mmext_is_mx_type(type_a) ||
            type_c != MCFG_TC_FP32 ||
            env->xmfrm != 0 ||
            env->mtilek == 0 ||
            env->mtilem > get_mrows(env) ||
            env->mtilen > get_mrows(env) ||
            blocks > RV_MX_MAX_SCALE_BLOCKS ||
            env->mtilem > RV_TROWNUM_MAX ||
            env->mtilen > RV_TROWNUM_MAX ||
            !env->mscale_valid[a_scale_reg] ||
            !env->mscale_valid[b_scale_reg] ||
            env->mtilem > env->mscale_rows[a_scale_reg] ||
            env->mtilen > env->mscale_rows[b_scale_reg] ||
            blocks > env->mscale_blocks[a_scale_reg] ||
            blocks > env->mscale_blocks[b_scale_reg] ||
            env->mtilek >
                (type_a == MCFG_TC_MXFP4 ? 2 * get_rlenb(env) :
                 get_rlenb(env))) {
            riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        }

        if (type_a == MCFG_TC_MXFP4) {
            format = MXFP_FORMAT_E2M1;
        } else if (type_a == MCFG_TC_FP8E5M2) {
            format = MXFP_FORMAT_E5M2;
        } else {
            format = MXFP_FORMAT_E4M3;
        }
        mmext_mxfp_macc(md, ms1, ms2, env, format,
                        a_scale_reg, b_scale_reg);
        return;
    }

    if (table_a != 0 || table_b != 0 || table_c != 0) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    uint32_t k_capacity;
    switch (type_a) {
    case MCFG_TC_INT4:
    case MCFG_TC_UINT4:
    case MCFG_TC_NVFP4:
        k_capacity = 2 * get_rlenb(env);
        break;
    case MCFG_TC_INT8:
    case MCFG_TC_UINT8:
        k_capacity = get_rlenb(env);
        break;
    case MCFG_TC_FP16:
    case MCFG_TC_BF16:
        k_capacity = get_rlenb(env) / 2;
        break;
    case MCFG_TC_INT32:
    case MCFG_TC_FP32:
    case MCFG_TC_TF32:
        k_capacity = get_rlenb(env) / 4;
        break;
    default:
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }
    if (env->mtilek == 0 || env->mtilek > k_capacity) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    if (type_c == MCFG_TC_INT32) {
        /* int8/uint8 -> int32 */
        if (type_a == MCFG_TC_INT8 && type_b == MCFG_TC_INT8) {
            mmext_mmacc_w_b(md, ms1, ms2, env);
            return;
        }
        if (type_a == MCFG_TC_UINT8 && type_b == MCFG_TC_UINT8) {
            mmext_mmaccu_w_b(md, ms1, ms2, env);
            return;
        }
        if (type_a == MCFG_TC_UINT8 && type_b == MCFG_TC_INT8) {
            mmext_mmaccus_w_b(md, ms1, ms2, env);
            return;
        }
        if (type_a == MCFG_TC_INT8 && type_b == MCFG_TC_UINT8) {
            mmext_mmaccsu_w_b(md, ms1, ms2, env);
            return;
        }

        /* packed int4/uint4 -> int32 */
        if (type_a == MCFG_TC_INT4 && type_b == MCFG_TC_INT4) {
            mmext_pmmacc_w_b(md, ms1, ms2, env);
            return;
        }
        if (type_a == MCFG_TC_UINT4 && type_b == MCFG_TC_UINT4) {
            mmext_pmmaccu_w_b(md, ms1, ms2, env);
            return;
        }
        if (type_a == MCFG_TC_UINT4 && type_b == MCFG_TC_INT4) {
            mmext_pmmaccus_w_b(md, ms1, ms2, env);
            return;
        }
        if (type_a == MCFG_TC_INT4 && type_b == MCFG_TC_UINT4) {
            mmext_pmmaccsu_w_b(md, ms1, ms2, env);
            return;
        }
    }

    if (type_a == MCFG_TC_FP16 && type_b == MCFG_TC_FP16 && type_c == MCFG_TC_FP16) {
        mmext_mfmacc_h(md, ms1, ms2, env);
        return;
    }
    if (type_a == MCFG_TC_FP16 && type_b == MCFG_TC_FP16 && type_c == MCFG_TC_FP32) {
        mmext_mfmacc_s_h(md, ms1, ms2, env);
        return;
    }
    if (type_a == MCFG_TC_BF16 && type_b == MCFG_TC_BF16 && type_c == MCFG_TC_FP32) {
        mmext_mfmacc_s_bf16(md, ms1, ms2, env);
        return;
    }
    if (type_a == MCFG_TC_TF32 && type_b == MCFG_TC_TF32 && type_c == MCFG_TC_FP32) {
        mmext_mfmacc_s_tf32(md, ms1, ms2, env);
        return;
    }
    if (type_a == MCFG_TC_FP32 && type_b == MCFG_TC_FP32 && type_c == MCFG_TC_FP32) {
        mmext_mfmacc_s(md, ms1, ms2, env);
        return;
    }

    /* Add more type macc here ... */
    printf("Unsupported mmacc type combination: A=%u, B=%u, C=%u\n", type_a, type_b, type_c);
    return;
}

void HELPER(sync_skip)(CPURISCVState *env) {
    /* 直接跳过指令的模拟执行 */
    return;
}
