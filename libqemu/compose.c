/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "compose-internal.h"

int compose_error(char **errp, const char *fmt, ...)
{
    if (errp) {
        va_list ap;
        va_start(ap, fmt);
        *errp = g_strdup_vprintf(fmt, ap);
        va_end(ap);
    }
    return -1;
}

void libqemu_compose_error_free(char *error)
{
    g_free(error);
}

int compose_qerror(LibQemuCompose *c, char **errp, const char *stage,
                   const char *name, char *qerr)
{
    int ret = compose_error(errp, "compose:%s:%s: %s", stage, name,
                            qerr ? qerr : "QEMU operation failed");
    c->api->error_free(qerr);
    c->failed = true;
    return ret;
}

/* Evaluate call before inspecting the error it writes. */
#define QCALL(c, call, errp, stage, name) \
    ({ qe = NULL; int qr = (call); \
       qr ? compose_qerror(c, errp, stage, name, qe) : 0; })

static const char *save_string(LibQemuCompose *c, const char *str)
{
    char *copy = g_strdup(str);
    g_ptr_array_add(c->strings, copy);
    return copy;
}

static bool token(const char *v)
{
    if (!v || !*v || !strcmp(v, ".") || !strcmp(v, "..")) {
        return false;
    }
    for (; *v; v++) {
        if (!(g_ascii_isalnum(*v) || *v == '_' || *v == '-' || *v == '.')) {
            return false;
        }
    }
    return true;
}

static bool option_id(const char *v)
{
    /* QemuOpts IDs must start with a letter (util/id.c). */
    return token(v) && g_ascii_isalpha(v[0]);
}

static char *drive_spec(const LibQemuComposeBackend *b)
{
    GString *spec = g_string_new(NULL);
    g_string_append_printf(spec, "if=none,id=%s,format=%s,file=", b->id, b->format);
    for (const char *p = b->path; *p; p++) {
        g_string_append_c(spec, *p);
        if (*p == ',') {
            g_string_append_c(spec, ',');
        }
    }
    if (b->read_only) {
        g_string_append(spec, ",readonly=on");
    }
    return g_string_free(spec, false);
}

static bool abi_supported(const LibQemuExports *api)
{
    if (!api || api->abi_version != LIBQEMU_ABI_VERSION ||
        api->struct_size < sizeof(*api) || !api->core ||
        api->core->struct_size < sizeof(*api->core) ||
        !api->init || !api->finish_qemu_init || !api->get_state ||
        !api->request_shutdown || !api->wait || !api->error_free) {
        return false;
    }
    const LibQemuCoreExports *k = api->core;
    return k->object_new && k->object_unref && k->object_resolve &&
        k->object_add_child && k->property_set_uint && k->property_set_bool &&
        k->property_set_str && k->property_set_link && k->property_get_str &&
        k->device_realize && k->device_get_bus && k->sysbus_get_mmio &&
        k->sysbus_connect_irq && k->gpio_in && k->gpio_connect &&
        k->system_memory && k->memory_new && k->memory_add && k->memory_object &&
        k->cpu_register_reset && k->rom_add_blob;
}

