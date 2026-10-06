/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real public-ABI regression: moved devices, DT phandles, preflight failures. */
#include <dlfcn.h>
#include <glib.h>
#include <libfdt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libqemu/compose.h"

static char *error;

#define OK(call) do { \
    if ((call)) { \
        fprintf(stderr, "%s: %s\n", #call, error ? error : "failed"); \
        exit(1); \
    } \
} while (0)
#define BAD(call, fragment) do { \
    g_assert_cmpint((call), ==, -1); \
    g_assert_nonnull(error); \
    g_assert_nonnull(strstr(error, fragment)); \
    libqemu_compose_error_free(error); error = NULL; \
} while (0)

static uint32_t cell(const void *fdt, int n, const char *key, int index)
{
    int len;
    const fdt32_t *p = fdt_getprop(fdt, n, key, &len);
    g_assert_nonnull(p);
    g_assert_cmpint(len, >=, (index + 1) * 4);
    return fdt32_to_cpu(p[index]);
}

static int node(const void *fdt, const char *path)
{
    int n = fdt_path_offset(fdt, path);
    g_assert_cmpint(n, >=, 0);
    return n;
}

int main(int argc, char **argv)
{
    g_assert_true(argc == 2 || argc == 3);
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    g_assert_nonnull(lib);
    const LibQemuExports *(*get_api)(uint32_t, size_t) =
        (const LibQemuExports *(*)(uint32_t, size_t))dlsym(lib, "libqemu_get_api");
    g_assert_nonnull(get_api);
    const LibQemuExports *api = get_api(LIBQEMU_ABI_VERSION, sizeof(*api));
    g_assert_nonnull(api);
    LibQemuCompose *c = NULL;
    LibQemuSoc *s;
    LibQemuCpu *cpu;
    LibQemuAia *aia;
    LibQemuPcie *pcie;
    LibQemuDtb *d = NULL, *stale = NULL;
    LibQemuComposeChardev chars[] = {
        { "uart0", LIBQEMU_CHAR_NULL }, { "uart1", LIBQEMU_CHAR_NULL },
    };
    LibQemuComposeLaunch launch = {
        .accel = "tcg", .smp = 2, .chardevs = chars, .chardev_count = 2,
    };
    BAD(libqemu_compose_init(api, &launch, &c, &error), "smp=1");
    launch.smp = 1;
    chars[0].id = "0-invalid-id";
    BAD(libqemu_compose_init(api, &launch, &c, &error), "chardev");
    chars[0].id = "uart0";
    LibQemuExports short_api = *api;
    short_api.struct_size = offsetof(LibQemuExports, core);
    BAD(libqemu_compose_init(&short_api, &launch, &c, &error), "ABI");
    LibQemuCoreExports bad_core = *api->core;
    bad_core.rom_add_blob = NULL;
    short_api = *api;
    short_api.core = &bad_core;
    BAD(libqemu_compose_init(&short_api, &launch, &c, &error), "ABI");
    OK(libqemu_compose_init(api, &launch, &c, &error));

    KmhProfile p;
    kmh_profile_init_defaults(&p);
    p.dram.size = UINT64_C(1) << 30;
    p.blk_enabled = p.pcie_enabled = p.nvme_enabled = false;
    p.uart.base = 0x310c0000;
    p.uart.irq = 20;
    p.aia.imsic_m_base = 0x3c000000;
    p.aia.imsic_s_base = 0x3d000000;
    p.aia.aplic_m_base = 0x31200000;
    p.aia.aplic_s_base = 0x31220000;
    p.aclint.base = 0x39000000;
    p.finisher.base = 0x200000;
    /* Stack-backed strings may be changed/freed after a builder returns. */
    char model[] = "Module regression";
    p.soc.model = model;
    if (argc == 3) {
        OK(kmh_soc_build(c, &p, &s, &error));
        OK(libqemu_soc_build_dtb(s, "", &d, &error));
        int status;
        OK(api->request_shutdown(&error));
        OK(api->wait(&status, &error));
        libqemu_dtb_free(d);
        libqemu_compose_free(c);
        puts("COMPOSE_PROFILE_PASS");
        return status;
    }
    OK(libqemu_soc_new(c, &p.soc, &s, &error));
    OK(libqemu_soc_add_memory(s, &p.dram, &error));
    OK(libqemu_soc_add_memory(s, &p.rom, &error));
    OK(libqemu_riscv_cpu_create(s, &p.cpu, &cpu, &error));
    LibQemuAiaConfig invalid_aia = p.aia;
    invalid_aia.guest_files = 64;
    BAD(libqemu_riscv_aia_create(s, cpu, &invalid_aia, &aia, &error), "counts");
    OK(libqemu_riscv_aia_create(s, cpu, &p.aia, &aia, &error));
    OK(libqemu_riscv_aclint_create(s, cpu, &p.aclint, &error));
    /* Insert /poweroff before later modules to exercise offset invalidation. */
    OK(libqemu_finisher_create(s, &p.finisher, &error));
    OK(libqemu_uart_create(s, &p.uart, aia, &error));
    LibQemuUartConfig uart1 = {
        "uart1", 0x310d0000, p.uart.irq, "uart1", false,
    };
    BAD(libqemu_uart_create(s, &uart1, aia, &error), "occupied IRQ");
    uart1.irq = p.aia.num_sources + 1;
    BAD(libqemu_uart_create(s, &uart1, aia, &error), "IRQ");
    uart1.irq = 21;
    uart1.chardev = "nonexistent";
    BAD(libqemu_uart_create(s, &uart1, aia, &error), "chardev");
    uart1.chardev = p.uart.chardev;
    BAD(libqemu_uart_create(s, &uart1, aia, &error), "chardev");
    uart1.chardev = "uart1";
    OK(libqemu_uart_create(s, &uart1, aia, &error));
    model[0] = 'X';
    LibQemuRamConfig overlap = { "collision", p.uart.base, 0x1000, LIBQEMU_RAM, false };
    BAD(libqemu_soc_add_memory(s, &overlap, &error), "overlaps");
    overlap.base = UINT64_MAX - 0xfff;
    BAD(libqemu_soc_add_memory(s, &overlap, &error), "invalid/unaligned");
    overlap = p.dram;
    overlap.system_ram = false;
    BAD(libqemu_soc_add_memory(s, &overlap, &error), "duplicate");
    overlap.name = "..";
    BAD(libqemu_soc_add_memory(s, &overlap, &error), "invalid/duplicate");
    OK(libqemu_soc_build_dtb(s, "console=ttyS0", &stale, &error));
    size_t size;
    const void *fdt = libqemu_dtb_data(stale, &size);
    g_assert_cmpint(fdt_check_full(fdt, size), ==, 0);
    g_assert_cmpstr(fdt_getprop(fdt, 0, "model", NULL), ==, "Module regression");
    g_assert_cmpint(fdt_path_offset(fdt, "/soc/virtio@310a0000"), ==, -FDT_ERR_NOTFOUND);
    g_assert_cmpint(fdt_path_offset(fdt, "/soc/pcie@32000000"), ==, -FDT_ERR_NOTFOUND);
    g_assert_cmpint(fdt_path_offset(fdt, "/soc/serial@310b0000"), ==, -FDT_ERR_NOTFOUND);
    int uart = node(fdt, "/soc/serial@310c0000");
    g_assert_cmpuint(cell(fdt, uart, "reg", 1), ==, p.uart.base);
    g_assert_cmpuint(cell(fdt, uart, "interrupts", 0), ==, 20);
    int aplic_s = node(fdt, "/soc/aplic@31220000");
    uint32_t s_ph = cell(fdt, aplic_s, "phandle", 0);
    g_assert_cmpuint(cell(fdt, uart, "interrupt-parent", 0), ==, s_ph);
    int aplic_m = node(fdt, "/soc/aplic@31200000");
    g_assert_cmpuint(cell(fdt, aplic_m, "riscv,delegate", 0), ==, s_ph);
    g_assert_cmpuint(cell(fdt, aplic_m, "riscv,delegate", 2), ==, 96);
    int imsic = node(fdt, "/soc/imsics@3d000000");
    g_assert_cmpuint(cell(fdt, aplic_s, "msi-parent", 0), ==,
                     cell(fdt, imsic, "phandle", 0));
    g_assert_cmpuint(cell(fdt, imsic, "riscv,guest-index-bits", 0), ==, 3);
    int clint = node(fdt, "/soc/clint@39000000");
    g_assert_cmpuint(cell(fdt, clint, "reg", 1), ==, p.aclint.base);
    int chosen = node(fdt, "/chosen");
    g_assert_cmpstr(fdt_getprop(fdt, chosen, "stdout-path", NULL), ==,
                    "/soc/serial@310c0000:115200n8");
    int finisher = node(fdt, "/soc/test@200000");
    g_assert_cmpint(fdt_node_check_compatible(fdt, finisher, "syscon"), ==, 0);
    g_assert_cmpuint(cell(fdt, node(fdt, "/poweroff"), "regmap", 0), ==,
                     cell(fdt, finisher, "phandle", 0));
    LibQemuDwcConfig dwc = p.pcie;
    dwc.dbi_base = 0x33000000;
    dwc.cpu_mem_base = 0x70000000;
    dwc.config_base = 0x77ff0000;
    dwc.msi_irq = 22;
    dwc.hp_irq = 23;
    dwc.domain = 1;
    OK(libqemu_dwc_create(s, &dwc, aia, &pcie, &error));
    g_assert_nonnull(libqemu_pcie_bus(pcie));
    LibQemuNvmeConfig missing_nvme = { "nvme", "missing", "TEST" };
    BAD(libqemu_nvme_attach(pcie, &missing_nvme, &error), "backend");
    /* Adding a module invalidates a previously produced DTB for boot loading. */
    LibQemuRamConfig extra = { "extra", 0x20000000, 0x1000, LIBQEMU_RAM, false };
    OK(libqemu_soc_add_memory(s, &extra, &error));
    BAD(libqemu_riscv_boot_load(s, &p.boot, stale, &error), "stale");
    OK(libqemu_soc_build_dtb(s, "", &d, &error));
    fdt = libqemu_dtb_data(d, &size);
    g_assert_cmpint(fdt_check_full(fdt, size), ==, 0);
    int pci = node(fdt, "/soc/pcie@33000000");
    g_assert_cmpuint(cell(fdt, pci, "reg", 1), ==, dwc.dbi_base);
    g_assert_cmpuint(cell(fdt, pci, "reg", 5), ==, dwc.config_base);
    g_assert_cmpuint(cell(fdt, pci, "ranges", 4), ==, dwc.cpu_mem_base);
    g_assert_cmpuint(cell(fdt, pci, "interrupts", 2), ==, dwc.hp_irq);
    g_assert_cmpuint(cell(fdt, pci, "linux,pci-domain", 0), ==, 1);

    char path[] = "/tmp/libqemu-compose-XXXXXX";
    int fd = mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    uint8_t stub[64] = { 0x6f, 0, 0, 0 }; /* jal x0,0; never executed here */
    g_assert_cmpint(write(fd, stub, sizeof(stub)), ==, sizeof(stub));
    close(fd);
    LibQemuBootConfig boot = p.boot;
    boot.firmware = boot.kernel = path;
    boot.fdt_base = 0x90000000;
    boot.kernel_base = boot.firmware_base;
    BAD(libqemu_riscv_boot_load(s, &boot, d, &error), "overlaps");
    boot.kernel_base = p.boot.kernel_base + 1;
    BAD(libqemu_riscv_boot_load(s, &boot, d, &error), "alignment");
    boot.kernel_base = p.boot.kernel_base;
    boot.fdt_base = UINT64_C(0x100000000);
    BAD(libqemu_riscv_boot_load(s, &boot, d, &error), "exceeds");
    boot.fdt_base = 0x90000000;
    boot.kernel = "/no-such-compose-test-kernel";
    BAD(libqemu_riscv_boot_load(s, &boot, d, &error), "cannot open");
    boot.kernel = path;
    OK(libqemu_dtb_write(d, path, &error));
    char *written;
    gsize written_size;
    g_assert_true(g_file_get_contents(path, &written, &written_size, NULL));
    g_assert_cmpmem(written, written_size, fdt, size);
    g_free(written);
    g_assert_true(g_file_set_contents(path, (char *)stub, sizeof(stub), NULL));
    /* A 64-byte synthetic Linux header reserves 4 MiB, including BSS. */
    memcpy(stub + 56, "RSC\005", 4);
    stub[18] = 0x40;
    g_assert_true(g_file_set_contents(path, (char *)stub, sizeof(stub), NULL));
    boot.fdt_base = boot.kernel_base + 0x1000;
    BAD(libqemu_riscv_boot_load(s, &boot, d, &error), "overlaps");
    boot.fdt_base = 0x90000000;
    OK(libqemu_riscv_boot_load(s, &boot, d, &error));
    BAD(libqemu_riscv_boot_load(s, &boot, d, &error), "phase");
    BAD(libqemu_soc_add_memory(s, &extra, &error), "phase");
    unlink(path);
    int status;
    OK(api->request_shutdown(&error));
    OK(api->wait(&status, &error));
    g_assert_cmpint(status, ==, 0);
    libqemu_dtb_free(stale);
    libqemu_dtb_free(d);
    libqemu_compose_free(c);
    puts("COMPOSE_MODULE_DTB_PREFLIGHT_PASS");
    return 0;
}
