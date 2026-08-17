#include "io_system.h"

#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define IO_RTL_SYSTEM_DMAC_BASE      0x30040000ULL
#define IO_RTL_SYSTEM_DMAC_REG_ID    0x000
#define IO_RTL_SYSTEM_DMAC_REG_CTRL  0x008
#define IO_RTL_SYSTEM_DMAC_REG_STAT  0x00c
#define IO_RTL_SYSTEM_DMAC_REG_SRC_LO 0x010
#define IO_RTL_SYSTEM_DMAC_REG_SRC_HI 0x014
#define IO_RTL_SYSTEM_DMAC_REG_DST_LO 0x018
#define IO_RTL_SYSTEM_DMAC_REG_DST_HI 0x01c
#define IO_RTL_SYSTEM_DMAC_REG_SIZE  0x020

#define IO_RTL_SYSTEM_DMAC_ID        0x4d445452u
#define IO_RTL_SYSTEM_DMAC_CTRL_START (1u << 0)
#define IO_RTL_SYSTEM_DMAC_CTRL_CLEAR (1u << 1)
#define IO_RTL_SYSTEM_DMAC_STAT_BUSY  (1u << 0)
#define IO_RTL_SYSTEM_DMAC_STAT_DONE  (1u << 1)
#define IO_RTL_SYSTEM_DMAC_STAT_ERROR (1u << 2)

#define IO_RTL_SYSTEM_APLIC_M_BASE    0x31100000ULL
#define IO_RTL_SYSTEM_APLIC_S_BASE    0x31120000ULL
#define IO_RTL_SYSTEM_BLK_BASE        0x310a0000ULL
#define IO_RTL_SYSTEM_BLK_IRQ         15u
#define IO_RTL_SYSTEM_BLK_EIID        0x55u

#define APLIC_DOMAINCFG               0x0000
#define APLIC_SOURCECFG_BASE          0x0004
#define APLIC_SMSICFGADDR             0x1bc8
#define APLIC_SMSICFGADDRH            0x1bcc
#define APLIC_SETIP                   0x1c00
#define APLIC_SETIPNUM                0x1cdc
#define APLIC_SETIE                   0x1e00
#define APLIC_SETIENUM                0x1edc
#define APLIC_TARGET_BASE             0x3004
#define APLIC_DOMAINCFG_IE            (1u << 8)
#define APLIC_DOMAINCFG_DM            (1u << 2)
#define APLIC_SOURCECFG_DELEGATED     (1u << 10)
#define APLIC_SOURCECFG_LEVEL_HIGH    0x6u

#define VIRTIO_MMIO_MAGIC_VALUE       0x000
#define VIRTIO_MMIO_VERSION           0x004
#define VIRTIO_MMIO_DEVICE_ID         0x008
#define VIRTIO_MMIO_HOST_FEATURES     0x010
#define VIRTIO_MMIO_GUEST_FEATURES    0x020
#define VIRTIO_MMIO_GUEST_FEATURES_SEL 0x024
#define VIRTIO_MMIO_GUEST_PAGE_SIZE   0x028
#define VIRTIO_MMIO_QUEUE_SEL         0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX     0x034
#define VIRTIO_MMIO_QUEUE_NUM         0x038
#define VIRTIO_MMIO_QUEUE_ALIGN       0x03c
#define VIRTIO_MMIO_QUEUE_PFN         0x040
#define VIRTIO_MMIO_QUEUE_NOTIFY      0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS  0x060
#define VIRTIO_MMIO_STATUS            0x070
#define VIRTIO_MMIO_CONFIG            0x100

#define VIRTIO_BLK_T_OUT              1u
#define VIRTIO_BLK_S_OK               0u
#define VRING_DESC_F_NEXT             1u
#define VRING_DESC_F_WRITE            2u

