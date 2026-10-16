/*
 * Per-root DID mapper: real PCIe DMA, fallback, faults and isolation.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/units.h"
#include "qemu/bswap.h"
#include "hw/misc/bosc-did-mapper.h"
#include "hw/riscv/riscv-iommu-bits.h"

static const uint64_t dbis[] = { 0x4f00000000, 0x4f00800000, 0x4f01000000 };
static const uint64_t ecams[] = { 0x4800000000, 0x4900000000, 0x4a00000000 };
static const uint64_t bars[] = { 0x48000000000, 0x4e000000000, 0x4f000000000 };

static uint64_t global(unsigned root, uint64_t local)
{
    return ((uint64_t)(root / 3 + 1) << 44) | local;
}

static uint64_t mapper(unsigned root)
{
    return global(root, 0x4f80201000 + (root % 3) * 0x10000);
}

static uint64_t iommu(unsigned root)
{
    return mapper(root) - 0x1000;
}

static uint64_t pa(unsigned root, bool override)
{
    return 0x83000000 + root * 0x10000 + override * 0x4000;
}

static void reg64(QTestState *q, uint64_t addr, uint64_t value)
{
    qtest_writel(q, addr, value);
    qtest_writel(q, addr + 4, value >> 32);
}

static void slot(QTestState *q, unsigned root, unsigned n,
                 uint64_t start, uint64_t end, uint32_t did)
{
    uint64_t m = mapper(root);

    reg64(q, m + BOSC_DID_MAPPER_START_BASE + n * 8, start);
    reg64(q, m + BOSC_DID_MAPPER_END_BASE + n * 8, end);
    qtest_writel(q, m + BOSC_DID_MAPPER_DID_BASE + n * 4, did);
    qtest_writel(q, m + BOSC_DID_MAPPER_PID_BASE + n * 4, 0);
}

static QTestState *start(bool all)
{
    GString *args = g_string_new(
        "-M kmh-bosc-soc,soc-mode=multi,die-mask=0xf,core-mask=0x1,"
        "dw-pcie=on,generated-dtb=on,iommu-sys=on "
        "-smp 64,maxcpus=68 -m 256M");

    for (unsigned root = 0; root < (all ? 12 : 1); root++) {
        g_string_append_printf(args, " -device edu,bus=pcie-d%u-p%u,"
                               "dma_mask=0xffffffffffffffff",
                               root / 3, root % 3);
    }
    QTestState *q = qtest_init(args->str);
    g_string_free(args, true);
    return q;
}

static void setup(QTestState *q, unsigned root)
{
    unsigned port = root % 3;
    uint64_t dbi = global(root, dbis[port]);
    uint64_t ep = global(root, ecams[port]) + 0x100000;
    uint64_t bar = global(root, bars[port]);
    uint64_t ddt = 0x82000000 + root * 0x10000;
    uint64_t dir = ddt + 0x1000;

    qtest_writew(q, dbi + 4, 6);
    qtest_writel(q, dbi + 0x18, 0x00010100);
    qtest_writel(q, dbi + 0x20, 0x50005000);
    reg64(q, dbi + 0x90c, bars[port]);
    qtest_writel(q, dbi + 0x914, bars[port] + MiB - 1);
    reg64(q, dbi + 0x918, 0x50000000);
    qtest_writel(q, dbi + 0x908, 0x80000000);
    qtest_writel(q, dbi + 0x900, 1);
    qtest_writel(q, dbi + 0x904, 4);
    reg64(q, dbi + 0x90c, ecams[port] + MiB);
    qtest_writel(q, dbi + 0x914, ecams[port] + 2 * MiB - 1);
    reg64(q, dbi + 0x918, 1U << 24);
    qtest_writel(q, dbi + 0x908, 0x80000000);
    g_assert_cmphex(qtest_readl(q, ep), ==, 0x11e81234);
    qtest_writel(q, ep + 0x10, 0x50000000);
    qtest_writew(q, ep + 4, 6);
    g_assert_cmphex(qtest_readl(q, bar), ==, 0x010000ed);
    qtest_writel(q, mapper(root), 0);

    qtest_writeq(q, ddt, (dir >> 2) | 1);
    for (unsigned context = 0; context < 2; context++) {
        uint32_t did = context ? 0x101 : 0x100;
        uint64_t dc = ddt + 0x2000 + context * 64;
        uint64_t pt = ddt + 0x3000 + context * 0x3000;

        qtest_writeq(q, dir + (did >> 6) * 8, ((ddt + 0x2000) >> 2) | 1);
        qtest_writeq(q, dc, RISCV_IOMMU_DC_TC_V);
        /* Different DCs, distinct PSCIDs; identical tags across all roots. */
        qtest_writeq(q, dc + 16, (uint64_t)(context + 1) << 12);
        qtest_writeq(q, dc + 24, (8ULL << 60) | (pt >> 12));
        qtest_writeq(q, pt, ((pt + 0x1000) >> 2) | 1);
        qtest_writeq(q, pt + 0x1000, ((pt + 0x2000) >> 2) | 1);
        qtest_writeq(q, pt + 0x2000 + 8, (pa(root, context) >> 2) | 0xc7);
        qtest_writeq(q, pt + 0x2000 + 16,
                     ((pa(root, context) + 0x1000) >> 2) | 0xc7);
    }
    qtest_writeq(q, iommu(root) + RISCV_IOMMU_REG_FQB,
                 ((ddt + 0x9000) >> 2) | 3);
    qtest_writel(q, iommu(root) + RISCV_IOMMU_REG_FQCSR,
                 RISCV_IOMMU_FQCSR_FQEN);
    qtest_writeq(q, iommu(root) + RISCV_IOMMU_REG_DDTP,
                 (ddt >> 2) | RISCV_IOMMU_DDTP_MODE_3LVL);
}

