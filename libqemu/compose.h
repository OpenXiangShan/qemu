/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LIBQEMU_COMPOSE_H
#define LIBQEMU_COMPOSE_H

#include "libqemu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Host-side helpers, linked separately from libqemu-system-*.so. No QEMU
 * internal headers, worker threads or private ABI are used here.
 *
 * One compose context / RV64 hart 0 / TCG instance per process. Call builders
 * sequentially during ASSEMBLY. Config strings are copied. Handles and metadata
 * belong to the context until compose_free(). DTBs have independent ownership.
 *
 * Known configuration errors are checked before creating the affected module.
 * Property/model-specific failures are still reported by QEMU during creation.
 * A QEMU failure can leave a partial module: abandon that instance, call
 * request_shutdown(), wait(), then compose_free(). There is no rollback.
 * free() releases host metadata only; it never stops QEMU.
 * Helper errors use compose_error_free(); direct api errors use error_free().
 */
typedef struct LibQemuCompose LibQemuCompose;
typedef struct LibQemuSoc LibQemuSoc;
typedef struct LibQemuCpu LibQemuCpu;
typedef struct LibQemuAia LibQemuAia;
typedef struct LibQemuPcie LibQemuPcie;
typedef struct LibQemuDtb LibQemuDtb;

typedef struct LibQemuComposeBackend {
    const char *id, *path, *format;
    bool read_only;
} LibQemuComposeBackend;

typedef enum LibQemuChardevKind {
    LIBQEMU_CHAR_STDIO, LIBQEMU_CHAR_NULL,
} LibQemuChardevKind;

typedef struct LibQemuComposeChardev {
    const char *id;
    LibQemuChardevKind kind;
} LibQemuComposeChardev;

typedef struct LibQemuComposeLaunch {
    const char *accel;              /* "tcg" only */
    unsigned smp;                   /* 1: -M none has max_cpus=1 */
    const LibQemuComposeBackend *backends;
    size_t backend_count;
    const LibQemuComposeChardev *chardevs;
    size_t chardev_count;
} LibQemuComposeLaunch;

typedef struct LibQemuSocConfig {
    const char *model, *compatible;
} LibQemuSocConfig;

typedef struct LibQemuRamConfig {
    const char *name;
    uint64_t base, size;
    LibQemuMemoryKind kind;          /* LIBQEMU_RAM or LIBQEMU_ROM */
    bool system_ram;                /* One DRAM node / boot destination */
} LibQemuRamConfig;

typedef struct LibQemuCpuConfig {
    const char *name, *type;         /* RV64 QOM CPU type */
    uint64_t resetvec;
    const char *mmu_type;           /* DT binding, e.g. riscv,sv48 */
    unsigned cbom_block_size, cboz_block_size;
} LibQemuCpuConfig;

/* M/S interrupt subsystem: physical sources enter M APLIC; firmware delegates
 * them to S. DT consumers reference S. num_ids excludes interrupt ID 0;
 * guest_files excludes the S interrupt file.
 */
typedef struct LibQemuAiaConfig {
    const char *name;
    uint64_t imsic_m_base, imsic_s_base, aplic_m_base, aplic_s_base;
    unsigned num_sources, num_ids, guest_files;
} LibQemuAiaConfig;

typedef struct LibQemuWiredIrq {
    LibQemuAia *controller;
    unsigned source;               /* 1 .. num_sources, level-high */
} LibQemuWiredIrq;

/* CLINT-compatible layout: MSWI at base, MTIMER at base+0x4000,
 * MTIME at base+0xbff8. One hart, including rdtime wiring.
 */
typedef struct LibQemuAclintConfig {
    const char *name;
    uint64_t base;
    uint32_t timebase_freq;
} LibQemuAclintConfig;

/* NS16550 little-endian MMIO, 32-bit register stride, 115200 baud. */
typedef struct LibQemuUartConfig {
    const char *name;
    uint64_t base;
    unsigned irq;
    const char *chardev;
    bool console;
} LibQemuUartConfig;

/* Backend-owned raw image, distinct from QEMU -drive backends. */
typedef struct LibQemuVirtioBlkConfig {
    const char *name;
    uint64_t base;
    unsigned irq;
    const char *image;
    bool packed;
} LibQemuVirtioBlkConfig;

/* DWC RC: one non-prefetchable PCI memory window, MSI to AIA. Linux programs
 * iATU from DT ranges. PCI DMA/MSI use system memory. No INTx or IOMMU module.
 */
