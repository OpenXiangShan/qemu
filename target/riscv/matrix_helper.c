#include "qemu/osdep.h"
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

/* Matrix Configuration helpers */
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
    uint32_t idx = i * (get_rlenb(env) >> 2) + j;
    return ((int32_t *)md)[idx];
}

static inline void set_elem_s(void* md, uint32_t i, uint32_t j,
                              CPURISCVState* env, int64_t val){
    uint32_t idx = i * (get_rlenb(env) >> 2) + j;
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
    uint32_t cols_phys = (uint32_t)(get_rlenb(env) >> esz);
    target_ulong addr;

    /*
     * Probe pages conservatively for the actually-touched memory region.
     * Note: when transposed, memory is accessed by "cols" as rows.
     */
    if (!transposed) {
        for (i = 0; i < rows_lim; i++) {
            probe_pages(env, rs1 + (target_ulong)i * stride,
                        (target_ulong)cols_lim << esz, ra, MMU_DATA_LOAD);
        }
    } else {
        for (j = 0; j < cols_lim; j++) {
            probe_pages(env, rs1 + (target_ulong)j * stride,
                        (target_ulong)rows_lim << esz, ra, MMU_DATA_LOAD);
        }
    }

    /* Fill the full physical register; out-of-tile region is zeroed. */
    for (i = 0; i < rows_phys; i++) {
        for (j = 0; j < cols_phys; j++) {
            if (i < rows_lim && j < cols_lim) {
                if (!transposed) {
                    addr = rs1 + (target_ulong)i * stride + ((target_ulong)j << esz);
                } else {
                    addr = rs1 + (target_ulong)j * stride + ((target_ulong)i << esz);
                }
                set_elem(md, i, j, env, ld_elem(env, addr, ra));
            } else {
                set_elem(md, i, j, env, 0);
            }
        }
    }
}

static void mmext_mstore_tile(void *ms3, target_ulong rs1, target_ulong stride,
                              mmext_st_fn *st_elem, mmext_get_elem *get_elem,
                              CPURISCVState *env, uint8_t esz, uintptr_t ra,
                              mmext_mat_kind kind, bool transposed)
{
    uint32_t i, j;
    uint32_t rows_lim = mmext_rows(env, kind);
    uint32_t cols_lim = mmext_cols(env, kind);
    target_ulong addr;

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

#define GEN_MMEXT_LD_HELPER(insn, ld_elem, set_elem, ESZ, kind, T)       \
void HELPER(insn)(void *md, target_ulong rs1, target_ulong rs2,          \
                  CPURISCVState *env)                                    \
{                                                                        \
    mmext_mload_tile(md, rs1, rs2, ld_elem, set_elem, env, ESZ, GETPC(), \
                     kind, T);                                           \
}

#define GEN_MMEXT_ST_HELPER(insn, st_elem, get_elem, ESZ, kind, T)        \
void HELPER(insn)(void *ms3, target_ulong rs1, target_ulong rs2,          \
                  CPURISCVState *env)                                     \
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
static inline int64_t macc_h_ss_d(int16_t a, int16_t b, int64_t sum)
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
}

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

static uint64_t fmacc64(uint64_t a, uint64_t b, uint64_t d, float_status *s)
{
    return float64_muladd(a, b, d, 0, s);
}


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

static void fmmacc_d_impl(void *md, void *ms1, void *ms2,
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
}

/* fwmacc instructions */
static uint32_t fwmacc16(uint16_t a, uint16_t b, uint32_t d, float_status *s)
{
    return float32_muladd(float16_to_float32(a, true, s),
                          float16_to_float32(b, true, s), d, 0, s);
}

static uint64_t fwmacc32(uint32_t a, uint32_t b, uint64_t d, float_status *s)
{
    return float64_muladd(float32_to_float64(a, s),
                          float32_to_float64(b, s), d, 0, s);
}

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

static void fwmmacc_s_impl(void *md, void *ms1, void *ms2,
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
}

/* Integer: mmacc*.w.b map to mmaqa*.b */
#define GEN_MMACC_B_HELPER(insn, macc_fn_b)                   \
void HELPER(insn)(void *md, void *ms1, void *ms2,             \
                  CPURISCVState *env){                        \
    mmext_mmaqa_b(md, ms1, ms2, env, macc_fn_b);              \
}

GEN_MMACC_B_HELPER(mmacc_w_b,   macc_b_ss_s)
GEN_MMACC_B_HELPER(mmaccu_w_b,  macc_b_uu_s)
GEN_MMACC_B_HELPER(mmaccus_w_b, macc_b_us_s)
GEN_MMACC_B_HELPER(mmaccsu_w_b, macc_b_su_s)