static void dma(QTestState *q, unsigned root, uint64_t iova,
                unsigned size, bool write)
{
    uint64_t bar = global(root, bars[root % 3]);

    qtest_writeq(q, bar + 0x80, write ? 0x40000 : iova);
    qtest_writeq(q, bar + 0x88, write ? iova : 0x40000);
    qtest_writeq(q, bar + 0x90, size);
    qtest_writeq(q, bar + 0x98, write ? 3 : 1);
    qtest_clock_step(q, 100000000);
    g_assert_cmphex(qtest_readq(q, bar + 0x98) & 1, ==, 0);
}

static void expect(QTestState *q, unsigned root, uint64_t iova, bool override)
{
    uint64_t offset = iova - 0x1000;
    uint32_t value = 0xcafe0000 + root;

    qtest_writel(q, pa(root, false) + offset, value);
    qtest_writel(q, pa(root, true) + offset, value + 0x100);
    qtest_writel(q, pa(root, false) + 0x1000, 0);
    qtest_writel(q, pa(root, true) + 0x1000, 0);
    dma(q, root, iova, 4, false);
    dma(q, root, 0x2000, 4, true);
    g_assert_cmphex(qtest_readl(q, pa(root, override) + 0x1000), ==,
                    value + override * 0x100);
    g_assert_cmphex(qtest_readl(q, pa(root, !override) + 0x1000), ==, 0);
    g_assert_cmpuint(qtest_readl(q, iommu(root) + RISCV_IOMMU_REG_FQT), ==, 0);
}

static void test_ranges(void)
{
    QTestState *q = start(false);
    setup(q, 0);
    slot(q, 0, 0, 0x1000, 0x3000, 0x101);
    expect(q, 0, 0x1000, false); /* Disabled, with a valid matching slot. */
    qtest_writel(q, mapper(0), BOSC_DID_MAPPER_MUX_DID_PID_EN);
    expect(q, 0, 0x1000, true);
    for (unsigned n = 0; n < 8; n++) {
        slot(q, 0, n, 0x1000, 0x3000, 0x101);
        expect(q, 0, 0x1000, true);
        slot(q, 0, n, UINT64_MAX, UINT64_MAX, 0);
        expect(q, 0, 0x1000, false); /* Enabled empty slots retain RID. */
    }
    slot(q, 0, 7, 0x1000, 0x3000, 0x101);
    slot(q, 0, 0, 0x1000, 0x3000, 0x100);
    expect(q, 0, 0x1000, false); /* Low slot number wins. */
    slot(q, 0, 0, 0x3000, 0x1000, 0x100);
    expect(q, 0, 0x1000, true); /* Reversed slot is invalid. */
    slot(q, 0, 7, 0x1004, 0x2000, 0x101);
    expect(q, 0, 0x1000, false); /* Nonempty miss. */
    /* Destination is outside the slot: source override reaches RID page. */
    qtest_writel(q, pa(0, true) + 4, 0x12345678);
    dma(q, 0, 0x1004, 4, false); /* Inclusive start. */
    dma(q, 0, 0x2000, 4, true); /* Exclusive end. */
    g_assert_cmphex(qtest_readl(q, pa(0, false) + 0x1000), ==, 0x12345678);
    qtest_quit(q);
}

