/*
 * QTest testcase for BOSC AXI4 traffic generator.
 *
 * Copyright (c) 2026 Beijing Institute of Open Source Chip (BOSC)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"
#include "hw/riscv/riscv-iommu-bits.h"

#define KMH_BOSC_IOMMU_BASE              0x0000004f80200000ULL
#define KMH_BOSC_DDR_BASE                0x0000000080000000ULL
#define BOSC_AXI4_TRAFFIC_MMIO_SIZE      0x1000
#define BOSC_AXI4_TRAFFIC_BEAT_BYTES     32
#define BOSC_AXI4_TRAFFIC_PAGE_SIZE      4096
#define BOSC_AXI4_TRAFFIC_PAGE_SHIFT     12
#define BOSC_AXI4_TRAFFIC_NR_SLOTS       8
#define BOSC_AXI4_TRAFFIC_NR_CFGS        6

#define BOSC_AXI4_TRAFFIC_MUX            0x000
#define BOSC_AXI4_TRAFFIC_DID_BASE       0x080
#define BOSC_AXI4_TRAFFIC_PID_BASE       0x0a0
#define BOSC_AXI4_TRAFFIC_START_BASE     0x0c0
#define BOSC_AXI4_TRAFFIC_END_BASE       0x100
#define BOSC_AXI4_TRAFFIC_AR_CFG_BASE    0x200
#define BOSC_AXI4_TRAFFIC_AW_CFG_BASE    0x220
#define BOSC_AXI4_TRAFFIC_START          0x240
#define BOSC_AXI4_TRAFFIC_STATUS         0x244

#define BOSC_AXI4_TRAFFIC_MUX_DID_PID_EN BIT(1)
#define BOSC_AXI4_TRAFFIC_MUX_TRAFFIC    BIT(2)

#define BOSC_AXI4_TRAFFIC_STATUS_AW_DONE BIT(29)
#define BOSC_AXI4_TRAFFIC_STATUS_AR_DONE BIT(28)

#define BOSC_AXI4_TRAFFIC_START_AR       BIT(0)
#define BOSC_AXI4_TRAFFIC_START_AW       BIT(1)

#define BOSC_AXI4_TRAFFIC_PID_VALID      BIT(20)

#define BOSC_AXI4_TRAFFIC_PTE_V          BIT_ULL(0)
#define BOSC_AXI4_TRAFFIC_PTE_R          BIT_ULL(1)
#define BOSC_AXI4_TRAFFIC_PTE_W          BIT_ULL(2)
#define BOSC_AXI4_TRAFFIC_PTE_A          BIT_ULL(6)
#define BOSC_AXI4_TRAFFIC_PTE_D          BIT_ULL(7)
#define BOSC_AXI4_TRAFFIC_PTE_PPN_SHIFT  10

#define BOSC_AXI4_TRAFFIC_DDT_BASE       (KMH_BOSC_DDR_BASE + 0x01000000)
#define BOSC_AXI4_TRAFFIC_TABLE_BASE     (KMH_BOSC_DDR_BASE + 0x01010000)
#define BOSC_AXI4_TRAFFIC_PA_BASE        (KMH_BOSC_DDR_BASE + 0x02000000)

static const uint64_t traffic_bases[] = {
    0x0000004f80201000ULL,
    0x0000004f80211000ULL,
    0x0000004f80221000ULL,
};

static QTestState *bosc_axi4_traffic_start_qtest(void)
{
    return qtest_init("-M kmh-bosc-soc,boot-source=ddr,die-mask=0x1,"
                      "core-mask=0x1,generated-dtb=on,iommu-sys=on "
                      "-smp 64,maxcpus=68 -m 512M");
}

static void traffic_write64_regs(QTestState *qts, uint64_t base,
                                 uint32_t offset, uint64_t value)
{
    qtest_writel(qts, base + offset, value);
    qtest_writel(qts, base + offset + 4, value >> 32);
}

static void traffic_memwrite64(QTestState *qts, uint64_t addr, uint64_t value)
{
    uint64_t le = cpu_to_le64(value);

    qtest_memwrite(qts, addr, &le, sizeof(le));
}

static void traffic_zero_page(QTestState *qts, uint64_t addr)
{
    uint8_t zero[BOSC_AXI4_TRAFFIC_PAGE_SIZE] = { 0 };

    qtest_memwrite(qts, addr, zero, sizeof(zero));
}

static void traffic_pattern_init(uint8_t data[static 32])
{
    int i;

    memset(data, 0, BOSC_AXI4_TRAFFIC_BEAT_BYTES);
    for (i = 0; i < 16; i++) {
        data[i * 2] = i;
    }
}

static void traffic_pattern_rotate(uint8_t data[static 32])
{
    uint8_t carry = data[31] >> 7;
    int i;

    for (i = 0; i < BOSC_AXI4_TRAFFIC_BEAT_BYTES; i++) {
        uint8_t next = data[i] >> 7;

        data[i] = (data[i] << 1) | carry;
        carry = next;
    }
}

static void traffic_build_expected(uint8_t *buf, size_t len)
{
    uint8_t pattern[BOSC_AXI4_TRAFFIC_BEAT_BYTES];
    size_t off;

    g_assert_cmpuint(len % BOSC_AXI4_TRAFFIC_BEAT_BYTES, ==, 0);
    traffic_pattern_init(pattern);

    for (off = 0; off < len; off += BOSC_AXI4_TRAFFIC_BEAT_BYTES) {
        memcpy(buf + off, pattern, BOSC_AXI4_TRAFFIC_BEAT_BYTES);
        traffic_pattern_rotate(pattern);
    }
}

static void traffic_init_target(QTestState *qts, uint64_t addr, size_t len)
{
    uint8_t buf[BOSC_AXI4_TRAFFIC_BEAT_BYTES];

    g_assert_cmpuint(len, <=, sizeof(buf));
    memset(buf, 0xa5, sizeof(buf));
    qtest_memwrite(qts, addr, buf, len);
}

static bool traffic_target_matches(QTestState *qts, uint64_t addr,
                                   const uint8_t *expected, size_t len)
{
    uint8_t actual[BOSC_AXI4_TRAFFIC_BEAT_BYTES];

    g_assert_cmpuint(len, <=, sizeof(actual));
    qtest_memread(qts, addr, actual, len);
    return memcmp(actual, expected, len) == 0;
}

static uint64_t traffic_ppn_field(uint64_t pa)
{
    return pa >> BOSC_AXI4_TRAFFIC_PAGE_SHIFT;
}

static uint64_t traffic_ddtp(uint64_t base, uint64_t mode)
{
    return (traffic_ppn_field(base) << 10) | mode;
}

static uint64_t traffic_atp(uint64_t root, uint64_t mode)
{
    return traffic_ppn_field(root) | (mode << 60);
}

static uint64_t traffic_pte(uint64_t pa, uint64_t flags)
{
    return (traffic_ppn_field(pa) << BOSC_AXI4_TRAFFIC_PTE_PPN_SHIFT) |
           flags;
}

static void traffic_map_sv39_page(QTestState *qts, uint64_t root,
                                  uint64_t l1, uint64_t l0,
                                  uint64_t iova, uint64_t pa)
{
    uint64_t vpn2 = (iova >> 30) & 0x1ff;
    uint64_t vpn1 = (iova >> 21) & 0x1ff;
    uint64_t vpn0 = (iova >> 12) & 0x1ff;

    traffic_zero_page(qts, root);
    traffic_zero_page(qts, l1);
    traffic_zero_page(qts, l0);

    traffic_memwrite64(qts, root + vpn2 * sizeof(uint64_t),
                       traffic_pte(l1, BOSC_AXI4_TRAFFIC_PTE_V));
    traffic_memwrite64(qts, l1 + vpn1 * sizeof(uint64_t),
                       traffic_pte(l0, BOSC_AXI4_TRAFFIC_PTE_V));
    traffic_memwrite64(qts, l0 + vpn0 * sizeof(uint64_t),
                       traffic_pte(pa, BOSC_AXI4_TRAFFIC_PTE_V |
                                       BOSC_AXI4_TRAFFIC_PTE_R |
                                       BOSC_AXI4_TRAFFIC_PTE_W |
                                       BOSC_AXI4_TRAFFIC_PTE_A |
                                       BOSC_AXI4_TRAFFIC_PTE_D));
}

static void traffic_write_device_context(QTestState *qts, uint32_t did,
                                         uint64_t tc, uint64_t iohgatp,
                                         uint64_t ta, uint64_t fsc)
{
    uint64_t dc = BOSC_AXI4_TRAFFIC_DDT_BASE + did * 64;

    g_assert_cmpuint(did, <, 64);
    traffic_memwrite64(qts, dc + 0, tc);
    traffic_memwrite64(qts, dc + 8, iohgatp);
    traffic_memwrite64(qts, dc + 16, ta);
    traffic_memwrite64(qts, dc + 24, fsc);
}

static void traffic_write_sv39_device(QTestState *qts, uint32_t did,
                                      uint32_t pscid, uint64_t root)
{
    traffic_write_device_context(qts, did, RISCV_IOMMU_DC_TC_V, 0,
                                 (uint64_t)pscid << 12,
                                 traffic_atp(root,
                                             RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV39));
}

static void traffic_write_pdt_entry(QTestState *qts, uint64_t pdt,
                                    uint32_t pid, uint32_t pscid,
                                    uint64_t root)
{
    uint64_t entry = pdt + pid * 16;

    g_assert_cmpuint(pid, <, 256);
    traffic_memwrite64(qts, entry, RISCV_IOMMU_PC_TA_V |
                                   ((uint64_t)pscid << 12));
    traffic_memwrite64(qts, entry + 8,
                       traffic_atp(root, RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV39));
}

static void traffic_prepare_iommu_tables(QTestState *qts, uint64_t iova,
                                         uint64_t pid_iova)
{
    const uint32_t did_mapper = 4;
    const uint32_t did_user = 5;
    const uint32_t did_pid = 6;
    const uint32_t pid_a = 0x11;
    const uint32_t pid_b = 0x22;
    uint64_t slot0_root = BOSC_AXI4_TRAFFIC_TABLE_BASE + 0x3000;
    uint64_t slot0_l1 = BOSC_AXI4_TRAFFIC_TABLE_BASE + 0x4000;
    uint64_t slot0_l0 = BOSC_AXI4_TRAFFIC_TABLE_BASE + 0x5000;
    uint64_t slot1_root = BOSC_AXI4_TRAFFIC_TABLE_BASE + 0x6000;
    uint64_t slot1_l1 = BOSC_AXI4_TRAFFIC_TABLE_BASE + 0x7000;
    uint64_t slot1_l0 = BOSC_AXI4_TRAFFIC_TABLE_BASE + 0x8000;
    uint64_t pdt = BOSC_AXI4_TRAFFIC_TABLE_BASE + 0x9000;
    uint64_t pid_a_root = BOSC_AXI4_TRAFFIC_TABLE_BASE + 0xa000;
    uint64_t pid_a_l1 = BOSC_AXI4_TRAFFIC_TABLE_BASE + 0xb000;
    uint64_t pid_a_l0 = BOSC_AXI4_TRAFFIC_TABLE_BASE + 0xc000;
    uint64_t pid_b_root = BOSC_AXI4_TRAFFIC_TABLE_BASE + 0xd000;
    uint64_t pid_b_l1 = BOSC_AXI4_TRAFFIC_TABLE_BASE + 0xe000;
    uint64_t pid_b_l0 = BOSC_AXI4_TRAFFIC_TABLE_BASE + 0xf000;

    traffic_zero_page(qts, BOSC_AXI4_TRAFFIC_DDT_BASE);
    traffic_zero_page(qts, pdt);

    traffic_map_sv39_page(qts, slot0_root, slot0_l1, slot0_l0, iova,
                          BOSC_AXI4_TRAFFIC_PA_BASE + 0x0000);
    traffic_write_sv39_device(qts, did_mapper, 0x40, slot0_root);

    traffic_map_sv39_page(qts, slot1_root, slot1_l1, slot1_l0, iova,
                          BOSC_AXI4_TRAFFIC_PA_BASE + 0x1000);
    traffic_write_sv39_device(qts, did_user, 0x50, slot1_root);

    traffic_map_sv39_page(qts, pid_a_root, pid_a_l1, pid_a_l0, pid_iova,
                          BOSC_AXI4_TRAFFIC_PA_BASE + 0x3000);
    traffic_map_sv39_page(qts, pid_b_root, pid_b_l1, pid_b_l0, pid_iova,
                          BOSC_AXI4_TRAFFIC_PA_BASE + 0x4000);
    traffic_write_pdt_entry(qts, pdt, pid_a, 0x61, pid_a_root);
    traffic_write_pdt_entry(qts, pdt, pid_b, 0x62, pid_b_root);
    traffic_write_device_context(qts, did_pid,
                                 RISCV_IOMMU_DC_TC_V |
                                 RISCV_IOMMU_DC_TC_PDTV,
                                 0, 0,
                                 traffic_atp(pdt,
                                             RISCV_IOMMU_DC_FSC_PDTP_MODE_PD8));

    qtest_writeq(qts, KMH_BOSC_IOMMU_BASE + RISCV_IOMMU_REG_DDTP,
                 traffic_ddtp(BOSC_AXI4_TRAFFIC_DDT_BASE,
                              RISCV_IOMMU_DDTP_MODE_1LVL));
}

static uint32_t traffic_cfg2(uint64_t addr, uint32_t beats)
{
    return ((addr >> 32) & 0xff) | ((beats - 1) << 12);
}

static uint32_t traffic_user3(uint32_t did)
{
    return (did & 0xffffff) << 8;
}

static uint32_t traffic_user4(uint32_t pid, bool pv, bool translated)
{
    return (pid & 0xfffff) | (pv ? BIT(20) : 0) |
           (translated ? BIT(21) : 0);
}

static void traffic_program_channel(QTestState *qts, uint64_t base,
                                    uint32_t cfg_base, uint64_t addr,
                                    uint32_t bursts, uint32_t beats,
                                    uint32_t did, uint32_t pid, bool pv)
{
    qtest_writel(qts, base + cfg_base + 0, bursts << 12);
    qtest_writel(qts, base + cfg_base + 4, addr);
    qtest_writel(qts, base + cfg_base + 8, traffic_cfg2(addr, beats));
    qtest_writel(qts, base + cfg_base + 12, traffic_user3(did));
    qtest_writel(qts, base + cfg_base + 16, traffic_user4(pid, pv, false));
    qtest_writel(qts, base + cfg_base + 20, 0);
}

static void test_reset_and_readback(void)
{
    QTestState *qts = bosc_axi4_traffic_start_qtest();
    uint64_t base = traffic_bases[0];
    int i;

    g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_MUX), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_STATUS), ==, 0);

    for (i = 0; i < BOSC_AXI4_TRAFFIC_NR_SLOTS; i++) {
        g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_DID_BASE +
                                    i * 4), ==, 0);
        g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_PID_BASE +
                                    i * 4), ==, 0);
        g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_START_BASE +
                                    i * 8), ==, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_START_BASE +
                                    i * 8 + 4), ==, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_END_BASE +
                                    i * 8), ==, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_END_BASE +
                                    i * 8 + 4), ==, UINT32_MAX);
    }

    for (i = 0; i < BOSC_AXI4_TRAFFIC_NR_CFGS; i++) {
        g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_AR_CFG_BASE +
                                    i * 4), ==, 0);
        g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_AW_CFG_BASE +
                                    i * 4), ==, 0);
    }

    qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_MUX, 0xf);
    qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_DID_BASE, 0x12345678);
    qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_PID_BASE, 0xffffffff);
    traffic_write64_regs(qts, base, BOSC_AXI4_TRAFFIC_START_BASE,
                         0x1122334455667788ULL);
    traffic_write64_regs(qts, base, BOSC_AXI4_TRAFFIC_END_BASE,
                         0x99aabbccddeeff00ULL);
    traffic_program_channel(qts, base, BOSC_AXI4_TRAFFIC_AR_CFG_BASE,
                            0x12345000, 3, 2, 0x654321, 0x54321, true);

    g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_MUX), ==, 0xf);
    g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_DID_BASE),
                    ==, 0x345678);
    g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_PID_BASE),
                    ==, 0x3fffff);
    g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_START_BASE),
                    ==, 0x55667788);
    g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_START_BASE + 4),
                    ==, 0x11223344);
    g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_END_BASE),
                    ==, 0xddeeff00);
    g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_END_BASE + 4),
                    ==, 0x99aabbcc);
    g_assert_cmphex(qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_AR_CFG_BASE),
                    ==, 3 << 12);

    qtest_quit(qts);
}

static void test_bare_traffic_windows(void)
{
    QTestState *qts = bosc_axi4_traffic_start_qtest();
    int i;

    qtest_writeq(qts, KMH_BOSC_IOMMU_BASE + RISCV_IOMMU_REG_DDTP,
                 RISCV_IOMMU_DDTP_MODE_BARE);

    for (i = 0; i < G_N_ELEMENTS(traffic_bases); i++) {
        uint64_t base = traffic_bases[i];
        uint64_t addr = KMH_BOSC_DDR_BASE + 0x100000 + i * 0x1000;
        uint32_t bursts = 2;
        uint32_t beats = 4;
        size_t len = bursts * beats * BOSC_AXI4_TRAFFIC_BEAT_BYTES;
        uint8_t expected[256];
        uint8_t actual[256];
        uint32_t status;

        g_assert_cmpuint(len, <=, sizeof(actual));
        memset(actual, 0xa5, sizeof(actual));
        qtest_memwrite(qts, addr, actual, len);

        qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_MUX,
                     BOSC_AXI4_TRAFFIC_MUX_DID_PID_EN |
                     BOSC_AXI4_TRAFFIC_MUX_TRAFFIC);
        traffic_write64_regs(qts, base, BOSC_AXI4_TRAFFIC_START_BASE,
                             addr);
        traffic_write64_regs(qts, base, BOSC_AXI4_TRAFFIC_END_BASE,
                             addr + len);
        qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_DID_BASE,
                     0x120000 + i);
        qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_PID_BASE,
                     BIT(20) | i);

        traffic_program_channel(qts, base, BOSC_AXI4_TRAFFIC_AW_CFG_BASE,
                                addr, bursts, beats, 0xeeeeee, 0xabcde, true);
        qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_START,
                     BOSC_AXI4_TRAFFIC_START_AW);
        status = qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_STATUS);
        g_assert_cmphex(status & BOSC_AXI4_TRAFFIC_STATUS_AW_DONE,
                        ==, BOSC_AXI4_TRAFFIC_STATUS_AW_DONE);

        traffic_build_expected(expected, len);
        qtest_memread(qts, addr, actual, len);
        g_assert_cmpmem(actual, len, expected, len);

        traffic_program_channel(qts, base, BOSC_AXI4_TRAFFIC_AR_CFG_BASE,
                                addr, bursts, beats, 0xeeeeee, 0xabcde, true);
        qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_START,
                     BOSC_AXI4_TRAFFIC_START_AR);
        status = qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_STATUS);
        g_assert_cmphex(status & BOSC_AXI4_TRAFFIC_STATUS_AR_DONE,
                        ==, BOSC_AXI4_TRAFFIC_STATUS_AR_DONE);
        g_assert_cmpuint((status >> 8) & 0xfffff, ==, bursts);
        g_assert_cmpuint(status & 0xff, ==, 0);
    }

    qtest_quit(qts);
}

static void test_user_requester_selection(void)
{
    QTestState *qts = bosc_axi4_traffic_start_qtest();
    uint64_t base = traffic_bases[0];
    uint64_t iova = 0x1000;
    uint64_t pid_iova = 0x4000;
    uint64_t pa_mapper = BOSC_AXI4_TRAFFIC_PA_BASE + 0x0000;
    uint64_t pa_user = BOSC_AXI4_TRAFFIC_PA_BASE + 0x1000;
    uint64_t pa_pid_a = BOSC_AXI4_TRAFFIC_PA_BASE + 0x3000;
    uint64_t pa_pid_b = BOSC_AXI4_TRAFFIC_PA_BASE + 0x4000;
    uint8_t expected[BOSC_AXI4_TRAFFIC_BEAT_BYTES];
    uint32_t status;

    traffic_pattern_init(expected);
    traffic_prepare_iommu_tables(qts, iova, pid_iova);

    traffic_init_target(qts, pa_mapper, sizeof(expected));
    traffic_init_target(qts, pa_user, sizeof(expected));

    qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_MUX,
                 BOSC_AXI4_TRAFFIC_MUX_DID_PID_EN |
                 BOSC_AXI4_TRAFFIC_MUX_TRAFFIC);
    traffic_write64_regs(qts, base, BOSC_AXI4_TRAFFIC_START_BASE + 0 * 8,
                         iova);
    traffic_write64_regs(qts, base, BOSC_AXI4_TRAFFIC_END_BASE + 0 * 8,
                         iova + BOSC_AXI4_TRAFFIC_BEAT_BYTES);
    qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_DID_BASE + 0 * 4, 4);
    qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_PID_BASE + 0 * 4, 0);

    traffic_program_channel(qts, base, BOSC_AXI4_TRAFFIC_AW_CFG_BASE,
                            iova, 1, 1, 5, 0, false);
    qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_START,
                 BOSC_AXI4_TRAFFIC_START_AW);
    status = qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_STATUS);
    g_assert_cmphex(status & BOSC_AXI4_TRAFFIC_STATUS_AW_DONE,
                    ==, BOSC_AXI4_TRAFFIC_STATUS_AW_DONE);
    g_assert_false(traffic_target_matches(qts, pa_mapper, expected,
                                          sizeof(expected)));
    g_assert_true(traffic_target_matches(qts, pa_user, expected,
                                         sizeof(expected)));

    traffic_init_target(qts, pa_pid_a, sizeof(expected));
    traffic_init_target(qts, pa_pid_b, sizeof(expected));
    qtest_memwrite(qts, pa_pid_b, expected, sizeof(expected));

    traffic_write64_regs(qts, base, BOSC_AXI4_TRAFFIC_START_BASE + 0 * 8,
                         pid_iova);
    traffic_write64_regs(qts, base, BOSC_AXI4_TRAFFIC_END_BASE + 0 * 8,
                         pid_iova + BOSC_AXI4_TRAFFIC_BEAT_BYTES);
    qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_DID_BASE + 0 * 4, 6);
    qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_PID_BASE + 0 * 4,
                 BOSC_AXI4_TRAFFIC_PID_VALID | 0x11);

    traffic_program_channel(qts, base, BOSC_AXI4_TRAFFIC_AR_CFG_BASE,
                            pid_iova, 1, 1, 6, 0x22, true);
    qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_START,
                 BOSC_AXI4_TRAFFIC_START_AR);
    status = qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_STATUS);
    g_assert_cmphex(status & BOSC_AXI4_TRAFFIC_STATUS_AR_DONE,
                    ==, BOSC_AXI4_TRAFFIC_STATUS_AR_DONE);
    g_assert_cmpuint((status >> 8) & 0xfffff, ==, 1);
    g_assert_cmpuint(status & 0xff, ==, 0);
    g_assert_false(traffic_target_matches(qts, pa_pid_a, expected,
                                          sizeof(expected)));

    qtest_quit(qts);
}

static void test_error_dma_has_no_done(void)
{
    QTestState *qts = bosc_axi4_traffic_start_qtest();
    uint64_t base = traffic_bases[0];
    uint64_t addr = KMH_BOSC_DDR_BASE + 0x200000;
    uint32_t status;

    qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_MUX,
                 BOSC_AXI4_TRAFFIC_MUX_TRAFFIC);
    traffic_program_channel(qts, base, BOSC_AXI4_TRAFFIC_AW_CFG_BASE,
                            addr, 1, 1, 0x123456, 0, false);
    qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_START,
                 BOSC_AXI4_TRAFFIC_START_AW);
    status = qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_STATUS);
    g_assert_cmphex(status & BOSC_AXI4_TRAFFIC_STATUS_AW_DONE, ==, 0);

    traffic_program_channel(qts, base, BOSC_AXI4_TRAFFIC_AR_CFG_BASE,
                            addr, 1, 1, 0x123456, 0, false);
    qtest_writel(qts, base + BOSC_AXI4_TRAFFIC_START,
                 BOSC_AXI4_TRAFFIC_START_AR);
    status = qtest_readl(qts, base + BOSC_AXI4_TRAFFIC_STATUS);
    g_assert_cmphex(status & BOSC_AXI4_TRAFFIC_STATUS_AR_DONE, ==, 0);
    g_assert_cmpuint(status & 0xff, ==, 1);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/riscv64/bosc_axi4_traffic/reset_and_readback",
                   test_reset_and_readback);
    qtest_add_func("/riscv64/bosc_axi4_traffic/bare_traffic_windows",
                   test_bare_traffic_windows);
    qtest_add_func("/riscv64/bosc_axi4_traffic/user_requester_selection",
                   test_user_requester_selection);
    qtest_add_func("/riscv64/bosc_axi4_traffic/error_dma_has_no_done",
                   test_error_dma_has_no_done);

    return g_test_run();
}
