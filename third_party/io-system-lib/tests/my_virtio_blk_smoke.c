#include "io_aplic.h"
#include "io_my_virtio_blk.h"

#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TEST_APLIC_M_BASE       0x31100000ULL
#define TEST_APLIC_S_BASE       0x31120000ULL
#define TEST_BLK_BASE           0x310a0000ULL
#define TEST_BLK_IRQ            15u
#define TEST_MSI_ADDR           0x00400000ULL
#define TEST_MSI_EIID           0x55u

#define TEST_PAGE_SIZE          4096u
#define TEST_QUEUE_NUM          128u
#define TEST_QUEUE_BASE         0x00100000ULL
#define TEST_REQ_OUT_BASE       0x00200000ULL
#define TEST_REQ_IN_BASE        0x00201000ULL
#define TEST_IMAGE_SIZE         (4096u)
#define TEST_SECTOR_SIZE        512u

#define VIRTIO_MMIO_MAGIC_VALUE         0x000
#define VIRTIO_MMIO_VERSION             0x004
#define VIRTIO_MMIO_DEVICE_ID           0x008
#define VIRTIO_MMIO_HOST_FEATURES       0x010
#define VIRTIO_MMIO_GUEST_FEATURES      0x020
#define VIRTIO_MMIO_GUEST_FEATURES_SEL  0x024
#define VIRTIO_MMIO_GUEST_PAGE_SIZE     0x028
#define VIRTIO_MMIO_QUEUE_SEL           0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034
#define VIRTIO_MMIO_QUEUE_NUM           0x038
#define VIRTIO_MMIO_QUEUE_ALIGN         0x03c
#define VIRTIO_MMIO_QUEUE_PFN           0x040
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064
#define VIRTIO_MMIO_STATUS              0x070
#define VIRTIO_MMIO_CONFIG              0x100

#define VRING_DESC_F_NEXT       1u
#define VRING_DESC_F_WRITE      2u

#define VIRTIO_BLK_T_IN         0u
#define VIRTIO_BLK_T_OUT        1u
#define VIRTIO_BLK_S_OK         0u

typedef struct TestRange {
    uint64_t base;
    uint8_t *data;
    size_t size;
} TestRange;

typedef struct TestMemory {
    TestRange ranges[4];
    size_t range_count;
    uint64_t last_msi_addr;
    uint32_t last_msi_value;
    int msi_writes;
} TestMemory;

static uint64_t load_le(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint64_t value = 0;

    for (size_t i = 0; i < size && i < sizeof(value); i++) {
        value |= (uint64_t)bytes[i] << (8 * i);
    }

    return value;
}

static void store_le(void *data, uint64_t value, size_t size)
{
    uint8_t *bytes = data;

    for (size_t i = 0; i < size; i++) {
        bytes[i] = (uint8_t)(value >> (8 * i));
    }
}

static uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1u) & ~(align - 1u);
}

static bool find_range(TestMemory *mem, uint64_t addr, size_t len,
                       TestRange **range, size_t *offset)
{
    for (size_t i = 0; i < mem->range_count; i++) {
        TestRange *candidate = &mem->ranges[i];

        if (addr >= candidate->base &&
            addr - candidate->base <= candidate->size &&
            len <= candidate->size - (addr - candidate->base)) {
            *range = candidate;
            *offset = (size_t)(addr - candidate->base);
            return true;
        }
    }

    return false;
}

static int test_guest_memory_read(void *opaque, uint64_t gpa, void *dst,
                                  uint32_t len)
{
    TestMemory *mem = opaque;
    TestRange *range = NULL;
    size_t offset = 0;

    if (!find_range(mem, gpa, len, &range, &offset)) {
        return -1;
    }

    memcpy(dst, range->data + offset, len);
    return (int)len;
}

