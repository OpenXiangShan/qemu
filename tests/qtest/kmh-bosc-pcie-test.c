/*
 * KmhBosc PCIe AXI width and integrated MSI receiver.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"
#include "qobject/qdict.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "qemu/units.h"
#include "hw/riscv/riscv-iommu-bits.h"

#define DBI 0x4f00000000ULL
#define ECAM 0x4800000000ULL
#define WINDOW 0x48000000000ULL
#define DOORBELL 0x81000000ULL
#define ENABLE 0x828
#define MASK 0x82c
#define STATUS 0x830
#define APP_OFFSET 0x400000
#define EP_INT_STA_REG0 0x80000
#define MSI_CTRL_INTSTA 0xff00

static bool aplic_input(QTestState *q, uint64_t aplic, unsigned source)
{
    return qtest_readl(q, aplic + 0x1d00 + (source / 32) * 4) &
           (1U << (source % 32));
}

static uint64_t ga(unsigned die, uint64_t offset)
{
    return ((uint64_t)(die + 1) << 44) | offset;
}

static void reg64(QTestState *q, uint64_t reg, uint64_t val)
{
    qtest_writel(q, reg, val);
    qtest_writel(q, reg + 4, val >> 32);
}

static void program_outbound(QTestState *q, uint64_t dbi, unsigned index,
                              uint32_t type, uint64_t base, uint64_t target)
{
    qtest_writel(q, dbi + 0x900, index);
    qtest_writel(q, dbi + 0x904, type);
    reg64(q, dbi + 0x90c, base);
    qtest_writel(q, dbi + 0x914, base + MiB - 1);
    reg64(q, dbi + 0x918, target);
    qtest_writel(q, dbi + 0x908, 0x80000000);
}

static void setup_bosc_edu(QTestState *q, uint64_t dbi, uint64_t ep,
                            uint64_t bar, uint64_t local_ecam,
                            uint64_t local_bar)
{
    qtest_writew(q, dbi + 4, 6);
    qtest_writel(q, dbi + 0x18, 0x00010100);
    qtest_writel(q, dbi + 0x20, 0x50005000);
    program_outbound(q, dbi, 0, 0, local_bar, 0x50000000);
    program_outbound(q, dbi, 1, 4, local_ecam + MiB, 1U << 24);
    g_assert_cmphex(qtest_readl(q, ep), ==, 0x11e81234);
    qtest_writel(q, ep + 0x10, 0x50000000);
    qtest_writew(q, ep + 4, 6);
    g_assert_cmphex(qtest_readl(q, bar), ==, 0x010000ed);
}

static void setup_edu(QTestState *q, unsigned die)
{
    setup_bosc_edu(q, ga(die, DBI), ga(die, ECAM) + MiB,
                   ga(die, WINDOW), ECAM, WINDOW);
}

/* The fabric routes first; each RC compares only the local CFG/MEM address. */
static void test_outbound_routing(const void *opaque)
{
    bool single = (uintptr_t)opaque;
    const uint64_t dbis[] = { DBI, 0x4f00800000, 0x4f01000000 };
    const uint64_t ecams[] = { ECAM, 0x4900000000, 0x4a00000000 };
    const uint64_t bars[] = { WINDOW, 0x4e000000000, 0x4f000000000 };
    unsigned roots = single ? 3 : 12;
    GString *args = g_string_new(single ?
        "-M kmh-bosc-soc,soc-mode=single,core-mask=0x1,dw-pcie=on,"
        "generated-dtb=on -smp 16,maxcpus=17 -m 256M" :
        "-M kmh-bosc-soc,soc-mode=multi,die-mask=0xf,core-mask=0x1,"
        "dw-pcie=on,generated-dtb=on -smp 64,maxcpus=68 -m 256M");
    QTestState *q;

    for (unsigned root = 0; root < roots; root++) {
        g_string_append_printf(args, " -device edu,bus=pcie-d%u-p%u",
                               root / 3, root % 3);
    }
    q = qtest_init(args->str);
    g_string_free(args, true);

    for (unsigned root = 0; root < roots; root++) {
        unsigned port = root % 3;
        uint64_t id = single ? 0 : (uint64_t)(root / 3 + 1) << 44;
        uint64_t dbi = id | dbis[port];
        uint64_t ep = (id | ecams[port]) + MiB;
        uint64_t bar = id | bars[port];

        setup_bosc_edu(q, dbi, ep, bar, ecams[port], bars[port]);
        qtest_writel(q, bar + 4, 0x12340000 + root);

        /* Disable/move CFG: no direct ECAM path may bypass the comparator. */
        qtest_writel(q, dbi + 0x900, 1);
        qtest_writel(q, dbi + 0x908, 0);
        g_assert_cmphex(qtest_readl(q, ep), !=, 0x11e81234);
        program_outbound(q, dbi, 1, 4, ecams[port] + 2 * MiB, 1U << 24);
        g_assert_cmphex(qtest_readl(q, ep), !=, 0x11e81234);
        g_assert_cmphex(qtest_readl(q, ep + MiB), ==, 0x11e81234);
        program_outbound(q, dbi, 1, 4, ecams[port] + MiB, 1U << 24);

        /* BASE still carrying a die ID must not match the RC-local input. */
        program_outbound(q, dbi, 0, 0, ga(0, bars[port]), 0x50000000);
        g_assert_cmphex(qtest_readl(q, bar), !=, 0x010000ed);
        program_outbound(q, dbi, 0, 0, bars[port], 0x50000000);
        qtest_writel(q, dbi + 0x908, 0);
        g_assert_cmphex(qtest_readl(q, bar), !=, 0x010000ed);
        qtest_writel(q, dbi + 0x908, 0x80000000);
    }
    for (unsigned root = 0; root < roots; root++) {
        uint64_t id = single ? 0 : (uint64_t)(root / 3 + 1) << 44;

        /* Identical per-port local windows retain distinct endpoint state. */
        g_assert_cmphex(qtest_readl(q, (id | bars[root % 3]) + 4), ==,
                        (uint32_t)~(0x12340000 + root));
    }
    qtest_quit(q);
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

static uint64_t imsic_eip0(QTestState *q, unsigned die, bool mmode, bool clear)
{
    uint64_t value = 0x80; /* EIP0 */
    unsigned select = mmode ? 0x350 : 0x150;

    qtest_csr_call(q, "set_csr", die * 16, select, &value);
    value = 0;
    qtest_csr_call(q, clear ? "set_csr" : "get_csr", die * 16,
                   select + 1, &value);
    return value;
}

/* PCIe MSI loses the target die ID and must still pass the system IOMMU. */
static void test_imsic_axi44(void)
{
    g_autofree char *bios = NULL;
    g_autofree char *bios_arg = NULL;
    uint32_t code = cpu_to_le32(0x10500073); /* WFI; CPUs remain stopped. */
    int fd = g_file_open_tmp("kmh-bosc-pcie-test-XXXXXX", &bios, NULL);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(bios, (const char *)&code,
                                      sizeof(code), NULL));
    bios_arg = g_shell_quote(bios);
    for (unsigned mode = 0; mode < 3; mode++) {
        QTestState *q = qtest_initf(
            "-M kmh-bosc-soc,soc-mode=multi,die-mask=0x3,"
            "core-mask=0x1:0x1:0:0,dw-pcie=on,generated-dtb=on,"
            "iommu-sys=%s,iommu-bypass=%s "
            "-smp 64,maxcpus=68 -m 256M -accel tcg -S -bios %s "
            "-device edu,bus=pcie-d0-p0,dma_mask=0xffffffffffffffff "
            "-device edu,bus=pcie-d1-p0,dma_mask=0xffffffffffffffff",
            mode ? "on" : "off", mode == 2 ? "off" : "on", bios_arg);

        for (unsigned die = 0; die < 2; die++) {
            setup_edu(q, die);
            if (mode == 2) {
                qtest_writeq(q, ga(die, 0x4f80200000ULL) +
                             RISCV_IOMMU_REG_DDTP, RISCV_IOMMU_DDTP_MODE_OFF);
            }
        }
        for (unsigned mmode = 0; mmode < 2; mmode++) {
            uint64_t local = mmode ? 0x1c000000 : 0x1d000000;

            for (unsigned source = 0; source < 2; source++) {
                for (unsigned target = 0; target < 3; target++) {
                    uint64_t address = target < 2 ? ga(target, local) : local;

                    for (unsigned die = 0; die < 2; die++) {
                        imsic_eip0(q, die, mmode, true);
                    }
                    send_msi(q, source, address, 33);
                    for (unsigned die = 0; die < 2; die++) {
                        uint64_t expected = mode != 2 && die == source ?
                                            1ULL << 33 : 0;

                        g_assert_cmphex(imsic_eip0(q, die, mmode, false),
                                        ==, expected);
                    }
                }
            }
        }
        qtest_quit(q);
    }
    unlink(bios);
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
        "generated-dtb=on,iommu-sys=%s,iommu-bypass=off "
        "-smp 64,maxcpus=68 -m 256M "
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
        g_autofree char *path =
            g_strdup_printf("/machine/kmh-bosc-pcie-d%u-p0-app", die);

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
            g_assert_true(die != 0 || qtest_get_irq(q, 0));
            qtest_writel(q, status, bit);
            /* Clearing DWC alone must leave the APP output asserted. */
            g_assert_true(die != 0 || qtest_get_irq(q, 0));
            g_assert_cmphex(qtest_readl(q, dbi + APP_OFFSET + EP_INT_STA_REG0) &
                            MSI_CTRL_INTSTA, ==, 1U << (8 + v / 32));
            qtest_writel(q, dbi + APP_OFFSET + EP_INT_STA_REG0,
                          1U << (8 + v / 32));
            g_assert_false(die == 0 && qtest_get_irq(q, 0));
            qtest_writel(q, status, bit); /* W1C is idempotent. */
            g_assert_cmphex(qtest_readl(q, status), ==, 0);
            g_assert_cmphex(qtest_readl(q, DOORBELL), ==,
                            iommu ? 0x12345678 : v);
        }
        /* Pending in one bank must survive clearing a different bank. */
        send_msi(q, die, DOORBELL, 31);
        send_msi(q, die, DOORBELL, 255);
        qtest_writel(q, dbi + STATUS, 1U << 31);
        qtest_writel(q, dbi + APP_OFFSET + EP_INT_STA_REG0, 1U << 8);
        g_assert_true(die != 0 || qtest_get_irq(q, 0));
        qtest_writel(q, dbi + MASK + 7 * 12, UINT32_MAX);
        g_assert_true(die != 0 || qtest_get_irq(q, 0));
        qtest_writel(q, dbi + APP_OFFSET + EP_INT_STA_REG0, 1U << 15);
        g_assert_false(die == 0 && qtest_get_irq(q, 0));
        qtest_writel(q, dbi + MASK + 7 * 12, 0);
        g_assert_true(die != 0 || qtest_get_irq(q, 0));
        qtest_writel(q, dbi + ENABLE + 7 * 12, 0);
        qtest_writel(q, dbi + APP_OFFSET + EP_INT_STA_REG0, 1U << 15);
        g_assert_false(die == 0 && qtest_get_irq(q, 0));
        qtest_writel(q, dbi + ENABLE + 7 * 12, UINT32_MAX);
        g_assert_true(die != 0 || qtest_get_irq(q, 0));
        qtest_writel(q, dbi + STATUS + 7 * 12, UINT32_MAX);
        qtest_writel(q, dbi + APP_OFFSET + EP_INT_STA_REG0, 1U << 15);
        send_msi(q, die, DOORBELL, 256);
        g_assert_false(die == 0 && qtest_get_irq(q, 0));

        /* Bank zero need not be enabled for bank seven to receive MSI. */
        for (unsigned bank = 0; bank < 7; bank++) {
            qtest_writel(q, dbi + ENABLE + bank * 12, 0);
        }
        send_msi(q, die, DOORBELL, 255);
        g_assert_cmphex(qtest_readl(q, dbi + STATUS + 7 * 12), ==, 1U << 31);
        qtest_writel(q, dbi + STATUS + 7 * 12, UINT32_MAX);
        qtest_writel(q, dbi + APP_OFFSET + EP_INT_STA_REG0, 1U << 15);

        /* Full-width receiver match precedes truncation and forwarding. */
        reg64(q, dbi + 0x820, DOORBELL | (2ULL << 44));
        send_msi(q, die, DOORBELL | (2ULL << 44), 255);
        g_assert_true(die != 0 || qtest_get_irq(q, 0));
        g_assert_cmphex(qtest_readl(q, DOORBELL), ==,
                        iommu ? 0x12345678 : 255);
        qtest_writel(q, dbi + STATUS + 7 * 12, UINT32_MAX);
        qtest_writel(q, dbi + APP_OFFSET + EP_INT_STA_REG0, 1U << 15);
        reg64(q, dbi + 0x820, DOORBELL);

        if (iommu) {
            /* Off blocks forwarding; the upstream monitor still fires. */
            qtest_writel(q, DOORBELL + 0x200, 0x87654321);
            edu_dma(q, die, 0x40000, DOORBELL + 0x200, 4, true);
            g_assert_cmphex(qtest_readl(q, DOORBELL + 0x200), ==, 0x87654321);
            send_msi(q, die, DOORBELL, 255);
            g_assert_cmphex(qtest_readl(q, dbi + STATUS + 7 * 12), ==,
                            1U << 31);
            g_assert_cmphex(qtest_readl(q, DOORBELL), ==, 0x12345678);
            qtest_writel(q, dbi + STATUS + 7 * 12, UINT32_MAX);
            qtest_writel(q, dbi + APP_OFFSET + EP_INT_STA_REG0, 1U << 15);
            /* No valid context was cached in Off; enable Bare for DMA. */
            qtest_writeq(q, ga(die, 0x4f80200010ULL), 1);
        }

        /* Bare/no-IOMMU forwards matched MSI writes to RAM as well. */
        send_msi(q, die, DOORBELL, 255);
        g_assert_cmphex(qtest_readl(q, DOORBELL), ==, 255);
        g_assert_cmphex(qtest_readl(q, dbi + STATUS + 7 * 12), ==, 1U << 31);
        qtest_writel(q, dbi + STATUS + 7 * 12, UINT32_MAX);
        qtest_writel(q, dbi + APP_OFFSET + EP_INT_STA_REG0, 1U << 15);

        /* Reads of the MSI address remain DMA, even while enabled. */
        qtest_writel(q, DOORBELL, 0x12345678);
        copy_dma(q, die, DOORBELL, DOORBELL + 0x100);
        g_assert_cmphex(qtest_readl(q, DOORBELL + 0x100), ==, 0x12345678);

        /* Moving the receiver releases the old address back to ordinary DMA. */
        reg64(q, dbi + 0x820, DOORBELL + 0x20);
        qtest_writel(q, DOORBELL + 0x20, 0x87654321);
        send_msi(q, die, DOORBELL, 255);
        g_assert_cmphex(qtest_readl(q, DOORBELL), ==, 255);
        g_assert_cmphex(qtest_readl(q, dbi + STATUS + 7 * 12), ==, 0);
        send_msi(q, die, DOORBELL + 0x20, 255);
        g_assert_cmphex(qtest_readl(q, DOORBELL + 0x20), ==, 255);
        g_assert_cmphex(qtest_readl(q, dbi + STATUS + 7 * 12), ==, 1U << 31);
        qtest_writel(q, dbi + STATUS + 7 * 12, UINT32_MAX);
        qtest_writel(q, dbi + APP_OFFSET + EP_INT_STA_REG0, 1U << 15);

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
        g_assert_cmphex(qtest_readl(q, DOORBELL), ==, 255);
        qtest_writel(q, dbi + STATUS + 7 * 12, UINT32_MAX);
        qtest_writel(q, dbi + APP_OFFSET + EP_INT_STA_REG0, 1U << 15);
        qtest_writel(q, DOORBELL, 0x12345678);

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

