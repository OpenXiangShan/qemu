/*
 * KmhBosc requester-local/global die-ID routing.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bswap.h"

#define APLIC_M 0x1e020000
#define FORCE_REG 0x0409014c
#define CSR_MISELECT 0x350
#define CSR_MIREG 0x351
#define EIP0 0x80

static uint64_t global_addr(unsigned die, uint64_t offset)
{
    return ((uint64_t)(die + 1) << 44) | offset;
}

static uint64_t csr(QTestState *qts, unsigned hart, unsigned reg,
                    const char *op, uint64_t value)
{
    g_assert_cmpuint(qtest_csr_call(qts, op, hart, reg, &value), ==, 0);
    return value;
}

static void clear_pending(QTestState *qts)
{
    for (unsigned hart = 0; hart < 64; hart++) {
        csr(qts, hart, CSR_MISELECT, "set_csr", EIP0);
        csr(qts, hart, CSR_MIREG, "set_csr", 0);
    }
}

static void assert_only_pending(QTestState *qts, unsigned target)
{
    for (unsigned hart = 0; hart < 64; hart++) {
        uint64_t expected = hart == target ? (1ULL << 33) : 0;

        g_assert_cmphex(csr(qts, hart, CSR_MIREG, "get_csr", 0), ==, expected);
    }
}

static void test_die_routing(void)
{
    g_autofree char *bios = NULL;
    /*
     * Each hart writes local SRAM, reads/writes its canonical alias, then
     * reads local SRAM again and publishes the result before parking.
     */
    uint32_t code[] = {
        0xf1402573, 0x00455593, 0x00158593, 0x02c59613,
        0x011002b7, 0x00251313, 0x006282b3, 0x00b2a023,
        0x00c2e3b3, 0x0003ae03, 0x010e0e13, 0x01c3a023,
        0x0002ae83, 0x0330000f, 0x11d3a023, 0x10500073, 0xffdff06f,
    };
    int fd = g_file_open_tmp("kmh-bosc-die-test-XXXXXX", &bios, NULL);
    QTestState *qts;

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    for (unsigned i = 0; i < ARRAY_SIZE(code); i++) {
        code[i] = cpu_to_le32(code[i]);
    }
    g_assert_true(g_file_set_contents(bios, (const char *)code,
                                      sizeof(code), NULL));
    qts = qtest_initf("-M kmh-bosc-soc,die-mask=0xf,core-mask=0x8001 "
                     "-smp 64,maxcpus=68 -m 256M -accel tcg -S -bios %s", bios);

    for (unsigned source_die = 0; source_die < 4; source_die++) {
        uint64_t aplic = global_addr(source_die, APLIC_M);

        qtest_writel(qts, aplic + 0x1bc0, 0x1c000);
        qtest_writel(qts, aplic + 0x1bc4, 0x14034000);
        for (unsigned core = 0; core < 16; core += 15) {
            /* No current_cpu: a local MSI belongs to the APLIC's die. */
            clear_pending(qts);
            qtest_writel(qts, aplic + 0x3000, (core << 18) | 33);
            assert_only_pending(qts, source_die * 16 + core);
            for (unsigned target_die = 0; target_die < 4; target_die++) {
                unsigned target = ((target_die + 1) << 4) | core;

                clear_pending(qts);
                qtest_writel(qts, aplic + 0x3000, (target << 18) | 33);
                assert_only_pending(qts, target_die * 16 + core);
            }
        }
        /* Force/clear is per die; check the APLIC input, not helper ROZ. */
        qtest_writel(qts, aplic + 4, 6);
        qtest_writel(qts, global_addr(source_die, FORCE_REG), UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, aplic + 0x1d00) & 2, ==, 2);
        qtest_writel(qts, global_addr((source_die + 1) % 4, FORCE_REG), 1);
        g_assert_cmphex(qtest_readl(qts, aplic + 0x1d00) & 2, ==, 2);
        qtest_writel(qts, global_addr(source_die, FORCE_REG), 1);
        g_assert_cmphex(qtest_readl(qts, aplic + 0x1d00) & 2, ==, 0);

        /* DMA uses the DMAC's die even when programmed through global MMIO. */
        for (unsigned target_die = 0; target_die < 4; target_die++) {
            uint64_t dmac = global_addr(source_die, 0xb000000);
            uint64_t dst = global_addr(target_die, 0x1100400);

            qtest_writel(qts, global_addr(source_die, 0x1100300),
                         0x12340000 + source_die);
            qtest_writel(qts, dst, 0);
            qtest_writel(qts, dmac + 0x100, 0x1100300);
            qtest_writel(qts, dmac + 0x104, 0);
            qtest_writel(qts, dmac + 0x108, dst);
            qtest_writel(qts, dmac + 0x10c, dst >> 32);
            qtest_writel(qts, dmac + 0x110, 0);
            qtest_writel(qts, dmac + 0x118, (2 << 8) | (2 << 11));
            qtest_writel(qts, dmac + 0x10, 1);
            qtest_writel(qts, dmac + 0x18, 0x101);
            g_assert_cmphex(qtest_readl(qts, dst), ==,
                            0x12340000 + source_die);
        }
    }
    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    for (unsigned die = 0; die < 4; die++) {
        for (unsigned core = 0; core < 16; core += 15) {
            unsigned hart = die * 16 + core;
            uint64_t result = global_addr(die, 0x1100100 + hart * 4);
            gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

            while (qtest_readl(qts, result) == 0 &&
                   g_get_monotonic_time() < deadline) {
                g_usleep(1000);
            }
            g_assert_cmphex(qtest_readl(qts, result), ==, die + 17);
        }
    }
    qtest_qmp_assert_success(qts, "{'execute':'stop'}");
    qtest_quit(qts);
    unlink(bios);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/kmh-bosc/die-routing", test_die_routing);
    return g_test_run();
}