static int test_guest_memory_write(void *opaque, uint64_t gpa,
                                   const void *src, uint32_t len)
{
    TestMemory *mem = opaque;
    TestRange *range = NULL;
    size_t offset = 0;

    if (find_range(mem, gpa, len, &range, &offset)) {
        memcpy(range->data + offset, src, len);
        return (int)len;
    }

    if (gpa == TEST_MSI_ADDR && len == sizeof(uint32_t)) {
        mem->last_msi_addr = gpa;
        mem->last_msi_value = (uint32_t)load_le(src, len);
        mem->msi_writes++;
        return (int)len;
    }

    return -1;
}

static int test_guest_memory_atomic(void *opaque, uint64_t gpa, void *value,
                                    uint32_t len, uint32_t op)
{
    (void)opaque;
    (void)gpa;
    (void)value;
    (void)len;
    (void)op;

    return IO_SYSTEM_ERR_UNSUPPORTED;
}

static uint64_t test_clock(void *opaque)
{
    (void)opaque;
    return 0;
}

static void test_log(void *opaque, int level, const char *fmt, va_list ap)
{
    (void)opaque;
    (void)level;
    (void)fmt;
    (void)ap;
}

static void test_mem_write(TestMemory *mem, uint64_t addr, const void *src,
                           size_t len)
{
    assert(test_guest_memory_write(mem, addr, src, len) == (int)len);
}

static void test_mem_read(TestMemory *mem, uint64_t addr, void *dst,
                          size_t len)
{
    assert(test_guest_memory_read(mem, addr, dst, len) == (int)len);
}

static void test_mem_write_le(TestMemory *mem, uint64_t addr, uint64_t value,
                              size_t size)
{
    uint8_t bytes[8];

    assert(size <= sizeof(bytes));
    store_le(bytes, value, size);
    test_mem_write(mem, addr, bytes, size);
}

static uint64_t test_mem_read_le(TestMemory *mem, uint64_t addr, size_t size)
{
    uint8_t bytes[8];

    assert(size <= sizeof(bytes));
    test_mem_read(mem, addr, bytes, size);
    return load_le(bytes, size);
}

static void test_write_desc(TestMemory *mem, uint64_t desc_pa,
                            uint64_t addr, uint32_t len,
                            uint16_t flags, uint16_t next)
{
    uint8_t desc[16];

    store_le(desc, addr, 8);
    store_le(desc + 8, len, 4);
    store_le(desc + 12, flags, 2);
    store_le(desc + 14, next, 2);
    test_mem_write(mem, desc_pa, desc, sizeof(desc));
}

static void test_write_blk_header(TestMemory *mem, uint64_t addr,
                                  uint32_t type, uint64_t sector)
{
    uint8_t hdr[16];

    memset(hdr, 0, sizeof(hdr));
    store_le(hdr, type, 4);
    store_le(hdr + 8, sector, 8);
    test_mem_write(mem, addr, hdr, sizeof(hdr));
}

static void mmio_write32(IoSystem *system, uint64_t addr, uint32_t value)
{
    uint8_t bytes[4];

    store_le(bytes, value, sizeof(bytes));
    assert(io_system_q2io_write(system, addr, bytes, sizeof(bytes)) ==
           IO_SYSTEM_OK);
    assert(io_system_service(system, 0) == IO_SYSTEM_OK);
}

static uint32_t mmio_read32(IoSystem *system, uint64_t addr)
{
    uint8_t bytes[4];

    assert(io_system_q2io_read(system, addr, bytes, sizeof(bytes)) ==
           IO_SYSTEM_OK);
    assert(io_system_service(system, 0) == IO_SYSTEM_OK);
    return (uint32_t)load_le(bytes, sizeof(bytes));
}

static uint64_t mmio_read64(IoSystem *system, uint64_t addr)
{
    uint8_t bytes[8];

    assert(io_system_q2io_read(system, addr, bytes, sizeof(bytes)) ==
           IO_SYSTEM_OK);
    assert(io_system_service(system, 0) == IO_SYSTEM_OK);
    return load_le(bytes, sizeof(bytes));
}