static void setup_kmh_edu(QTestState *q, unsigned port)
{
    uint64_t dbi = port ? 0x40000000 : 0x32000000;
    uint64_t cfg = 0x67ff0000 + port * 0x10000000;
    uint64_t bar = 0x60000000 + port * 0x10000000;

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

    setup_kmh_edu(q, 0);
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
        "generated-dtb=on,iommu-sys=on,iommu-bypass=off "
        "-smp 64,maxcpus=68 -m 256M "
        "-device edu,bus=pcie-d0-p0,dma_mask=0xffffffffffffffff "
        "-device edu,bus=pcie-d1-p0,dma_mask=0xffffffffffffffff "
        "-device edu,bus=pcie-d2-p0,dma_mask=0xffffffffffffffff "
        "-device edu,bus=pcie-d3-p0,dma_mask=0xffffffffffffffff");
    const uint64_t iova = 0x1000;
    const uint32_t did = 0x100; /* per-die IOMMU uses plain RID */

    for (unsigned die = 0; die < 4; die++) {
        uint64_t ddt = 0x82000000 + die * 0x10000;
        uint64_t dir = ddt + 0x1000;
        uint64_t leaf = dir + 0x1000;
        uint64_t pt = dir + 0x2000;
        uint64_t pa = 0x83000000 + die * 0x1000;
        uint64_t iommu = ga(die, 0x4f80200000ULL);

        setup_edu(q, die);
        qtest_writeq(q, ddt + (did >> 15) * 8, (dir >> 2) | 1);
        qtest_writeq(q, dir + ((did >> 6) & 0x1ff) * 8, (leaf >> 2) | 1);
        qtest_writeq(q, leaf, RISCV_IOMMU_DC_TC_V);
        qtest_writeq(q, leaf + 16, 1ULL << 12);
        qtest_writeq(q, leaf + 24, (8ULL << 60) | (pt >> 12));
        qtest_writeq(q, pt, ((pt + 0x1000) >> 2) | 1);
        qtest_writeq(q, pt + 0x1000, ((pt + 0x2000) >> 2) | 1);
        qtest_writeq(q, pt + 0x2000 + 8, (pa >> 2) | 0xc7);
        qtest_writel(q, pa, 0xcafe0000 + die);
        qtest_writeq(q, iommu + RISCV_IOMMU_REG_DDTP,
                     (ddt >> 2) | RISCV_IOMMU_DDTP_MODE_3LVL);
    }
    for (unsigned die = 0; die < 4; die++) {
        copy_dma(q, die, iova, iova + 4);
        for (unsigned d = 0; d < 4; d++) {
            g_assert_cmphex(qtest_readl(q, 0x83000004 + d * 0x1000), ==,
                            d <= die ? 0xcafe0000 + d : 0);
        }
    }
    qtest_quit(q);
}