#define TEST_PAGE_SIZE 4096u
#define TEST_QUEUE_NUM 128u
#define TEST_QUEUE_BASE (TEST_MEM_BASE + 0x1000)
#define TEST_USED_BASE  (TEST_MEM_BASE + 0x2000)
#define TEST_REQ_BASE   (TEST_MEM_BASE + 0x4000)
#define TEST_MSI_ADDR   (TEST_MEM_BASE + 0xf000)
#define TEST_IMAGE_SIZE 4096u

#define TEST_MEM_BASE 0x80000000ULL
#define TEST_MEM_SIZE 0x10000

typedef struct TestHost {
    uint8_t mem[TEST_MEM_SIZE];
} TestHost;

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

static int host_guest_read(void *opaque, uint64_t gpa, void *dst,
                           uint32_t len)
{
    TestHost *host = opaque;
    uint64_t off;

    if (gpa < TEST_MEM_BASE) {
        return -1;
    }
    off = gpa - TEST_MEM_BASE;
    if (off + len > TEST_MEM_SIZE) {
        return -1;
    }
    memcpy(dst, host->mem + off, len);
    return (int)len;
}

static int host_guest_write(void *opaque, uint64_t gpa, const void *src,
                            uint32_t len)
{
    TestHost *host = opaque;
    uint64_t off;

    if (gpa < TEST_MEM_BASE) {
        return -1;
    }
    off = gpa - TEST_MEM_BASE;
    if (off + len > TEST_MEM_SIZE) {
        return -1;
    }
    memcpy(host->mem + off, src, len);
    return (int)len;
}

static void host_log(void *opaque, int level, const char *fmt, va_list ap)
{
    (void)opaque;
    (void)level;

    fputs("rtl_system_smoke: ", stderr);
    vfprintf(stderr, fmt, ap);
}

static uint32_t q2io_read32(IoSystem *system, uint64_t addr)
{
    uint8_t data[4];

    assert(io_system_q2io_read(system, addr, data, sizeof(data)) ==
           IO_SYSTEM_OK);
    return (uint32_t)load_le(data, sizeof(data));
}

static void q2io_write32(IoSystem *system, uint64_t addr, uint32_t value)
{
    uint8_t data[4];

    store_le(data, value, sizeof(data));
    assert(io_system_q2io_write(system, addr, data, sizeof(data)) ==
           IO_SYSTEM_OK);
}

static void write_addr64(IoSystem *system, uint64_t lo_reg, uint64_t value)
{
    q2io_write32(system, lo_reg, (uint32_t)value);
    q2io_write32(system, lo_reg + 4, (uint32_t)(value >> 32));
}

static void test_mem_write(TestHost *host, uint64_t addr, uint64_t value,
                           size_t size)
{
    assert(addr >= TEST_MEM_BASE);
    assert(addr - TEST_MEM_BASE + size <= TEST_MEM_SIZE);
    store_le(host->mem + (addr - TEST_MEM_BASE), value, size);
}

static uint64_t test_mem_read(TestHost *host, uint64_t addr, size_t size)
{
    assert(addr >= TEST_MEM_BASE);
    assert(addr - TEST_MEM_BASE + size <= TEST_MEM_SIZE);
    return load_le(host->mem + (addr - TEST_MEM_BASE), size);
}

static void test_write_desc(TestHost *host, unsigned int index,
                            uint64_t addr, uint32_t len,
                            uint16_t flags, uint16_t next)
{
    uint64_t desc = TEST_QUEUE_BASE + index * 16u;

    test_mem_write(host, desc, addr, 8);
    test_mem_write(host, desc + 8, len, 4);
    test_mem_write(host, desc + 12, flags, 2);
    test_mem_write(host, desc + 14, next, 2);
}