static void configure_aplic(IoSystem *system)
{
    mmio_write32(system, TEST_APLIC_M_BASE + IO_APLIC_SMSICFGADDR,
                 TEST_MSI_ADDR >> 12);
    mmio_write32(system, TEST_APLIC_M_BASE + IO_APLIC_SMSICFGADDRH, 0);

    mmio_write32(system, TEST_APLIC_S_BASE + IO_APLIC_SOURCECFG_BASE +
                 (TEST_BLK_IRQ - 1) * 4, IO_APLIC_SOURCECFG_SM_EDGE_RISE);
    mmio_write32(system, TEST_APLIC_S_BASE + IO_APLIC_TARGET_BASE +
                 (TEST_BLK_IRQ - 1) * 4, TEST_MSI_EIID);
    mmio_write32(system, TEST_APLIC_S_BASE + IO_APLIC_DOMAINCFG,
                 IO_APLIC_DOMAINCFG_IE);
    mmio_write32(system, TEST_APLIC_S_BASE + IO_APLIC_SETIENUM,
                 TEST_BLK_IRQ);
}

static void configure_blk(IoSystem *system)
{
    uint32_t features;

    assert(mmio_read32(system, TEST_BLK_BASE + VIRTIO_MMIO_MAGIC_VALUE) ==
           0x74726976u);
    assert(mmio_read32(system, TEST_BLK_BASE + VIRTIO_MMIO_VERSION) == 1);
    assert(mmio_read32(system, TEST_BLK_BASE + VIRTIO_MMIO_DEVICE_ID) == 2);
    assert(mmio_read64(system, TEST_BLK_BASE + VIRTIO_MMIO_CONFIG) ==
           TEST_IMAGE_SIZE / TEST_SECTOR_SIZE);

    features = mmio_read32(system, TEST_BLK_BASE + VIRTIO_MMIO_HOST_FEATURES);
    mmio_write32(system, TEST_BLK_BASE + VIRTIO_MMIO_GUEST_FEATURES_SEL, 0);
    mmio_write32(system, TEST_BLK_BASE + VIRTIO_MMIO_GUEST_FEATURES,
                 features);

    mmio_write32(system, TEST_BLK_BASE + VIRTIO_MMIO_GUEST_PAGE_SIZE,
                 TEST_PAGE_SIZE);
    mmio_write32(system, TEST_BLK_BASE + VIRTIO_MMIO_QUEUE_SEL, 0);
    assert(mmio_read32(system, TEST_BLK_BASE + VIRTIO_MMIO_QUEUE_NUM_MAX) ==
           128);
    mmio_write32(system, TEST_BLK_BASE + VIRTIO_MMIO_QUEUE_NUM,
                 TEST_QUEUE_NUM);
    mmio_write32(system, TEST_BLK_BASE + VIRTIO_MMIO_QUEUE_ALIGN,
                 TEST_PAGE_SIZE);
    mmio_write32(system, TEST_BLK_BASE + VIRTIO_MMIO_QUEUE_PFN,
                 TEST_QUEUE_BASE / TEST_PAGE_SIZE);
    mmio_write32(system, TEST_BLK_BASE + VIRTIO_MMIO_STATUS, 0x0f);
}

