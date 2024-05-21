/* Automatically generated nanopb header */
/* Generated by nanopb-0.4.9-dev */

#ifndef PB_CHECKPOINT_PB_H_INCLUDED
#define PB_CHECKPOINT_PB_H_INCLUDED
#include "checkpoint/pb.h"

#if PB_PROTO_HEADER_VERSION != 40
#error Regenerate this file with the current version of nanopb generator.
#endif

/* Struct definitions */
typedef struct _checkpoint_header {
    uint64_t magic_number;
    uint64_t cpt_offset;
    uint64_t cpu_num;
    uint64_t single_core_size;
    uint64_t version;
} checkpoint_header;

typedef struct _single_core_rvgc_rvv_rvh_memlayout {
    uint64_t pc_cpt_addr;
    uint64_t mode_cpt_addr;
    uint64_t mtime_cpt_addr;
    uint64_t mtime_cmp_cpt_addr;
    uint64_t misc_done_cpt_addr;
    uint64_t misc_reserve;
    uint64_t int_reg_cpt_addr;
    uint64_t int_reg_done;
    uint64_t float_reg_cpt_addr;
    uint64_t float_reg_done;
    uint64_t csr_reg_cpt_addr;
    uint64_t csr_reg_done;
    uint64_t csr_reserve;
    uint64_t vector_reg_cpt_addr;
    uint64_t vector_reg_done;
} single_core_rvgc_rvv_rvh_memlayout;


/* Initializer values for message structs */
#define checkpoint_header_init_default           {0, 0, 0, 0, 0}
#define single_core_rvgc_rvv_rvh_memlayout_init_default {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
#define checkpoint_header_init_zero              {0, 0, 0, 0, 0}
#define single_core_rvgc_rvv_rvh_memlayout_init_zero {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

/* Field tags (for use in manual encoding/decoding) */
#define checkpoint_header_magic_number_tag       1
#define checkpoint_header_cpt_offset_tag         2
#define checkpoint_header_cpu_num_tag            3
#define checkpoint_header_single_core_size_tag   5
#define checkpoint_header_version_tag            6
#define single_core_rvgc_rvv_rvh_memlayout_pc_cpt_addr_tag 12
#define single_core_rvgc_rvv_rvh_memlayout_mode_cpt_addr_tag 13
#define single_core_rvgc_rvv_rvh_memlayout_mtime_cpt_addr_tag 14
#define single_core_rvgc_rvv_rvh_memlayout_mtime_cmp_cpt_addr_tag 15
#define single_core_rvgc_rvv_rvh_memlayout_misc_done_cpt_addr_tag 16
#define single_core_rvgc_rvv_rvh_memlayout_misc_reserve_tag 17
#define single_core_rvgc_rvv_rvh_memlayout_int_reg_cpt_addr_tag 18
#define single_core_rvgc_rvv_rvh_memlayout_int_reg_done_tag 19
#define single_core_rvgc_rvv_rvh_memlayout_float_reg_cpt_addr_tag 20
#define single_core_rvgc_rvv_rvh_memlayout_float_reg_done_tag 21
#define single_core_rvgc_rvv_rvh_memlayout_csr_reg_cpt_addr_tag 22
#define single_core_rvgc_rvv_rvh_memlayout_csr_reg_done_tag 23
#define single_core_rvgc_rvv_rvh_memlayout_csr_reserve_tag 24
#define single_core_rvgc_rvv_rvh_memlayout_vector_reg_cpt_addr_tag 25
#define single_core_rvgc_rvv_rvh_memlayout_vector_reg_done_tag 26

/* Struct field encoding specification for nanopb */
#define checkpoint_header_FIELDLIST(X, a) \
X(a, STATIC,   SINGULAR, UINT64,   magic_number,      1) \
X(a, STATIC,   SINGULAR, UINT64,   cpt_offset,        2) \
X(a, STATIC,   SINGULAR, UINT64,   cpu_num,           3) \
X(a, STATIC,   SINGULAR, UINT64,   single_core_size,   5) \
X(a, STATIC,   SINGULAR, UINT64,   version,           6)
#define checkpoint_header_CALLBACK NULL
#define checkpoint_header_DEFAULT NULL

#define single_core_rvgc_rvv_rvh_memlayout_FIELDLIST(X, a) \
X(a, STATIC,   SINGULAR, UINT64,   pc_cpt_addr,      12) \
X(a, STATIC,   SINGULAR, UINT64,   mode_cpt_addr,    13) \
X(a, STATIC,   SINGULAR, UINT64,   mtime_cpt_addr,   14) \
X(a, STATIC,   SINGULAR, UINT64,   mtime_cmp_cpt_addr,  15) \
X(a, STATIC,   SINGULAR, UINT64,   misc_done_cpt_addr,  16) \
X(a, STATIC,   SINGULAR, UINT64,   misc_reserve,     17) \
X(a, STATIC,   SINGULAR, UINT64,   int_reg_cpt_addr,  18) \
X(a, STATIC,   SINGULAR, UINT64,   int_reg_done,     19) \
X(a, STATIC,   SINGULAR, UINT64,   float_reg_cpt_addr,  20) \
X(a, STATIC,   SINGULAR, UINT64,   float_reg_done,   21) \
X(a, STATIC,   SINGULAR, UINT64,   csr_reg_cpt_addr,  22) \
X(a, STATIC,   SINGULAR, UINT64,   csr_reg_done,     23) \
X(a, STATIC,   SINGULAR, UINT64,   csr_reserve,      24) \
X(a, STATIC,   SINGULAR, UINT64,   vector_reg_cpt_addr,  25) \
X(a, STATIC,   SINGULAR, UINT64,   vector_reg_done,  26)
#define single_core_rvgc_rvv_rvh_memlayout_CALLBACK NULL
#define single_core_rvgc_rvv_rvh_memlayout_DEFAULT NULL

extern const pb_msgdesc_t checkpoint_header_msg;
extern const pb_msgdesc_t single_core_rvgc_rvv_rvh_memlayout_msg;

/* Defines for backwards compatibility with code written before nanopb-0.4.0 */
#define checkpoint_header_fields &checkpoint_header_msg
#define single_core_rvgc_rvv_rvh_memlayout_fields &single_core_rvgc_rvv_rvh_memlayout_msg

/* Maximum encoded size of messages (where known) */
#define CHECKPOINT_PB_H_MAX_SIZE                 single_core_rvgc_rvv_rvh_memlayout_size
#define checkpoint_header_size                   55
#define single_core_rvgc_rvv_rvh_memlayout_size  176

#endif