static void run_virtio_blk_smoke(IoSystem *system, TestHost *host)
{
    uint64_t avail = TEST_QUEUE_BASE + TEST_QUEUE_NUM * 16u;
    uint32_t features;

    assert(q2io_read32(system, IO_RTL_SYSTEM_BLK_BASE +
                       VIRTIO_MMIO_MAGIC_VALUE) == 0x74726976u);
    assert(q2io_read32(system, IO_RTL_SYSTEM_BLK_BASE +
                       VIRTIO_MMIO_VERSION) == 1u);
    assert(q2io_read32(system, IO_RTL_SYSTEM_BLK_BASE +
                       VIRTIO_MMIO_DEVICE_ID) == 2u);
    assert(q2io_read32(system, IO_RTL_SYSTEM_BLK_BASE +
                       VIRTIO_MMIO_CONFIG) == TEST_IMAGE_SIZE / 512u);

    q2io_write32(system, IO_RTL_SYSTEM_APLIC_M_BASE +
                 APLIC_SMSICFGADDR, (uint32_t)(TEST_MSI_ADDR >> 12));
    q2io_write32(system, IO_RTL_SYSTEM_APLIC_M_BASE +
                 APLIC_SMSICFGADDRH, 0);
    q2io_write32(system, IO_RTL_SYSTEM_APLIC_M_BASE + APLIC_DOMAINCFG,
                 APLIC_DOMAINCFG_IE | APLIC_DOMAINCFG_DM);
    q2io_write32(system, IO_RTL_SYSTEM_APLIC_M_BASE +
                 APLIC_SOURCECFG_BASE + (IO_RTL_SYSTEM_BLK_IRQ - 1u) * 4u,
                 APLIC_SOURCECFG_DELEGATED);
    q2io_write32(system, IO_RTL_SYSTEM_APLIC_S_BASE + APLIC_DOMAINCFG,
                 APLIC_DOMAINCFG_IE | APLIC_DOMAINCFG_DM);
    q2io_write32(system, IO_RTL_SYSTEM_APLIC_S_BASE +
                 APLIC_SOURCECFG_BASE + (IO_RTL_SYSTEM_BLK_IRQ - 1u) * 4u,
                 APLIC_SOURCECFG_LEVEL_HIGH);
    q2io_write32(system, IO_RTL_SYSTEM_APLIC_S_BASE +
                 APLIC_TARGET_BASE + (IO_RTL_SYSTEM_BLK_IRQ - 1u) * 4u,
                 IO_RTL_SYSTEM_BLK_EIID);
    q2io_write32(system, IO_RTL_SYSTEM_APLIC_S_BASE + APLIC_SETIENUM,
                 IO_RTL_SYSTEM_BLK_IRQ);
    assert((q2io_read32(system, IO_RTL_SYSTEM_APLIC_S_BASE +
                        APLIC_DOMAINCFG) &
            (APLIC_DOMAINCFG_IE | APLIC_DOMAINCFG_DM)) ==
           (APLIC_DOMAINCFG_IE | APLIC_DOMAINCFG_DM));
    assert(q2io_read32(system, IO_RTL_SYSTEM_APLIC_M_BASE +
                       APLIC_SOURCECFG_BASE +
                       (IO_RTL_SYSTEM_BLK_IRQ - 1u) * 4u) ==
           APLIC_SOURCECFG_DELEGATED);
    assert(q2io_read32(system, IO_RTL_SYSTEM_APLIC_S_BASE +
                       APLIC_SOURCECFG_BASE +
                       (IO_RTL_SYSTEM_BLK_IRQ - 1u) * 4u) ==
           APLIC_SOURCECFG_LEVEL_HIGH);
    assert(q2io_read32(system, IO_RTL_SYSTEM_APLIC_S_BASE +
                       APLIC_TARGET_BASE +
                       (IO_RTL_SYSTEM_BLK_IRQ - 1u) * 4u) ==
           IO_RTL_SYSTEM_BLK_EIID);
    assert(q2io_read32(system, IO_RTL_SYSTEM_APLIC_S_BASE + APLIC_SETIE) &
           (1u << IO_RTL_SYSTEM_BLK_IRQ));

    features = q2io_read32(system, IO_RTL_SYSTEM_BLK_BASE +
                           VIRTIO_MMIO_HOST_FEATURES);
    q2io_write32(system, IO_RTL_SYSTEM_BLK_BASE +
                 VIRTIO_MMIO_GUEST_FEATURES_SEL, 0);
    q2io_write32(system, IO_RTL_SYSTEM_BLK_BASE +
                 VIRTIO_MMIO_GUEST_FEATURES, features);
    q2io_write32(system, IO_RTL_SYSTEM_BLK_BASE +
                 VIRTIO_MMIO_GUEST_PAGE_SIZE, TEST_PAGE_SIZE);
    q2io_write32(system, IO_RTL_SYSTEM_BLK_BASE + VIRTIO_MMIO_QUEUE_SEL, 0);
    assert(q2io_read32(system, IO_RTL_SYSTEM_BLK_BASE +
                       VIRTIO_MMIO_QUEUE_NUM_MAX) == 128u);
    q2io_write32(system, IO_RTL_SYSTEM_BLK_BASE + VIRTIO_MMIO_QUEUE_NUM,
                 TEST_QUEUE_NUM);
    q2io_write32(system, IO_RTL_SYSTEM_BLK_BASE + VIRTIO_MMIO_QUEUE_ALIGN,
                 TEST_PAGE_SIZE);
    q2io_write32(system, IO_RTL_SYSTEM_BLK_BASE + VIRTIO_MMIO_QUEUE_PFN,
                 TEST_QUEUE_BASE / TEST_PAGE_SIZE);
    q2io_write32(system, IO_RTL_SYSTEM_BLK_BASE + VIRTIO_MMIO_STATUS, 0x0f);

    memset(host->mem + (TEST_QUEUE_BASE - TEST_MEM_BASE), 0,
           TEST_PAGE_SIZE * 2u);
    memset(host->mem + (TEST_REQ_BASE - TEST_MEM_BASE), 0, 0x1000);
    test_mem_write(host, TEST_REQ_BASE, VIRTIO_BLK_T_OUT, 4);
    test_mem_write(host, TEST_REQ_BASE + 8, 0, 8);
    memcpy(host->mem + (TEST_REQ_BASE + 16 - TEST_MEM_BASE),
           "rtl-system-virtio-blk", sizeof("rtl-system-virtio-blk"));
    test_mem_write(host, TEST_REQ_BASE + 16 + 512, 0xff, 1);

    test_write_desc(host, 0, TEST_REQ_BASE, 16, VRING_DESC_F_NEXT, 1);
    test_write_desc(host, 1, TEST_REQ_BASE + 16, 512,
                    VRING_DESC_F_NEXT, 2);
    test_write_desc(host, 2, TEST_REQ_BASE + 16 + 512, 1,
                    VRING_DESC_F_WRITE, 0);
    test_mem_write(host, avail + 4, 0, 2);
    test_mem_write(host, avail + 2, 1, 2);

    q2io_write32(system, IO_RTL_SYSTEM_BLK_BASE + VIRTIO_MMIO_QUEUE_NOTIFY,
                 0);
    for (int i = 0; i < 64; i++) {
        assert(io_system_service(system, 0) == IO_SYSTEM_OK);
    }

    assert(test_mem_read(host, TEST_REQ_BASE + 16 + 512, 1) ==
           VIRTIO_BLK_S_OK);
    assert(test_mem_read(host, TEST_USED_BASE + 2, 2) == 1u);
    assert(test_mem_read(host, TEST_USED_BASE + 4, 4) == 0u);
    assert(q2io_read32(system, IO_RTL_SYSTEM_BLK_BASE +
                       VIRTIO_MMIO_INTERRUPT_STATUS) & 1u);
    for (int i = 0; i < 8; i++) {
        assert(io_system_service(system, 0) == IO_SYSTEM_OK);
    }
    assert(test_mem_read(host, TEST_MSI_ADDR, 4) ==
           IO_RTL_SYSTEM_BLK_EIID);
}