int libqemu_compose_init(const LibQemuExports *api,
                         const LibQemuComposeLaunch *launch,
                         LibQemuCompose **out, char **errp)
{
    const LibQemuComposeLaunch defaults = { .accel = "tcg", .smp = 1 };
    const char *args[] = {
        "libqemu-compose", "-M", "none", "-m", "0", "-smp", "1",
        "-accel", "tcg", "-display", "none", "-monitor", "none",
        "-serial", "none",
    };
    LibQemuCompose *c;
    GPtrArray *argv;
    char *qe = NULL;
    unsigned stdios = 0;
    int ret = -1;

    if (out) {
        *out = NULL;
    }
    if (!out || !abi_supported(api)) {
        return compose_error(errp, "compose:init:incompatible or incomplete ABI");
    }
    if (!launch) {
        launch = &defaults;
    }
    if (!launch->accel || strcmp(launch->accel, "tcg") || launch->smp != 1 ||
        launch->backend_count > 256 || launch->chardev_count > 256 ||
        (launch->backend_count && !launch->backends) ||
        (launch->chardev_count && !launch->chardevs)) {
        return compose_error(errp,
                             "compose:init:requires TCG, smp=1 and valid backends");
    }
    if (api->get_state() != LIBQEMU_NEW) {
        return compose_error(errp, "compose:init:runtime already initialized");
    }
    c = g_new0(LibQemuCompose, 1);
    c->api = api;
    c->core = api->core;
    c->strings = g_ptr_array_new_with_free_func(g_free);
    c->backends = g_hash_table_new(g_str_hash, g_str_equal);
    c->chardevs = g_hash_table_new(g_str_hash, g_str_equal);
    c->used_backends = g_hash_table_new(g_str_hash, g_str_equal);
    c->used_chardevs = g_hash_table_new(g_str_hash, g_str_equal);
    argv = g_ptr_array_new_with_free_func(g_free);
    for (size_t i = 0; i < G_N_ELEMENTS(args); i++) {
        g_ptr_array_add(argv, g_strdup(args[i]));
    }
    for (size_t i = 0; i < launch->backend_count; i++) {
        const LibQemuComposeBackend *b = &launch->backends[i];
        if (!option_id(b->id) || !b->path || !*b->path || !token(b->format) ||
            g_hash_table_contains(c->backends, b->id)) {
            compose_error(errp, "compose:init:backend[%zu]: invalid or duplicate", i);
            goto done;
        }
        g_hash_table_add(c->backends, (void *)save_string(c, b->id));
        g_ptr_array_add(argv, g_strdup("-drive"));
        g_ptr_array_add(argv, drive_spec(b));
    }
    for (size_t i = 0; i < launch->chardev_count; i++) {
        const LibQemuComposeChardev *ch = &launch->chardevs[i];
        if (!option_id(ch->id) ||
            (ch->kind != LIBQEMU_CHAR_STDIO && ch->kind != LIBQEMU_CHAR_NULL) ||
            g_hash_table_contains(c->chardevs, ch->id) ||
            (ch->kind == LIBQEMU_CHAR_STDIO && ++stdios > 1)) {
            compose_error(errp, "compose:init:chardev[%zu]: invalid or duplicate", i);
            goto done;
        }
        g_hash_table_add(c->chardevs, (void *)save_string(c, ch->id));
        g_ptr_array_add(argv, g_strdup("-chardev"));
        g_ptr_array_add(argv, g_strdup_printf("%s,id=%s%s",
            ch->kind == LIBQEMU_CHAR_STDIO ? "stdio" : "null", ch->id,
            ch->kind == LIBQEMU_CHAR_STDIO ? ",signal=off" : ""));
    }
    g_ptr_array_add(argv, NULL);
    if (api->init(argv->len - 1, (const char *const *)argv->pdata, &qe)) {
        compose_qerror(c, errp, "init", "qemu", qe);
        goto done;
    }
    c->machine = c->core->object_resolve("/machine", &qe);
    if (!c->machine) {
        compose_qerror(c, errp, "init", "machine", qe);
        goto stop;
    }
    c->memory = c->core->system_memory(&qe);
    if (!c->memory) {
        compose_qerror(c, errp, "init", "system-memory", qe);
        goto stop;
    }
    *out = c;
    ret = 0;
    goto done;
stop:
    api->request_shutdown(NULL);
    {
        int status;
        api->wait(&status, NULL);
    }
done:
    g_ptr_array_free(argv, true);
    if (ret) {
        libqemu_compose_free(c);
    }
    return ret;
}

static void module_free(void *data)
{
    ComposeModule *m = data;
    if (m->kind == COMPOSE_AIA) {
        g_hash_table_destroy(((LibQemuAia *)m)->sources);
    }
    g_free(m);
}

void libqemu_compose_free(LibQemuCompose *c)
{
    if (!c) {
        return;
    }
    if (c->soc) {
        g_ptr_array_free(c->soc->modules, true);
        g_array_free(c->soc->ranges, true);
        g_hash_table_destroy(c->soc->names);
        g_free(c->soc);
    }
    g_hash_table_destroy(c->backends);
    g_hash_table_destroy(c->chardevs);
    g_hash_table_destroy(c->used_backends);
    g_hash_table_destroy(c->used_chardevs);
    g_ptr_array_free(c->strings, true);
    g_free(c);
}

int compose_ready(LibQemuSoc *s, char **errp)
{
    if (!s || s->compose->failed || s->boot_loaded ||
        s->compose->api->get_state() != LIBQEMU_ASSEMBLY) {
        return compose_error(errp, "compose:phase:requires healthy, unsealed ASSEMBLY");
    }
    return 0;
}

