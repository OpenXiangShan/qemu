#ifndef IO_MY_VIRTIO_BLK_H
#define IO_MY_VIRTIO_BLK_H

#include <stdbool.h>
#include <stdint.h>

#include "io_aplic.h"
#include "io_system.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IoMyVirtioBlk IoMyVirtioBlk;

typedef struct IoMyVirtioBlkConfig {
    const char *name;
    uint64_t base;
    uint64_t size;
    uint32_t irq;
    uint32_t requester_id;
    bool use_iommu;
    const char *image_path;
} IoMyVirtioBlkConfig;

IoMyVirtioBlk *io_my_virtio_blk_create(IoSystem *system,
                                       const IoMyVirtioBlkConfig *config,
                                       IoAplic *irq_parent);
void io_my_virtio_blk_destroy(IoMyVirtioBlk *blk);
IoSystemStatus io_my_virtio_blk_reset(IoMyVirtioBlk *blk);

#ifdef __cplusplus
}
#endif

#endif