/* The same RID, PSCID and IOVA must be independent across KMH IOMMUs. */
static void test_kmh_iommu_root_contexts(void)
{
    QTestState *q = qtest_init(
        "-M xiangshan-kunminghu,dw-pcie=on,generated-dtb=on,iommu-sys=on "
        "-smp 1 -m 256M "
        "-device edu,bus=dw-pcie,dma_mask=0xffffffffffffffff "
        "-device edu,bus=dw-pcie1,dma_mask=0xffffffffffffffff");

    for (unsigned port = 0; port < 2; port++) {
        uint64_t ddt = 0x82000000 + port * 0x10000;
        uint64_t dir = ddt + 0x1000;
        uint64_t leaf = dir + 0x1000;
        uint64_t pt = dir + 0x2000;
        uint64_t pa = 0x83000000 + port * 0x1000;
        const uint32_t did = 0x100;

        setup_kmh_edu(q, port);
        qtest_writeq(q, ddt + (did >> 15) * 8, (dir >> 2) | 1);
        qtest_writeq(q, dir + ((did >> 6) & 0x1ff) * 8, (leaf >> 2) | 1);
        qtest_writeq(q, leaf, RISCV_IOMMU_DC_TC_V);
        qtest_writeq(q, leaf + 16, 1ULL << 12);
        qtest_writeq(q, leaf + 24, (8ULL << 60) | (pt >> 12));
        qtest_writeq(q, pt, ((pt + 0x1000) >> 2) | 1);
        qtest_writeq(q, pt + 0x1000, ((pt + 0x2000) >> 2) | 1);
        qtest_writeq(q, pt + 0x2000 + 8, (pa >> 2) | 0xc7);
        qtest_writel(q, pa, 0xcafe0000 + port);
        qtest_writeq(q, 0x311f0000 + port * 0x1000 + RISCV_IOMMU_REG_DDTP,
                     (ddt >> 2) | RISCV_IOMMU_DDTP_MODE_3LVL);
    }
    for (unsigned port = 0; port < 2; port++) {
        uint64_t bar = 0x60000000 + port * 0x10000000;

        qtest_writeq(q, bar + 0x80, 0x1000);
        qtest_writeq(q, bar + 0x88, 0x40000);
        qtest_writeq(q, bar + 0x90, 4);
        qtest_writeq(q, bar + 0x98, 1);
        qtest_clock_step(q, 100000000);
        g_assert_cmphex(qtest_readq(q, bar + 0x98) & 1, ==, 0);
        qtest_writeq(q, bar + 0x80, 0x40000);
        qtest_writeq(q, bar + 0x88, 0x1004);
        qtest_writeq(q, bar + 0x98, 3);
        qtest_clock_step(q, 100000000);
        g_assert_cmphex(qtest_readq(q, bar + 0x98) & 1, ==, 0);
        for (unsigned p = 0; p < 2; p++) {
            g_assert_cmphex(qtest_readl(q, 0x83000004 + p * 0x1000), ==,
                            p <= port ? 0xcafe0000 + p : 0);
        }
    }
    qtest_quit(q);
}