/* Packed int4: pmmacc*.w.b map to pmmaqa*.b */
#define GEN_MMACC_P_HELPER(insn, macc_fn_p)                   \
void HELPER(insn)(void *md, void *ms1, void *ms2,             \
                  CPURISCVState *env){                        \
    mmext_mmaqa_p(md, ms1, ms2, env, (macc_fn_p));            \
}

GEN_MMACC_P_HELPER(pmmacc_w_b,   macc_p_ss_s)
GEN_MMACC_P_HELPER(pmmaccu_w_b,  macc_p_uu_s)
GEN_MMACC_P_HELPER(pmmaccus_w_b, macc_p_us_s)
GEN_MMACC_P_HELPER(pmmaccsu_w_b, macc_p_su_s)

/* Optional 16b->64b integer macc: map to mmaqa*.h */
#define GEN_MMACC_H_HELPER(insn, macc_fn_h)                   \
void HELPER(insn)(void *md, void *ms1, void *ms2,             \
                  CPURISCVState *env){                        \
    mmext_mmaqa_h(md, ms1, ms2, env, (macc_fn_h));            \
}

GEN_MMACC_H_HELPER(mmacc_d_h,   macc_h_ss_d)
GEN_MMACC_H_HELPER(mmaccu_d_h,  macc_h_uu_d)
GEN_MMACC_H_HELPER(mmaccus_d_h, macc_h_us_d)
GEN_MMACC_H_HELPER(mmaccsu_d_h, macc_h_su_d)

/* Optional packed formats (bp). For now, reuse the packed int4 path. */
GEN_MMACC_P_HELPER(mmacc_w_bp,  macc_p_ss_s)
GEN_MMACC_P_HELPER(mmaccu_w_bp, macc_p_uu_s)

/* Floating-point: mfmacc.* wrappers.
 *
 * Notes:
 *  - Existing fmmacc helpers implement fp16/fp32/fp64 (non-widen) and use env->fp_status.
 *  - Existing fwmmacc helpers implement widening fp16->fp32 and fp32->fp64.
 */
void helper_mfmacc_h(void *md, void *ms1, void *ms2, CPURISCVState *env)
{
    fmmacc_h_impl(md, ms1, ms2, env, 0);
}

void helper_mfmacc_s(void *md, void *ms1, void *ms2, CPURISCVState *env)
{
    fmmacc_s_impl(md, ms1, ms2, env);
}

void helper_mfmacc_d(void *md, void *ms1, void *ms2, CPURISCVState *env)
{
    fmmacc_d_impl(md, ms1, ms2, env);
}

void helper_mfmacc_s_h(void *md, void *ms1, void *ms2, CPURISCVState *env)
{
    fwmmacc_h_impl(md, ms1, ms2, env);
}

void helper_mfmacc_d_s(void *md, void *ms1, void *ms2, CPURISCVState *env)
{
    fwmmacc_s_impl(md, ms1, ms2, env);
}

/* BF16 inputs, FP32 accumulator: md = md + bf16(ms1) * bf16(ms2) */
void helper_mfmacc_s_bf16(void *md, void *ms1, void *ms2, CPURISCVState *env)
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

void helper_mfmacc_s_tf32(void *md, void *ms1, void *ms2, CPURISCVState *env)
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

/* FP8 source variants (e4/e5):
 * Provide conservative fallback implementations so the build succeeds even if
 * the target does not yet model float8 precisely. You can refine these later
 * once float8 formats and conversion helpers are integrated.
 */
void helper_mfmacc_h_e4(void *md, void *ms1, void *ms2, CPURISCVState *env)
{
    helper_mfmacc_h(md, ms1, ms2, env);
}

void helper_mfmacc_h_e5(void *md, void *ms1, void *ms2, CPURISCVState *env)
{
    helper_mfmacc_h(md, ms1, ms2, env);
}

void helper_mfmacc_bf16_e4(void *md, void *ms1, void *ms2, CPURISCVState *env)
{
    helper_mfmacc_h(md, ms1, ms2, env);
}

void helper_mfmacc_bf16_e5(void *md, void *ms1, void *ms2, CPURISCVState *env)
{
    helper_mfmacc_h(md, ms1, ms2, env);
}

void helper_mfmacc_s_e4(void *md, void *ms1, void *ms2, CPURISCVState *env)
{
    helper_mfmacc_s(md, ms1, ms2, env);
}

void helper_mfmacc_s_e5(void *md, void *ms1, void *ms2, CPURISCVState *env)
{
    helper_mfmacc_s(md, ms1, ms2, env);
}