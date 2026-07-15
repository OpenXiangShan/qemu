/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef SYSTEM_DEVPROXY_DMA_H
#define SYSTEM_DEVPROXY_DMA_H

#include "qapi/error.h"
#include "qemu/typedefs.h"
#include "system/memory.h"

bool devproxy_dma_address_space(AddressSpace *as, hwaddr addr, hwaddr len,
                               bool is_write, MemTxAttrs attrs);
bool devproxy_dma_access_valid(AddressSpace *as, hwaddr addr, hwaddr len,
                               bool is_write, MemTxAttrs attrs);
MemTxResult devproxy_dma_memory_rw(AddressSpace *as, hwaddr addr,
                                   MemTxAttrs attrs, void *buf, hwaddr len,
                                   bool is_write);
AddressSpace *devproxy_dma_get_address_space(void);
bool devproxy_dma_register_platform_requester(uint32_t requester_id,
                                              Error **errp);
void *devproxy_dma_memory_map(AddressSpace *as, hwaddr addr, hwaddr *plen,
                              bool is_write, MemTxAttrs attrs);
void devproxy_dma_memory_unmap(AddressSpace *as, void *buffer, hwaddr len,
                               bool is_write, hwaddr access_len);

#endif
