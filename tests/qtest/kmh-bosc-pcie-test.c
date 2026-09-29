/*
 * KmhBosc PCIe AXI width and integrated MSI receiver.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"
#include "qobject/qdict.h"
#include "qemu/bitops.h"
#include "qemu/units.h"
#include "hw/riscv/riscv-iommu-bits.h"

#define DBI 0x4f00000000ULL
#define ECAM 0x4800000000ULL
#define WINDOW 0x48000000000ULL
#define DOORBELL 0x81000000ULL
#define ENABLE 0x828
#define MASK 0x82c
#define STATUS 0x830

static uint64_t ga(unsigned die, uint64_t offset)
{
    return ((uint64_t)(die + 1) << 44) | offset;
}

static void reg64(QTestState *q, uint64_t reg, uint64_t val)
{
    qtest_writel(q, reg, val);
    qtest_writel(q, reg + 4, val >> 32);
}

static void setup_edu(QTestState *q, unsigned die)
{
    uint64_t dbi = ga(die, DBI);
    uint64_t ep = ga(die, ECAM) + 0x100000;

    qtest_writew(q, dbi + 4, 6);
    qtest_writel(q, dbi + 0x18, 0x00010100);
    qtest_writel(q, dbi + 0x20, 0x50005000);
    qtest_writel(q, dbi + 0x900, 0);
    qtest_writel(q, dbi + 0x904, 0);
    reg64(q, dbi + 0x90c, ga(die, WINDOW));
    qtest_writel(q, dbi + 0x914, (ga(die, WINDOW) + MiB - 1));
    reg64(q, dbi + 0x918, 0x50000000);
    qtest_writel(q, dbi + 0x908, 0x80000000);
    g_assert_cmphex(qtest_readl(q, ep), ==, 0x11e81234);
    qtest_writel(q, ep + 0x10, 0x50000000);
    qtest_writew(q, ep + 4, 6);
    g_assert_cmphex(qtest_readl(q, ga(die, WINDOW)), ==, 0x010000ed);
}

static void send_msi(QTestState *q, unsigned die, uint64_t addr,
                     unsigned vector)
{
    uint64_t ep = ga(die, ECAM) + 0x100000;
    unsigned cap = qtest_readb(q, ep + 0x34);

    g_assert_cmphex(qtest_readb(q, ep + cap), ==, 5);
    reg64(q, ep + cap + 4, addr);
    qtest_writew(q, ep + cap + 12, vector);
    qtest_writew(q, ep + cap + 2, 0x81);
    qtest_writel(q, ga(die, WINDOW) + 0x60, 1);
    qtest_writel(q, ga(die, WINDOW) + 0x64, 1);
}

static void edu_dma(QTestState *q, unsigned die, uint64_t src, uint64_t dst,
                    unsigned size, bool to_pci)
{
    uint64_t bar = ga(die, WINDOW);

    qtest_writeq(q, bar + 0x80, src);
    qtest_writeq(q, bar + 0x88, dst);
    qtest_writeq(q, bar + 0x90, size);
    qtest_writeq(q, bar + 0x98, to_pci ? 3 : 1);
    qtest_clock_step(q, 100000000);
    g_assert_cmphex(qtest_readq(q, bar + 0x98) & 1, ==, 0);
}

static void copy_dma(QTestState *q, unsigned die, uint64_t src, uint64_t dst)
{
    edu_dma(q, die, src, 0x40000, 4, false);
    edu_dma(q, die, 0x40000, dst, 4, true);
}

static void test_pcie(const void *opaque)
{
    bool iommu = (uintptr_t)opaque;
    QTestState *q = qtest_initf(
        "-M kmh-bosc-soc,die-mask=0xf,core-mask=0x1,dw-pcie=on,"
        "generated-dtb=on,iommu-sys=%s -smp 64,maxcpus=68 -m 256M "
        "-device edu,bus=pcie-d0-p0,dma_mask=0xffffffffffffffff "
        "-device edu,bus=pcie-d1-p0,dma_mask=0xffffffffffffffff "
        "-device edu,bus=pcie-d2-p0,dma_mask=0xffffffffffffffff "
        "-device edu,bus=pcie-d3-p0,dma_mask=0xffffffffffffffff",
        iommu ? "on" : "off");

    if (iommu) {
        /* Start Off before any context has been cached. */
        qtest_writeq(q, ga(0, 0x4f80200010ULL), 0);
    }
    for (unsigned die = 0; die < 4; die++) {
        uint64_t dbi = ga(die, DBI);
        g_autofree char *path = g_strdup_printf("/machine/pcie-d%u-p0", die);

        setup_edu(q, die);
        if (die == 0) {
            qtest_irq_intercept_out_named(q, path, "sysbus-irq");
        }
        reg64(q, dbi + 0x820, DOORBELL);
        for (unsigned bank = 0; bank < 8; bank++) {
            qtest_writel(q, dbi + ENABLE + bank * 12, UINT32_MAX);
        }
        qtest_writel(q, DOORBELL, 0x12345678);
        for (unsigned v = 0; v < 256; v++) {
            uint64_t status = dbi + STATUS + (v / 32) * 12;
            uint32_t bit = 1U << (v % 32);

            send_msi(q, die, DOORBELL, v);
            g_assert_cmphex(qtest_readl(q, status), ==, bit);
            g_assert_true(die != 0 || qtest_get_irq(q, 4));
            qtest_writel(q, status, bit);
            g_assert_false(die == 0 && qtest_get_irq(q, 4));
            qtest_writel(q, status, bit); /* W1C is idempotent. */
            g_assert_cmphex(qtest_readl(q, status), ==, 0);
            g_assert_cmphex(qtest_readl(q, DOORBELL), ==, 0x12345678);
        }
        /* Pending in one bank must survive clearing a different bank. */
        send_msi(q, die, DOORBELL, 31);
        send_msi(q, die, DOORBELL, 255);
        qtest_writel(q, dbi + STATUS, 1U << 31);
        g_assert_true(die != 0 || qtest_get_irq(q, 4));
        qtest_writel(q, dbi + MASK + 7 * 12, UINT32_MAX);
        g_assert_false(die == 0 && qtest_get_irq(q, 4));
        qtest_writel(q, dbi + MASK + 7 * 12, 0);
        g_assert_true(die != 0 || qtest_get_irq(q, 4));
        qtest_writel(q, dbi + ENABLE + 7 * 12, 0);
        g_assert_false(die == 0 && qtest_get_irq(q, 4));
        qtest_writel(q, dbi + ENABLE + 7 * 12, UINT32_MAX);
        g_assert_true(die != 0 || qtest_get_irq(q, 4));
        qtest_writel(q, dbi + STATUS + 7 * 12, UINT32_MAX);
        send_msi(q, die, DOORBELL, 256);
        g_assert_false(die == 0 && qtest_get_irq(q, 4));

        /* Bank zero need not be enabled for bank seven to receive MSI. */
        for (unsigned bank = 0; bank < 7; bank++) {
            qtest_writel(q, dbi + ENABLE + bank * 12, 0);
        }
        send_msi(q, die, DOORBELL, 255);
        g_assert_cmphex(qtest_readl(q, dbi + STATUS + 7 * 12), ==, 1U << 31);
        qtest_writel(q, dbi + STATUS + 7 * 12, UINT32_MAX);

        /* 64-bit receiver match precedes truncation; DDR is not written. */
        reg64(q, dbi + 0x820, DOORBELL | (2ULL << 44));
        send_msi(q, die, DOORBELL | (2ULL << 44), 255);
        g_assert_true(die != 0 || qtest_get_irq(q, 4));
        g_assert_cmphex(qtest_readl(q, DOORBELL), ==, 0x12345678);
        qtest_writel(q, dbi + STATUS + 7 * 12, UINT32_MAX);
        reg64(q, dbi + 0x820, DOORBELL);

        if (iommu && die == 0) {
            /* Off blocks DMA while iMSI-RX still works upstream. */
            qtest_writel(q, DOORBELL + 0x200, 0x87654321);
            edu_dma(q, die, 0x40000, DOORBELL + 0x200, 4, true);
            g_assert_cmphex(qtest_readl(q, DOORBELL + 0x200), ==, 0x87654321);
            send_msi(q, die, DOORBELL, 255);
            g_assert_cmphex(qtest_readl(q, dbi + STATUS + 7 * 12), ==,
                            1U << 31);
            qtest_writel(q, dbi + STATUS + 7 * 12, UINT32_MAX);
            /* No valid context was cached in Off; enable Bare for DMA. */
            qtest_writeq(q, ga(0, 0x4f80200010ULL), 1);
        }

        /* Reads of the MSI address remain DMA, even while enabled. */
        copy_dma(q, die, DOORBELL, DOORBELL + 0x100);
        g_assert_cmphex(qtest_readl(q, DOORBELL + 0x100), ==, 0x12345678);

        /* Moving the receiver releases the old address back to ordinary DMA. */
        reg64(q, dbi + 0x820, DOORBELL + 0x20);
        qtest_writel(q, DOORBELL + 0x20, 0x87654321);
        send_msi(q, die, DOORBELL, 255);
        g_assert_cmphex(qtest_readl(q, DOORBELL), ==, 255);
        g_assert_cmphex(qtest_readl(q, dbi + STATUS + 7 * 12), ==, 0);
        send_msi(q, die, DOORBELL + 0x20, 255);
        g_assert_cmphex(qtest_readl(q, DOORBELL + 0x20), ==, 0x87654321);
        g_assert_cmphex(qtest_readl(q, dbi + STATUS + 7 * 12), ==, 1U << 31);
        qtest_writel(q, dbi + STATUS + 7 * 12, UINT32_MAX);

        /* Disabling all banks releases the current address too. */
        qtest_writel(q, dbi + ENABLE + 7 * 12, 0);
        send_msi(q, die, DOORBELL + 0x20, 255);
        g_assert_cmphex(qtest_readl(q, DOORBELL + 0x20), ==, 255);
        g_assert_cmphex(qtest_readl(q, dbi + STATUS + 7 * 12), ==, 0);
        reg64(q, dbi + 0x820, DOORBELL);
        qtest_writel(q, DOORBELL, 0x12345678);
        qtest_writel(q, dbi + ENABLE + 7 * 12, UINT32_MAX);
        send_msi(q, die, DOORBELL, 255);
        g_assert_cmphex(qtest_readl(q, dbi + STATUS + 7 * 12), ==, 1U << 31);
        g_assert_cmphex(qtest_readl(q, DOORBELL), ==, 0x12345678);
        qtest_writel(q, dbi + STATUS + 7 * 12, UINT32_MAX);

        /* Nonmatching high address drops bits [63:44] on reads and writes. */
        copy_dma(q, die, DOORBELL | (0xfffffULL << 44),
                 (DOORBELL + 0x104) | (0xfffffULL << 44));
        g_assert_cmphex(qtest_readl(q, DOORBELL + 0x104), ==, 0x12345678);

        if (!iommu) {
            /* Encoded foreign SRAM becomes requester-local after truncation. */
            for (unsigned d = 0; d < 4; d++) {
                qtest_writel(q, ga(d, 0x1100000), 0x55550000 + d);
                qtest_writel(q, ga(d, 0x1100004), 0);
            }
            copy_dma(q, die, ga((die + 1) % 4, 0x1100000),
                     ga((die + 2) % 4, 0x1100004));
            for (unsigned d = 0; d < 4; d++) {
                g_assert_cmphex(qtest_readl(q, ga(d, 0x1100004)), ==,
                                d == die ? 0x55550000 + die : 0);
            }
        }
        /* DDR still uses global slots, including memory on another die. */
        for (unsigned d = 0; d < 4; d++) {
            uint64_t ddr = 0x80000000ULL + d * 32 * GiB;

            qtest_writel(q, ddr, 0xaaaa0000 + d);
            copy_dma(q, die, ddr, ddr + 4);
            g_assert_cmphex(qtest_readl(q, ddr + 4), ==, 0xaaaa0000 + d);
        }
        for (unsigned d = 0; d < 4; d++) {
            for (unsigned bank = 0; bank < 8; bank++) {
                g_assert_cmphex(qtest_readl(q, ga(d, DBI) + STATUS + bank * 12),
                                ==, 0);
            }
        }
    }
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    for (unsigned d = 0; d < 4; d++) {
        for (unsigned bank = 0; bank < 8; bank++) {
            g_assert_cmphex(qtest_readl(q, ga(d, DBI) + ENABLE + bank * 12),
                            ==, 0);
        }
    }
    qtest_quit(q);
}