int libqemu_soc_new(LibQemuCompose *c, const LibQemuSocConfig *cfg,
                    LibQemuSoc **out, char **errp)
{
    if (out) {
        *out = NULL;
    }
    if (!c || !cfg || !out || !cfg->model || !*cfg->model ||
        !cfg->compatible || !*cfg->compatible || c->soc || c->failed ||
        c->api->get_state() != LIBQEMU_ASSEMBLY) {
        return compose_error(errp, "compose:soc:invalid config, phase or second SoC");
    }
    LibQemuSoc *s = g_new0(LibQemuSoc, 1);
    s->compose = c;
    s->config.model = save_string(c, cfg->model);
    s->config.compatible = save_string(c, cfg->compatible);
    s->modules = g_ptr_array_new_with_free_func(module_free);
    s->ranges = g_array_new(false, false, sizeof(ComposeRange));
    s->names = g_hash_table_new(g_str_hash, g_str_equal);
    s->next_phandle = 1;
    c->soc = s;
    *out = s;
    return 0;
}

bool compose_range_valid(uint64_t base, uint64_t size)
{
    return size && base <= UINT64_MAX - size;
}

bool compose_overlap(uint64_t a, uint64_t as, uint64_t b, uint64_t bs)
{
    return a < b + bs && b < a + as;
}

unsigned compose_guest_bits(unsigned files)
{
    unsigned bits = 0;
    while ((1U << bits) < files + 1) {
        bits++;
    }
    return bits;
}

/* Reserve firmware-visible ranges, including PCI windows and IMSIC padding.
 * Validate the whole module before modifying registry or QEMU topology.
 */
static int validate(LibQemuSoc *s, const char *name, const ComposeRange *r,
                     size_t n, LibQemuWiredIrq irq, char **errp)
{
    if (compose_ready(s, errp)) {
        return -1;
    }
    if (!token(name) || g_hash_table_contains(s->names, name)) {
        return compose_error(errp, "compose:validate:%s: invalid/duplicate name",
                             name ? name : "(null)");
    }
    if (irq.controller) {
        LibQemuAia *a = irq.controller;
        if (a != s->aia || !a->m.complete || !irq.source ||
            irq.source > a->m.cfg.aia.num_sources ||
            g_hash_table_contains(a->sources, GUINT_TO_POINTER(irq.source))) {
            return compose_error(errp, "compose:validate:%s: invalid/occupied IRQ %u",
                                 name, irq.source);
        }
    } else if (irq.source) {
        return compose_error(errp, "compose:validate:%s: missing IRQ controller", name);
    }
    for (size_t i = 0; i < n; i++) {
        if (!compose_range_valid(r[i].base, r[i].size) || (r[i].base & 0xfff) ||
            (r[i].size & 0xfff)) {
            return compose_error(errp,
                                 "compose:validate:%s: invalid/unaligned range",
                                 name);
        }
        for (size_t j = 0; j < i; j++) {
            if (compose_overlap(r[i].base, r[i].size, r[j].base, r[j].size)) {
                return compose_error(errp,
                                     "compose:validate:%s: overlapping windows",
                                     name);
            }
        }
        for (unsigned j = 0; j < s->ranges->len; j++) {
            const ComposeRange *old = &g_array_index(s->ranges, ComposeRange, j);
            if (compose_overlap(r[i].base, r[i].size, old->base, old->size)) {
                return compose_error(errp, "compose:validate:%s: range overlaps %s",
                                     name, old->name);
            }
        }
    }
    return 0;
}

static ComposeModule *module_new(LibQemuSoc *s, ComposeKind kind, size_t size,
                                  const char *name, const ComposeRange *r,
                                  size_t n, LibQemuWiredIrq irq)
{
    ComposeModule *m = g_malloc0(size);
    m->kind = kind;
    m->soc = s;
    m->name = save_string(s->compose, name);
    m->irq = irq;
    m->phandle = s->next_phandle;
    s->next_phandle += kind == COMPOSE_AIA ? 4 : 1;
    g_ptr_array_add(s->modules, m);
    g_hash_table_add(s->names, (void *)m->name);
    for (size_t i = 0; i < n; i++) {
        ComposeRange copy = { r[i].base, r[i].size, m->name };
        g_array_append_val(s->ranges, copy);
    }
    if (irq.controller) {
        g_hash_table_add(irq.controller->sources, GUINT_TO_POINTER(irq.source));
    }
    s->generation++;
    return m;
}

static LibQemuObject *new_object(ComposeModule *m, LibQemuObject *parent,
                                 const char *name, const char *type, char **errp)
{
    LibQemuCompose *c = m->soc->compose;
    char *qe = NULL;
    LibQemuObject *obj = c->core->object_new(type, &qe);
    if (!obj) {
        compose_qerror(c, errp, "create", m->name, qe);
        return NULL;
    }
    if (QCALL(c, c->core->object_add_child(parent, name, obj, &qe), errp,
              "parent", m->name)) {
        c->core->object_unref(obj, NULL);
        return NULL;
    }
    if (QCALL(c, c->core->object_unref(obj, &qe), errp, "unref", m->name)) {
        return NULL;
    }
    return obj;
}

