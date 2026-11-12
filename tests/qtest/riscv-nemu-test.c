/* SPDX-License-Identifier: GPL-2.0-or-later */
/* RISC-V NEMU CPU inventory and interrupt routing. */
#include "qemu/osdep.h"
#include "libqtest.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"

#define CLINT_BASE 0x38000000
#define MTIMECMP_BASE (CLINT_BASE + 0x4000)
#define PLIC_BASE 0x3c000000
#define CSR_MHARTID 0xf14
#define IRQ_MSIP 3
#define IRQ_MTIP 7
#define IRQ_SEIP 9

static uint64_t read_csr(QTestState *qts, unsigned int hart, unsigned int csr)
{
    uint64_t value = 0;

    g_assert_cmpuint(qtest_csr_call(qts, "get_csr", hart, csr, &value), ==, 0);
    return value;
}

static void test_harts(const void *opaque)
{
    const char *ids = opaque;
    g_auto(GStrv) parts = ids ? g_strsplit(ids, ":", -1) : NULL;
    unsigned int count = parts ? g_strv_length(parts) : 2;
    unsigned int context = 2 * count - 1;
    unsigned int hart = 1;

    g_autofree char *args = g_strdup_printf(
        "-machine nemu%s%s -smp %u -m 128M -bios none -cpu sifive-u54",
        ids ? ",hart-ids=" : "", ids ? ids : "", count);
    QTestState *qts = qtest_init(args);
    g_autoptr(QDict) cpus = qtest_qmp(qts,
        "{'execute': 'query-cpus-fast'}");
    g_autoptr(QDict) devices = NULL;
    g_autofree char *plic = NULL;
    const char *cpu_path = NULL;
    const QListEntry *entry;

    for (unsigned int i = 0; parts && i < count; i++) {
        hart = MAX(hart, g_ascii_strtoull(parts[i], NULL, 0));
    }
    g_assert_cmpuint(qlist_size(qdict_get_qlist(cpus, "return")), ==, count);
    g_assert_cmpuint(read_csr(qts, 0, CSR_MHARTID), ==, 0);
    for (unsigned int i = 0; parts && i < count; i++) {
        unsigned int id = g_ascii_strtoull(parts[i], NULL, 0);

        g_assert_cmpuint(read_csr(qts, id, CSR_MHARTID), ==, id);
    }
    g_assert_cmpuint(read_csr(qts, hart, CSR_MHARTID), ==, hart);
    QLIST_FOREACH_ENTRY(qdict_get_qlist(cpus, "return"), entry) {
        QDict *cpu = qobject_to(QDict, qlist_entry_obj(entry));

        if (qdict_get_int(cpu, "cpu-index") == count - 1) {
            cpu_path = qdict_get_str(cpu, "qom-path");
        }
    }
    g_assert_nonnull(cpu_path);
    qtest_irq_intercept_in(qts, cpu_path);

    /* MSIP is addressed by physical ID and must reach the target CPU input. */
    qtest_writel(qts, CLINT_BASE + hart * 4, 1);
    g_assert_true(qtest_get_irq(qts, IRQ_MSIP));
    qtest_writel(qts, CLINT_BASE + hart * 4, 0);
    g_assert_false(qtest_get_irq(qts, IRQ_MSIP));

    qtest_writeq(qts, MTIMECMP_BASE, UINT64_MAX);
    qtest_writeq(qts, MTIMECMP_BASE + hart * 8, 0);
    g_assert_cmpuint(qtest_readq(qts, MTIMECMP_BASE + hart * 8), ==, 0);
    g_assert_true(qtest_get_irq(qts, IRQ_MTIP));
    qtest_writeq(qts, MTIMECMP_BASE + hart * 8, UINT64_MAX);
    g_assert_false(qtest_get_irq(qts, IRQ_MTIP));

    devices = qtest_qmp(qts,
        "{'execute': 'qom-list', 'arguments': {'path': '/machine/unattached'}}");
    QLIST_FOREACH_ENTRY(qdict_get_qlist(devices, "return"), entry) {
        QDict *device = qobject_to(QDict, qlist_entry_obj(entry));

        if (!strcmp(qdict_get_str(device, "type"),
                    "child<riscv.sifive.plic>")) {
            plic = g_strdup_printf("/machine/unattached/%s",
                                   qdict_get_str(device, "name"));
            break;
        }
    }
    g_assert_nonnull(plic);

    /* PLIC contexts follow CPU order, regardless of physical IDs. */
    qtest_writel(qts, PLIC_BASE + 3 * 4, 1);
    qtest_writel(qts, PLIC_BASE + 0x2000 + context * 0x80, 1 << 3);
    qtest_set_irq_in(qts, plic, NULL, 3, 1);
    g_assert_true(qtest_get_irq(qts, IRQ_SEIP));
    g_assert_cmpuint(qtest_readl(qts, PLIC_BASE + 0x200000 +
                                     context * 0x1000 + 4), ==, 3);
    g_assert_false(qtest_get_irq(qts, IRQ_SEIP));
    qtest_set_irq_in(qts, plic, NULL, 3, 0);
    qtest_writel(qts, PLIC_BASE + 0x200000 + context * 0x1000 + 4, 3);
    qtest_quit(qts);
}

/* Feature implication uses a CPU-count-sized bitmap under TCG. */
static void test_tcg_high_harts(void)
{
    QTestState *qts;
    g_autoptr(QDict) cpus = NULL;

    if (!qtest_has_accel("tcg")) {
        g_test_skip("TCG is not available");
        return;
    }
    qts = qtest_init("-machine nemu,hart-ids=0:0x100:0x101 -smp 3 "
                     "-m 128M -bios none -cpu max -accel tcg -S");
    cpus = qtest_qmp(qts, "{'execute': 'query-cpus-fast'}");
    g_assert_cmpuint(qlist_size(qdict_get_qlist(cpus, "return")), ==, 3);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_data_func("/riscv/nemu/dense-harts", NULL, test_harts);
    qtest_add_data_func("/riscv/nemu/sparse-harts", "0:4", test_harts);
    qtest_add_data_func("/riscv/nemu/zero-last", "4:0", test_harts);
    qtest_add_data_func("/riscv/nemu/maximum-hart", "0x0:0xffe", test_harts);
    qtest_add_data_func("/riscv/nemu/high-harts", "0:0x100:0x101", test_harts);
    qtest_add_func("/riscv/nemu/tcg-high-harts", test_tcg_high_harts);
    return g_test_run();
}
