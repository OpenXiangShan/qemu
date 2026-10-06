/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LIBQEMU_COMPOSE_INTERNAL_H
#define LIBQEMU_COMPOSE_INTERNAL_H

#include <glib.h>
#include "compose.h"

typedef enum ComposeKind {
    COMPOSE_MEMORY, COMPOSE_CPU, COMPOSE_AIA, COMPOSE_ACLINT, COMPOSE_UART,
    COMPOSE_BLK, COMPOSE_PCIE, COMPOSE_NVME, COMPOSE_FINISHER,
} ComposeKind;

typedef struct ComposeRange {
    uint64_t base, size;
    const char *name;
} ComposeRange;

typedef struct ComposeModule {
    ComposeKind kind;
    LibQemuSoc *soc;
    const char *name;
    LibQemuObject *container, *object;
    LibQemuWiredIrq irq;
    uint32_t phandle;
    bool complete;
    union {
        LibQemuRamConfig ram;
        LibQemuCpuConfig cpu;
        LibQemuAiaConfig aia;
        LibQemuAclintConfig aclint;
        LibQemuUartConfig uart;
        LibQemuVirtioBlkConfig blk;
        LibQemuDwcConfig pcie;
        LibQemuNvmeConfig nvme;
        LibQemuFinisherConfig finisher;
    } cfg;
} ComposeModule;

struct LibQemuCpu { ComposeModule m; };
struct LibQemuAia {
    ComposeModule m;
    LibQemuObject *aplic_m, *aplic_s;
    GHashTable *sources;
};
struct LibQemuPcie { ComposeModule m; LibQemuObject *bus; };

struct LibQemuCompose {
    const LibQemuExports *api;
    const LibQemuCoreExports *core;
    LibQemuObject *machine;
    LibQemuMemory *memory;
    LibQemuSoc *soc;
    GPtrArray *strings;
    GHashTable *backends, *chardevs, *used_backends, *used_chardevs;
    bool failed;
};

struct LibQemuSoc {
    LibQemuCompose *compose;
    LibQemuSocConfig config;
    GPtrArray *modules;
    GArray *ranges;
    GHashTable *names;
    LibQemuCpu *cpu;
    LibQemuAia *aia;
    ComposeModule *dram, *aclint, *console, *finisher;
    uint32_t next_phandle;
    unsigned generation;
    bool boot_loaded;
};

struct LibQemuDtb {
    void *blob;
    size_t size;
    LibQemuSoc *soc;                /* Identity only; free never dereferences */
    unsigned generation;
};

enum {
    COMPOSE_IRQ_MSIP = 3, COMPOSE_IRQ_MTIP = 7,
    COMPOSE_IRQ_SEIP = 9, COMPOSE_IRQ_MEIP = 11,
    COMPOSE_IRQ_GUEST_BASE = 64,
    COMPOSE_LITTLE_ENDIAN = 2,       /* QEMU enum device_endian */
    COMPOSE_DWC_MSI_OUTPUT = 4,
};

int compose_error(char **errp, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
int compose_qerror(LibQemuCompose *, char **errp, const char *stage,
                   const char *name, char *qerr);
int compose_ready(LibQemuSoc *, char **errp);
bool compose_range_valid(uint64_t base, uint64_t size);
bool compose_overlap(uint64_t a, uint64_t as, uint64_t b, uint64_t bs);
unsigned compose_guest_bits(unsigned files);

#endif