static LibQemuObject *device(ComposeModule *m, const char *name,
                             const char *type, char **errp)
{
    if (!m->container) {
        m->container = new_object(m, m->soc->compose->machine, m->name,
                                  "container", errp);
        if (!m->container) {
            return NULL;
        }
    }
    return new_object(m, m->container, name, type, errp);
}

#define SETTER(fn, ctype, member) \
static int fn(ComposeModule *m, LibQemuObject *obj, const char *key, \
               ctype value, char **errp) \
{ \
    LibQemuCompose *c = m->soc->compose; \
    char *qe = NULL; \
    return QCALL(c, c->core->member(obj, key, value, &qe), errp, key, m->name); \
}
SETTER(set_uint, uint64_t, property_set_uint)
SETTER(set_bool, bool, property_set_bool)
SETTER(set_str, const char *, property_set_str)
SETTER(set_link, LibQemuObject *, property_set_link)

static int realize(ComposeModule *m, LibQemuObject *obj,
                    LibQemuObject *bus, char **errp)
{
    LibQemuCompose *c = m->soc->compose;
    char *qe = NULL;
    return QCALL(c, c->core->device_realize(obj, bus, &qe), errp, "realize", m->name);
}

static int map_mmio(ComposeModule *m, LibQemuObject *obj, uint64_t base,
                     char **errp)
{
    LibQemuCompose *c = m->soc->compose;
    char *qe = NULL;
    LibQemuMemory *mr = c->core->sysbus_get_mmio(obj, 0, &qe);
    if (!mr) {
        return compose_qerror(c, errp, "mmio", m->name, qe);
    }
    return QCALL(c, c->core->memory_add(c->memory, base, mr, 0, &qe),
                  errp, "map", m->name);
}

static int wire(ComposeModule *m, LibQemuObject *source, unsigned output,
                 LibQemuObject *target, unsigned input, bool sysbus, char **errp)
{
    LibQemuCompose *c = m->soc->compose;
    char *qe = NULL;
    LibQemuIRQ *irq = c->core->gpio_in(target, NULL, input, &qe);
    if (!irq) {
        return compose_qerror(c, errp, "irq-input", m->name, qe);
    }
    if (sysbus) {
        return QCALL(c, c->core->sysbus_connect_irq(source, output, irq, &qe),
                      errp, "irq-wire", m->name);
    }
    return QCALL(c, c->core->gpio_connect(source, NULL, output, irq, &qe),
                  errp, "irq-wire", m->name);
}

static int peripheral_finish(ComposeModule *m, uint64_t base,
                              unsigned output, char **errp)
{
    if (realize(m, m->object, NULL, errp) ||
        map_mmio(m, m->object, base, errp) ||
        wire(m, m->object, output, m->irq.controller->aplic_m,
             m->irq.source, true, errp)) {
        return -1;
    }
    m->complete = true;
    return 0;
}

int libqemu_soc_add_memory(LibQemuSoc *s, const LibQemuRamConfig *cfg, char **errp)
{
    if (!cfg || (cfg->kind != LIBQEMU_RAM && cfg->kind != LIBQEMU_ROM) ||
        (cfg->system_ram && (cfg->kind != LIBQEMU_RAM || (s && s->dram)))) {
        return compose_error(errp, "compose:memory:invalid kind or second system RAM");
    }
    ComposeRange r = { cfg->base, cfg->size };
    if (validate(s, cfg->name, &r, 1, (LibQemuWiredIrq){0}, errp)) {
        return -1;
    }
    ComposeModule *m = module_new(s, COMPOSE_MEMORY, sizeof(*m), cfg->name,
                                  &r, 1, (LibQemuWiredIrq){0});
    LibQemuCompose *c = s->compose;
    m->cfg.ram = *cfg;
    m->cfg.ram.name = m->name;
    LibQemuMemoryConfig mc = { .kind = cfg->kind, .name = m->name, .size = cfg->size };
    char *qe = NULL;
    LibQemuMemory *mr = c->core->memory_new(&mc, &qe);
    if (!mr) {
        return compose_qerror(c, errp, "memory", m->name, qe);
    }
    if (QCALL(c, c->core->memory_add(c->memory, cfg->base, mr, 0, &qe),
              errp, "map", m->name)) {
        return -1;
    }
    if (cfg->system_ram) {
        s->dram = m;
    }
    m->complete = true;
    return 0;
}

static bool power_of_two(unsigned x)
{
    return x && !(x & (x - 1));
}