static void submit_write_request(IoSystem *system, TestMemory *mem,
                                 uint64_t used_pa)
{
    const char payload[TEST_SECTOR_SIZE] = "io-system-blk-write";
    uint64_t avail_pa = TEST_QUEUE_BASE + TEST_QUEUE_NUM * 16u;
    uint64_t desc_pa = TEST_QUEUE_BASE;
    uint8_t status = 0xff;

    test_write_blk_header(mem, TEST_REQ_OUT_BASE, VIRTIO_BLK_T_OUT, 0);
    test_mem_write(mem, TEST_REQ_OUT_BASE + 16, payload, sizeof(payload));
    test_mem_write(mem, TEST_REQ_OUT_BASE + 16 + TEST_SECTOR_SIZE,
                   &status, sizeof(status));

    test_write_desc(mem, desc_pa + 0 * 16u, TEST_REQ_OUT_BASE, 16,
                    VRING_DESC_F_NEXT, 1);
    test_write_desc(mem, desc_pa + 1 * 16u, TEST_REQ_OUT_BASE + 16,
                    TEST_SECTOR_SIZE, VRING_DESC_F_NEXT, 2);
    test_write_desc(mem, desc_pa + 2 * 16u,
                    TEST_REQ_OUT_BASE + 16 + TEST_SECTOR_SIZE, 1,
                    VRING_DESC_F_WRITE, 0);
    test_mem_write_le(mem, avail_pa + 4, 0, 2);
    test_mem_write_le(mem, avail_pa + 2, 1, 2);

    mmio_write32(system, TEST_BLK_BASE + VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    assert(test_mem_read_le(mem, TEST_REQ_OUT_BASE + 16 + TEST_SECTOR_SIZE,
                            1) == VIRTIO_BLK_S_OK);
    assert(test_mem_read_le(mem, used_pa + 2, 2) == 1);
    assert(test_mem_read_le(mem, used_pa + 4, 4) == 0);
    assert(test_mem_read_le(mem, used_pa + 8, 4) == TEST_SECTOR_SIZE);
    assert(mmio_read32(system, TEST_BLK_BASE + VIRTIO_MMIO_INTERRUPT_STATUS) &
           1u);
    mmio_write32(system, TEST_BLK_BASE + VIRTIO_MMIO_INTERRUPT_ACK, 1);
}

static void submit_read_request(IoSystem *system, TestMemory *mem,
                                uint64_t used_pa)
{
    uint64_t avail_pa = TEST_QUEUE_BASE + TEST_QUEUE_NUM * 16u;
    uint64_t desc_pa = TEST_QUEUE_BASE;
    uint8_t data[TEST_SECTOR_SIZE];
    uint8_t status = 0xff;

    memset(data, 0, sizeof(data));
    test_write_blk_header(mem, TEST_REQ_IN_BASE, VIRTIO_BLK_T_IN, 0);
    test_mem_write(mem, TEST_REQ_IN_BASE + 16, data, sizeof(data));
    test_mem_write(mem, TEST_REQ_IN_BASE + 16 + TEST_SECTOR_SIZE,
                   &status, sizeof(status));

    test_write_desc(mem, desc_pa + 3 * 16u, TEST_REQ_IN_BASE, 16,
                    VRING_DESC_F_NEXT, 4);
    test_write_desc(mem, desc_pa + 4 * 16u, TEST_REQ_IN_BASE + 16,
                    TEST_SECTOR_SIZE,
                    VRING_DESC_F_NEXT | VRING_DESC_F_WRITE, 5);
    test_write_desc(mem, desc_pa + 5 * 16u,
                    TEST_REQ_IN_BASE + 16 + TEST_SECTOR_SIZE, 1,
                    VRING_DESC_F_WRITE, 0);
    test_mem_write_le(mem, avail_pa + 4 + 2, 3, 2);
    test_mem_write_le(mem, avail_pa + 2, 2, 2);
    test_mem_write_le(mem, avail_pa + 4 + TEST_QUEUE_NUM * 2, 1, 2);

    mmio_write32(system, TEST_BLK_BASE + VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    test_mem_read(mem, TEST_REQ_IN_BASE + 16, data, sizeof(data));
    assert(!memcmp(data, "io-system-blk-write", strlen("io-system-blk-write")));
    assert(test_mem_read_le(mem, TEST_REQ_IN_BASE + 16 + TEST_SECTOR_SIZE,
                            1) == VIRTIO_BLK_S_OK);
    assert(test_mem_read_le(mem, used_pa + 2, 2) == 2);
    assert(test_mem_read_le(mem, used_pa + 4 + 8, 4) == 3);
    assert(test_mem_read_le(mem, used_pa + 4 + 8 + 4, 4) ==
           TEST_SECTOR_SIZE);
}

int main(void)
{
    uint8_t queue_mem[0x2000] = { 0 };
    uint8_t req_out_mem[0x1000] = { 0 };
    uint8_t req_in_mem[0x1000] = { 0 };
    char image_path[] = "/tmp/io-system-blk.XXXXXX";
    TestMemory mem = {
        .ranges = {
            { TEST_QUEUE_BASE, queue_mem, sizeof(queue_mem) },
            { TEST_REQ_OUT_BASE, req_out_mem, sizeof(req_out_mem) },
            { TEST_REQ_IN_BASE, req_in_mem, sizeof(req_in_mem) },
        },
        .range_count = 3,
    };
    IoSystemHostOps host_ops = {
        .opaque = &mem,
        .guest_memory_read = test_guest_memory_read,
        .guest_memory_write = test_guest_memory_write,
        .guest_memory_atomic = test_guest_memory_atomic,
        .clock = test_clock,
        .log = test_log,
    };
    IoSystem *system;
    IoAplic *aplic_m;
    IoAplic *aplic_s;
    IoMyVirtioBlk *blk;
    uint64_t used_pa = align_up(TEST_QUEUE_BASE + TEST_QUEUE_NUM * 16u + 4u +
                                TEST_QUEUE_NUM * sizeof(uint16_t),
                                TEST_PAGE_SIZE);
    int fd;
    uint8_t image_data[TEST_SECTOR_SIZE];
    bool saw_msi_trace = false;
    bool saw_dma_read = false;
    bool saw_dma_write = false;

    fd = mkstemp(image_path);
    assert(fd >= 0);
    assert(ftruncate(fd, TEST_IMAGE_SIZE) == 0);

    system = io_system_create(NULL, &host_ops);
    assert(system);

    aplic_m = io_aplic_create(system, &(IoAplicConfig) {
        .name = "aplic-m",
        .base = TEST_APLIC_M_BASE,
        .size = 0x4000,
        .num_sources = 96,
        .num_harts = 1,
        .iprio_bits = 8,
        .msimode = true,
        .mmode = true,
    }, NULL);
    assert(aplic_m);

    aplic_s = io_aplic_create(system, &(IoAplicConfig) {
        .name = "aplic-s",
        .base = TEST_APLIC_S_BASE,
        .size = 0x4000,
        .num_sources = 96,
        .num_harts = 1,
        .iprio_bits = 8,
        .msimode = true,
        .mmode = false,
    }, aplic_m);
    assert(aplic_s);

    blk = io_my_virtio_blk_create(system, &(IoMyVirtioBlkConfig) {
        .name = "my-virtio-blk",
        .base = TEST_BLK_BASE,
        .size = 0x1000,
        .irq = TEST_BLK_IRQ,
        .image_path = image_path,
    }, aplic_s);
    assert(blk);

    configure_aplic(system);
    configure_blk(system);
    submit_write_request(system, &mem, used_pa);

    memset(image_data, 0, sizeof(image_data));
    assert(pread(fd, image_data, sizeof(image_data), 0) ==
           (ssize_t)sizeof(image_data));
    assert(!memcmp(image_data, "io-system-blk-write",
                   strlen("io-system-blk-write")));
    assert(mem.msi_writes == 1);
    assert(mem.last_msi_addr == TEST_MSI_ADDR);
    assert(mem.last_msi_value == TEST_MSI_EIID);

    submit_read_request(system, &mem, used_pa);
    assert(mem.msi_writes == 2);

    for (size_t i = 0; i < io_system_trace_count(system); i++) {
        const IoAxiBeatTrace *trace = io_system_trace_at(system, i);

        if (!trace || strcmp(trace->port, "io2q")) {
            continue;
        }
        if (!strcmp(trace->channel, "memory-write") &&
            trace->address == TEST_MSI_ADDR &&
            trace->data[0] == TEST_MSI_EIID) {
            saw_msi_trace = true;
        }
        if (!strcmp(trace->channel, "memory-read") &&
            trace->address == TEST_REQ_OUT_BASE + 16) {
            saw_dma_read = true;
        }
        if (!strcmp(trace->channel, "memory-write") &&
            trace->address == TEST_REQ_IN_BASE + 16) {
            saw_dma_write = true;
        }
    }
    assert(saw_msi_trace);
    assert(saw_dma_read);
    assert(saw_dma_write);

    io_my_virtio_blk_destroy(blk);
    io_aplic_destroy(aplic_s);
    io_aplic_destroy(aplic_m);
    io_system_destroy(system);
    close(fd);
    unlink(image_path);
    return 0;
}
