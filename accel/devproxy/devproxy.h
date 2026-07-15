/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef ACCEL_DEVPROXY_DEVPROXY_H
#define ACCEL_DEVPROXY_DEVPROXY_H

#include "accel/accel-ops.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/core/cpu.h"
#include "qemu/bitmap.h"
#include "qemu/notify.h"
#include "qemu/thread.h"
#include "system/devproxy-dma.h"
#include "system/memory.h"

#define TYPE_DEVPROXY_ACCEL ACCEL_CLASS_NAME("devproxy")
#define DEVPROXY_STATE(obj) \
    OBJECT_CHECK(DevProxyState, (obj), TYPE_DEVPROXY_ACCEL)

typedef struct DevProxyProxyState DevProxyProxyState;

typedef enum DevProxyDMABackendMode {
    DEVPROXY_DMA_BACKEND_SOCKET = 0,
    DEVPROXY_DMA_BACKEND_SHARED_BOUNCE,
} DevProxyDMABackendMode;

#define DEVPROXY_PLATFORM_DMA_REQUESTER_BITS (1U << 16)

typedef struct DevProxyState {
    AccelState parent_obj;
    char *socket;
    char *dma_socket;
    char *dma_shm_path;
    uint64_t dma_shm_size;
    DevProxyDMABackendMode dma_backend_mode;
    int dma_fd;
    int dma_shm_fd;
    QemuMutex dma_lock;
    bool dma_lock_initialized;
    DevProxyProxyState *proxy;
    Notifier machine_done;
    MemoryRegion dma_root_mr;
    MemoryRegion dma_mr;
    AddressSpace dma_as;
    void *dma_shm_ptr;
    hwaddr dma_local_base;
    hwaddr dma_local_size;
    DECLARE_BITMAP(platform_dma_requesters,
                   DEVPROXY_PLATFORM_DMA_REQUESTER_BITS);
    bool dma_root_mr_initialized;
    bool dma_mr_initialized;
    bool dma_as_initialized;
} DevProxyState;

int devproxy_init_machine(AccelState *as, MachineState *ms);
int devproxy_cpu_exec(CPUState *cpu);
void devproxy_kick_vcpu(CPUState *cpu);
void devproxy_dma_set_local_window(hwaddr base, hwaddr size);

#endif
