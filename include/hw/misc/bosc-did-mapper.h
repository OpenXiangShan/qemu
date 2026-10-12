/*
 * BOSC IOMMU DID/PID address-range mapper.
 *
 * Copyright (c) 2026 Beijing Institute of Open Source Chip (BOSC)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_BOSC_DID_MAPPER_H
#define HW_MISC_BOSC_DID_MAPPER_H

#include "qemu/bitops.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_BOSC_DID_MAPPER "bosc-did-mapper"
OBJECT_DECLARE_SIMPLE_TYPE(BoscDidMapperState, BOSC_DID_MAPPER)

#define BOSC_DID_MAPPER_MMIO_SIZE         0x200
#define BOSC_DID_MAPPER_NR_SLOTS          8

#define BOSC_DID_MAPPER_MUX               0x000
#define BOSC_DID_MAPPER_DID_BASE          0x080
#define BOSC_DID_MAPPER_PID_BASE          0x0a0
#define BOSC_DID_MAPPER_START_BASE        0x0c0
#define BOSC_DID_MAPPER_END_BASE          0x100

#define BOSC_DID_MAPPER_MUX_BYPASS        BIT(0)
#define BOSC_DID_MAPPER_MUX_DID_PID_EN    BIT(1)
#define BOSC_DID_MAPPER_MUX_TRAFFIC       BIT(2)

/*
 * If mapper enable is set and @iova hits a programmed slot, overwrite
 * *devid / *process_id / *process_id_valid and return true. Otherwise
 * leave the outputs unchanged and return false (caller keeps the original
 * requester identity).
 */
bool bosc_did_mapper_apply(void *opaque, uint64_t iova, uint32_t *devid,
                           uint32_t *process_id, bool *process_id_valid);
uint32_t bosc_did_mapper_get_mux(BoscDidMapperState *s);
/* Largest aligned translation mask that cannot cross an enabled slot edge. */
uint64_t bosc_did_mapper_address_mask(void *opaque, uint64_t iova);

#endif /* HW_MISC_BOSC_DID_MAPPER_H */
