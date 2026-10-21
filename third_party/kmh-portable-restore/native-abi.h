/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef KMH_NATIVE_ABI_H
#define KMH_NATIVE_ABI_H

/* All fields are little endian. This header is shared by host and RV64 code. */
#define KMH_NATIVE_VERSION       3
#define KMH_NATIVE_HEADER_SIZE   192
#define KMH_NATIVE_ENTRY         0x80000000ULL
#define KMH_NATIVE_WORK          0x80300000ULL
#define KMH_NATIVE_WORK_SIZE     0x100000U
#define KMH_NATIVE_DIRECTORY     0x92000000ULL
#define KMH_NATIVE_ENTRY_SIZE    64U
#define KMH_NATIVE_SECTION       5U
#define KMH_NATIVE_MAGIC         0x33564e4bU /* KNV3 */
#define KMH_NATIVE_OP_SIZE       24U
#define KMH_NATIVE_OPS_OFFSET    128U
#define KMH_NATIVE_GLOBAL        0xffffffffU
#define KMH_NATIVE_MMIO32        1U
#define KMH_NATIVE_MMIO8         2U
#define KMH_NATIVE_MIREG         3U
#define KMH_NATIVE_SIREG         4U
#define KMH_NATIVE_CPU_ABI       2U

/* Native section: magic/u32, count/u32, UART PA/u64, DDR segments/u32,
 * reserved bytes to 64, original entry bytes[64], operation records[].
 * Each record: address/u64, value/u64, hart/u32, kind/u16, phase/u16.
 * DDR segment descriptors follow the operation records: PA/offset/size (u64).
 * phase 0 configures disabled devices; phase 1 enables delivery.
 */
#define KMH_NATIVE_BACKUP_OFFSET 64U
#define KMH_NATIVE_SEGMENT_SIZE  24U
#endif