int libqemu_riscv_cpu_create(LibQemuSoc *s, const LibQemuCpuConfig *cfg,
                            LibQemuCpu **out, char **errp)
{
    if (out) {
        *out = NULL;
    }
    if (!cfg || !out || !token(cfg->type) || !cfg->mmu_type ||
        (s && s->cpu) || (cfg->resetvec & 7) ||
        !power_of_two(cfg->cbom_block_size) || !power_of_two(cfg->cboz_block_size) ||
        cfg->cbom_block_size > UINT16_MAX || cfg->cboz_block_size > UINT16_MAX) {
        return compose_error(errp, "compose:cpu:invalid config or second CPU");
    }
    if (validate(s, cfg->name, NULL, 0, (LibQemuWiredIrq){0}, errp)) {
        return -1;
    }
    LibQemuCpu *cpu = (LibQemuCpu *)module_new(s, COMPOSE_CPU, sizeof(*cpu),
                              cfg->name, NULL, 0, (LibQemuWiredIrq){0});
    ComposeModule *m = &cpu->m;
    LibQemuCompose *c = s->compose;
    m->cfg.cpu = *cfg;
    m->cfg.cpu.name = m->name;
    m->cfg.cpu.type = save_string(c, cfg->type);
    m->cfg.cpu.mmu_type = save_string(c, cfg->mmu_type);
    m->object = device(m, "cpu", cfg->type, errp);
    if (!m->object || set_uint(m, m->object, "resetvec", cfg->resetvec, errp) ||
        set_uint(m, m->object, "cbom_blocksize", cfg->cbom_block_size, errp) ||
        set_uint(m, m->object, "cboz_blocksize", cfg->cboz_block_size, errp)) {
        return -1;
    }
    char *qe = NULL;
    LibQemuObject *mem = c->core->memory_object(c->memory, &qe);
    if (!mem) {
        return compose_qerror(c, errp, "cpu-memory", m->name, qe);
    }
    if (set_link(m, m->object, "memory", mem, errp) ||
        realize(m, m->object, NULL, errp) ||
        QCALL(c, c->core->cpu_register_reset(m->object, &qe), errp, "reset", m->name)) {
        return -1;
    }
    m->complete = true;
    s->cpu = cpu;
    *out = cpu;
    return 0;
}

int libqemu_riscv_aia_create(LibQemuSoc *s, LibQemuCpu *cpu,
                            const LibQemuAiaConfig *cfg, LibQemuAia **out,
                            char **errp)
{
    if (out) {
        *out = NULL;
    }
    if (!s || !cfg || !out || !cpu || cpu != s->cpu || s->aia ||
        !cfg->num_sources || cfg->num_sources > 1023 || cfg->num_ids < 63 ||
        cfg->num_ids > 2047 || ((cfg->num_ids + 1) % 64) || cfg->guest_files > 63) {
        return compose_error(errp, "compose:aia:invalid CPU, counts or second AIA");
    }
    uint64_t stride = UINT64_C(0x1000) << compose_guest_bits(cfg->guest_files);
    ComposeRange r[] = {
        { cfg->imsic_m_base, 0x1000 }, { cfg->imsic_s_base, stride },
        { cfg->aplic_m_base, 0x4000 }, { cfg->aplic_s_base, 0x4000 },
    };
    if ((cfg->imsic_s_base & (stride - 1)) ||
        validate(s, cfg->name, r, G_N_ELEMENTS(r), (LibQemuWiredIrq){0}, errp)) {
        if (cfg->imsic_s_base & (stride - 1)) {
            compose_error(errp,
                          "compose:aia:S IMSIC base must align with guest stride");
        }
        return -1;
    }
    LibQemuAia *a = (LibQemuAia *)module_new(s, COMPOSE_AIA, sizeof(*a),
                         cfg->name, r, G_N_ELEMENTS(r), (LibQemuWiredIrq){0});
    ComposeModule *m = &a->m;
    m->cfg.aia = *cfg;
    m->cfg.aia.name = m->name;
    a->sources = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (unsigned mode = 0; mode < 2; mode++) {
        bool mmode = mode == 0;
        LibQemuObject *obj = device(m, mmode ? "imsic-m" : "imsic-s",
                                    "riscv.imsic", errp);
        if (!obj || set_bool(m, obj, "mmode", mmode, errp) ||
            set_uint(m, obj, "hartid", 0, errp) ||
            set_uint(m, obj, "num-pages", mmode ? 1 : cfg->guest_files + 1, errp) ||
            set_uint(m, obj, "num-irqs", cfg->num_ids + 1, errp) ||
            realize(m, obj, NULL, errp) ||
            map_mmio(m, obj, mmode ? cfg->imsic_m_base : cfg->imsic_s_base, errp) ||
            wire(m, obj, 0, cpu->m.object,
                 mmode ? COMPOSE_IRQ_MEIP : COMPOSE_IRQ_SEIP, false, errp)) {
            return -1;
        }
        if (!mmode) {
            for (unsigned i = 0; i < cfg->guest_files; i++) {
                if (wire(m, obj, i + 1, cpu->m.object,
                         COMPOSE_IRQ_GUEST_BASE + i, false, errp)) {
                    return -1;
                }
            }
        }
    }
    a->aplic_m = device(m, "aplic-m", "riscv.aplic", errp);
    if (!a->aplic_m) {
        return -1;
    }
    a->aplic_s = device(m, "aplic-s", "riscv.aplic", errp);
    if (!a->aplic_s) {
        return -1;
    }
    for (unsigned mode = 0; mode < 2; mode++) {
        LibQemuObject *obj = mode ? a->aplic_s : a->aplic_m;
        if (set_uint(m, obj, "aperture-size", 0x4000, errp) ||
            set_uint(m, obj, "num-irqs", cfg->num_sources + 1, errp) ||
            set_uint(m, obj, "iprio-mask", 1, errp) ||
            set_bool(m, obj, "msimode", true, errp) ||
            set_bool(m, obj, "mmode", !mode, errp)) {
            return -1;
        }
    }
    if (set_link(m, a->aplic_s, "delegate-parent", a->aplic_m, errp) ||
        realize(m, a->aplic_m, NULL, errp) || realize(m, a->aplic_s, NULL, errp) ||
        map_mmio(m, a->aplic_m, cfg->aplic_m_base, errp) ||
        map_mmio(m, a->aplic_s, cfg->aplic_s_base, errp)) {
        return -1;
    }
    m->complete = true;
    s->aia = a;
    *out = a;
    return 0;
}

