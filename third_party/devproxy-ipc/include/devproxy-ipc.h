/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef DEVPROXY_IPC_H
#define DEVPROXY_IPC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool devproxy_mmio_access_size_valid(uint32_t size);

typedef enum DevProxyIPCOpcode {
    DEVPROXY_IPC_OPCODE_PING = 0,
    DEVPROXY_IPC_OPCODE_MMIO_READ = 1,
    DEVPROXY_IPC_OPCODE_MMIO_WRITE = 2,
    DEVPROXY_IPC_OPCODE_DMA_READ = 3,
    DEVPROXY_IPC_OPCODE_DMA_WRITE = 4,
    DEVPROXY_IPC_OPCODE_IRQ = 5,
    DEVPROXY_IPC_OPCODE_MSI = 6,
} DevProxyIPCOpcode;

void *devproxy_mmio_client_create(const char *socket_path);
void devproxy_mmio_client_destroy(void *handle);
int devproxy_mmio_client_read(void *handle, uint32_t cpu_index, uint32_t size,
                              uint64_t phys_addr, uint64_t *data);
int devproxy_mmio_client_write(void *handle, uint32_t cpu_index, uint32_t size,
                               uint64_t phys_addr, uint64_t data);

typedef int (*DevProxyDmaTransferHandler)(void *opaque,
                                          uint32_t opcode,
                                          uint32_t cpu_index,
                                          uint32_t requester_id,
                                          uint32_t pasid,
                                          uint32_t flags,
                                          uint64_t phys_addr,
                                          void *buf,
                                          uint32_t len,
                                          bool is_write,
                                          uint32_t *response_size,
                                          uint64_t *response_data,
                                          uint64_t *response_data_offset);

typedef void (*DevProxyIRQSetHandler)(void *opaque,
                                      uint32_t irq,
                                      bool level);

typedef int (*DevProxyMSINotifyHandler)(void *opaque,
                                        uint64_t phys_addr,
                                        uint32_t data,
                                        uint32_t size);

void *devproxy_dma_server_create(const char *socket_path,
                                 const char *shm_path,
                                 uint32_t shm_size);
void devproxy_dma_server_destroy(void *handle);
int devproxy_dma_server_start(void *handle, DevProxyDmaTransferHandler handler,
                              void *opaque);
void devproxy_dma_server_stop(void *handle);

void *devproxy_irq_server_create(const char *socket_path);
void devproxy_irq_server_destroy(void *handle);
int devproxy_irq_server_start(void *handle, DevProxyIRQSetHandler handler,
                              void *opaque);
void devproxy_irq_server_stop(void *handle);

void *devproxy_msi_server_create(const char *socket_path);
void devproxy_msi_server_destroy(void *handle);
int devproxy_msi_server_start(void *handle, DevProxyMSINotifyHandler handler,
                              void *opaque);
void devproxy_msi_server_stop(void *handle);

#ifdef __cplusplus
}
#endif

#endif