/* Another DWC user must retain full-width DMA and the default 32 vectors. */
static void test_generic_dwc(void)
{
    QTestState *q = qtest_init(
        "-M xiangshan-kunminghu,dw-pcie=on,generated-dtb=on "
        "-smp 1 -m 256M -device edu,bus=dw-pcie,dma_mask=0xffffffffffffffff");
    uint64_t dbi = 0x32000000;
    uint64_t cfg = 0x67ff0000;
    uint64_t bar = 0x60000000;
    uint64_t high_msi = DOORBELL | (2ULL << 44);
    unsigned cap;

    qtest_writew(q, dbi + 4, 6);
    qtest_writel(q, dbi + 0x18, 0x00010100);
    qtest_writel(q, dbi + 0x20, 0x40004000);
    qtest_writel(q, dbi + 0x900, 0);
    qtest_writel(q, dbi + 0x904, 0);
    reg64(q, dbi + 0x90c, bar);
    qtest_writel(q, dbi + 0x914, bar + MiB - 1);
    reg64(q, dbi + 0x918, 0x40000000);
    qtest_writel(q, dbi + 0x908, 0x80000000);
    qtest_writel(q, dbi + 0x900, 1);
    qtest_writel(q, dbi + 0x904, 4);
    reg64(q, dbi + 0x90c, cfg);
    qtest_writel(q, dbi + 0x914, cfg + 0xffff);
    reg64(q, dbi + 0x918, 1U << 24);
    qtest_writel(q, dbi + 0x908, 0x80000000);
    g_assert_cmphex(qtest_readl(q, cfg), ==, 0x11e81234);
    qtest_writel(q, cfg + 0x10, 0x40000000);
    qtest_writew(q, cfg + 4, 6);
    g_assert_cmphex(qtest_readl(q, bar), ==, 0x010000ed);
    cap = qtest_readb(q, cfg + 0x34);
    {
        QDict *rsp = qtest_qmp(q, "{'execute':'qom-get', 'arguments':"
                              "{'path':'/machine/pcie0',"
                              "'property':'num-msi-vectors'}}");
        g_assert_cmpint(qdict_get_int(rsp, "return"), ==, 32);
        qobject_unref(rsp);
    }
    /* This machine routes external MSI directly to system memory. */
    reg64(q, dbi + 0x820, DOORBELL);
    qtest_writel(q, dbi + ENABLE, UINT32_MAX);
    reg64(q, cfg + cap + 4, DOORBELL);
    qtest_writew(q, cfg + cap + 2, 0x81);
    qtest_writew(q, cfg + cap + 12, 31);
    qtest_writel(q, bar + 0x60, 1);
    g_assert_cmphex(qtest_readl(q, DOORBELL), ==, 31);
    g_assert_cmphex(qtest_readl(q, dbi + STATUS), ==, 0);
    reg64(q, cfg + cap + 4, high_msi);
    qtest_writew(q, cfg + cap + 12, 32);
    qtest_writel(q, bar + 0x60, 1);
    g_assert_cmphex(qtest_readl(q, DOORBELL), ==, 31);

    /* A high address cannot alias the low DDR location on this machine. */
    qtest_writel(q, DOORBELL, 0x12345678);
    qtest_writel(q, DOORBELL + 0x1000, 0x12345678);
    qtest_writeq(q, bar + 0x80, high_msi + 0x1000);
    qtest_writeq(q, bar + 0x88, 0x40000);
    qtest_writeq(q, bar + 0x90, 4);
    qtest_writeq(q, bar + 0x98, 1);
    qtest_clock_step(q, 100000000);
    qtest_writeq(q, bar + 0x80, 0x40000);
    qtest_writeq(q, bar + 0x88, DOORBELL);
    qtest_writeq(q, bar + 0x98, 3);
    qtest_clock_step(q, 100000000);
    g_assert_cmphex(qtest_readl(q, DOORBELL), ==, 0);
    qtest_quit(q);
}

