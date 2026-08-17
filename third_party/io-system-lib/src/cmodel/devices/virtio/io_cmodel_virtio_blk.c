#include "io_my_virtio_blk.h"
#include "io_cmodel_internal.h"

#include "virtio_backend.h"
#include "virtio_wrapper.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IO_VIRTIO_MMIO_CONFIG 0x100
#define IO_VIRTIO_MMIO_QUEUE_SEL 0x30
#define IO_VIRTIO_MMIO_QUEUE_NOTIFY 0x50
#define IO_VIRTIO_MMIO_STATUS 0x70
#define IO_VIRTIO_MMIO_GUEST_FEATURES_SEL 0x24
#define IO_VIRTIO_MMIO_GUEST_FEATURES 0x20
#define IO_VIRTIO_MMIO_GUEST_PAGE_SIZE 0x28
#define IO_VIRTIO_MMIO_QUEUE_NUM 0x38
#define IO_VIRTIO_MMIO_QUEUE_ALIGN 0x3c
#define IO_VIRTIO_MMIO_QUEUE_PFN 0x40
#define IO_VIRTIO_MMIO_INTERRUPT_ACK 0x64
#define IO_VIRTIO_BLK_F_SEG_MAX 2
#define IO_VIRTIO_BLK_F_BLK_SIZE 6
#define IO_VIRTIO_BLK_F_FLUSH 9
#define IO_VIRTIO_BLK_SECTOR_SIZE 512ULL
#define IO_VIRTIO_BLK_SEG_MAX 126U

struct IoMyVirtioBlk {
    IoSystem *system;
    IoAplic *irq_parent;
    IoMyVirtioBlkConfig config;
    virtio_backend_handle_t backend;
    virtio_handle_t virtio;
    uint32_t shadow[0x204 / 4];
    uint32_t queue_sel;
    uint32_t guest_features_sel;
    uint32_t reset_seq;
    bool dirty;
};

static uint64_t cmodel_virtio_alloc64(int size)
{
    return (uint64_t)(uintptr_t)calloc(1, (size_t)size);
}

static void cmodel_virtio_free(uint64_t addr, int size)
{
    (void)size;
    free((void *)(uintptr_t)addr);
}

static int cmodel_virtio_vprint(const char *fmt, va_list ap)
{
    return vfprintf(stderr, fmt, ap);
}

static int cmodel_virtio_map(uint64_t gphys_addr, uint64_t gphys_size,
                             uint64_t *hphys_addr, uint64_t *hphys_size,
                             void *priv)
{
    (void)priv;
    if (!hphys_addr || !hphys_size || !gphys_size) {
        return -1;
    }
    *hphys_addr = gphys_addr;
    *hphys_size = gphys_size;
    return 0;
}

static int cmodel_virtio_guest_read(uint64_t gpa, void *dst, uint32_t len,
                                    void *priv)
{
    IoMyVirtioBlk *blk = priv;
    IoSystemDmaAttrs attrs;

    if (!blk) {
        return -1;
    }
    if (!blk->config.use_iommu) {
        return io_system_guest_memory_read(blk->system, gpa, dst, len) ==
               IO_SYSTEM_OK ? (int)len : -1;
    }
    attrs = (IoSystemDmaAttrs) {
        .requester_id = blk->config.requester_id,
    };
    return io_system_dma_read(blk->system, &attrs, gpa, dst, len) ==
           IO_SYSTEM_OK ? (int)len : -1;
}

static int cmodel_virtio_guest_write(uint64_t gpa, void *src, uint32_t len,
                                     void *priv)
{
    IoMyVirtioBlk *blk = priv;
    IoSystemDmaAttrs attrs;

    if (!blk) {
        return -1;
    }
    if (!blk->config.use_iommu) {
        return io_system_guest_memory_write(blk->system, gpa, src, len) ==
               IO_SYSTEM_OK ? (int)len : -1;
    }
    attrs = (IoSystemDmaAttrs) {
        .requester_id = blk->config.requester_id,
    };
    return io_system_dma_write(blk->system, &attrs, gpa, src, len) ==
           IO_SYSTEM_OK ? (int)len : -1;
}

static int cmodel_virtio_set_irq(void *priv)
{
    IoMyVirtioBlk *blk = priv;

    if (!blk || !blk->irq_parent) {
        return 0;
    }
    io_aplic_irq_line_set(blk->irq_parent, blk->config.irq, true);
    io_aplic_irq_line_set(blk->irq_parent, blk->config.irq, false);
    return 0;
}