static void test_cache_update(void)
{
    QTestState *q = start(false);
    setup(q, 0);
    slot(q, 0, 0, 0x1000, 0x3000, 0x101);
    qtest_writel(q, mapper(0), BOSC_DID_MAPPER_MUX_DID_PID_EN);
    expect(q, 0, 0x1000, true);
    /*
     * Populate both context/IOT caches, change the DC/PT behind those caches,
     * then use a mapper write as the only invalidation operation.
     */
    qtest_writeq(q, 0x82002040 + 24, (8ULL << 60) | (0x82003000 >> 12));
    qtest_writeq(q, 0x82002040 + 16, 1ULL << 12);
    slot(q, 0, 0, 0x1000, 0x3000, 0x101);
    expect(q, 0, 0x1000, false);
    qtest_writeq(q, 0x82005000 + 8, (pa(0, true) >> 2) | 0xc7);
    qtest_writeq(q, 0x82005000 + 16, ((pa(0, true) + 0x1000) >> 2) | 0xc7);
    qtest_writel(q, mapper(0), BOSC_DID_MAPPER_MUX_DID_PID_EN);
    expect(q, 0, 0x1000, true);
    qtest_quit(q);
}

static void test_fault_recovery(void)
{
    QTestState *q = start(false);
    setup(q, 0);
    expect(q, 0, 0x1000, false);
    slot(q, 0, 0, 0x1000, 0x3000, 0x200);
    qtest_writel(q, mapper(0), BOSC_DID_MAPPER_MUX_DID_PID_EN);
    qtest_writel(q, pa(0, false) + 0x1000, 0x87654321);
    dma(q, 0, 0x2000, 4, true);
    uint64_t fault = qtest_readq(q, 0x82009000);
    g_assert_cmpuint(qtest_readl(q, iommu(0) + RISCV_IOMMU_REG_FQT), ==, 1);
    g_assert_cmpuint(fault & RISCV_IOMMU_FQ_HDR_CAUSE, ==,
                     RISCV_IOMMU_FQ_CAUSE_DDT_INVALID);
    g_assert_cmphex(fault >> 40, ==, 0x200);
    g_assert_cmphex(qtest_readl(q, pa(0, false) + 0x1000), ==, 0x87654321);
    qtest_writel(q, iommu(0) + RISCV_IOMMU_REG_FQH, 1);
    /* A fresh queue lets the ordinary success helper reject extra faults. */
    qtest_writel(q, iommu(0) + RISCV_IOMMU_REG_FQCSR, 0);
    qtest_writel(q, iommu(0) + RISCV_IOMMU_REG_FQCSR, RISCV_IOMMU_FQCSR_FQEN);
    slot(q, 0, 0, 0x1000, 0x3000, 0x101);
    expect(q, 0, 0x1000, true);
    qtest_quit(q);
}

static void test_all_roots(void)
{
    QTestState *q = start(true);
    for (unsigned root = 0; root < 12; root++) {
        setup(q, root);
        slot(q, root, 0, 0x1000, 0x3000, 0x101);
        qtest_writel(q, mapper(root), BOSC_DID_MAPPER_MUX_DID_PID_EN);
        expect(q, root, 0x1000, true);
    }
    /* Toggle one mapper after all 12 independent contexts are cached. */
    for (unsigned changed = 0; changed < 12; changed++) {
        qtest_writel(q, mapper(changed), 0);
        for (unsigned root = 0; root < 12; root++) {
            g_assert_cmphex(qtest_readl(q, mapper(root)), ==,
                            root == changed ? 0 :
                            BOSC_DID_MAPPER_MUX_DID_PID_EN);
            expect(q, root, 0x1000, root != changed);
        }
        qtest_writel(q, mapper(changed), BOSC_DID_MAPPER_MUX_DID_PID_EN);
    }
    qtest_quit(q);
}

