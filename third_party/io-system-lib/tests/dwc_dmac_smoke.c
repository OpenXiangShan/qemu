#include "io_aplic.h"
#include "io_dwc_dmac.h"

#include <assert.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#define TEST_APLIC_BASE   0x31100000ULL
#define TEST_DMAC_BASE    0x30040000ULL
#define TEST_DMAC_CH0     (TEST_DMAC_BASE + 0x100ULL)
#define TEST_MSI_ADDR     0x00400000ULL
#define TEST_SRC_ADDR     0x01000000ULL
#define TEST_DST_ADDR     0x02000000ULL
#define TEST_LLI_ADDR     0x03000000ULL
#define TEST_LLI_SIZE     64u

typedef struct TestMemory {
    uint8_t src[128];
    uint8_t dst[128];
    uint8_t lli[TEST_LLI_SIZE * 2];
    uint64_t last_msi_addr;
    uint32_t last_msi_value;
    int msi_writes;
} TestMemory;

static uint32_t load_le32(const void *data)
{
    const uint8_t *bytes = data;

    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void store_le32(void *data, uint32_t value)
{
    uint8_t *bytes = data;

    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void store_le64(void *data, uint64_t value)
{
    uint8_t *bytes = data;

    store_le32(bytes, (uint32_t)value);
    store_le32(bytes + 4, (uint32_t)(value >> 32));
}

static int test_guest_memory_read(void *opaque, uint64_t gpa, void *dst,
                                  uint32_t len)
{
    TestMemory *mem = opaque;

    if (gpa >= TEST_SRC_ADDR && gpa + len <= TEST_SRC_ADDR + sizeof(mem->src)) {
        memcpy(dst, mem->src + (size_t)(gpa - TEST_SRC_ADDR), len);
        return (int)len;
    }
    if (gpa >= TEST_DST_ADDR && gpa + len <= TEST_DST_ADDR + sizeof(mem->dst)) {
        memcpy(dst, mem->dst + (size_t)(gpa - TEST_DST_ADDR), len);
        return (int)len;
    }
    if (gpa >= TEST_LLI_ADDR && gpa + len <= TEST_LLI_ADDR + sizeof(mem->lli)) {
        memcpy(dst, mem->lli + (size_t)(gpa - TEST_LLI_ADDR), len);
        return (int)len;
    }

    return -1;
}

static int test_guest_memory_write(void *opaque, uint64_t gpa, const void *src,
                                   uint32_t len)
{
    TestMemory *mem = opaque;

    if (gpa >= TEST_SRC_ADDR && gpa + len <= TEST_SRC_ADDR + sizeof(mem->src)) {
        memcpy(mem->src + (size_t)(gpa - TEST_SRC_ADDR), src, len);
        return (int)len;
    }
    if (gpa >= TEST_DST_ADDR && gpa + len <= TEST_DST_ADDR + sizeof(mem->dst)) {
        memcpy(mem->dst + (size_t)(gpa - TEST_DST_ADDR), src, len);
        return (int)len;
    }
    if (gpa >= TEST_LLI_ADDR && gpa + len <= TEST_LLI_ADDR + sizeof(mem->lli)) {
        memcpy(mem->lli + (size_t)(gpa - TEST_LLI_ADDR), src, len);
        return (int)len;
    }
    if (gpa == TEST_MSI_ADDR && len == sizeof(uint32_t)) {
        mem->last_msi_addr = gpa;
        mem->last_msi_value = load_le32(src);
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

static void mmio_write32(IoSystem *system, uint64_t addr, uint32_t value)
{
    uint8_t bytes[4];

    store_le32(bytes, value);
    assert(io_system_q2io_write(system, addr, bytes, sizeof(bytes)) ==
           IO_SYSTEM_OK);
}

static uint32_t mmio_read32(IoSystem *system, uint64_t addr)
{
    uint8_t bytes[4];

    assert(io_system_q2io_read(system, addr, bytes, sizeof(bytes)) ==
           IO_SYSTEM_OK);
    return load_le32(bytes);
}

static void init_lli(uint8_t *lli, uint64_t sar, uint64_t dar,
                     uint32_t block_ts, uint64_t llp,
                     uint32_t ctl_lo, uint32_t ctl_hi)
{
    memset(lli, 0, TEST_LLI_SIZE);
    store_le64(lli + 0x00, sar);
    store_le64(lli + 0x08, dar);
    store_le32(lli + 0x10, block_ts);
    store_le64(lli + 0x18, llp);
    store_le32(lli + 0x20, ctl_lo);
    store_le32(lli + 0x24, ctl_hi);
}

int main(void)
{
    TestMemory mem = { 0 };
    IoSystemHostOps host_ops = {
        .opaque = &mem,
        .guest_memory_read = test_guest_memory_read,
        .guest_memory_write = test_guest_memory_write,
        .guest_memory_atomic = test_guest_memory_atomic,
        .clock = test_clock,
        .log = test_log,
    };
    IoSystem *system = io_system_create(NULL, &host_ops);
    IoAplic *aplic;
    IoDwcDmac *dmac;
    const IoAxiBeatTrace *trace;
    const uint8_t pattern[8] = {
        0xde, 0xad, 0xbe, 0xef, 0x11, 0x22, 0x33, 0x44,
    };
    uint8_t lli_pattern[16];
    uint32_t ctl_lo;
    uint32_t value;
    bool saw_msi = false;
    bool saw_dma_read = false;
    bool saw_dma_write = false;
    bool saw_lli_fetch = false;

    assert(system);
    memcpy(mem.src, pattern, sizeof(pattern));
    for (size_t i = 0; i < sizeof(lli_pattern); i++) {
        lli_pattern[i] = (uint8_t)(0xa0u + i);
    }

    aplic = io_aplic_create(system, &(IoAplicConfig) {
        .name = "test-aplic",
        .base = TEST_APLIC_BASE,
        .size = 0x4000,
        .num_sources = 96,
        .num_harts = 1,
        .iprio_bits = 8,
        .msimode = true,
        .mmode = true,
    }, NULL);
    assert(aplic);

    dmac = io_dwc_dmac_create(system, &(IoDwcDmacConfig) {
        .name = "test-dmac",
        .base = TEST_DMAC_BASE,
        .size = IO_DWC_DMAC_MMIO_SIZE,
        .irq = 18,
        .requester_id = 0x8,
    }, aplic);
    assert(dmac);

    mmio_write32(system, TEST_APLIC_BASE + IO_APLIC_MMSICFGADDR,
                 TEST_MSI_ADDR >> 12);
    mmio_write32(system, TEST_APLIC_BASE + IO_APLIC_MMSICFGADDRH, 0);
    mmio_write32(system, TEST_APLIC_BASE + IO_APLIC_SOURCECFG_BASE +
                 (18u - 1u) * 4u, IO_APLIC_SOURCECFG_SM_LEVEL_HIGH);
    mmio_write32(system, TEST_APLIC_BASE + IO_APLIC_TARGET_BASE +
                 (18u - 1u) * 4u, 0x55);
    mmio_write32(system, TEST_APLIC_BASE + IO_APLIC_DOMAINCFG,
                 IO_APLIC_DOMAINCFG_IE);
    mmio_write32(system, TEST_APLIC_BASE + IO_APLIC_SETIENUM, 18u);

    assert(mmio_read32(system, TEST_DMAC_BASE + IO_DWC_DMAC_ID) ==
           0x44574158u);
    assert(mmio_read32(system, TEST_DMAC_BASE + IO_DWC_DMAC_COMPVER) ==
           0x31303161u);

    ctl_lo = (3u << IO_DWC_DMAC_CH_CTL_L_SRC_WIDTH_POS) |
             (3u << IO_DWC_DMAC_CH_CTL_L_DST_WIDTH_POS) |
             (IO_DWC_DMAC_CH_CTL_L_INC << IO_DWC_DMAC_CH_CTL_L_SRC_INC_POS) |
             (IO_DWC_DMAC_CH_CTL_L_INC << IO_DWC_DMAC_CH_CTL_L_DST_INC_POS);

    mmio_write32(system, TEST_DMAC_BASE + IO_DWC_DMAC_CFG,
                 IO_DWC_DMAC_CFG_EN | IO_DWC_DMAC_CFG_INT_EN);
    mmio_write32(system, TEST_DMAC_CH0 + IO_DWC_DMAC_CH_SAR,
                 TEST_SRC_ADDR);
    mmio_write32(system, TEST_DMAC_CH0 + IO_DWC_DMAC_CH_DAR,
                 TEST_DST_ADDR);
    mmio_write32(system, TEST_DMAC_CH0 + IO_DWC_DMAC_CH_BLOCK_TS, 0);
    mmio_write32(system, TEST_DMAC_CH0 + IO_DWC_DMAC_CH_CTL, ctl_lo);
    mmio_write32(system, TEST_DMAC_CH0 + IO_DWC_DMAC_CH_CTL_H,
                 IO_DWC_DMAC_CH_CTL_H_LLI_VALID);
    mmio_write32(system, TEST_DMAC_CH0 + IO_DWC_DMAC_CH_INTSTATUS_ENA,
                 IO_DWC_DMAC_IRQ_BLOCK_TRF | IO_DWC_DMAC_IRQ_DMA_TRF);
    mmio_write32(system, TEST_DMAC_CH0 + IO_DWC_DMAC_CH_INTSIGNAL_ENA,
                 IO_DWC_DMAC_IRQ_BLOCK_TRF | IO_DWC_DMAC_IRQ_DMA_TRF);
    mmio_write32(system, TEST_DMAC_BASE + IO_DWC_DMAC_CHEN, 0x101);

    assert(memcmp(mem.dst, mem.src, sizeof(pattern)) == 0);
    assert(mem.msi_writes == 1);
    assert(mem.last_msi_addr == TEST_MSI_ADDR);
    assert(mem.last_msi_value == 0x55u);

    value = mmio_read32(system, TEST_DMAC_CH0 + IO_DWC_DMAC_CH_STATUS);
    assert(value == 0);
    value = mmio_read32(system, TEST_DMAC_CH0 + IO_DWC_DMAC_CH_INTSTATUS);
    assert(value == (IO_DWC_DMAC_IRQ_BLOCK_TRF | IO_DWC_DMAC_IRQ_DMA_TRF));
    value = mmio_read32(system, TEST_DMAC_BASE + IO_DWC_DMAC_CHEN);
    assert(value == 0);

    mmio_write32(system, TEST_DMAC_CH0 + IO_DWC_DMAC_CH_INTCLEAR,
                 IO_DWC_DMAC_IRQ_BLOCK_TRF | IO_DWC_DMAC_IRQ_DMA_TRF);
    assert(mmio_read32(system, TEST_DMAC_CH0 + IO_DWC_DMAC_CH_INTSTATUS) == 0);
    assert(mmio_read32(system, TEST_DMAC_BASE + IO_DWC_DMAC_INTSTATUS) == 0);

    for (size_t i = 0; i < io_system_trace_count(system); i++) {
        trace = io_system_trace_at(system, i);
        if (!trace || strcmp(trace->port, "io2q") != 0) {
            continue;
        }
        if (strcmp(trace->channel, "memory-read") == 0 &&
            trace->address == TEST_SRC_ADDR) {
            saw_dma_read = true;
        }
        if (strcmp(trace->channel, "memory-write") == 0 &&
            trace->address == TEST_DST_ADDR) {
            saw_dma_write = true;
        }
        if (strcmp(trace->channel, "memory-write") == 0 &&
            trace->address == TEST_MSI_ADDR) {
            saw_msi = true;
        }
    }

    assert(saw_dma_read);
    assert(saw_dma_write);
    assert(saw_msi);

    memset(mem.dst, 0, sizeof(mem.dst));
    memset(mem.lli, 0, sizeof(mem.lli));
    memcpy(mem.src, lli_pattern, sizeof(lli_pattern));
    mem.msi_writes = 0;

    init_lli(mem.lli, TEST_SRC_ADDR, TEST_DST_ADDR, 0,
             TEST_LLI_ADDR + TEST_LLI_SIZE, ctl_lo,
             IO_DWC_DMAC_CH_CTL_H_LLI_VALID);
    init_lli(mem.lli + TEST_LLI_SIZE, TEST_SRC_ADDR + 8, TEST_DST_ADDR + 8, 0,
             TEST_LLI_ADDR,
             ctl_lo,
             IO_DWC_DMAC_CH_CTL_H_LLI_VALID |
             IO_DWC_DMAC_CH_CTL_H_LLI_LAST);

    mmio_write32(system, TEST_DMAC_CH0 + IO_DWC_DMAC_CH_LLP,
                 (uint32_t)TEST_LLI_ADDR);
    mmio_write32(system, TEST_DMAC_CH0 + IO_DWC_DMAC_CH_LLP + 4, 0);
    mmio_write32(system, TEST_DMAC_CH0 + IO_DWC_DMAC_CH_INTSTATUS_ENA,
                 IO_DWC_DMAC_IRQ_DMA_TRF);
    mmio_write32(system, TEST_DMAC_CH0 + IO_DWC_DMAC_CH_INTSIGNAL_ENA,
                 IO_DWC_DMAC_IRQ_DMA_TRF);
    mmio_write32(system, TEST_DMAC_BASE + IO_DWC_DMAC_CHEN, 0x101);

    assert(memcmp(mem.dst, lli_pattern, sizeof(lli_pattern)) == 0);
    assert(mem.msi_writes == 1);
    assert(mmio_read32(system, TEST_DMAC_CH0 + IO_DWC_DMAC_CH_STATUS) == 0);
    assert(mmio_read32(system, TEST_DMAC_CH0 + IO_DWC_DMAC_CH_INTSTATUS) ==
           IO_DWC_DMAC_IRQ_DMA_TRF);
    assert(mmio_read32(system, TEST_DMAC_BASE + IO_DWC_DMAC_CHEN) == 0);

    for (size_t i = 0; i < io_system_trace_count(system); i++) {
        trace = io_system_trace_at(system, i);
        if (!trace || strcmp(trace->port, "io2q") != 0) {
            continue;
        }
        if (strcmp(trace->channel, "memory-read") == 0 &&
            trace->address == TEST_LLI_ADDR &&
            trace->beat_size == TEST_LLI_SIZE) {
            saw_lli_fetch = true;
            break;
        }
    }
    assert(saw_lli_fetch);

    io_dwc_dmac_destroy(dmac);
    io_aplic_destroy(aplic);
    io_system_destroy(system);
    return 0;
}
