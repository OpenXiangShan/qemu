/*
 * KmhBosc requester-local/global die-ID routing and fixed DDR slots.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bswap.h"
#include "qemu/bitops.h"
#include "qemu/units.h"
#include <libfdt.h>

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


static void test_single_soc_addresses(void)
{
    QTestState *qts = qtest_init(
        "-M kmh-bosc-soc,soc-mode=single,die-mask=0xf,core-mask=0xffff,"
        "generated-dtb=on -smp 16,maxcpus=17 -m 256M");

    qtest_writeq(qts, 0x01100000, 0x1122334455667788ULL);
    g_assert_cmphex(qtest_readq(qts, 0x01100000), ==,
                    0x1122334455667788ULL);
    g_assert_cmphex(qtest_readq(qts, 0x100001100000ULL), !=,
                    0x1122334455667788ULL);


    qtest_quit(qts);
}

static void test_timebase(gconstpointer opaque)
{
    bool single = GPOINTER_TO_INT(opaque);
    const uint64_t acpi_base = 0x81000000;
    const uint64_t acpi_size = 0x20000;
    const uint64_t mtime = 0x1900bff8;
    /* The packed DT occupies the last 2 MiB of the boot DDR slot. */
    uint64_t dtb_addr = 0x80000000 + (single ? 256 : 64) * MiB - 2 * MiB;
    QTestState *qts = qtest_initf(
        "-M kmh-bosc-soc,soc-mode=%s,core-mask=0x1,generated-dtb=on,"
        "generated-acpi=on,acpi-handoff-addr=0x81000000 "
        "-smp %s -m 256M",
        single ? "single" : "multi",
        single ? "16,maxcpus=17" : "64,maxcpus=68");
    struct fdt_header header;
    g_autofree uint8_t *dtb = NULL;
    const fdt32_t *frequency;
    uint64_t before, after, xsdt;
    uint32_t size, xsdt_size;
    unsigned rhct_count = 0;
    char signature[8];
    int cpus, len;

    before = qtest_readq(qts, mtime);
    qtest_clock_step(qts, 1000000000);
    after = qtest_readq(qts, mtime);
    g_assert_cmpuint(after - before, ==, 13000000);
    if (!single) {
        for (unsigned die = 0; die < 4; die++) {
            g_assert_cmpuint(qtest_readq(qts, global_addr(die, mtime)),
                             ==, after);
        }
    }

    qtest_memread(qts, dtb_addr, &header, sizeof(header));
    g_assert_cmpint(fdt_check_header(&header), ==, 0);
    size = fdt_totalsize(&header);
    g_assert_cmpuint(size, >=, sizeof(header));
    g_assert_cmpuint(size, <, 2 * MiB);
    dtb = g_malloc(size);
    qtest_memread(qts, dtb_addr, dtb, size);
    cpus = fdt_path_offset(dtb, "/cpus");
    g_assert_cmpint(cpus, >=, 0);
    frequency = fdt_getprop(dtb, cpus, "timebase-frequency", &len);
    g_assert_nonnull(frequency);
    g_assert_cmpint(len, ==, sizeof(*frequency));
    g_assert_cmpuint(fdt32_to_cpu(*frequency), ==, 13000000);

    qtest_memread(qts, acpi_base, signature, sizeof(signature));
    g_assert_cmpmem(signature, sizeof(signature), "RSD PTR ", 8);
    xsdt = qtest_readq(qts, acpi_base + 24);
    g_assert_cmpuint(xsdt, >=, acpi_base);
    g_assert_cmpuint(xsdt, <=, acpi_base + acpi_size - 36);
    qtest_memread(qts, xsdt, signature, 4);
    g_assert_cmpmem(signature, 4, "XSDT", 4);
    xsdt_size = qtest_readl(qts, xsdt + 4);
    g_assert_cmpuint(xsdt_size, >=, 36);
    g_assert_cmpuint(xsdt_size, <=, acpi_base + acpi_size - xsdt);
    g_assert_cmpuint((xsdt_size - 36) % 8, ==, 0);
    for (unsigned offset = 36; offset < xsdt_size; offset += 8) {
        uint64_t table = qtest_readq(qts, xsdt + offset);

        g_assert_cmpuint(table, >=, acpi_base);
        g_assert_cmpuint(table, <=, acpi_base + acpi_size - 48);
        qtest_memread(qts, table, signature, 4);
        if (!memcmp(signature, "RHCT", 4)) {
            g_assert_cmpuint(qtest_readl(qts, table + 4), >=, 48);
            g_assert_cmpuint(qtest_readq(qts, table + 40), ==, 13000000);
            rhct_count++;
        }
    }
    g_assert_cmpuint(rhct_count, ==, 1);
    qtest_quit(qts);
}