/* Same IOVA has three distinguishable destinations: direct, RID and mapper. */
static void expect_bypass(QTestState *q, unsigned root, bool bypass)
{
    const uint64_t direct = 0x81000000;
    const uint32_t value = 0xcafe0000 + root;

    qtest_writel(q, direct, value);
    qtest_writel(q, pa(root, false), value + 0x100);
    qtest_writel(q, pa(root, true), value + 0x200);
    qtest_writel(q, direct + 0x1000, 0);
    qtest_writel(q, pa(root, false) + 0x1000, 0);
    qtest_writel(q, pa(root, true) + 0x1000, 0);
    /* High die-ID bits are truncated in both bypass and translated modes. */
    dma(q, root, direct | (4ULL << 44), 4, false);
    dma(q, root, (direct + 0x1000) | (2ULL << 44), 4, true);
    g_assert_cmphex(qtest_readl(q, direct + 0x1000), ==,
                    bypass ? value : 0);
    g_assert_cmphex(qtest_readl(q, pa(root, true) + 0x1000), ==,
                    bypass ? 0 : value + 0x200);
    g_assert_cmphex(qtest_readl(q, pa(root, false) + 0x1000), ==, 0);
    g_assert_cmpuint(qtest_readl(q, iommu(root) + RISCV_IOMMU_REG_FQT), ==, 0);
}

static void test_bypass(void)
{
    QTestState *q = start(true);
    const uint64_t iova = 0x81000000;

    for (unsigned root = 0; root < 12; root++) {
        g_assert_cmphex(qtest_readl(q, mapper(root)), ==,
                        BOSC_DID_MAPPER_MUX_BYPASS);
        setup(q, root);
        for (unsigned context = 0; context < 2; context++) {
            uint64_t pt = 0x82003000 + root * 0x10000 + context * 0x3000;

            qtest_writeq(q, pt + ((iova >> 30) & 0x1ff) * 8,
                         ((pt + 0x1000) >> 2) | 1);
            qtest_writeq(q, pt + 0x1000 + ((iova >> 21) & 0x1ff) * 8,
                         ((pt + 0x2000) >> 2) | 1);
            qtest_writeq(q, pt + 0x2000,
                         (pa(root, context) >> 2) | 0xc7);
            qtest_writeq(q, pt + 0x2000 + 8,
                         ((pa(root, context) + 0x1000) >> 2) | 0xc7);
        }
        slot(q, root, 0, 0, 1ULL << 44, 0x101);
        qtest_writel(q, mapper(root), BOSC_DID_MAPPER_MUX_DID_PID_EN);
        expect_bypass(q, root, false);
    }
    for (unsigned changed = 0; changed < 12; changed++) {
        const uint32_t enabled = BOSC_DID_MAPPER_MUX_DID_PID_EN;
        const uint32_t bypass = enabled | BOSC_DID_MAPPER_MUX_BYPASS;
        const uint64_t ddt = 0x82000000 + changed * 0x10000;

        qtest_writel(q, mapper(changed), bypass);
        for (unsigned root = 0; root < 12; root++) {
            expect_bypass(q, root, root == changed);
        }
        /* Even a matching invalid DID cannot fault while bypass is set. */
        slot(q, changed, 0, 0, 1ULL << 44, 0x200);
        expect_bypass(q, changed, true);
        qtest_writel(q, mapper(changed), enabled);
        qtest_writel(q, iova + 0x1000, 0x12345678);
        qtest_writel(q, pa(changed, true) + 0x1000, 0x87654321);
        dma(q, changed, iova + 0x1000, 4, true);
        uint64_t fault = qtest_readq(q, ddt + 0x9000);
        g_assert_cmpuint(qtest_readl(q, iommu(changed) + RISCV_IOMMU_REG_FQT),
                         ==, 1);
        g_assert_cmpuint(fault & RISCV_IOMMU_FQ_HDR_CAUSE, ==,
                         RISCV_IOMMU_FQ_CAUSE_DDT_INVALID);
        g_assert_cmphex(fault >> 40, ==, 0x200);
        g_assert_cmphex(qtest_readl(q, iova + 0x1000), ==, 0x12345678);
        g_assert_cmphex(qtest_readl(q, pa(changed, true) + 0x1000), ==,
                        0x87654321);
        qtest_writel(q, iommu(changed) + RISCV_IOMMU_REG_FQCSR, 0);
        qtest_writel(q, iommu(changed) + RISCV_IOMMU_REG_FQCSR,
                     RISCV_IOMMU_FQCSR_FQEN);
        slot(q, changed, 0, 0, 1ULL << 44, 0x101);
        qtest_writel(q, mapper(changed), bypass);
        /* DDTP Off blocks translation, but not the wrapper's direct path. */
        qtest_writeq(q, iommu(changed) + RISCV_IOMMU_REG_DDTP, 0);
        expect_bypass(q, changed, true);
        qtest_writeq(q, iommu(changed) + RISCV_IOMMU_REG_DDTP,
                     (ddt >> 2) | RISCV_IOMMU_DDTP_MODE_3LVL);
        qtest_writel(q, mapper(changed), enabled);
        expect_bypass(q, changed, false);
    }
    /* Bypass uses the requester die's local peripheral aliases. */
    for (unsigned root = 0; root < 12; root++) {
        const uint64_t local = 0x1101000;

        qtest_writel(q, mapper(root), BOSC_DID_MAPPER_MUX_BYPASS);
        qtest_writel(q, global(root, local), 0xfeed0000 + root);
        qtest_writel(q, global(root, local + 4), 0);
        dma(q, root, local, 4, false);
        dma(q, root, local + 4, 4, true);
        g_assert_cmphex(qtest_readl(q, global(root, local + 4)), ==,
                        0xfeed0000 + root);
    }
    qtest_quit(q);
}