int libqemu_riscv_aclint_create(LibQemuSoc *s, LibQemuCpu *cpu,
                               const LibQemuAclintConfig *cfg, char **errp)
{
    if (!s || !cfg || !cpu || cpu != s->cpu || s->aclint || !cfg->timebase_freq) {
        return compose_error(errp,
                             "compose:aclint:invalid CPU/frequency or second timer");
    }
    ComposeRange r = { cfg->base, 0x10000 };
    if (validate(s, cfg->name, &r, 1, (LibQemuWiredIrq){0}, errp)) {
        return -1;
    }
    ComposeModule *m = module_new(s, COMPOSE_ACLINT, sizeof(*m), cfg->name,
                                  &r, 1, (LibQemuWiredIrq){0});
    m->cfg.aclint = *cfg;
    m->cfg.aclint.name = m->name;
    LibQemuObject *obj = device(m, "mswi", "riscv.aclint.swi", errp);
    if (!obj || realize(m, obj, NULL, errp) || map_mmio(m, obj, cfg->base, errp) ||
        wire(m, obj, 0, cpu->m.object, COMPOSE_IRQ_MSIP, false, errp)) {
        return -1;
    }
    obj = device(m, "mtimer", "riscv.aclint.mtimer", errp);
    if (!obj || set_uint(m, obj, "timebase-freq", cfg->timebase_freq, errp) ||
        set_uint(m, obj, "aperture-size", 0x8000, errp) ||
        set_bool(m, obj, "provide-rdtime", true, errp) ||
        realize(m, obj, NULL, errp) || map_mmio(m, obj, cfg->base + 0x4000, errp) ||
        wire(m, obj, 0, cpu->m.object, COMPOSE_IRQ_MTIP, false, errp)) {
        return -1;
    }
    m->complete = true;
    s->aclint = m;
    return 0;
}

int libqemu_uart_create(LibQemuSoc *s, const LibQemuUartConfig *cfg,
                        LibQemuAia *aia, char **errp)
{
    if (!s || !cfg || !aia || !cfg->chardev ||
        !g_hash_table_contains(s->compose->chardevs, cfg->chardev) ||
        g_hash_table_contains(s->compose->used_chardevs, cfg->chardev) ||
        (cfg->console && s->console)) {
        return compose_error(errp,
                             "compose:uart:missing/used chardev or second console");
    }
    ComposeRange r = { cfg->base, 0x1000 };
    LibQemuWiredIrq irq = { aia, cfg->irq };
    if (validate(s, cfg->name, &r, 1, irq, errp)) {
        return -1;
    }
    ComposeModule *m = module_new(s, COMPOSE_UART, sizeof(*m), cfg->name, &r, 1, irq);
    m->cfg.uart = *cfg;
    m->cfg.uart.name = m->name;
    m->cfg.uart.chardev = save_string(s->compose, cfg->chardev);
    g_hash_table_add(s->compose->used_chardevs, (void *)m->cfg.uart.chardev);
    m->object = device(m, "uart", "serial-mm", errp);
    if (!m->object || set_uint(m, m->object, "regshift", 2, errp) ||
        set_uint(m, m->object, "endianness", COMPOSE_LITTLE_ENDIAN, errp) ||
        set_uint(m, m->object, "baudbase", 115200, errp) ||
        set_str(m, m->object, "chardev", cfg->chardev, errp) ||
        peripheral_finish(m, cfg->base, 0, errp)) {
        return -1;
    }
    if (cfg->console) {
        s->console = m;
    }
    return 0;
}