static int cmodel_virtio_submit_blk_io(uint64_t sector, void *buf, int len,
                                       uint8_t flags, void *priv)
{
    IoMyVirtioBlk *blk = priv;
    struct virtio_backend_io io = {
        .type = VIRTIO_BACKEND_IO_BLK,
        .buf = buf,
        .len = len < 0 ? 0 : (size_t)len,
        .u.blk.sector = sector,
    };

    if (!blk || !blk->backend || len < 0) {
        return -1;
    }
    if (flags == MY_BLK_REQ_READ) {
        io.u.blk.op = VIRTIO_BACKEND_BLK_READ;
        return virtio_backend_read(blk->backend, &io);
    }
    io.u.blk.op = flags == MY_BLK_REQ_WRITE ? VIRTIO_BACKEND_BLK_WRITE :
                  flags == MY_BLK_REQ_FLUSH ? VIRTIO_BACKEND_BLK_FLUSH :
                  VIRTIO_BACKEND_BLK_READ;
    if (flags != MY_BLK_REQ_WRITE && flags != MY_BLK_REQ_FLUSH) {
        return -1;
    }
    return virtio_backend_write(blk->backend, &io);
}

static int cmodel_virtio_get_blk_capacity(void *priv)
{
    IoMyVirtioBlk *blk = priv;
    struct virtio_backend_info info;

    if (!blk || !blk->backend || virtio_backend_get_info(blk->backend, &info) < 0) {
        return -1;
    }
    return info.type == VIRTIO_BACKEND_BLK ? (int)info.u.blk.capacity : -1;
}

static struct libvirtio_ops cmodel_virtio_ops = {
    .vprint = cmodel_virtio_vprint,
    .mm_alloc = cmodel_virtio_alloc64,
    .mm_free = cmodel_virtio_free,
    .map = cmodel_virtio_map,
    .guest_mem_read = cmodel_virtio_guest_read,
    .guest_mem_write = cmodel_virtio_guest_write,
    .set_irq = cmodel_virtio_set_irq,
    .blk_ops = {
        .submit_blk_io = cmodel_virtio_submit_blk_io,
        .get_blk_capacity = cmodel_virtio_get_blk_capacity,
    },
};

static int cmodel_virtio_gbus_read(void *opaque, uint32_t addr,
                                   uint32_t *value)
{
    IoMyVirtioBlk *blk = opaque;

    if (!blk || (addr & 3) || addr >= sizeof(blk->shadow)) {
        return -1;
    }
    *value = blk->shadow[addr / 4];
    return 0;
}

static int cmodel_virtio_gbus_write(void *opaque, uint32_t addr,
                                    uint32_t value)
{
    IoMyVirtioBlk *blk = opaque;

    if (!blk || !value || (addr & 3) || addr >= sizeof(blk->shadow)) {
        if (addr != VIRTIO_GBUS_CSR_HOST_IRQ_SET) {
            return -1;
        }
    }
    if (addr == VIRTIO_GBUS_CSR_HOST_IRQ_SET) {
        return cmodel_virtio_set_irq(blk);
    }
    blk->shadow[addr / 4] = value;
    return 0;
}

static const struct virtio_gbus_ops cmodel_virtio_gbus_ops = {
    .read = cmodel_virtio_gbus_read,
    .write = cmodel_virtio_gbus_write,
};