/* Check DC legality on real PCIe DMA, including Off with a nonzero PPN. */
static void test_iommu_msi_mode(const void *opaque)
{
    bool bosc = (uintptr_t)opaque;
    const uint64_t ddt = 0x82000000;
    const uint64_t dir = ddt + 0x1000;
    const uint64_t dc = ddt + 0x2000;
    const uint64_t pt = ddt + 0x4000;
    const uint64_t fq = ddt + 0x8000;
    const uint64_t pa = 0x83000000;
    const uint64_t iommu = bosc ? ga(0, 0x4f80200000ULL) : 0x311f0000;
    const uint64_t bar = bosc ? ga(0, WINDOW) : 0x60000000;

    for (unsigned gstage = 0; gstage < 2; gstage++) {
        for (unsigned flat = 0; flat < 2; flat++) {
            QTestState *q = qtest_initf(
                "-M %s,dw-pcie=on,generated-dtb=on,iommu-sys=on "
                "-smp %s -m 256M -device edu,bus=%s,"
                "dma_mask=0xffffffffffffffff",
                bosc ? "kmh-bosc-soc,die-mask=0xf,core-mask=0x1,"
                       "iommu-bypass=off" :
                       "xiangshan-kunminghu",
                bosc ? "64,maxcpus=68" : "1",
                bosc ? "pcie-d0-p0" : "dw-pcie");

            if (bosc) {
                setup_edu(q, 0);
            } else {
                setup_kmh_edu(q, 0);
            }
            /* DID 0x100, extended DC. Identity Sv39/Sv39x4 1-GiB leaf. */
            qtest_writeq(q, ddt, (dir >> 2) | 1);
            qtest_writeq(q, dir + 4 * 8, (dc >> 2) | 1);
            qtest_writeq(q, dc, RISCV_IOMMU_DC_TC_V);
            qtest_writeq(q, dc + (gstage ? 8 : 24),
                         (8ULL << 60) | (pt >> 12));
            qtest_writeq(q, pt + 2 * 8, (0x80000000ULL >> 2) | 0xd7);
            qtest_writeq(q, dc + 32,
                         ((uint64_t)flat << 60) | ((ddt + 0xc000) >> 12));
            qtest_writeq(q, iommu + RISCV_IOMMU_REG_FQB, (fq >> 2) | 2);
            qtest_writel(q, iommu + RISCV_IOMMU_REG_FQCSR,
                         RISCV_IOMMU_FQCSR_FQEN);
            g_assert_true(qtest_readl(q, iommu + RISCV_IOMMU_REG_FQCSR) &
                          RISCV_IOMMU_FQCSR_FQON);
            qtest_writeq(q, iommu + RISCV_IOMMU_REG_DDTP,
                         (ddt >> 2) | RISCV_IOMMU_DDTP_MODE_3LVL);

            qtest_writel(q, pa, 0x12345678);
            /* EDU's initially zeroed buffer attempts to overwrite the marker. */
            qtest_writeq(q, bar + 0x80, 0x40000);
            qtest_writeq(q, bar + 0x88, pa);
            qtest_writeq(q, bar + 0x90, 4);
            qtest_writeq(q, bar + 0x98, 3);
            qtest_clock_step(q, 100000000);
            g_assert_cmphex(qtest_readq(q, bar + 0x98) & 1, ==, 0);

            if (!gstage && flat) {
                uint64_t fault = qtest_readq(q, fq);

                g_assert_cmphex(qtest_readl(q, pa), ==, 0x12345678);
                g_assert_cmpuint(qtest_readl(q, iommu + RISCV_IOMMU_REG_FQT),
                                 ==, 1);
                g_assert_cmpuint(fault & RISCV_IOMMU_FQ_HDR_CAUSE, ==,
                                 RISCV_IOMMU_FQ_CAUSE_DDT_MISCONFIGURED);
                g_assert_cmphex(fault >> 40, ==, 0x100);
            } else {
                g_assert_cmphex(qtest_readl(q, pa), ==, 0);
                g_assert_cmpuint(qtest_readl(q, iommu + RISCV_IOMMU_REG_FQT),
                                 ==, 0);
            }
            qtest_quit(q);
        }
    }
}