int libqemu_my_virtio_blk_create(LibQemuSoc *s, const LibQemuVirtioBlkConfig *cfg,
                                 LibQemuAia *aia, char **errp)
{
    struct stat st;
    if (!cfg || !aia || !cfg->image || stat(cfg->image, &st) ||
        !S_ISREG(st.st_mode) || st.st_size < 512 || st.st_size % 512) {
        return compose_error(errp, "compose:my-virtio-blk:requires a raw "
                             "sector-aligned image");
    }
    ComposeRange r = { cfg->base, 0x1000 };
    LibQemuWiredIrq irq = { aia, cfg->irq };
    if (validate(s, cfg->name, &r, 1, irq, errp)) {
        return -1;
    }
    ComposeModule *m = module_new(s, COMPOSE_BLK, sizeof(*m), cfg->name, &r, 1, irq);
    m->cfg.blk = *cfg;
    m->cfg.blk.name = m->name;
    m->cfg.blk.image = save_string(s->compose, cfg->image);
    m->object = device(m, "blk", "my-virtio-blk", errp);
    if (!m->object || set_uint(m, m->object, "mmio-base", cfg->base, errp) ||
        set_uint(m, m->object, "mmio-size", 0x1000, errp) ||
        set_bool(m, m->object, "packed", cfg->packed, errp) ||
        set_str(m, m->object, "image-path", cfg->image, errp)) {
        return -1;
    }
    return peripheral_finish(m, cfg->base, 0, errp);
}

int libqemu_dwc_create(LibQemuSoc *s, const LibQemuDwcConfig *cfg, LibQemuAia *aia,
                       LibQemuPcie **out, char **errp)
{
    if (out) {
        *out = NULL;
    }
    if (!cfg || !out || !aia || cfg->domain > 65535 || cfg->dbi_size < 0x1000 ||
        cfg->config_size < 0x10000 || (cfg->pci_mem_base & 0xfff) ||
        !compose_range_valid(cfg->pci_mem_base, cfg->mem_size) ||
        cfg->pci_mem_base + cfg->mem_size > (UINT64_C(1) << 32)) {
        return compose_error(errp, "compose:dwc:invalid config/window/domain");
    }
    ComposeRange r[] = {
        { cfg->dbi_base, cfg->dbi_size }, { cfg->config_base, cfg->config_size },
        { cfg->cpu_mem_base, cfg->mem_size },
    };
    LibQemuWiredIrq irq = { aia, cfg->msi_irq };
    if (validate(s, cfg->name, r, G_N_ELEMENTS(r), irq, errp)) {
        return -1;
    }
    LibQemuWiredIrq hp = { aia, cfg->hp_irq };
    if (!cfg->hp_irq || cfg->hp_irq == cfg->msi_irq ||
        validate(s, cfg->name, NULL, 0, hp, errp)) {
        if (!cfg->hp_irq || cfg->hp_irq == cfg->msi_irq) {
            compose_error(errp,
                          "compose:dwc:%s: distinct reserved hp IRQ required",
                          cfg->name);
        }
        return -1;
    }
    for (unsigned i = 0; i < s->modules->len; i++) {
        ComposeModule *old = g_ptr_array_index(s->modules, i);
        if (old->kind == COMPOSE_PCIE && old->cfg.pcie.domain == cfg->domain) {
            return compose_error(errp, "compose:dwc:%s: duplicate PCI domain",
                                 cfg->name);
        }
    }
    LibQemuPcie *p = (LibQemuPcie *)module_new(s, COMPOSE_PCIE, sizeof(*p),
                                 cfg->name, r, G_N_ELEMENTS(r), irq);
    ComposeModule *m = &p->m;
    LibQemuCompose *c = s->compose;
    g_hash_table_add(aia->sources, GUINT_TO_POINTER(cfg->hp_irq));
    m->cfg.pcie = *cfg;
    m->cfg.pcie.name = m->name;
    m->object = device(m, "host", "designware-pcie-host", errp);
    g_autofree char *root_name = g_strdup_printf("%s-root", m->name);
    g_autofree char *bus_name = g_strdup_printf("%s-bus", m->name);
    g_autofree char *bus_path = g_strdup_printf("%04x:00", cfg->domain);
    if (!m->object || set_str(m, m->object, "root-bus-name", root_name, errp) ||
        set_str(m, m->object, "sec-bus-name", bus_name, errp) ||
        set_str(m, m->object, "root-bus-path", bus_path, errp) ||
        set_bool(m, m->object, "dma-to-system-memory", true, errp) ||
        peripheral_finish(m, cfg->dbi_base, COMPOSE_DWC_MSI_OUTPUT, errp)) {
        return -1;
    }
    g_autofree char *root_path = g_strdup_printf("/machine/%s/host/root", m->name);
    char *qe = NULL;
    LibQemuObject *bridge = c->core->object_resolve(root_path, &qe);
    if (!bridge) {
        return compose_qerror(c, errp, "dwc-root", m->name, qe);
    }
    p->bus = c->core->device_get_bus(bridge, bus_name, &qe);
    if (!p->bus) {
        return compose_qerror(c, errp, "dwc-bus", m->name, qe);
    }
    *out = p;
    return 0;
}