static uint32_t fdt_cell(const void *fdt, int node, const char *name)
{
    int len;
    const fdt32_t *value = fdt_getprop(fdt, node, name, &len);

    g_assert_nonnull(value);
    g_assert_cmpint(len, ==, sizeof(*value));
    return fdt32_to_cpu(*value);
}

/* Validate the DT actually passed in DDR, including sparse-die affinity. */
static void test_generated_numa_fdt(gconstpointer opaque)
{
    unsigned mask = GPOINTER_TO_UINT(opaque);
    bool single = mask == 0;
    unsigned dies = single ? 1 : 4;
    unsigned selected = single ? 1 : ctpop32(mask);
    uint64_t node_size = (single ? 256 : 64) * MiB;
    uint64_t dtb_addr = 0x80000000 + node_size - 2 * MiB;
    QTestState *qts = qtest_initf(
        "-M kmh-bosc-soc,soc-mode=%s,die-mask=0x%x,core-mask=0x1,"
        "dw-pcie=on,iommu-sys=on,generated-dtb=on "
        "-smp %s -m 256M",
        single ? "single" : "multi", single ? 1 : mask,
        single ? "16,maxcpus=17" : "64,maxcpus=68");
    struct fdt_header header;
    g_autofree uint8_t *fdt = NULL;
    int len, node;
    const fdt32_t *cells;
    static const uint64_t dbi[] = {
        0x4f00000000, 0x4f00800000, 0x4f01000000,
    };
    static const uint64_t workspaces[] = {
        0x87000000, 0x87003000, 0x87004000,
    };
    static const unsigned sizes[] = { 0x3000, 0x1000, 0x2000 };
    static const char *const names[] = { "einj", "erst", "error-log" };

    qtest_memread(qts, dtb_addr, &header, sizeof(header));
    g_assert_cmpint(fdt_check_header(&header), ==, 0);
    g_assert_cmpuint(fdt_totalsize(&header), <, 2 * MiB);
    fdt = g_malloc(fdt_totalsize(&header));
    qtest_memread(qts, dtb_addr, fdt, fdt_totalsize(&header));
    node = fdt_path_offset(fdt, "/distance-map");
    cells = fdt_getprop(fdt, node, "distance-matrix", &len);
    g_assert_nonnull(cells);
    g_assert_cmpuint(len, ==, selected * selected * 3 * sizeof(*cells));
    for (unsigned entry = 0; entry < selected * selected; entry++) {
        unsigned a = fdt32_to_cpu(cells[entry * 3]);
        unsigned b = fdt32_to_cpu(cells[entry * 3 + 1]);

        g_assert_cmpuint(a, <, selected);
        g_assert_cmpuint(b, <, selected);
        g_assert_cmpuint(fdt32_to_cpu(cells[entry * 3 + 2]), ==,
                         a == b ? 10 : 20);
    }
    for (unsigned die = 0; die < dies; die++) {
        bool enabled = single || (mask & (1U << die));
        unsigned nid = single ? 0 : ctpop32(mask & ((1U << die) - 1));
        g_autofree char *path = g_strdup_printf(
            "/memory@%" PRIx64, (uint64_t)(0x80000000 + die * 0x800000000ULL));

        node = fdt_path_offset(fdt, path);
        if (!enabled) {
            g_assert_cmpint(node, ==, -FDT_ERR_NOTFOUND);
            continue;
        }
        g_assert_cmpuint(fdt_cell(fdt, node, "numa-node-id"), ==, nid);
        g_clear_pointer(&path, g_free);
        path = g_strdup_printf("/cpus/cpu@%u", die * 16);
        node = fdt_path_offset(fdt, path);
        g_assert_cmpuint(fdt_cell(fdt, node, "numa-node-id"), ==, nid);
        for (unsigned port = 0; port < 3; port++) {
            uint64_t base = single ? dbi[port] : global_addr(die, dbi[port]);
            g_autofree char *pci = g_strdup_printf("/soc/pci@%" PRIx64, base);

            node = fdt_path_offset(fdt, pci);
            g_assert_cmpint(node, >=, 0);
            g_assert_cmpint(fdt_node_check_compatible(
                fdt, node, "bosc,kmh-bosc-soc-pcie"), ==, 0);
            g_assert_cmpuint(fdt_cell(fdt, node, "numa-node-id"), ==, nid);
            g_assert_cmpuint(fdt_cell(fdt, node, "linux,pci-domain"), ==,
                             die * 3 + port);
            g_assert_null(fdt_getprop(fdt, node, "msi-parent", NULL));
            g_assert_null(fdt_getprop(fdt, node, "msi-map", NULL));
            g_assert_nonnull(fdt_getprop(fdt, node, "snps,imsi-rx-forward",
                                        NULL));
            cells = fdt_getprop(fdt, node, "interrupts", &len);
            g_assert_nonnull(cells);
            g_assert_cmpint(len, ==, 4 * sizeof(*cells));
            g_assert_cmpuint(fdt32_to_cpu(cells[0]), ==, 76 + port * 6);
            g_assert_cmpuint(fdt32_to_cpu(cells[2]), ==, 75 + port * 6);
            cells = fdt_getprop(fdt, node, "ranges", &len);
            g_assert_nonnull(cells);
            g_assert_cmpint(len, ==, 14 * sizeof(*cells));
            g_assert_cmphex(fdt32_to_cpu(cells[7]), ==, 0x43000000);
            g_assert_cmphex(fdt32_to_cpu(cells[11]), ==, 0x20000000);
            cells = fdt_getprop(fdt, node, "dma-ranges", &len);
            g_assert_nonnull(cells);
            g_assert_cmpint(len, ==, 7 * sizeof(*cells));
            g_assert_cmphex(fdt32_to_cpu(cells[2]), ==, 0x80000000);
            g_assert_cmphex(fdt32_to_cpu(cells[4]), ==, 0x80000000);
            g_assert_cmphex(fdt32_to_cpu(cells[6]), ==, 0x80000000);
        }
    }
    for (unsigned i = 0; i < ARRAY_SIZE(workspaces); i++) {
        g_autofree char *path = g_strdup_printf(
            "/reserved-memory/%s@%" PRIx64, names[i], workspaces[i]);

        node = fdt_path_offset(fdt, path);
        g_assert_cmpint(node, >=, 0);
        g_assert_nonnull(fdt_getprop(fdt, node, "no-map", NULL));
        cells = fdt_getprop(fdt, node, "reg", &len);
        g_assert_nonnull(cells);
        g_assert_cmpint(len, ==, 4 * sizeof(*cells));
        g_assert_cmpuint(fdt32_to_cpu(cells[1]), ==, workspaces[i]);
        g_assert_cmpuint(fdt32_to_cpu(cells[3]), ==, sizes[i]);
    }
    qtest_quit(qts);
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

typedef struct DdrLayout {
    unsigned ram_gib;
    unsigned die_mask;
} DdrLayout;

static void test_ddr_layout(const void *opaque)
{
    const DdrLayout *layout = opaque;
    const uint64_t base = 0x80000000ULL;
    const uint64_t stride = 32 * GiB;
    uint64_t size = layout->ram_gib * GiB / 4;
    QTestState *qts = qtest_initf(
        "-M kmh-bosc-soc,die-mask=0x%x,core-mask=0x1,generated-dtb=on "
        "-smp 64,maxcpus=68 -m %uG", layout->die_mask, layout->ram_gib);

    struct fdt_header header;
    uint64_t dtb_addr = base + size - 2 * MiB;
    qtest_memread(qts, dtb_addr, &header, sizeof(header));
    g_assert_cmpint(fdt_check_header(&header), ==, 0);
    g_autofree uint8_t *dtb = g_malloc(fdt_totalsize(&header));
    qtest_memread(qts, dtb_addr, dtb, fdt_totalsize(&header));
    const char *nodes[] = { "/soc/mswi@19000000", "/soc/mtimer@19004000" };
    for (unsigned n = 0; n < ARRAY_SIZE(nodes); n++) {
        int node = fdt_path_offset(dtb, nodes[n]);
        int len;
        const fdt32_t *parents = fdt_getprop(dtb, node,
                                           "interrupts-extended", &len);
        g_assert_nonnull(parents);
        g_assert_cmpint(len, ==, 64 * 2 * sizeof(fdt32_t));
        for (unsigned hart = 0; hart < 64; hart++) {
            g_autofree char *path = g_strdup_printf(
                "/cpus/cpu@%u/interrupt-controller", hart);
            int intc = fdt_path_offset(dtb, path);
            g_assert_cmphex(fdt32_to_cpu(parents[hart * 2]), ==,
                            fdt_get_phandle(dtb, intc));
            g_assert_cmpuint(fdt32_to_cpu(parents[hart * 2 + 1]), ==,
                             n ? 7 : 3);
        }
    }

    /* Keep distinct sentinels in all slots to catch accidental RAM aliases. */
    for (unsigned die = 0; die < 4; die++) {
        uint64_t start = base + die * stride;
        uint64_t value = 0x123456789abc0000ULL + die;

        qtest_writeq(qts, start, value);
        qtest_writeq(qts, start + size - 8, ~value);
        if (!(layout->die_mask & (1U << die))) {
            g_assert_cmphex(qtest_readq(qts, start), !=, value);
            g_assert_cmphex(qtest_readq(qts, start + size - 8), !=, ~value);
        }
        if (size < stride) {
            qtest_writeq(qts, start + size, value);
            qtest_writeq(qts, start + stride - 8, value);
            g_assert_cmphex(qtest_readq(qts, start + size), !=, value);
            g_assert_cmphex(qtest_readq(qts, start + stride - 8), !=, value);
        }
    }
    for (unsigned die = 0; die < 4; die++) {
        uint64_t start = base + die * stride;
        uint64_t value = 0x123456789abc0000ULL + die;

        if (layout->die_mask & (1U << die)) {
            g_assert_cmphex(qtest_readq(qts, start), ==, value);
            g_assert_cmphex(qtest_readq(qts, start + size - 8), ==, ~value);
        }
    }
    /* The first byte beyond the full 128 GiB aperture is not RAM. */
    qtest_writeq(qts, base + 4 * stride, 0xfeed1234);
    g_assert_cmphex(qtest_readq(qts, base + 4 * stride), !=, 0xfeed1234);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    static const DdrLayout small = { 16, 0xf };
    static const DdrLayout full = { 128, 0xf };
    static const DdrLayout sparse = { 16, 0x5 };

    g_test_init(&argc, &argv, NULL);
    qtest_add_data_func("/kmh-bosc/generated-fdt/single", GUINT_TO_POINTER(0),
                        test_generated_numa_fdt);
    qtest_add_data_func("/kmh-bosc/generated-fdt/two-die", GUINT_TO_POINTER(3),
                        test_generated_numa_fdt);
    qtest_add_data_func("/kmh-bosc/generated-fdt/four-die",
                        GUINT_TO_POINTER(15), test_generated_numa_fdt);
    qtest_add_data_func("/kmh-bosc/generated-fdt/sparse-die",
                        GUINT_TO_POINTER(5), test_generated_numa_fdt);
    qtest_add_func("/kmh-bosc/single/addresses", test_single_soc_addresses);
    qtest_add_data_func("/kmh-bosc/timebase/single", GINT_TO_POINTER(1),
                        test_timebase);
    qtest_add_data_func("/kmh-bosc/timebase/multi", GINT_TO_POINTER(0),
                        test_timebase);
    qtest_add_func("/kmh-bosc/die-routing", test_die_routing);
    qtest_add_data_func("/kmh-bosc/ddr/16g", &small, test_ddr_layout);
    qtest_add_data_func("/kmh-bosc/ddr/128g", &full, test_ddr_layout);
    qtest_add_data_func("/kmh-bosc/ddr/sparse", &sparse, test_ddr_layout);
    return g_test_run();
}