int main(void)
{
    char image_path[] = "/tmp/io-rtl-system-blk.XXXXXX";
    TestHost host = {};
    IoSystemHostOps ops = {
        .opaque = &host,
        .guest_memory_read = host_guest_read,
        .guest_memory_write = host_guest_write,
        .log = host_log,
    };
    IoSystemConfig config = {
        .name = "rtl-system-smoke",
        .backend_kind = IO_SYSTEM_BACKEND_RTL_SYSTEM,
        .backend_library_path = getenv("IO_RTL_SYSTEM_API_SO"),
        .backend_vcs_libdir = getenv("IO_RTL_SYSTEM_VCS_LIBDIR"),
        .my_virtio_blk_enabled = true,
        .my_virtio_blk_image_path = image_path,
        .trace_capacity = 256,
    };
    const uint64_t src = TEST_MEM_BASE + 0x100;
    const uint64_t dst = TEST_MEM_BASE + 0x300;
    const uint32_t size = 64;
    IoSystem *system;
    uint32_t status = 0;
    int image_fd;

    image_fd = mkstemp(image_path);
    assert(image_fd >= 0);
    assert(ftruncate(image_fd, TEST_IMAGE_SIZE) == 0);
    close(image_fd);

    for (uint32_t i = 0; i < size; i++) {
        host.mem[(src - TEST_MEM_BASE) + i] = (uint8_t)(0x40 + i);
        host.mem[(dst - TEST_MEM_BASE) + i] = 0;
    }

    system = io_system_create(&config, &ops);
    assert(system);
    assert(q2io_read32(system, IO_RTL_SYSTEM_DMAC_BASE +
                       IO_RTL_SYSTEM_DMAC_REG_ID) ==
           IO_RTL_SYSTEM_DMAC_ID);

    q2io_write32(system, IO_RTL_SYSTEM_DMAC_BASE +
                 IO_RTL_SYSTEM_DMAC_REG_CTRL,
                 IO_RTL_SYSTEM_DMAC_CTRL_CLEAR);
    write_addr64(system, IO_RTL_SYSTEM_DMAC_BASE +
                 IO_RTL_SYSTEM_DMAC_REG_SRC_LO, src);
    write_addr64(system, IO_RTL_SYSTEM_DMAC_BASE +
                 IO_RTL_SYSTEM_DMAC_REG_DST_LO, dst);
    q2io_write32(system, IO_RTL_SYSTEM_DMAC_BASE +
                 IO_RTL_SYSTEM_DMAC_REG_SIZE, size);
    q2io_write32(system, IO_RTL_SYSTEM_DMAC_BASE +
                 IO_RTL_SYSTEM_DMAC_REG_CTRL,
                 IO_RTL_SYSTEM_DMAC_CTRL_START);

    for (uint32_t i = 0; i < 20000; i++) {
        assert(io_system_service(system, 0) == IO_SYSTEM_OK);
        status = q2io_read32(system, IO_RTL_SYSTEM_DMAC_BASE +
                             IO_RTL_SYSTEM_DMAC_REG_STAT);
        if (status & IO_RTL_SYSTEM_DMAC_STAT_DONE) {
            break;
        }
    }

    assert(status & IO_RTL_SYSTEM_DMAC_STAT_DONE);
    assert(!(status & IO_RTL_SYSTEM_DMAC_STAT_BUSY));
    assert(!(status & IO_RTL_SYSTEM_DMAC_STAT_ERROR));
    assert(memcmp(host.mem + (src - TEST_MEM_BASE),
                  host.mem + (dst - TEST_MEM_BASE), size) == 0);
    run_virtio_blk_smoke(system, &host);
    assert(io_system_trace_count(system) >= 8);

    io_system_destroy(system);
    unlink(image_path);
    return 0;
}