int libqemu_nvme_attach(LibQemuPcie *p, const LibQemuNvmeConfig *cfg, char **errp)
{
    if (!p || !cfg || !cfg->backend_id || !cfg->serial || !*cfg->serial ||
        strlen(cfg->serial) > 20 || !p->m.complete) {
        return compose_error(errp,
                             "compose:nvme:invalid RC/backend/serial (1..20 bytes)");
    }
    LibQemuSoc *s = p->m.soc;
    LibQemuCompose *c = s->compose;
    if (!g_hash_table_contains(c->backends, cfg->backend_id) ||
        g_hash_table_contains(c->used_backends, cfg->backend_id)) {
        return compose_error(errp, "compose:nvme:%s: missing/used block backend",
                             cfg->name ? cfg->name : "(null)");
    }
    if (validate(s, cfg->name, NULL, 0, (LibQemuWiredIrq){0}, errp)) {
        return -1;
    }
    ComposeModule *m = module_new(s, COMPOSE_NVME, sizeof(*m), cfg->name,
                                  NULL, 0, (LibQemuWiredIrq){0});
    m->cfg.nvme = *cfg;
    m->cfg.nvme.name = m->name;
    m->cfg.nvme.backend_id = save_string(c, cfg->backend_id);
    m->cfg.nvme.serial = save_string(c, cfg->serial);
    g_hash_table_add(c->used_backends, (void *)m->cfg.nvme.backend_id);
    m->object = device(m, "nvme", "nvme", errp);
    if (!m->object || set_str(m, m->object, "serial", cfg->serial, errp) ||
        set_str(m, m->object, "drive", cfg->backend_id, errp) ||
        realize(m, m->object, p->bus, errp)) {
        return -1;
    }
    m->complete = true;
    return 0;
}

int libqemu_finisher_create(LibQemuSoc *s, const LibQemuFinisherConfig *cfg,
                            char **errp)
{
    if (!cfg || (s && s->finisher)) {
        return compose_error(errp,
                             "compose:finisher:invalid config or second finisher");
    }
    ComposeRange r = { cfg->base, 0x1000 };
    if (validate(s, cfg->name, &r, 1, (LibQemuWiredIrq){0}, errp)) {
        return -1;
    }
    ComposeModule *m = module_new(s, COMPOSE_FINISHER, sizeof(*m), cfg->name,
                                  &r, 1, (LibQemuWiredIrq){0});
    m->cfg.finisher = *cfg;
    m->cfg.finisher.name = m->name;
    m->object = device(m, "test", "riscv.sifive.test", errp);
    if (!m->object || realize(m, m->object, NULL, errp) ||
        map_mmio(m, m->object, cfg->base, errp)) {
        return -1;
    }
    s->finisher = m;
    m->complete = true;
    return 0;
}

LibQemuObject *libqemu_cpu_object(LibQemuCpu *c)
{
    return c ? c->m.object : NULL;
}

LibQemuObject *libqemu_pcie_bus(LibQemuPcie *p)
{
    return p ? p->bus : NULL;
}

LibQemuObject *libqemu_soc_machine(LibQemuSoc *s)
{
    return s ? s->compose->machine : NULL;
}
