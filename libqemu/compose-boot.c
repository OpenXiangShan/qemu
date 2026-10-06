/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <string.h>
#include "compose-internal.h"

typedef struct BootBlob {
    const char *name;
    uint64_t base;
    void *data;
    size_t size;
    uint64_t footprint;
} BootBlob;

static int read_blob(BootBlob *b, const char *path, size_t limit, char **errp)
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    long length;
    if (!f) {
        return compose_error(errp, "compose:boot:%s: cannot open %s", b->name,
                             path ? path : "(null)");
    }
    if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        return compose_error(errp, "compose:boot:%s: cannot seek", b->name);
    }
    length = ftell(f);
    if (length <= 0 || (unsigned long)length > limit || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return compose_error(errp, "compose:boot:%s: invalid/oversized file", b->name);
    }
    b->size = length;
    b->footprint = length;
    b->data = g_malloc(b->size);
    if (fread(b->data, 1, b->size, f) != b->size) {
        fclose(f);
        return compose_error(errp, "compose:boot:%s: short read", b->name);
    }
    fclose(f);
    if (b->size >= 4 && !memcmp(b->data, "\177ELF", 4)) {
        return compose_error(errp,
                             "compose:boot:%s: raw binary required, got ELF",
                             b->name);
    }
    return 0;
}

static uint64_t load_le64(const uint8_t *p)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; i++) {
        value |= (uint64_t)p[i] << (i * 8);
    }
    return value;
}

static bool contains(const LibQemuRamConfig *r, uint64_t base, uint64_t size)
{
    return compose_range_valid(base, size) && base >= r->base &&
           base + size <= r->base + r->size;
}

int libqemu_riscv_boot_load(LibQemuSoc *s, const LibQemuBootConfig *cfg,
                           const LibQemuDtb *dtb, char **errp)
{
    uint32_t reset_words[10] = {
        0x00000297, /* auipc t0, 0 */
        0x02828613, /* addi a2, t0, 40: fw_dynamic_info */
        0xf1402573, /* csrr a0, mhartid */
        0x0202b583, /* ld a1, 32(t0): DTB */
        0x0182b283, /* ld t0, 24(t0): firmware entry */
        0x00028067, /* jr t0 */
    };
    uint64_t info[6];
    uint8_t reset[sizeof(reset_words) + sizeof(info)];
    BootBlob blobs[4] = {
        { .name = "firmware" }, { .name = "kernel" },
        { .name = "fdt" }, { .name = "reset" },
    };
    int ret = -1;

    if (compose_ready(s, errp)) {
        return -1;
    }
    if (!cfg || !dtb || !s->dram || !s->cpu || dtb->soc != s ||
        dtb->generation != s->generation) {
        return compose_error(errp, "compose:boot:missing or stale topology/DTB");
    }
    if ((cfg->firmware_base & 3) || (cfg->kernel_base & 0x1fffff) ||
        (cfg->fdt_base & 7)) {
        return compose_error(errp, "compose:boot:alignment requires "
                             "firmware/4, kernel/2MiB, DTB/8");
    }
    blobs[0].base = cfg->firmware_base;
    blobs[1].base = cfg->kernel_base;
    blobs[2].base = cfg->fdt_base;
    blobs[2].data = dtb->blob;
    blobs[2].size = dtb->size;
    blobs[2].footprint = dtb->size;
    blobs[3].base = s->cpu->m.cfg.cpu.resetvec;
    blobs[3].data = reset;
    blobs[3].size = sizeof(reset);
    bool reset_mapped = false;
    for (unsigned i = 0; i < s->modules->len; i++) {
        ComposeModule *m = g_ptr_array_index(s->modules, i);
        if (m->kind == COMPOSE_MEMORY && m->complete &&
            m->cfg.ram.kind == LIBQEMU_ROM &&
            contains(&m->cfg.ram, blobs[3].base, blobs[3].size)) {
            reset_mapped = true;
        }
    }
    if (!reset_mapped) {
        return compose_error(errp,
                             "compose:boot:reset trampoline outside mapped ROM");
    }
    if (read_blob(&blobs[0], cfg->firmware, 128 * 1024 * 1024, errp) ||
        read_blob(&blobs[1], cfg->kernel, 512 * 1024 * 1024, errp)) {
        goto done;
    }
    /* Linux's effective image_size includes BSS, absent from the raw file.
     * Do not place the FDT in memory which the kernel will zero on entry.
     * Offsets follow arch/riscv/include/asm/image.h in the Linux tree.
     */
    const uint8_t *kernel = blobs[1].data;
    if (blobs[1].size >= 64 && !memcmp(kernel + 56, "RSC\005", 4)) {
        blobs[1].footprint = MAX(blobs[1].footprint, load_le64(kernel + 16));
    }
    for (unsigned i = 0; i < 3; i++) {
        if (!contains(&s->dram->cfg.ram, blobs[i].base, blobs[i].footprint)) {
            compose_error(errp, "compose:boot:%s: exceeds system RAM", blobs[i].name);
            goto done;
        }
        for (unsigned j = 0; j < i; j++) {
            if (compose_overlap(blobs[i].base, blobs[i].footprint,
                                blobs[j].base, blobs[j].footprint)) {
                compose_error(errp, "compose:boot:%s: overlaps %s",
                              blobs[i].name, blobs[j].name);
                goto done;
            }
        }
    }
    reset_words[6] = cfg->firmware_base;
    reset_words[7] = cfg->firmware_base >> 32;
    reset_words[8] = cfg->fdt_base;
    reset_words[9] = cfg->fdt_base >> 32;
    for (unsigned i = 0; i < G_N_ELEMENTS(reset_words); i++) {
        for (unsigned j = 0; j < 4; j++) {
            reset[i * 4 + j] = reset_words[i] >> (j * 8);
        }
    }
    info[0] = GUINT64_TO_LE(UINT64_C(0x4942534f)); /* OpenSBI dynamic magic */
    info[1] = GUINT64_TO_LE(2);
    info[2] = GUINT64_TO_LE(cfg->kernel_base);
    info[3] = GUINT64_TO_LE(1); /* next_mode = S */
    info[4] = 0;               /* options */
    info[5] = 0;               /* boot_hart */
    memcpy(reset + sizeof(reset_words), info, sizeof(info));
    /* No writes above this point: a bad file or placement can be corrected. */
    for (unsigned i = 0; i < G_N_ELEMENTS(blobs); i++) {
        char *qe = NULL;
        g_autofree char *name = g_strdup_printf("compose.%s", blobs[i].name);
        if (s->compose->core->rom_add_blob(name, blobs[i].base, blobs[i].data,
                                          blobs[i].size, &qe)) {
            compose_qerror(s->compose, errp, "boot", blobs[i].name, qe);
            goto done;
        }
    }
    s->boot_loaded = true;
    ret = 0;
done:
    g_free(blobs[0].data);
    g_free(blobs[1].data);
    return ret;
}
