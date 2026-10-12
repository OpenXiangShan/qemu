/*
 * BOSC IOMMU DID/PID address-range mapper.
 *
 * Copyright (c) 2026 Beijing Institute of Open Source Chip (BOSC)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/bosc-did-mapper.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/riscv-iommu.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/module.h"

#define BOSC_DID_MAPPER_PID_MASK       MAKE_64BIT_MASK(0, 20)
#define BOSC_DID_MAPPER_PID_VALID      BIT(20)
#define BOSC_DID_MAPPER_PID_TRANSLATED BIT(21)

struct BoscDidMapperState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    RISCVIOMMUState *iommu;

    uint32_t mux;
    uint32_t did[BOSC_DID_MAPPER_NR_SLOTS];
    uint32_t pid[BOSC_DID_MAPPER_NR_SLOTS];
    uint64_t start[BOSC_DID_MAPPER_NR_SLOTS];
    uint64_t end[BOSC_DID_MAPPER_NR_SLOTS];
};

static void bosc_did_mapper_flush(BoscDidMapperState *s)
{
    if (s->iommu) {
        riscv_iommu_flush_translation_caches(s->iommu);
    }
}

uint32_t bosc_did_mapper_get_mux(BoscDidMapperState *s)
{
    return s ? s->mux : 0;
}

bool bosc_did_mapper_apply(void *opaque, uint64_t iova, uint32_t *devid,
                           uint32_t *process_id, bool *process_id_valid)
{
    BoscDidMapperState *s = opaque;
    int i;

    if (!s || !devid || !process_id || !process_id_valid) {
        return false;
    }
    if (!(s->mux & BOSC_DID_MAPPER_MUX_DID_PID_EN)) {
        return false;
    }

    for (i = 0; i < BOSC_DID_MAPPER_NR_SLOTS; i++) {
        if (s->start[i] >= s->end[i]) {
            continue;
        }
        if (iova >= s->start[i] && iova < s->end[i]) {
            *devid = s->did[i] & 0xffffff;
            *process_id = s->pid[i] & BOSC_DID_MAPPER_PID_MASK;
            *process_id_valid = !!(s->pid[i] & BOSC_DID_MAPPER_PID_VALID);
            return true;
        }
    }

    return false;
}

uint64_t bosc_did_mapper_address_mask(void *opaque, uint64_t iova)
{
    BoscDidMapperState *s = opaque;
    uint64_t first = 0, last = UINT64_MAX, mask = UINT64_MAX;

    if (!s || !(s->mux & BOSC_DID_MAPPER_MUX_DID_PID_EN)) {
        return mask;
    }
    for (unsigned i = 0; i < BOSC_DID_MAPPER_NR_SLOTS; i++) {
        if (s->start[i] >= s->end[i]) {
            continue;
        }
        uint64_t edges[] = { s->start[i], s->end[i] };

        for (unsigned j = 0; j < ARRAY_SIZE(edges); j++) {
            if (edges[j] <= iova) {
                first = MAX(first, edges[j]);
            } else {
                last = MIN(last, edges[j] - 1);
            }
        }
    }
    /*
     * Conservatively split at every valid slot edge, including masked slots.
     * A page/superpage must not extend an override into a hit or miss range.
     */
    while ((iova & ~mask) < first || (iova | mask) > last) {
        mask >>= 1;
    }
    return mask;
}

static uint64_t bosc_did_mapper_read(void *opaque, hwaddr addr, unsigned size)
{
    BoscDidMapperState *s = opaque;
    unsigned idx;

    if (addr == BOSC_DID_MAPPER_MUX) {
        return s->mux;
    }
    if (addr >= BOSC_DID_MAPPER_DID_BASE &&
        addr < BOSC_DID_MAPPER_DID_BASE +
               BOSC_DID_MAPPER_NR_SLOTS * sizeof(uint32_t)) {
        idx = (addr - BOSC_DID_MAPPER_DID_BASE) / sizeof(uint32_t);
        return s->did[idx];
    }
    if (addr >= BOSC_DID_MAPPER_PID_BASE &&
        addr < BOSC_DID_MAPPER_PID_BASE +
               BOSC_DID_MAPPER_NR_SLOTS * sizeof(uint32_t)) {
        idx = (addr - BOSC_DID_MAPPER_PID_BASE) / sizeof(uint32_t);
        return s->pid[idx];
    }
    if (addr >= BOSC_DID_MAPPER_START_BASE &&
        addr < BOSC_DID_MAPPER_START_BASE +
               BOSC_DID_MAPPER_NR_SLOTS * sizeof(uint64_t)) {
        idx = (addr - BOSC_DID_MAPPER_START_BASE) / sizeof(uint64_t);
        return (addr & 4) ? (uint32_t)(s->start[idx] >> 32) :
                            (uint32_t)s->start[idx];
    }
    if (addr >= BOSC_DID_MAPPER_END_BASE &&
        addr < BOSC_DID_MAPPER_END_BASE +
               BOSC_DID_MAPPER_NR_SLOTS * sizeof(uint64_t)) {
        idx = (addr - BOSC_DID_MAPPER_END_BASE) / sizeof(uint64_t);
        return (addr & 4) ? (uint32_t)(s->end[idx] >> 32) :
                            (uint32_t)s->end[idx];
    }

    return 0;
}

