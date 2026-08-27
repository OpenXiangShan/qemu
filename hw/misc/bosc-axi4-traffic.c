/*
 * BOSC AXI4 traffic generator with DID/PID mapper.
 *
 * Copyright (c) 2026 Beijing Institute of Open Source Chip (BOSC)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/bosc-axi4-traffic.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/iommu.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/module.h"
#include "system/address-spaces.h"
#include "system/memory.h"

#define BOSC_AXI4_TRAFFIC_NR_SLOTS        8
#define BOSC_AXI4_TRAFFIC_NR_CFGS         6
#define BOSC_AXI4_TRAFFIC_ADDR_MASK       MAKE_64BIT_MASK(0, 40)
#define BOSC_AXI4_TRAFFIC_BEAT_BYTES      32

#define BOSC_AXI4_TRAFFIC_MUX             0x000
#define BOSC_AXI4_TRAFFIC_TRIGGER         0x010
#define BOSC_AXI4_TRAFFIC_DID_BASE        0x080
#define BOSC_AXI4_TRAFFIC_PID_BASE        0x0a0
#define BOSC_AXI4_TRAFFIC_START_BASE      0x0c0
#define BOSC_AXI4_TRAFFIC_END_BASE        0x100
#define BOSC_AXI4_TRAFFIC_AR_CFG_BASE     0x200
#define BOSC_AXI4_TRAFFIC_AW_CFG_BASE     0x220
#define BOSC_AXI4_TRAFFIC_START           0x240
#define BOSC_AXI4_TRAFFIC_STATUS          0x244

#define BOSC_AXI4_TRAFFIC_MUX_BYPASS      BIT(0)
#define BOSC_AXI4_TRAFFIC_MUX_DID_PID_EN  BIT(1)
#define BOSC_AXI4_TRAFFIC_MUX_TRAFFIC     BIT(2)

#define BOSC_AXI4_TRAFFIC_PID_MASK        MAKE_64BIT_MASK(0, 20)
#define BOSC_AXI4_TRAFFIC_PID_VALID       BIT(20)
#define BOSC_AXI4_TRAFFIC_PID_TRANSLATED  BIT(21)

#define BOSC_AXI4_TRAFFIC_START_AR        BIT(0)
#define BOSC_AXI4_TRAFFIC_START_AW        BIT(1)

#define BOSC_AXI4_TRAFFIC_STATUS_AW_DONE  BIT(29)
#define BOSC_AXI4_TRAFFIC_STATUS_AR_DONE  BIT(28)

struct BoscAxi4TrafficState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    RISCVIOMMUState *iommu;

    uint32_t mux;
    uint32_t ar_cfg[BOSC_AXI4_TRAFFIC_NR_CFGS];
    uint32_t aw_cfg[BOSC_AXI4_TRAFFIC_NR_CFGS];
    uint32_t did[BOSC_AXI4_TRAFFIC_NR_SLOTS];
    uint32_t pid[BOSC_AXI4_TRAFFIC_NR_SLOTS];
    uint64_t start[BOSC_AXI4_TRAFFIC_NR_SLOTS];
    uint64_t end[BOSC_AXI4_TRAFFIC_NR_SLOTS];

    uint32_t ar_bursts_done;
    uint8_t ar_error_count;
    bool ar_done;
    bool aw_done;
};

typedef struct BoscAxi4TrafficReq {
    uint64_t addr;
    uint32_t bursts;
    uint32_t beats;
    uint32_t did;
    uint32_t pid;
    bool pid_valid;
    bool translated;
} BoscAxi4TrafficReq;

static uint64_t bosc_axi4_traffic_user(const uint32_t cfg[static 6])
{
    return (((uint64_t)cfg[4] & MAKE_64BIT_MASK(0, 22)) << 32) | cfg[3];
}

static void bosc_axi4_traffic_decode_req(const uint32_t cfg[static 6],
                                         BoscAxi4TrafficReq *req)
{
    uint64_t user = bosc_axi4_traffic_user(cfg);

    memset(req, 0, sizeof(*req));
    req->addr = ((((uint64_t)cfg[2]) & 0xff) << 32) | cfg[1];
    req->addr &= BOSC_AXI4_TRAFFIC_ADDR_MASK;
    req->bursts = extract32(cfg[0], 12, 20);
    req->beats = extract32(cfg[2], 12, 8) + 1;
    req->did = (user >> 8) & 0xffffff;
    req->pid = (user >> 32) & 0xfffff;
    req->pid_valid = extract64(user, 52, 1);
    req->translated = extract64(user, 53, 1);
}

static int bosc_axi4_traffic_find_slot(BoscAxi4TrafficState *s, uint64_t addr)
{
    int i;

    for (i = 0; i < BOSC_AXI4_TRAFFIC_NR_SLOTS; i++) {
        if (addr >= s->start[i] && addr < s->end[i]) {
            return i;
        }
    }

    return -1;
}

static void bosc_axi4_traffic_apply_mapper(BoscAxi4TrafficState *s,
                                           BoscAxi4TrafficReq *req)
{
    int slot;

    if (!(s->mux & BOSC_AXI4_TRAFFIC_MUX_DID_PID_EN)) {
        return;
    }

    slot = bosc_axi4_traffic_find_slot(s, req->addr);
    if (slot < 0) {
        return;
    }

    req->did = s->did[slot] & 0xffffff;
    req->pid = s->pid[slot] & BOSC_AXI4_TRAFFIC_PID_MASK;
    req->pid_valid = s->pid[slot] & BOSC_AXI4_TRAFFIC_PID_VALID;
    req->translated = s->pid[slot] & BOSC_AXI4_TRAFFIC_PID_TRANSLATED;
}

static AddressSpace *bosc_axi4_traffic_get_as(BoscAxi4TrafficState *s,
                                              const BoscAxi4TrafficReq *req)
{
    if (s->mux & BOSC_AXI4_TRAFFIC_MUX_BYPASS) {
        return &address_space_memory;
    }

    return riscv_iommu_get_platform_requester_as(s->iommu, req->did, req->pid,
                                                req->pid_valid);
}

static void bosc_axi4_traffic_pattern_init(uint8_t data[static 32])
{
    int i;

    memset(data, 0, BOSC_AXI4_TRAFFIC_BEAT_BYTES);
    for (i = 0; i < 16; i++) {
        data[i * 2] = i;
    }
}

static void bosc_axi4_traffic_pattern_rotate(uint8_t data[static 32])
{
    uint8_t carry = data[31] >> 7;
    int i;

    for (i = 0; i < BOSC_AXI4_TRAFFIC_BEAT_BYTES; i++) {
        uint8_t next = data[i] >> 7;

        data[i] = (data[i] << 1) | carry;
        carry = next;
    }
}

static MemTxResult bosc_axi4_traffic_rw(AddressSpace *as, hwaddr addr,
                                        uint8_t data[static 32],
                                        bool is_write)
{
    return address_space_rw(as, addr, MEMTXATTRS_UNSPECIFIED, data,
                            BOSC_AXI4_TRAFFIC_BEAT_BYTES, is_write);
}

static bool bosc_axi4_traffic_run_write(BoscAxi4TrafficState *s)
{
    BoscAxi4TrafficReq req;
    AddressSpace *as;
    uint8_t pattern[BOSC_AXI4_TRAFFIC_BEAT_BYTES];
    uint32_t burst;

    s->aw_done = false;
    bosc_axi4_traffic_decode_req(s->aw_cfg, &req);

    if (!(s->mux & BOSC_AXI4_TRAFFIC_MUX_TRAFFIC)) {
        return false;
    }

    bosc_axi4_traffic_pattern_init(pattern);

    for (burst = 0; burst < req.bursts; burst++) {
        BoscAxi4TrafficReq eff_req = req;
        uint32_t beat;

        eff_req.addr = req.addr +
                       (uint64_t)burst * req.beats *
                       BOSC_AXI4_TRAFFIC_BEAT_BYTES;
        bosc_axi4_traffic_apply_mapper(s, &eff_req);
        as = bosc_axi4_traffic_get_as(s, &eff_req);

        for (beat = 0; beat < req.beats; beat++) {
            uint64_t addr = eff_req.addr +
                            (uint64_t)beat * BOSC_AXI4_TRAFFIC_BEAT_BYTES;

            if (bosc_axi4_traffic_rw(as, addr, pattern, true) != MEMTX_OK) {
                return false;
            }
            bosc_axi4_traffic_pattern_rotate(pattern);
        }
    }

    s->aw_done = true;
    return true;
}

static bool bosc_axi4_traffic_run_read(BoscAxi4TrafficState *s)
{
    BoscAxi4TrafficReq req;
    AddressSpace *as;
    uint8_t expected[BOSC_AXI4_TRAFFIC_BEAT_BYTES];
    uint32_t burst;

    s->ar_done = false;
    s->ar_bursts_done = 0;
    s->ar_error_count = 0;

    bosc_axi4_traffic_decode_req(s->ar_cfg, &req);

    if (!(s->mux & BOSC_AXI4_TRAFFIC_MUX_TRAFFIC)) {
        return false;
    }

    bosc_axi4_traffic_pattern_init(expected);

    for (burst = 0; burst < req.bursts; burst++) {
        BoscAxi4TrafficReq eff_req = req;
        uint32_t beat;

        eff_req.addr = req.addr +
                       (uint64_t)burst * req.beats *
                       BOSC_AXI4_TRAFFIC_BEAT_BYTES;
        bosc_axi4_traffic_apply_mapper(s, &eff_req);
        as = bosc_axi4_traffic_get_as(s, &eff_req);

        for (beat = 0; beat < req.beats; beat++) {
            uint8_t actual[BOSC_AXI4_TRAFFIC_BEAT_BYTES];
            uint64_t addr = eff_req.addr +
                            (uint64_t)beat * BOSC_AXI4_TRAFFIC_BEAT_BYTES;

            if (bosc_axi4_traffic_rw(as, addr, actual, false) != MEMTX_OK) {
                s->ar_error_count++;
                return false;
            }
            if (memcmp(actual, expected, sizeof(actual))) {
                s->ar_error_count++;
            }
            bosc_axi4_traffic_pattern_rotate(expected);
        }

        s->ar_bursts_done++;
    }

    s->ar_done = true;
    return true;
}

static uint32_t bosc_axi4_traffic_status(BoscAxi4TrafficState *s)
{
    uint32_t status = 0;

    if (s->aw_done) {
        status |= BOSC_AXI4_TRAFFIC_STATUS_AW_DONE;
    }
    if (s->ar_done) {
        status |= BOSC_AXI4_TRAFFIC_STATUS_AR_DONE;
    }

    status |= (s->ar_bursts_done & 0xfffff) << 8;
    status |= s->ar_error_count;
    return status;
}

static uint64_t bosc_axi4_traffic_read(void *opaque, hwaddr addr,
                                       unsigned size)
{
    BoscAxi4TrafficState *s = opaque;
    unsigned idx;

    if (addr == BOSC_AXI4_TRAFFIC_MUX) {
        return s->mux;
    }
    if (addr == BOSC_AXI4_TRAFFIC_TRIGGER) {
        return 0;
    }
    if (addr >= BOSC_AXI4_TRAFFIC_DID_BASE &&
        addr < BOSC_AXI4_TRAFFIC_DID_BASE +
               BOSC_AXI4_TRAFFIC_NR_SLOTS * sizeof(uint32_t)) {
        idx = (addr - BOSC_AXI4_TRAFFIC_DID_BASE) / sizeof(uint32_t);
        return s->did[idx];
    }
    if (addr >= BOSC_AXI4_TRAFFIC_PID_BASE &&
        addr < BOSC_AXI4_TRAFFIC_PID_BASE +
               BOSC_AXI4_TRAFFIC_NR_SLOTS * sizeof(uint32_t)) {
        idx = (addr - BOSC_AXI4_TRAFFIC_PID_BASE) / sizeof(uint32_t);
        return s->pid[idx];
    }
    if (addr >= BOSC_AXI4_TRAFFIC_START_BASE &&
        addr < BOSC_AXI4_TRAFFIC_START_BASE +
               BOSC_AXI4_TRAFFIC_NR_SLOTS * sizeof(uint64_t)) {
        idx = (addr - BOSC_AXI4_TRAFFIC_START_BASE) / sizeof(uint64_t);
        return (addr & 4) ? (uint32_t)(s->start[idx] >> 32) :
                            (uint32_t)s->start[idx];
    }
    if (addr >= BOSC_AXI4_TRAFFIC_END_BASE &&
        addr < BOSC_AXI4_TRAFFIC_END_BASE +
               BOSC_AXI4_TRAFFIC_NR_SLOTS * sizeof(uint64_t)) {
        idx = (addr - BOSC_AXI4_TRAFFIC_END_BASE) / sizeof(uint64_t);
        return (addr & 4) ? (uint32_t)(s->end[idx] >> 32) :
                            (uint32_t)s->end[idx];
    }
    if (addr >= BOSC_AXI4_TRAFFIC_AR_CFG_BASE &&
        addr < BOSC_AXI4_TRAFFIC_AR_CFG_BASE +
               BOSC_AXI4_TRAFFIC_NR_CFGS * sizeof(uint32_t)) {
        idx = (addr - BOSC_AXI4_TRAFFIC_AR_CFG_BASE) / sizeof(uint32_t);
        return s->ar_cfg[idx];
    }
    if (addr >= BOSC_AXI4_TRAFFIC_AW_CFG_BASE &&
        addr < BOSC_AXI4_TRAFFIC_AW_CFG_BASE +
               BOSC_AXI4_TRAFFIC_NR_CFGS * sizeof(uint32_t)) {
        idx = (addr - BOSC_AXI4_TRAFFIC_AW_CFG_BASE) / sizeof(uint32_t);
        return s->aw_cfg[idx];
    }
    if (addr == BOSC_AXI4_TRAFFIC_START) {
        return 0;
    }
    if (addr == BOSC_AXI4_TRAFFIC_STATUS) {
        return bosc_axi4_traffic_status(s);
    }

    return 0;
}

static void bosc_axi4_traffic_reset_read_status(BoscAxi4TrafficState *s)
{
    s->ar_bursts_done = 0;
    s->ar_error_count = 0;
    s->ar_done = false;
}

static void bosc_axi4_traffic_reset_write_status(BoscAxi4TrafficState *s)
{
    s->aw_done = false;
}

static void bosc_axi4_traffic_write(void *opaque, hwaddr addr,
                                    uint64_t value, unsigned size)
{
    BoscAxi4TrafficState *s = opaque;
    uint32_t val = value;
    unsigned idx;

    if (addr == BOSC_AXI4_TRAFFIC_MUX) {
        s->mux = val & (BOSC_AXI4_TRAFFIC_MUX_BYPASS |
                        BOSC_AXI4_TRAFFIC_MUX_DID_PID_EN |
                        BOSC_AXI4_TRAFFIC_MUX_TRAFFIC |
                        BIT(3));
        return;
    }
    if (addr == BOSC_AXI4_TRAFFIC_TRIGGER) {
        return;
    }
    if (addr >= BOSC_AXI4_TRAFFIC_DID_BASE &&
        addr < BOSC_AXI4_TRAFFIC_DID_BASE +
               BOSC_AXI4_TRAFFIC_NR_SLOTS * sizeof(uint32_t)) {
        idx = (addr - BOSC_AXI4_TRAFFIC_DID_BASE) / sizeof(uint32_t);
        s->did[idx] = val & 0xffffff;
        return;
    }
    if (addr >= BOSC_AXI4_TRAFFIC_PID_BASE &&
        addr < BOSC_AXI4_TRAFFIC_PID_BASE +
               BOSC_AXI4_TRAFFIC_NR_SLOTS * sizeof(uint32_t)) {
        idx = (addr - BOSC_AXI4_TRAFFIC_PID_BASE) / sizeof(uint32_t);
        s->pid[idx] = val & (BOSC_AXI4_TRAFFIC_PID_MASK |
                             BOSC_AXI4_TRAFFIC_PID_VALID |
                             BOSC_AXI4_TRAFFIC_PID_TRANSLATED);
        return;
    }
    if (addr >= BOSC_AXI4_TRAFFIC_START_BASE &&
        addr < BOSC_AXI4_TRAFFIC_START_BASE +
               BOSC_AXI4_TRAFFIC_NR_SLOTS * sizeof(uint64_t)) {
        idx = (addr - BOSC_AXI4_TRAFFIC_START_BASE) / sizeof(uint64_t);
        if (addr & 4) {
            s->start[idx] = (s->start[idx] & MAKE_64BIT_MASK(0, 32)) |
                            ((uint64_t)val << 32);
        } else {
            s->start[idx] = (s->start[idx] & ~MAKE_64BIT_MASK(0, 32)) | val;
        }
        return;
    }
    if (addr >= BOSC_AXI4_TRAFFIC_END_BASE &&
        addr < BOSC_AXI4_TRAFFIC_END_BASE +
               BOSC_AXI4_TRAFFIC_NR_SLOTS * sizeof(uint64_t)) {
        idx = (addr - BOSC_AXI4_TRAFFIC_END_BASE) / sizeof(uint64_t);
        if (addr & 4) {
            s->end[idx] = (s->end[idx] & MAKE_64BIT_MASK(0, 32)) |
                          ((uint64_t)val << 32);
        } else {
            s->end[idx] = (s->end[idx] & ~MAKE_64BIT_MASK(0, 32)) | val;
        }
        return;
    }
    if (addr >= BOSC_AXI4_TRAFFIC_AR_CFG_BASE &&
        addr < BOSC_AXI4_TRAFFIC_AR_CFG_BASE +
               BOSC_AXI4_TRAFFIC_NR_CFGS * sizeof(uint32_t)) {
        idx = (addr - BOSC_AXI4_TRAFFIC_AR_CFG_BASE) / sizeof(uint32_t);
        s->ar_cfg[idx] = val;
        if (idx == 5 && (val & BIT(0))) {
            bosc_axi4_traffic_reset_read_status(s);
        }
        return;
    }
    if (addr >= BOSC_AXI4_TRAFFIC_AW_CFG_BASE &&
        addr < BOSC_AXI4_TRAFFIC_AW_CFG_BASE +
               BOSC_AXI4_TRAFFIC_NR_CFGS * sizeof(uint32_t)) {
        idx = (addr - BOSC_AXI4_TRAFFIC_AW_CFG_BASE) / sizeof(uint32_t);
        s->aw_cfg[idx] = val;
        if (idx == 5 && (val & BIT(0))) {
            bosc_axi4_traffic_reset_write_status(s);
        }
        return;
    }
    if (addr == BOSC_AXI4_TRAFFIC_START) {
        if (val & BOSC_AXI4_TRAFFIC_START_AW) {
            bosc_axi4_traffic_run_write(s);
        }
        if (val & BOSC_AXI4_TRAFFIC_START_AR) {
            bosc_axi4_traffic_run_read(s);
        }
    }
}

static const MemoryRegionOps bosc_axi4_traffic_ops = {
    .read = bosc_axi4_traffic_read,
    .write = bosc_axi4_traffic_write,
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

static void bosc_axi4_traffic_reset_hold(Object *obj, ResetType type)
{
    BoscAxi4TrafficState *s = BOSC_AXI4_TRAFFIC(obj);
    int i;

    s->mux = 0;
    memset(s->ar_cfg, 0, sizeof(s->ar_cfg));
    memset(s->aw_cfg, 0, sizeof(s->aw_cfg));
    memset(s->did, 0, sizeof(s->did));
    memset(s->pid, 0, sizeof(s->pid));

    for (i = 0; i < BOSC_AXI4_TRAFFIC_NR_SLOTS; i++) {
        s->start[i] = UINT64_MAX;
        s->end[i] = UINT64_MAX;
    }

    bosc_axi4_traffic_reset_read_status(s);
    bosc_axi4_traffic_reset_write_status(s);
}

static void bosc_axi4_traffic_realize(DeviceState *dev, Error **errp)
{
    BoscAxi4TrafficState *s = BOSC_AXI4_TRAFFIC(dev);

    if (!s->iommu) {
        error_setg(errp, "iommu link is required");
        return;
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &bosc_axi4_traffic_ops, s,
                          TYPE_BOSC_AXI4_TRAFFIC,
                          BOSC_AXI4_TRAFFIC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static const Property bosc_axi4_traffic_properties[] = {
    DEFINE_PROP_LINK("iommu", BoscAxi4TrafficState, iommu,
                     TYPE_RISCV_IOMMU, RISCVIOMMUState *),
};

static const VMStateDescription bosc_axi4_traffic_vmstate = {
    .name = TYPE_BOSC_AXI4_TRAFFIC,
    .unmigratable = 1,
};

static void bosc_axi4_traffic_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = bosc_axi4_traffic_realize;
    dc->vmsd = &bosc_axi4_traffic_vmstate;
    dc->user_creatable = false;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, bosc_axi4_traffic_properties);
    rc->phases.hold = bosc_axi4_traffic_reset_hold;
}

static const TypeInfo bosc_axi4_traffic_info = {
    .name = TYPE_BOSC_AXI4_TRAFFIC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BoscAxi4TrafficState),
    .class_init = bosc_axi4_traffic_class_init,
};

static void bosc_axi4_traffic_register_types(void)
{
    type_register_static(&bosc_axi4_traffic_info);
}

type_init(bosc_axi4_traffic_register_types)