static void cmodel_virtio_publish(IoMyVirtioBlk *blk, uint32_t addr,
                                  uint32_t value)
{
    uint32_t queue_addr;

    if (!blk || addr >= IO_VIRTIO_MMIO_CONFIG) {
        return;
    }
    switch (addr) {
    case IO_VIRTIO_MMIO_STATUS:
        if (!value) {
            blk->reset_seq++;
            blk->shadow[VIRTIO_GBUS_CSR_RESET_SEQ / 4] = blk->reset_seq;
        }
        blk->shadow[VIRTIO_GBUS_CSR_STATUS / 4] = value;
        break;
    case IO_VIRTIO_MMIO_GUEST_FEATURES_SEL:
        blk->guest_features_sel = value < 2 ? value : 0;
        break;
    case IO_VIRTIO_MMIO_GUEST_FEATURES:
        if (blk->guest_features_sel == 0) {
            blk->shadow[VIRTIO_GBUS_CSR_DRIVER_FEATURES_0 / 4] = value;
        } else {
            blk->shadow[VIRTIO_GBUS_CSR_DRIVER_FEATURES_1 / 4] = value;
        }
        break;
    case IO_VIRTIO_MMIO_GUEST_PAGE_SIZE:
        blk->shadow[VIRTIO_GBUS_CSR_GUEST_PAGE_SIZE / 4] = value;
        break;
    case IO_VIRTIO_MMIO_QUEUE_SEL:
        blk->queue_sel = value;
        break;
    case IO_VIRTIO_MMIO_QUEUE_NUM:
    case IO_VIRTIO_MMIO_QUEUE_ALIGN:
    case IO_VIRTIO_MMIO_QUEUE_PFN:
        queue_addr = VIRTIO_GBUS_CSR_QUEUE_BASE +
                     blk->queue_sel * VIRTIO_GBUS_CSR_QUEUE_STRIDE;
        blk->shadow[(queue_addr + addr - IO_VIRTIO_MMIO_QUEUE_NUM) / 4] =
            value;
        break;
    case IO_VIRTIO_MMIO_QUEUE_NOTIFY:
        queue_addr = VIRTIO_GBUS_CSR_QUEUE_BASE +
                     value * VIRTIO_GBUS_CSR_QUEUE_STRIDE +
                     VIRTIO_GBUS_CSR_QUEUE_NOTIFY_SEQ;
        blk->shadow[queue_addr / 4]++;
        break;
    default:
        return;
    }
    blk->shadow[VIRTIO_GBUS_CSR_UPDATE_SEQ / 4]++;
    blk->dirty = true;
}

static IoSystemStatus cmodel_virtio_read(void *opaque, uint64_t addr,
                                         void *data, size_t size)
{
    IoMyVirtioBlk *blk = opaque;
    size_t done = 0;

    if (!blk || !data || !size || size > 8 || addr < blk->config.base ||
        addr + size > blk->config.base + blk->config.size) {
        return IO_SYSTEM_ERR_INVALID;
    }
    while (done < size) {
        uint32_t raw = 0;
        uint64_t current = addr + done;
        uint64_t aligned = current & ~UINT64_C(3);
        size_t lane = (size_t)(current - aligned);
        size_t chunk = 4 - lane;

        if (chunk > size - done) {
            chunk = size - done;
        }
        if (virtio_mmio_read(blk->virtio, aligned, &raw, 4) < 0) {
            memset(data, 0xff, size);
            return IO_SYSTEM_ERR_UNMAPPED;
        }
        memcpy((uint8_t *)data + done, ((uint8_t *)&raw) + lane, chunk);
        done += chunk;
    }
    return IO_SYSTEM_OK;
}

static IoSystemStatus cmodel_virtio_write(void *opaque, uint64_t addr,
                                          const void *data, size_t size)
{
    IoMyVirtioBlk *blk = opaque;
    size_t done = 0;

    if (!blk || !data || !size || size > 8 || addr < blk->config.base ||
        addr + size > blk->config.base + blk->config.size) {
        return IO_SYSTEM_ERR_INVALID;
    }
    while (done < size) {
        uint32_t raw = 0;
        uint64_t current = addr + done;
        uint64_t aligned = current & ~UINT64_C(3);
        size_t lane = (size_t)(current - aligned);
        size_t chunk = 4 - lane;
        int doorbell = 0;
        uint32_t offset = (uint32_t)(aligned - blk->config.base);
        bool shadowed;

        if (chunk > size - done) {
            chunk = size - done;
        }
        if ((lane || chunk != 4) &&
            virtio_mmio_read(blk->virtio, aligned, &raw, 4) < 0) {
            return IO_SYSTEM_ERR_UNMAPPED;
        }
        memcpy(((uint8_t *)&raw) + lane, (const uint8_t *)data + done,
               chunk);
        shadowed = offset == IO_VIRTIO_MMIO_GUEST_FEATURES_SEL ||
                   offset == IO_VIRTIO_MMIO_GUEST_FEATURES ||
                   offset == IO_VIRTIO_MMIO_GUEST_PAGE_SIZE ||
                   offset == IO_VIRTIO_MMIO_QUEUE_SEL ||
                   offset == IO_VIRTIO_MMIO_QUEUE_NUM ||
                   offset == IO_VIRTIO_MMIO_QUEUE_ALIGN ||
                   offset == IO_VIRTIO_MMIO_QUEUE_PFN ||
                   offset == IO_VIRTIO_MMIO_QUEUE_NOTIFY ||
                   offset == IO_VIRTIO_MMIO_STATUS;
        if (!shadowed && virtio_mmio_write(blk->virtio, aligned, raw, 4,
                                           &doorbell) < 0) {
            return IO_SYSTEM_ERR_UNMAPPED;
        }
        cmodel_virtio_publish(blk, offset, raw);
        done += chunk;
    }
    return IO_SYSTEM_OK;
}

