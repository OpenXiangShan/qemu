/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef SYSTEM_DEVPROXY_WIRE_H
#define SYSTEM_DEVPROXY_WIRE_H

#include <stdint.h>

#include "qemu/compiler.h"

#define DEVPROXY_VERSION 6

enum DevProxyOpcode {
    DEVPROXY_OP_PING = 0,
    DEVPROXY_OP_MMIO_READ = 1,
    DEVPROXY_OP_MMIO_WRITE = 2,
    DEVPROXY_OP_DMA_READ = 3,
    DEVPROXY_OP_DMA_WRITE = 4,
    DEVPROXY_OP_IRQ = 5,
    DEVPROXY_OP_MSI = 6,
};

typedef struct DevProxyWireRequest {
    uint32_t version;
    uint32_t opcode;
    uint32_t cpu_index;
    uint32_t requester_id;
    uint32_t pasid;
    uint32_t flags;
    uint32_t size;
    uint64_t phys_addr;
    uint64_t data;
    uint64_t data_offset;
} QEMU_PACKED DevProxyWireRequest;

typedef struct DevProxyWireResponse {
    uint32_t version;
    int32_t status;
    uint32_t size;
    uint64_t data;
    uint64_t data_offset;
} QEMU_PACKED DevProxyWireResponse;

#endif