static void bosc_did_mapper_write(void *opaque, hwaddr addr, uint64_t value,
                                  unsigned size)
{
    BoscDidMapperState *s = opaque;
    uint32_t val = value;
    unsigned idx;
    bool flush = true;

    if (addr == BOSC_DID_MAPPER_MUX) {
        s->mux = val & (BOSC_DID_MAPPER_MUX_BYPASS |
                        BOSC_DID_MAPPER_MUX_DID_PID_EN |
                        BOSC_DID_MAPPER_MUX_TRAFFIC |
                        BIT(3));
    } else if (addr >= BOSC_DID_MAPPER_DID_BASE &&
               addr < BOSC_DID_MAPPER_DID_BASE +
                      BOSC_DID_MAPPER_NR_SLOTS * sizeof(uint32_t)) {
        idx = (addr - BOSC_DID_MAPPER_DID_BASE) / sizeof(uint32_t);
        s->did[idx] = val & 0xffffff;
    } else if (addr >= BOSC_DID_MAPPER_PID_BASE &&
               addr < BOSC_DID_MAPPER_PID_BASE +
                      BOSC_DID_MAPPER_NR_SLOTS * sizeof(uint32_t)) {
        idx = (addr - BOSC_DID_MAPPER_PID_BASE) / sizeof(uint32_t);
        s->pid[idx] = val & (BOSC_DID_MAPPER_PID_MASK |
                             BOSC_DID_MAPPER_PID_VALID |
                             BOSC_DID_MAPPER_PID_TRANSLATED);
    } else if (addr >= BOSC_DID_MAPPER_START_BASE &&
               addr < BOSC_DID_MAPPER_START_BASE +
                      BOSC_DID_MAPPER_NR_SLOTS * sizeof(uint64_t)) {
        idx = (addr - BOSC_DID_MAPPER_START_BASE) / sizeof(uint64_t);
        if (addr & 4) {
            s->start[idx] = (s->start[idx] & MAKE_64BIT_MASK(0, 32)) |
                            ((uint64_t)val << 32);
        } else {
            s->start[idx] = (s->start[idx] & ~MAKE_64BIT_MASK(0, 32)) | val;
        }
    } else if (addr >= BOSC_DID_MAPPER_END_BASE &&
               addr < BOSC_DID_MAPPER_END_BASE +
                      BOSC_DID_MAPPER_NR_SLOTS * sizeof(uint64_t)) {
        idx = (addr - BOSC_DID_MAPPER_END_BASE) / sizeof(uint64_t);
        if (addr & 4) {
            s->end[idx] = (s->end[idx] & MAKE_64BIT_MASK(0, 32)) |
                          ((uint64_t)val << 32);
        } else {
            s->end[idx] = (s->end[idx] & ~MAKE_64BIT_MASK(0, 32)) | val;
        }
    } else {
        flush = false;
    }

    if (flush) {
        bosc_did_mapper_flush(s);
    }
}

static const MemoryRegionOps bosc_did_mapper_ops = {
    .read = bosc_did_mapper_read,
    .write = bosc_did_mapper_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void bosc_did_mapper_reset_state(BoscDidMapperState *s)
{
    int i;

    s->mux = 0;
    memset(s->did, 0, sizeof(s->did));
    memset(s->pid, 0, sizeof(s->pid));
    for (i = 0; i < BOSC_DID_MAPPER_NR_SLOTS; i++) {
        s->start[i] = UINT64_MAX;
        s->end[i] = UINT64_MAX;
    }
}

static void bosc_did_mapper_reset_hold(Object *obj, ResetType type)
{
    bosc_did_mapper_reset_state(BOSC_DID_MAPPER(obj));
}

static void bosc_did_mapper_realize(DeviceState *dev, Error **errp)
{
    BoscDidMapperState *s = BOSC_DID_MAPPER(dev);

    bosc_did_mapper_reset_state(s);
    memory_region_init_io(&s->iomem, OBJECT(s), &bosc_did_mapper_ops, s,
                          TYPE_BOSC_DID_MAPPER, BOSC_DID_MAPPER_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static const Property bosc_did_mapper_properties[] = {
    DEFINE_PROP_LINK("iommu", BoscDidMapperState, iommu,
                     TYPE_RISCV_IOMMU, RISCVIOMMUState *),
};

static const VMStateDescription bosc_did_mapper_vmstate = {
    .name = TYPE_BOSC_DID_MAPPER,
    .unmigratable = 1,
};

static void bosc_did_mapper_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = bosc_did_mapper_realize;
    dc->vmsd = &bosc_did_mapper_vmstate;
    dc->user_creatable = false;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, bosc_did_mapper_properties);
    rc->phases.hold = bosc_did_mapper_reset_hold;
}

static const TypeInfo bosc_did_mapper_info = {
    .name = TYPE_BOSC_DID_MAPPER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BoscDidMapperState),
    .class_init = bosc_did_mapper_class_init,
};

static void bosc_did_mapper_register_types(void)
{
    type_register_static(&bosc_did_mapper_info);
}

type_init(bosc_did_mapper_register_types)