static IoSystemStatus cmodel_virtio_service(void *opaque, uint32_t budget)
{
    IoMyVirtioBlk *blk = opaque;
    int doorbell = 0;

    (void)budget;
    if (!blk || !blk->virtio) {
        return IO_SYSTEM_OK;
    }
    if (virtio_gbus_poll(blk->virtio) < 0) {
        return IO_SYSTEM_ERR_IO;
    }
    if (virtio_mmio_write(blk->virtio,
                          blk->config.base + IO_VIRTIO_MMIO_QUEUE_SEL,
                          blk->queue_sel, 4, &doorbell) < 0) {
        return IO_SYSTEM_ERR_IO;
    }
    blk->dirty = false;
    return IO_SYSTEM_OK;
}

static bool cmodel_virtio_needs_service(void *opaque)
{
    IoMyVirtioBlk *blk = opaque;

    return blk && blk->dirty;
}

IoMyVirtioBlk *io_my_virtio_blk_create(IoSystem *system,
                                       const IoMyVirtioBlkConfig *config,
                                       IoAplic *irq_parent)
{
    IoMyVirtioBlk *blk;
    struct virtio_backend_config backend_config = {
        .type = VIRTIO_BACKEND_BLK,
    };
    struct virtio_backend_info info;

    if (!system || !config || !config->base || !config->size ||
        !config->image_path || !*config->image_path) {
        return NULL;
    }
    blk = calloc(1, sizeof(*blk));
    if (!blk) {
        return NULL;
    }
    blk->system = system;
    blk->irq_parent = irq_parent;
    blk->config = *config;
    backend_config.u.blk.image_path = config->image_path;
    blk->backend = virtio_backend_create(&backend_config);
    if (!blk->backend || virtio_backend_get_info(blk->backend, &info) < 0) {
        io_my_virtio_blk_destroy(blk);
        return NULL;
    }
    blk->shadow[VIRTIO_GBUS_CSR_MAGIC / 4] = VIRTIO_GBUS_MAGIC;
    blk->shadow[VIRTIO_GBUS_CSR_VERSION / 4] = VIRTIO_GBUS_VERSION;
    blk->shadow[VIRTIO_GBUS_CSR_BLK_SEG_MAX / 4] = IO_VIRTIO_BLK_SEG_MAX;
    blk->shadow[VIRTIO_GBUS_CSR_BLK_SIZE / 4] = IO_VIRTIO_BLK_SECTOR_SIZE;
    blk->shadow[VIRTIO_GBUS_CSR_BLK_CAPACITY_LOW / 4] =
        (uint32_t)info.u.blk.capacity;
    blk->shadow[VIRTIO_GBUS_CSR_BLK_CAPACITY_HIGH / 4] =
        (uint32_t)(info.u.blk.capacity >> 32);
    blk->virtio = virtio_gbus_create(VIRTIO_EMU_NAME_BLK,
                                     config->base, config->size,
                                     &cmodel_virtio_ops, blk,
                                     &cmodel_virtio_gbus_ops, blk);
    if (!blk->virtio || io_cmodel_register_mmio_window(
            system, config->name ? config->name : "my-virtio-blk",
            config->base, config->size, cmodel_virtio_read,
            cmodel_virtio_write, blk) != IO_SYSTEM_OK ||
        io_cmodel_register_service(system, cmodel_virtio_service,
                                    cmodel_virtio_needs_service, blk) !=
            IO_SYSTEM_OK) {
        io_my_virtio_blk_destroy(blk);
        return NULL;
    }
    blk->dirty = true;
    return blk;
}

void io_my_virtio_blk_destroy(IoMyVirtioBlk *blk)
{
    if (!blk) {
        return;
    }
    if (blk->backend) {
        virtio_backend_destroy(blk->backend);
    }
    free(blk);
}

IoSystemStatus io_my_virtio_blk_reset(IoMyVirtioBlk *blk)
{
    if (!blk) {
        return IO_SYSTEM_ERR_INVALID;
    }
    blk->reset_seq++;
    blk->shadow[VIRTIO_GBUS_CSR_RESET_SEQ / 4] = blk->reset_seq;
    blk->shadow[VIRTIO_GBUS_CSR_STATUS / 4] = 0;
    blk->shadow[VIRTIO_GBUS_CSR_UPDATE_SEQ / 4]++;
    blk->dirty = true;
    return IO_SYSTEM_OK;
}