typedef struct LibQemuDwcConfig {
    const char *name;
    uint64_t dbi_base, dbi_size, config_base, config_size;
    uint64_t cpu_mem_base, pci_mem_base, mem_size;
    unsigned msi_irq;
    unsigned hp_irq;                /* Reserved for Linux's second IRQ entry;
                                    * QEMU model emits no hotplug event. */
    unsigned domain;
} LibQemuDwcConfig;

typedef struct LibQemuNvmeConfig {
    const char *name, *backend_id, *serial;
} LibQemuNvmeConfig;

typedef struct LibQemuFinisherConfig {
    const char *name;
    uint64_t base;
} LibQemuFinisherConfig;

/* Raw fw_dynamic.bin and Linux Image. Explicit addresses; file, placement and
 * overlap checks precede any ROM registration. No ELF/initrd loading here.
 */
typedef struct LibQemuBootConfig {
    const char *firmware, *kernel, *bootargs;
    uint64_t firmware_base, kernel_base, fdt_base;
} LibQemuBootConfig;

void libqemu_compose_error_free(char *error);
int libqemu_compose_init(const LibQemuExports *, const LibQemuComposeLaunch *,
                         LibQemuCompose **out, char **errp);
void libqemu_compose_free(LibQemuCompose *);
int libqemu_soc_new(LibQemuCompose *, const LibQemuSocConfig *,
                    LibQemuSoc **out, char **errp);
int libqemu_soc_add_memory(LibQemuSoc *, const LibQemuRamConfig *, char **errp);
int libqemu_riscv_cpu_create(LibQemuSoc *, const LibQemuCpuConfig *,
                            LibQemuCpu **out, char **errp);
int libqemu_riscv_aia_create(LibQemuSoc *, LibQemuCpu *,
                            const LibQemuAiaConfig *, LibQemuAia **out,
                            char **errp);
int libqemu_riscv_aclint_create(LibQemuSoc *, LibQemuCpu *,
                               const LibQemuAclintConfig *, char **errp);
int libqemu_uart_create(LibQemuSoc *, const LibQemuUartConfig *,
                        LibQemuAia *, char **errp);
int libqemu_my_virtio_blk_create(LibQemuSoc *, const LibQemuVirtioBlkConfig *,
                                 LibQemuAia *, char **errp);
int libqemu_dwc_create(LibQemuSoc *, const LibQemuDwcConfig *, LibQemuAia *,
                       LibQemuPcie **out, char **errp);
int libqemu_nvme_attach(LibQemuPcie *, const LibQemuNvmeConfig *, char **errp);
int libqemu_finisher_create(LibQemuSoc *, const LibQemuFinisherConfig *,
                            char **errp);

/* Borrowed handles for deliberate extensions through api->core. Such custom
 * devices are not tracked/validated/described by the module DTB builder.
 */
LibQemuObject *libqemu_cpu_object(LibQemuCpu *);
LibQemuObject *libqemu_pcie_bus(LibQemuPcie *);
LibQemuObject *libqemu_soc_machine(LibQemuSoc *);

int libqemu_soc_build_dtb(LibQemuSoc *, const char *bootargs,
                         LibQemuDtb **out, char **errp);
const void *libqemu_dtb_data(const LibQemuDtb *, size_t *size);
int libqemu_dtb_write(const LibQemuDtb *, const char *path, char **errp);
void libqemu_dtb_free(LibQemuDtb *);
int libqemu_riscv_boot_load(LibQemuSoc *, const LibQemuBootConfig *,
                           const LibQemuDtb *, char **errp);

/* Board policy separate from reusable modules. Edit these configs and call
 * modules explicitly, or use kmh_soc_build() for convenience.
 */
typedef struct KmhProfile {
    LibQemuSocConfig soc;
    LibQemuRamConfig dram, rom, sram, flash;
    LibQemuCpuConfig cpu;
    LibQemuAiaConfig aia;
    LibQemuAclintConfig aclint;
    LibQemuUartConfig uart;
    LibQemuVirtioBlkConfig blk;
    LibQemuDwcConfig pcie;
    LibQemuNvmeConfig nvme;
    LibQemuFinisherConfig finisher;
    LibQemuBootConfig boot;
    bool uart_enabled, blk_enabled, pcie_enabled, nvme_enabled;
    bool finisher_enabled;
} KmhProfile;

void kmh_profile_init_defaults(KmhProfile *);
int kmh_soc_build(LibQemuCompose *, const KmhProfile *, LibQemuSoc **out,
                  char **errp);

#ifdef __cplusplus
}
#endif
#endif