/* Identical RIDs and IOVAs on different roots must use distinct contexts. */
static void test_iommu_root_contexts(void)
{
    QTestState *q = qtest_init(
        "-M kmh-bosc-soc,die-mask=0xf,core-mask=0x1,dw-pcie=on,"
        "generated-dtb=on,iommu-sys=on -smp 64,maxcpus=68 -m 256M "
        "-device edu,bus=pcie-d0-p0,dma_mask=0xffffffffffffffff "
        "-device edu,bus=pcie-d1-p0,dma_mask=0xffffffffffffffff "
        "-device edu,bus=pcie-d2-p0,dma_mask=0xffffffffffffffff "
        "-device edu,bus=pcie-d3-p0,dma_mask=0xffffffffffffffff");
    const uint64_t ddt = 0x82000000;
    const uint64_t iova = 0x1000;

    /* Fresh zeroed RAM, extended (64-byte) device contexts, three-level DDT. */
    for (unsigned die = 0; die < 4; die++) {
        uint32_t did = ((die * 3) << 16) | 0x100;
        uint64_t dir = ddt + 0x1000 + die * 0x10000;
        uint64_t leaf = dir + 0x1000;
        uint64_t pt = dir + 0x2000;
        uint64_t pa = 0x83000000 + die * 0x1000;

        setup_edu(q, die);
        qtest_writeq(q, ddt + (did >> 15) * 8, (dir >> 2) | 1);
        qtest_writeq(q, dir + ((did >> 6) & 0x1ff) * 8,
                     (leaf >> 2) | 1);
        qtest_writeq(q, leaf, RISCV_IOMMU_DC_TC_V);
        /* A different PSCID also separates the final translation cache. */
        qtest_writeq(q, leaf + 16, (uint64_t)(die + 1) << 12);
        qtest_writeq(q, leaf + 24, (8ULL << 60) | (pt >> 12));
        qtest_writeq(q, pt, ((pt + 0x1000) >> 2) | 1);
        qtest_writeq(q, pt + 0x1000, ((pt + 0x2000) >> 2) | 1);
        qtest_writeq(q, pt + 0x2000 + 8, (pa >> 2) | 0xc7);
        qtest_writel(q, pa, 0xcafe0000 + die);
    }
    qtest_writeq(q, ga(0, 0x4f80200000ULL) + RISCV_IOMMU_REG_DDTP,
                 (ddt >> 2) | RISCV_IOMMU_DDTP_MODE_3LVL);
    for (unsigned die = 0; die < 4; die++) {
        copy_dma(q, die, iova, iova + 4);
        for (unsigned d = 0; d < 4; d++) {
            g_assert_cmphex(qtest_readl(q, 0x83000004 + d * 0x1000), ==,
                            d <= die ? 0xcafe0000 + d : 0);
        }
    }
    qtest_quit(q);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_data_func("/kmh-bosc/pcie/imsi-axi44", (void *)0, test_pcie);
    qtest_add_data_func("/kmh-bosc/pcie/imsi-iommu", (void *)1, test_pcie);
    qtest_add_func("/kmh-bosc/pcie/iommu-root-contexts",
                   test_iommu_root_contexts);
    if (qtest_has_machine("xiangshan-kunminghu")) {
        qtest_add_func("/kmh-bosc/pcie/generic-dwc", test_generic_dwc);
    }
    return g_test_run();
}
