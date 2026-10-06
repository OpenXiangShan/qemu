/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <inttypes.h>
#include <libfdt.h>
#include <stdio.h>
#include "compose-internal.h"

/* Node offsets are valid only until a modification before that node. Each
 * emitter completes a node before inserting its next sibling. Paths are used
 * to reacquire parents between emitters (in particular after /poweroff).
 */
#define FDT(call) do { int rc = (call); if (rc < 0) { return rc; } } while (0)

static int cells(void *fdt, int node, const char *name,
                  const uint32_t *values, size_t count)
{
    fdt32_t buf[16];
    g_assert(count <= G_N_ELEMENTS(buf));
    for (size_t i = 0; i < count; i++) {
        buf[i] = cpu_to_fdt32(values[i]);
    }
    return fdt_setprop(fdt, node, name, buf, count * sizeof(*buf));
}

#define CELLS(fdt, node, key, ...) \
    cells(fdt, node, key, (uint32_t[]){__VA_ARGS__}, \
          sizeof((uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t))
#define HI32(value) ((uint32_t)((uint64_t)(value) >> 32))
#define LO32(value) ((uint32_t)(value))

static int reg(void *fdt, int node, uint64_t base, uint64_t size)
{
    return CELLS(fdt, node, "reg", HI32(base), LO32(base), HI32(size), LO32(size));
}

static int unit_node(void *fdt, const char *parent, const char *prefix,
                      uint64_t addr)
{
    int p = fdt_path_offset(fdt, parent);
    if (p < 0) {
        return p;
    }
    g_autofree char *name = g_strdup_printf("%s@%" PRIx64, prefix, addr);
    return fdt_add_subnode(fdt, p, name);
}

static int emit_cpu(void *fdt, LibQemuSoc *s, const char *isa)
{
    const LibQemuCpuConfig *p = &s->cpu->m.cfg.cpu;
    int cpus = fdt_add_subnode(fdt, 0, "cpus");
    FDT(cpus);
    FDT(fdt_setprop_u32(fdt, cpus, "#address-cells", 1));
    FDT(fdt_setprop_u32(fdt, cpus, "#size-cells", 0));
    FDT(fdt_setprop_u32(fdt, cpus, "timebase-frequency",
                        s->aclint->cfg.aclint.timebase_freq));
    int cpu = fdt_add_subnode(fdt, cpus, "cpu@0");
    FDT(cpu);
    FDT(fdt_setprop_u32(fdt, cpu, "reg", 0));
    FDT(fdt_setprop_string(fdt, cpu, "device_type", "cpu"));
    FDT(fdt_setprop_string(fdt, cpu, "compatible", "riscv"));
    FDT(fdt_setprop_string(fdt, cpu, "status", "okay"));
    FDT(fdt_setprop_string(fdt, cpu, "riscv,isa", isa));
    FDT(fdt_setprop_string(fdt, cpu, "mmu-type", p->mmu_type));
    FDT(fdt_setprop_u32(fdt, cpu, "riscv,cbom-block-size", p->cbom_block_size));
    FDT(fdt_setprop_u32(fdt, cpu, "riscv,cboz-block-size", p->cboz_block_size));
    int intc = fdt_add_subnode(fdt, cpu, "interrupt-controller");
    FDT(intc);
    FDT(fdt_setprop_string(fdt, intc, "compatible", "riscv,cpu-intc"));
    FDT(fdt_setprop_empty(fdt, intc, "interrupt-controller"));
    FDT(fdt_setprop_u32(fdt, intc, "#interrupt-cells", 1));
    FDT(fdt_setprop_u32(fdt, intc, "phandle", s->cpu->m.phandle));
    return 0;
}

static int emit_aia(void *fdt, ComposeModule *m)
{
    const LibQemuAiaConfig *p = &m->cfg.aia;
    unsigned cpu_ph = m->soc->cpu->m.phandle;
    unsigned guest_bits = compose_guest_bits(p->guest_files);
    /* Four consecutive handles: M IMSIC, S IMSIC, M APLIC, S APLIC. */
    for (unsigned mode = 0; mode < 2; mode++) {
        uint64_t base = mode ? p->imsic_s_base : p->imsic_m_base;
        int n = unit_node(fdt, "/soc", "imsics", base);
        FDT(n);
        FDT(fdt_setprop_string(fdt, n, "compatible", "riscv,imsics"));
        FDT(reg(fdt, n, base, UINT64_C(0x1000) << (mode ? guest_bits : 0)));
        FDT(fdt_setprop_empty(fdt, n, "interrupt-controller"));
        FDT(fdt_setprop_empty(fdt, n, "msi-controller"));
        FDT(fdt_setprop_u32(fdt, n, "#interrupt-cells", 0));
        FDT(fdt_setprop_u32(fdt, n, "riscv,num-ids", p->num_ids));
        FDT(fdt_setprop_u32(fdt, n, "phandle", m->phandle + mode));
        if (mode && guest_bits) {
            FDT(fdt_setprop_u32(fdt, n, "riscv,guest-index-bits", guest_bits));
        }
        FDT(CELLS(fdt, n, "interrupts-extended", cpu_ph,
                  mode ? COMPOSE_IRQ_SEIP : COMPOSE_IRQ_MEIP));
    }
    for (unsigned mode = 0; mode < 2; mode++) {
        uint64_t base = mode ? p->aplic_s_base : p->aplic_m_base;
        int n = unit_node(fdt, "/soc", "aplic", base);
        FDT(n);
        FDT(fdt_setprop_string(fdt, n, "compatible", "riscv,aplic"));
        FDT(reg(fdt, n, base, 0x4000));
        FDT(fdt_setprop_empty(fdt, n, "interrupt-controller"));
        FDT(fdt_setprop_u32(fdt, n, "#interrupt-cells", 2));
        FDT(fdt_setprop_u32(fdt, n, "riscv,num-sources", p->num_sources));
        FDT(fdt_setprop_u32(fdt, n, "phandle", m->phandle + 2 + mode));
        FDT(fdt_setprop_u32(fdt, n, "msi-parent", m->phandle + mode));
        if (!mode) {
            FDT(fdt_setprop_u32(fdt, n, "riscv,children", m->phandle + 3));
            FDT(CELLS(fdt, n, "riscv,delegate", m->phandle + 3, 1, p->num_sources));
        }
    }
    return 0;
}

static int emit_irq(void *fdt, int n, ComposeModule *m)
{
    FDT(fdt_setprop_u32(fdt, n, "interrupt-parent", m->irq.controller->m.phandle + 3));
    FDT(CELLS(fdt, n, "interrupts", m->irq.source, 4)); /* level-high */
    return 0;
}

static int emit_dwc(void *fdt, ComposeModule *m)
{
    const LibQemuDwcConfig *p = &m->cfg.pcie;
    static const char names[] = "dbi\0config";
    static const char irq_names[] = "msi\0hp";
    int n = unit_node(fdt, "/soc", "pcie", p->dbi_base);
    FDT(n);
    FDT(fdt_setprop_string(fdt, n, "compatible", "snps,dw-pcie"));
    FDT(CELLS(fdt, n, "reg", HI32(p->dbi_base), LO32(p->dbi_base),
              HI32(p->dbi_size), LO32(p->dbi_size),
              HI32(p->config_base), LO32(p->config_base),
              HI32(p->config_size), LO32(p->config_size)));
    FDT(fdt_setprop(fdt, n, "reg-names", names, sizeof(names)));
    FDT(fdt_setprop_u32(fdt, n, "#address-cells", 3));
    FDT(fdt_setprop_u32(fdt, n, "#size-cells", 2));
    FDT(fdt_setprop_string(fdt, n, "device_type", "pci"));
    FDT(fdt_setprop_u32(fdt, n, "linux,pci-domain", p->domain));
    FDT(fdt_setprop_u32(fdt, n, "num-ib-windows", 1));
    FDT(fdt_setprop_u32(fdt, n, "num-lanes", 1));
    FDT(fdt_setprop_string(fdt, n, "status", "okay"));
    FDT(CELLS(fdt, n, "bus-range", 0, 255));
    FDT(CELLS(fdt, n, "ranges", 0x02000000,
              HI32(p->pci_mem_base), LO32(p->pci_mem_base),
              HI32(p->cpu_mem_base), LO32(p->cpu_mem_base),
              HI32(p->mem_size), LO32(p->mem_size)));
    FDT(fdt_setprop(fdt, n, "interrupt-names", irq_names, sizeof(irq_names)));
    FDT(fdt_setprop_u32(fdt, n, "interrupt-parent", m->irq.controller->m.phandle + 3));
    FDT(CELLS(fdt, n, "interrupts", p->msi_irq, 4, p->hp_irq, 4));
    FDT(fdt_setprop_u32(fdt, n, "msi-parent", m->irq.controller->m.phandle + 1));
    return 0;
}

static int emit_finisher(void *fdt, ComposeModule *m)
{
    static const char compat[] = "sifive,test1\0sifive,test0\0syscon";
    int n = unit_node(fdt, "/soc", "test", m->cfg.finisher.base);
    FDT(n);
    FDT(fdt_setprop(fdt, n, "compatible", compat, sizeof(compat)));
    FDT(reg(fdt, n, m->cfg.finisher.base, 0x1000));
    FDT(fdt_setprop_u32(fdt, n, "phandle", m->phandle));
    for (unsigned reboot = 0; reboot < 2; reboot++) {
        n = fdt_add_subnode(fdt, 0, reboot ? "reboot" : "poweroff");
        FDT(n);
        FDT(fdt_setprop_string(fdt, n, "compatible",
                              reboot ? "syscon-reboot" : "syscon-poweroff"));
        FDT(fdt_setprop_u32(fdt, n, "regmap", m->phandle));
        FDT(fdt_setprop_u32(fdt, n, "offset", 0));
        FDT(fdt_setprop_u32(fdt, n, "value", reboot ? 0x7777 : 0x5555));
    }
    return 0;
}

static int emit_module(void *fdt, ComposeModule *m)
{
    int n;
    switch (m->kind) {
    case COMPOSE_MEMORY:
        if (!m->cfg.ram.system_ram) {
            return 0;
        }
        n = unit_node(fdt, "/", "memory", m->cfg.ram.base);
        FDT(n);
        FDT(fdt_setprop_string(fdt, n, "device_type", "memory"));
        FDT(reg(fdt, n, m->cfg.ram.base, m->cfg.ram.size));
        break;
    case COMPOSE_AIA:
        return emit_aia(fdt, m);
    case COMPOSE_ACLINT:
        n = unit_node(fdt, "/soc", "clint", m->cfg.aclint.base);
        FDT(n);
        FDT(fdt_setprop_string(fdt, n, "compatible", "riscv,clint0"));
        FDT(reg(fdt, n, m->cfg.aclint.base, 0x10000));
        FDT(CELLS(fdt, n, "interrupts-extended",
                  m->soc->cpu->m.phandle, COMPOSE_IRQ_MSIP,
                  m->soc->cpu->m.phandle, COMPOSE_IRQ_MTIP));
        break;
    case COMPOSE_UART:
        n = unit_node(fdt, "/soc", "serial", m->cfg.uart.base);
        FDT(n);
        FDT(fdt_setprop_string(fdt, n, "compatible", "ns16550a"));
        FDT(reg(fdt, n, m->cfg.uart.base, 0x1000));
        FDT(fdt_setprop_u32(fdt, n, "reg-shift", 2));
        FDT(fdt_setprop_u32(fdt, n, "reg-io-width", 4));
        FDT(fdt_setprop_u32(fdt, n, "clock-frequency", 50000000));
        FDT(fdt_setprop_u32(fdt, n, "current-speed", 115200));
        FDT(emit_irq(fdt, n, m));
        break;
    case COMPOSE_BLK:
        n = unit_node(fdt, "/soc", "virtio", m->cfg.blk.base);
        FDT(n);
        FDT(fdt_setprop_string(fdt, n, "compatible", "virtio,mmio"));
        FDT(reg(fdt, n, m->cfg.blk.base, 0x1000));
        FDT(emit_irq(fdt, n, m));
        FDT(fdt_setprop_empty(fdt, n, "dma-coherent"));
        break;
    case COMPOSE_PCIE:
        return emit_dwc(fdt, m);
    case COMPOSE_FINISHER:
        return emit_finisher(fdt, m);
    case COMPOSE_CPU:
    case COMPOSE_NVME:              /* PCI endpoints are enumerated by Linux */
        break;
    }
    return 0;
}

static int emit_tree(void *fdt, LibQemuSoc *s, const char *bootargs, const char *isa)
{
    FDT(fdt_setprop_u32(fdt, 0, "#address-cells", 2));
    FDT(fdt_setprop_u32(fdt, 0, "#size-cells", 2));
    FDT(fdt_setprop_string(fdt, 0, "model", s->config.model));
    FDT(fdt_setprop_string(fdt, 0, "compatible", s->config.compatible));
    int n = fdt_add_subnode(fdt, 0, "chosen");
    FDT(n);
    FDT(fdt_setprop_string(fdt, n, "bootargs", bootargs ? bootargs : ""));
    if (s->console) {
        g_autofree char *path = g_strdup_printf("/soc/serial@%" PRIx64 ":115200n8",
                                                s->console->cfg.uart.base);
        FDT(fdt_setprop_string(fdt, n, "stdout-path", path));
    }
    FDT(emit_cpu(fdt, s, isa));
    n = fdt_add_subnode(fdt, 0, "soc");
    FDT(n);
    FDT(fdt_setprop_u32(fdt, n, "#address-cells", 2));
    FDT(fdt_setprop_u32(fdt, n, "#size-cells", 2));
    FDT(fdt_setprop_string(fdt, n, "compatible", "simple-bus"));
    FDT(fdt_setprop_empty(fdt, n, "ranges"));
    for (unsigned i = 0; i < s->modules->len; i++) {
        FDT(emit_module(fdt, g_ptr_array_index(s->modules, i)));
    }
    return fdt_pack(fdt);
}

int libqemu_soc_build_dtb(LibQemuSoc *s, const char *bootargs,
                         LibQemuDtb **out, char **errp)
{
    if (out) {
        *out = NULL;
    }
    if (compose_ready(s, errp)) {
        return -1;
    }
    if (!out || !s->cpu || !s->aia || !s->aclint || !s->dram) {
        return compose_error(errp,
                             "compose:dtb:requires CPU, AIA, ACLINT and RAM");
    }
    for (unsigned i = 0; i < s->modules->len; i++) {
        ComposeModule *m = g_ptr_array_index(s->modules, i);
        if (!m->complete) {
            return compose_error(errp, "compose:dtb:%s: incomplete module", m->name);
        }
    }
    LibQemuCompose *c = s->compose;
    char *qe = NULL;
    char *isa = c->core->property_get_str(s->cpu->m.object, "isa-string", &qe);
    if (!isa) {
        return compose_qerror(c, errp, "dtb", "isa-string", qe);
    }
    LibQemuDtb *d = g_new0(LibQemuDtb, 1);
    int ret;
    /* Retry entire emission on growth, avoiding retained mutable node offsets. */
    for (size_t capacity = 16384; ; capacity *= 2) {
        d->blob = g_realloc(d->blob, capacity);
        ret = fdt_create_empty_tree(d->blob, capacity);
        if (!ret) {
            ret = emit_tree(d->blob, s, bootargs, isa);
        }
        if (ret != -FDT_ERR_NOSPACE || capacity >= 2 * 1024 * 1024) {
            break;
        }
    }
    c->api->error_free(isa);
    if (ret) {
        libqemu_dtb_free(d);
        return compose_error(errp, "compose:dtb:build: %s", fdt_strerror(ret));
    }
    d->soc = s;
    d->generation = s->generation;
    d->size = fdt_totalsize(d->blob);
    *out = d;
    return 0;
}

const void *libqemu_dtb_data(const LibQemuDtb *d, size_t *size)
{
    if (size) {
        *size = d ? d->size : 0;
    }
    return d ? d->blob : NULL;
}

int libqemu_dtb_write(const LibQemuDtb *d, const char *path, char **errp)
{
    GError *error = NULL;
    if (!d || !path) {
        return compose_error(errp, "compose:dtb:write:invalid arguments");
    }
    if (!g_file_set_contents(path, d->blob, d->size, &error)) {
        compose_error(errp, "compose:dtb:write:%s: %s", path, error->message);
        g_error_free(error);
        return -1;
    }
    return 0;
}

void libqemu_dtb_free(LibQemuDtb *d)
{
    if (d) {
        g_free(d->blob);
        g_free(d);
    }
}