/* Check the board wires, not just the DWC sysbus outputs. */
static void test_hardware_irq_table(void)
{
    const uint64_t dbis[] = { DBI, 0x4f00800000, 0x4f01000000 };
    const uint64_t ecams[] = { ECAM, 0x4900000000, 0x4a00000000 };
    const uint64_t bars[] = { WINDOW, 0x4e000000000, 0x4f000000000 };
    GString *args = g_string_new(
        "-M kmh-bosc-soc,die-mask=0xf,core-mask=0x1,dw-pcie=on,"
        "generated-dtb=on,iommu-sys=on -smp 64,maxcpus=68 -m 256M");
    QTestState *q;

    for (unsigned die = 0; die < 4; die++) {
        for (unsigned port = 0; port < 3; port++) {
            g_string_append_printf(args, " -device edu,bus=pcie-d%u-p%u",
                                   die, port);
        }
    }
    q = qtest_init(args->str);
    g_string_free(args, true);

    for (unsigned die = 0; die < 4; die++) {
        uint64_t aplic = ga(die, 0x1e020000);
        uint64_t uart = ga(die, 0x04000000);

        qtest_writel(q, aplic + 32 * 4, 6);
        qtest_writeb(q, uart + 4, 2); /* THRE interrupt enable. */
        g_assert_true(aplic_input(q, aplic, 32));
        qtest_writeb(q, uart + 4, 0);
        g_assert_false(aplic_input(q, aplic, 32));

        for (unsigned port = 0; port < 3; port++) {
            uint64_t dbi = ga(die, dbis[port]);
            uint64_t ep = ga(die, ecams[port]) + MiB;
            uint64_t bar = ga(die, bars[port]);
            unsigned source = 76 + port * 6;
            unsigned cap;

            setup_bosc_edu(q, dbi, ep, bar, ecams[port], bars[port]);
            qtest_writel(q, aplic + source * 4, 6);
            qtest_writel(q, bar + 0x60, 1);
            g_assert_true(aplic_input(q, aplic, source));
            /* Live INTx bits are read-only, event bits are W1C. */
            g_assert_cmphex(qtest_readl(q, dbi + APP_OFFSET + EP_INT_STA_REG0),
                            ==, 0x10001);
            qtest_writel(q, dbi + APP_OFFSET + EP_INT_STA_REG0, UINT32_MAX);
            g_assert_cmphex(qtest_readl(q, dbi + APP_OFFSET + EP_INT_STA_REG0),
                            ==, 0x10000);
            g_assert_true(aplic_input(q, aplic, source));
            qtest_writel(q, bar + 0x64, 1);
            g_assert_false(aplic_input(q, aplic, source));
            g_assert_cmphex(qtest_readl(q, dbi + APP_OFFSET + EP_INT_STA_REG0),
                            ==, 0x10);
            qtest_writel(q, dbi + APP_OFFSET + EP_INT_STA_REG0, 0xff);

            reg64(q, dbi + 0x820, DOORBELL);
            qtest_writel(q, dbi + ENABLE, 1);
            cap = qtest_readb(q, ep + 0x34);
            reg64(q, ep + cap + 4, DOORBELL);
            qtest_writew(q, ep + cap + 12, 0);
            qtest_writew(q, ep + cap + 2, 0x81);
            qtest_writel(q, bar + 0x60, 1);
            g_assert_true(aplic_input(q, aplic, source));
            qtest_writel(q, bar + 0x64, 1);
            g_assert_true(aplic_input(q, aplic, source));
            qtest_writel(q, dbi + STATUS, 1);
            g_assert_cmphex(qtest_readl(q, dbi + STATUS), ==, 0);
            g_assert_true(aplic_input(q, aplic, source));
            /* The APP latch is at +0x80000, not a nearby address. */
            qtest_writel(q, dbi + APP_OFFSET + 0x8000, 1U << 8);
            g_assert_cmphex(qtest_readl(q, dbi + APP_OFFSET + 0x8000), ==, 0);
            g_assert_true(aplic_input(q, aplic, source));
            g_assert_cmphex(qtest_readl(q, dbi + APP_OFFSET + 0x80000) &
                            MSI_CTRL_INTSTA, ==, 1U << 8);
            qtest_writel(q, dbi + APP_OFFSET + EP_INT_STA_REG0, 1U << 9);
            g_assert_true(aplic_input(q, aplic, source));
            qtest_writel(q, dbi + APP_OFFSET + EP_INT_STA_REG0, 1U << 8);
            g_assert_false(aplic_input(q, aplic, source));

            /* APP-first cannot hide an active vector, nor its later latch. */
            qtest_writel(q, bar + 0x60, 1);
            qtest_writel(q, bar + 0x64, 1);
            qtest_writel(q, dbi + APP_OFFSET + EP_INT_STA_REG0, 1U << 8);
            g_assert_cmphex(qtest_readl(q, dbi + APP_OFFSET + EP_INT_STA_REG0) &
                            MSI_CTRL_INTSTA, ==, 1U << 8);
            g_assert_true(aplic_input(q, aplic, source));
            qtest_writel(q, dbi + STATUS, 1);
            g_assert_true(aplic_input(q, aplic, source));
            qtest_writel(q, dbi + APP_OFFSET + EP_INT_STA_REG0, 1U << 8);
            g_assert_false(aplic_input(q, aplic, source));
        }

        for (unsigned source = 43; source <= 63; source++) {
            g_assert_false(aplic_input(q, aplic, source));
        }
    }
    qtest_quit(q);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/kmh-bosc/pcie/hardware-irq-table",
                   test_hardware_irq_table);
    qtest_add_data_func("/kmh-bosc/pcie/outbound-multi", (void *)0,
                        test_outbound_routing);
    qtest_add_data_func("/kmh-bosc/pcie/outbound-single", (void *)1,
                        test_outbound_routing);
    qtest_add_func("/kmh-bosc/pcie/imsic-axi44", test_imsic_axi44);
    qtest_add_data_func("/kmh-bosc/pcie/imsi-axi44", (void *)0, test_pcie);
    qtest_add_data_func("/kmh-bosc/pcie/imsi-iommu", (void *)1, test_pcie);
    qtest_add_func("/kmh-bosc/pcie/iommu-root-contexts",
                   test_iommu_root_contexts);
    qtest_add_data_func("/kmh-bosc/pcie/iommu-msi-mode", (void *)1,
                       test_iommu_msi_mode);
    if (qtest_has_machine("xiangshan-kunminghu")) {
        qtest_add_func("/kmh-bosc/pcie/generic-dwc", test_generic_dwc);
        qtest_add_func("/kmh-bosc/pcie/kmh-iommu-root-contexts",
                       test_kmh_iommu_root_contexts);
        qtest_add_data_func("/kmh-bosc/pcie/kmh-iommu-msi-mode", (void *)0,
                           test_iommu_msi_mode);
    }
    return g_test_run();
}