static void test_boundary(void)
{
    QTestState *q = start(false);
    setup(q, 0);
    qtest_writel(q, mapper(0), BOSC_DID_MAPPER_MUX_DID_PID_EN);
    for (unsigned crossing = 0; crossing < 2; crossing++) {
        uint64_t iova = crossing ? 0x1ffc : 0x1000;
        uint64_t offset = iova - 0x1000;

        /* First four bytes override, remaining four retain the RID. */
        slot(q, 0, 0, iova, iova + 4, 0x101);
        qtest_writel(q, pa(0, true) + offset, 0x12345678);
        qtest_writel(q, pa(0, false) + offset + 4, 0x87654321);
        dma(q, 0, iova, 8, false);
        dma(q, 0, 0x2100, 8, true);
        g_assert_cmphex(qtest_readq(q, pa(0, false) + 0x1100), ==,
                        0x8765432112345678ULL);
        /* Reverse direction: miss first, then hit within the same page. */
        slot(q, 0, 0, iova + 4, iova + 8, 0x101);
        dma(q, 0, iova, 8, true);
        g_assert_cmphex(qtest_readl(q, pa(0, false) + offset), ==, 0x12345678);
        g_assert_cmphex(qtest_readl(q, pa(0, true) + offset + 4), ==,
                        0x87654321);
    }
    /* Unaligned edges within 1-GiB superpages must also split DMA. */
    const uint64_t rid_pa = 0x80001000;
    const uint64_t override_pa = 0x880001000;
    const uint8_t data[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t result[8];
    qtest_writeq(q, 0x82003000, (0x80000000ULL >> 2) | 0xc7);
    qtest_writeq(q, 0x82006000, (0x880000000ULL >> 2) | 0xc7);
    slot(q, 0, 0, 0x1000, 0x1003, 0x101);
    qtest_memwrite(q, override_pa, data, 3);
    qtest_memwrite(q, rid_pa + 3, data + 3, 5);
    dma(q, 0, 0x1000, 8, false);
    dma(q, 0, 0x2100, 8, true);
    qtest_memread(q, 0x80002100, result, sizeof(result));
    g_assert_cmpmem(result, sizeof(result), data, sizeof(data));
    slot(q, 0, 0, 0x1003, 0x1008, 0x101);
    dma(q, 0, 0x1000, 8, true);
    qtest_memread(q, rid_pa, result, 3);
    qtest_memread(q, override_pa + 3, result + 3, 5);
    g_assert_cmpmem(result, sizeof(result), data, sizeof(data));
    qtest_quit(q);
}

static void test_pid_pv(void)
{
    QTestState *q = start(false);
    setup(q, 0);
    slot(q, 0, 0, 0x1000, 0x3000, 0x101);
    qtest_writel(q, mapper(0), BOSC_DID_MAPPER_MUX_DID_PID_EN);
    /* Stored PID/translated without PV still uses the device context. */
    qtest_writel(q, mapper(0) + BOSC_DID_MAPPER_PID_BASE, BIT(21) | 42);
    expect(q, 0, 0x1000, true);
    /* PV now requests a process context, forbidden by this DC's PDTV=0. */
    qtest_writel(q, mapper(0) + BOSC_DID_MAPPER_PID_BASE, BIT(20) | 42);
    dma(q, 0, 0x1000, 4, false);
    uint64_t fault = qtest_readq(q, 0x82009000);
    g_assert_cmpuint(qtest_readl(q, iommu(0) + RISCV_IOMMU_REG_FQT), ==, 1);
    g_assert_cmpuint(fault & RISCV_IOMMU_FQ_HDR_CAUSE, ==,
                     RISCV_IOMMU_FQ_CAUSE_TTYPE_BLOCKED);
    g_assert_cmphex(fault >> 40, ==, 0x101);
    g_assert_true(fault & RISCV_IOMMU_FQ_HDR_PV);
    g_assert_cmpuint((fault & RISCV_IOMMU_FQ_HDR_PID) >> 12, ==, 42);
    qtest_quit(q);
}

static void test_imsi(void)
{
    QTestState *q = start(false);
    uint64_t dbi = global(0, dbis[0]);
    uint64_t ep = global(0, ecams[0]) + 0x100000;
    uint64_t bar = global(0, bars[0]);
    setup(q, 0);
    unsigned cap = qtest_readb(q, ep + 0x34);
    g_assert_cmphex(qtest_readb(q, ep + cap), ==, 5);
    slot(q, 0, 0, 0, 1ULL << 44, 0x200);
    qtest_writel(q, mapper(0), BOSC_DID_MAPPER_MUX_DID_PID_EN);
    reg64(q, dbi + 0x820, 0x81000000 | (2ULL << 44));
    qtest_writel(q, dbi + 0x828, UINT32_MAX);
    reg64(q, ep + cap + 4, 0x81000000 | (2ULL << 44));
    qtest_writew(q, ep + cap + 12, 7);
    qtest_writew(q, ep + cap + 2, 0x81);
    qtest_writel(q, 0x81000000, 0x12345678);
    qtest_writel(q, bar + 0x60, 1);
    g_assert_cmphex(qtest_readl(q, dbi + 0x830), ==, BIT(7));
    g_assert_cmphex(qtest_readl(q, 0x81000000), ==, 0x12345678);
    g_assert_cmpuint(qtest_readl(q, iommu(0) + RISCV_IOMMU_REG_FQT), ==, 1);
    uint64_t fault = qtest_readq(q, 0x82009000);
    g_assert_cmphex(fault >> 40, ==, 0x200);
    g_assert_cmpuint(fault & RISCV_IOMMU_FQ_HDR_CAUSE, ==,
                     RISCV_IOMMU_FQ_CAUSE_DDT_INVALID);
    qtest_writel(q, mapper(0), BOSC_DID_MAPPER_MUX_DID_PID_EN |
                              BOSC_DID_MAPPER_MUX_BYPASS);
    qtest_writel(q, bar + 0x60, 1);
    g_assert_cmphex(qtest_readl(q, dbi + 0x830), ==, BIT(7));
    g_assert_cmphex(qtest_readl(q, 0x81000000), ==, 7);
    g_assert_cmpuint(qtest_readl(q, iommu(0) + RISCV_IOMMU_REG_FQT), ==, 1);
    qtest_writel(q, mapper(0), BOSC_DID_MAPPER_MUX_DID_PID_EN);
    qtest_writel(q, 0x81000000, 0x12345678);
    /* Move the receiver: this same MSI becomes ordinary 44-bit DMA. */
    reg64(q, dbi + 0x820, 0x81000020);
    qtest_writel(q, bar + 0x60, 1);
    g_assert_cmpuint(qtest_readl(q, iommu(0) + RISCV_IOMMU_REG_FQT), ==, 2);
    fault = qtest_readq(q, 0x82009020);
    g_assert_cmphex(fault >> 40, ==, 0x200);
    g_assert_cmpuint(fault & RISCV_IOMMU_FQ_HDR_CAUSE, ==,
                     RISCV_IOMMU_FQ_CAUSE_DDT_INVALID);
    g_assert_cmphex(qtest_readl(q, 0x81000000), ==, 0x12345678);

    /* A matched MSI also uses the mapper-selected paged context. */
    uint64_t addr = 0x1000 | (2ULL << 44);
    reg64(q, dbi + 0x820, addr);
    reg64(q, ep + cap + 4, addr);
    slot(q, 0, 0, 0, 1ULL << 44, 0x101);
    qtest_writel(q, pa(0, false), 0x12345678);
    qtest_writel(q, pa(0, true), 0x87654321);
    qtest_writel(q, dbi + 0x830, UINT32_MAX);
    qtest_writel(q, bar + 0x60, 1);
    g_assert_cmphex(qtest_readl(q, dbi + 0x830), ==, BIT(7));
    g_assert_cmphex(qtest_readl(q, pa(0, true)), ==, 7);
    g_assert_cmphex(qtest_readl(q, pa(0, false)), ==, 0x12345678);
    g_assert_cmpuint(qtest_readl(q, iommu(0) + RISCV_IOMMU_REG_FQT), ==, 2);

    /* Disabling the mapper forwards using the endpoint's original RID. */
    qtest_writel(q, mapper(0), 0);
    qtest_writew(q, ep + cap + 12, 9);
    qtest_writel(q, bar + 0x60, 1);
    g_assert_cmphex(qtest_readl(q, dbi + 0x830), ==, BIT(7) | BIT(9));
    g_assert_cmphex(qtest_readl(q, pa(0, false)), ==, 9);
    g_assert_cmphex(qtest_readl(q, pa(0, true)), ==, 7);
    g_assert_cmpuint(qtest_readl(q, iommu(0) + RISCV_IOMMU_REG_FQT), ==, 2);
    qtest_quit(q);
}

static void test_reset(const void *opaque)
{
    bool single = GPOINTER_TO_INT(opaque) != 0;
    bool bypass = GPOINTER_TO_INT(opaque) != 2;
    QTestState *q = qtest_initf(
        "-M kmh-bosc-soc,soc-mode=%s,core-mask=0x1,"
        "iommu-sys=on,generated-dtb=on%s "
        "-smp %s -m 256M", single ? "single" : "multi",
        bypass ? "" : ",iommu-bypass=off",
        single ? "16,maxcpus=17" : "64,maxcpus=68");
    for (unsigned pass = 0; pass < 2; pass++) {
        for (unsigned root = 0; root < (single ? 3 : 12); root++) {
            uint64_t m = single ? (mapper(root) & ((1ULL << 44) - 1)) :
                                 mapper(root);
            g_assert_cmphex(qtest_readl(q, m), ==,
                            bypass ? BOSC_DID_MAPPER_MUX_BYPASS : 0);
            for (unsigned n = 0; n < 8; n++) {
                g_assert_cmphex(qtest_readq(q, m + 0xc0 + n * 8), ==,
                                UINT64_MAX);
                g_assert_cmphex(qtest_readq(q, m + 0x100 + n * 8), ==,
                                UINT64_MAX);
                g_assert_cmphex(qtest_readl(q, m + 0x80 + n * 4), ==, 0);
                g_assert_cmphex(qtest_readl(q, m + 0xa0 + n * 4), ==, 0);
                qtest_writel(q, m + 0x80 + n * 4, UINT32_MAX);
                qtest_writel(q, m + 0xa0 + n * 4, UINT32_MAX);
                g_assert_cmphex(qtest_readl(q, m + 0x80 + n * 4), ==, 0xffffff);
                g_assert_cmphex(qtest_readl(q, m + 0xa0 + n * 4), ==, 0x3fffff);
                reg64(q, m + 0xc0 + n * 8, 0x123456789abcdef0ULL);
                g_assert_cmphex(qtest_readq(q, m + 0xc0 + n * 8), ==,
                                0x123456789abcdef0ULL);
            }
            qtest_writel(q, m, UINT32_MAX);
            g_assert_cmphex(qtest_readl(q, m), ==, 0xf);
        }
        qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    }
    qtest_quit(q);
}

static void test_cpu_local(void)
{
    g_autofree char *bios = NULL;
    /*
     * RV64: derive hardware die ID from mhartid, write local mapper DID,
     * read/write its encoded alias, publish the local readback to local SRAM.
     * Repeat for ports 0..2. One active hart per die avoids test-side races.
     */
    uint32_t code[] = {
        0xf1402573, 0x00455593, 0x00158593, 0x02c59613,
        0x00000717, 0x05873703, 0x011002b7, 0x00251313,
        0x006282b3, 0x00300693, 0x00010fb7, 0x00b72023,
        0x00c767b3, 0x0007a803, 0x01080813, 0x0107a023,
        0x00072883, 0x2112a023, 0x01f70733, 0x10028293,
        0xfff68693, 0xfc069ce3, 0x0330000f, 0x10500073,
        0xffdff06f, 0x00000013, 0x80201080, 0x0000004f,
    };
    int fd = g_file_open_tmp("mapper-cpu-XXXXXX", &bios, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    for (unsigned i = 0; i < ARRAY_SIZE(code); i++) {
        code[i] = cpu_to_le32(code[i]);
    }
    g_assert_true(g_file_set_contents(bios, (const char *)code,
                                      sizeof(code), NULL));
    QTestState *q = qtest_initf(
        "-M kmh-bosc-soc,die-mask=0xf,core-mask=0x1,iommu-sys=on "
        "-smp 64,maxcpus=68 -m 256M -accel tcg -S -bios %s", bios);
    qtest_qmp_assert_success(q, "{'execute':'cont'}");
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    for (unsigned root = 0; root < 12; root++) {
        unsigned die = root / 3;
        uint64_t result = global(root, 0x1100200 + (root % 3) * 0x100 +
                                die * 16 * 4);
        while (!qtest_readl(q, result) && g_get_monotonic_time() < deadline) {
            g_usleep(1000);
        }
        g_assert_cmphex(qtest_readl(q, result), ==, die + 17);
        g_assert_cmphex(qtest_readl(q, mapper(root) + BOSC_DID_MAPPER_DID_BASE),
                        ==, die + 17);
    }
    qtest_qmp_assert_success(q, "{'execute':'stop'}");
    qtest_quit(q);
    unlink(bios);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_data_func("/bosc-did-mapper/reset-single", GINT_TO_POINTER(1),
                        test_reset);
    qtest_add_data_func("/bosc-did-mapper/reset-multi", GINT_TO_POINTER(0),
                        test_reset);
    qtest_add_data_func("/bosc-did-mapper/reset-no-bypass", GINT_TO_POINTER(2),
                        test_reset);
    qtest_add_func("/bosc-did-mapper/ranges", test_ranges);
    qtest_add_func("/bosc-did-mapper/cache-update", test_cache_update);
    qtest_add_func("/bosc-did-mapper/fault-recovery", test_fault_recovery);
    qtest_add_func("/bosc-did-mapper/all-roots", test_all_roots);
    qtest_add_func("/bosc-did-mapper/bypass", test_bypass);
    qtest_add_func("/bosc-did-mapper/boundary", test_boundary);
    qtest_add_func("/bosc-did-mapper/pid-pv", test_pid_pv);
    qtest_add_func("/bosc-did-mapper/imsi", test_imsi);
    qtest_add_func("/bosc-did-mapper/cpu-local", test_cpu_local);
    return g_test_run();
}
