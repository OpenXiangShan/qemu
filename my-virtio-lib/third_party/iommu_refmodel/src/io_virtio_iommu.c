// SPDX-License-Identifier: Apache-2.0

#include "io_virtio_iommu.h"

#include <endian.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io_refmodel_api.h"
#include "iommu.h"
#include "iommu_command_queue.h"
#include "iommu_data_structures.h"
#include "iommu_fault.h"
#include "iommu_registers.h"
#include "iommu_translate.h"

#define VIOMMU_PAGE_SHIFT 12U
#define VIOMMU_PAGE_SIZE (1ULL << VIOMMU_PAGE_SHIFT)
#define VIOMMU_PAGE_MASK (VIOMMU_PAGE_SIZE - 1)
#define VIOMMU_DEFAULT_SHADOW_BASE 0x100000000000ULL
#define VIOMMU_DEFAULT_COMBINED_SHADOW_BASE 0x100000000ULL
#define VIOMMU_DEFAULT_ENDPOINTS 64U
#define VIOMMU_MAX_ENDPOINTS 64U
#define VIOMMU_CQ_ENTRIES 256U
#define VIOMMU_FQ_ENTRIES 128U

#define VIOMMU_PTE_V (1ULL << 0)
#define VIOMMU_PTE_R (1ULL << 1)
#define VIOMMU_PTE_W (1ULL << 2)
#define VIOMMU_PTE_U (1ULL << 4)
#define VIOMMU_PTE_A (1ULL << 6)
#define VIOMMU_PTE_D (1ULL << 7)

typedef struct IoVirtioIommuPage {
    uint64_t address;
    uint8_t data[VIOMMU_PAGE_SIZE];
    struct IoVirtioIommuPage *next;
} IoVirtioIommuPage;

typedef struct IoVirtioIommuMapping {
    uint64_t virt_start;
    uint64_t virt_end;
    uint64_t phys_start;
    uint32_t flags;
    IoVirtioIommuStage stage;
    struct IoVirtioIommuMapping *next;
} IoVirtioIommuMapping;

typedef struct IoVirtioIommuDomain {
    uint32_t id;
    uint32_t endpoint_count;
    IoVirtioIommuMode mode;
    uint64_t s1_root;
    uint64_t s2_root;
    IoVirtioIommuMapping *mappings;
    struct IoVirtioIommuDomain *next;
} IoVirtioIommuDomain;

typedef struct IoVirtioIommuEndpoint {
    IoVirtioIommuDomain *domain;
} IoVirtioIommuEndpoint;

typedef struct IoVirtioIommuRelay {
    uint64_t namespace_id;
    uint32_t inner_domain;
    uint32_t endpoint;
    uint32_t outer_domain;
    struct IoVirtioIommuRelay *next;
} IoVirtioIommuRelay;

struct IoVirtioIommu {
    IoVirtioIommuConfig config;
    iommu_handle_t *refmodel;
    IoVirtioIommuPage *pages;
    IoVirtioIommuDomain *domains;
    IoVirtioIommuEndpoint endpoints[VIOMMU_MAX_ENDPOINTS];
    uint64_t next_shadow_address;
    uint64_t ddt_address;
    uint64_t cq_address;
    uint64_t fq_address;
    uint32_t cq_tail;
    IoVirtioIommuRelay *relays;
};

static uint64_t align_up(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static IoVirtioIommuPage *find_page(IoVirtioIommu *iommu, uint64_t address)
{
    IoVirtioIommuPage *page;

    for (page = iommu->pages; page; page = page->next) {
        if (address >= page->address &&
            address < page->address + VIOMMU_PAGE_SIZE) {
            return page;
        }
    }
    return NULL;
}

static int shadow_read(IoVirtioIommu *iommu, uint64_t address, void *data,
                       size_t length)
{
    size_t done = 0;

    while (done < length) {
        IoVirtioIommuPage *page = find_page(iommu, address + done);
        size_t offset;
        size_t chunk;

        if (!page) {
            return -EFAULT;
        }
        offset = (size_t)(address + done - page->address);
        chunk = VIOMMU_PAGE_SIZE - offset;
        if (chunk > length - done) {
            chunk = length - done;
        }
        memcpy((uint8_t *)data + done, page->data + offset, chunk);
        done += chunk;
    }
    return 0;
}

static int shadow_write(IoVirtioIommu *iommu, uint64_t address,
                        const void *data, size_t length)
{
    size_t done = 0;

    while (done < length) {
        IoVirtioIommuPage *page = find_page(iommu, address + done);
        size_t offset;
        size_t chunk;

        if (!page) {
            return -EFAULT;
        }
        offset = (size_t)(address + done - page->address);
        chunk = VIOMMU_PAGE_SIZE - offset;
        if (chunk > length - done) {
            chunk = length - done;
        }
        memcpy(page->data + offset, (const uint8_t *)data + done, chunk);
        done += chunk;
    }
    return 0;
}

static uint64_t allocate_pages(IoVirtioIommu *iommu, uint32_t count,
                               uint32_t alignment_pages)
{
    uint64_t base;

    if (!count || !alignment_pages ||
        (alignment_pages & (alignment_pages - 1))) {
        return 0;
    }
    base = align_up(iommu->next_shadow_address,
                    (uint64_t)alignment_pages * VIOMMU_PAGE_SIZE);
    if (base < iommu->next_shadow_address ||
        count > (UINT64_MAX - base) / VIOMMU_PAGE_SIZE) {
        return 0;
    }

    for (uint32_t i = 0; i < count; i++) {
        IoVirtioIommuPage *page = calloc(1, sizeof(*page));

        if (!page) {
            return 0;
        }
        page->address = base + (uint64_t)i * VIOMMU_PAGE_SIZE;
        page->next = iommu->pages;
        iommu->pages = page;
    }
    iommu->next_shadow_address = base + (uint64_t)count * VIOMMU_PAGE_SIZE;
    return base;
}

static void translation_write_cb(uint64_t address, const uint8_t *data,
                                 size_t length, uint32_t strobe, uint16_t id,
                                 size_t beat_index, size_t beat_count,
                                 const iommu_ace_lite_attrs_t *attrs,
                                 void *opaque)
{
    IoVirtioIommu *iommu = opaque;

    (void)id;
    (void)beat_index;
    (void)beat_count;
    for (size_t i = 0; i < length && i < 32; i++) {
        IoVirtioIommuPage *page;
        size_t offset;

        if (!(strobe & (1U << i))) {
            continue;
        }
        page = find_page(iommu, address + i);
        if (!page) {
            continue;
        }
        offset = (size_t)(address + i - page->address);
        if (attrs && attrs->awatop == 0x13) {
            page->data[offset] |= data[i];
        } else {
            page->data[offset] = data[i];
        }
    }
}

static int translation_read_cb(uint64_t address, uint8_t *data,
                               size_t length, uint16_t id,
                               size_t beat_index, size_t beat_count,
                               const iommu_ace_lite_attrs_t *attrs,
                               void *opaque)
{
    (void)id;
    (void)beat_index;
    (void)beat_count;
    (void)attrs;
    return shadow_read(opaque, address, data, length) == 0;
}

static void free_mappings(IoVirtioIommuMapping *mapping)
{
    while (mapping) {
        IoVirtioIommuMapping *next = mapping->next;

        free(mapping);
        mapping = next;
    }
}

static void free_domains(IoVirtioIommu *iommu)
{
    IoVirtioIommuDomain *domain = iommu->domains;

    while (domain) {
        IoVirtioIommuDomain *next = domain->next;

        free_mappings(domain->mappings);
        free(domain);
        domain = next;
    }
    iommu->domains = NULL;
    memset(iommu->endpoints, 0, sizeof(iommu->endpoints));
}

static void free_relays(IoVirtioIommu *iommu)
{
    IoVirtioIommuRelay *relay = iommu->relays;

    while (relay) {
        IoVirtioIommuRelay *next = relay->next;

        free(relay);
        relay = next;
    }
    iommu->relays = NULL;
}

static void free_pages(IoVirtioIommu *iommu)
{
    IoVirtioIommuPage *page = iommu->pages;

    while (page) {
        IoVirtioIommuPage *next = page->next;

        free(page);
        page = next;
    }
    iommu->pages = NULL;
}

static IoVirtioIommuDomain *find_domain(IoVirtioIommu *iommu,
                                        uint32_t id)
{
    IoVirtioIommuDomain *domain;

    for (domain = iommu->domains; domain; domain = domain->next) {
        if (domain->id == id) {
            return domain;
        }
    }
    return NULL;
}

static bool domain_id_valid(uint32_t domain, IoVirtioIommuMode mode)
{
    if (!domain) {
        return false;
    }
    if (mode == IO_VIRTIO_IOMMU_MODE_S1) {
        return domain <= 0xfffffU;
    }
    return domain <= 0xffffU;
}

static int prepare_domain_roots(IoVirtioIommu *iommu,
                                IoVirtioIommuDomain *domain)
{
    if (domain->mode == IO_VIRTIO_IOMMU_MODE_S1) {
        domain->s1_root = allocate_pages(iommu, 1, 1);
        return domain->s1_root ? 0 : -ENOMEM;
    }
    if (domain->mode == IO_VIRTIO_IOMMU_MODE_S2) {
        domain->s2_root = allocate_pages(iommu, 4, 4);
        return domain->s2_root ? 0 : -ENOMEM;
    }

    /* The combined path owns both shadow roots.  The S-stage root maps the
     * inner IOVA to a GPA; the G-stage root then maps that GPA to host PA. */
    domain->s1_root = allocate_pages(iommu, 1, 1);
    if (!domain->s1_root) {
        return -ENOMEM;
    }
    domain->s2_root = allocate_pages(iommu, 4, 4);
    if (!domain->s2_root) {
        return -ENOMEM;
    }
    return 0;
}

static IoVirtioIommuDomain *create_domain(IoVirtioIommu *iommu,
                                          uint32_t id,
                                          IoVirtioIommuMode mode)
{
    IoVirtioIommuDomain *domain = calloc(1, sizeof(*domain));

    if (!domain) {
        return NULL;
    }
    domain->id = id;
    domain->mode = mode;
    if (prepare_domain_roots(iommu, domain)) {
        free(domain);
        return NULL;
    }
    domain->next = iommu->domains;
    iommu->domains = domain;
    return domain;
}

static void remove_domain(IoVirtioIommu *iommu,
                          IoVirtioIommuDomain *domain)
{
    IoVirtioIommuDomain **link = &iommu->domains;

    while (*link && *link != domain) {
        link = &(*link)->next;
    }
    if (*link) {
        *link = domain->next;
        free_mappings(domain->mappings);
        free(domain);
    }
}

static int write_device_context(IoVirtioIommu *iommu, uint32_t endpoint,
                                IoVirtioIommuDomain *domain)
{
    device_context_t context;
    uint64_t address = iommu->ddt_address +
                       (uint64_t)endpoint * sizeof(context);

    memset(&context, 0, sizeof(context));
    if (domain) {
        context.tc.V = 1;
        context.tc.SADE = 0;
        context.tc.GADE = 0;
        context.tc.SXL = 0;
        context.ta.PSCID = domain->id & 0xfffffU;
        if (domain->mode == IO_VIRTIO_IOMMU_MODE_S1) {
            context.fsc.iosatp.MODE = IOSATP_Sv39;
            context.fsc.iosatp.PPN = domain->s1_root >> VIOMMU_PAGE_SHIFT;
            context.iohgatp.MODE = IOHGATP_Bare;
        } else if (domain->mode == IO_VIRTIO_IOMMU_MODE_S2) {
            context.fsc.iosatp.MODE = IOSATP_Bare;
            context.iohgatp.MODE = IOHGATP_Sv39x4;
            context.iohgatp.GSCID = domain->id & 0xffffU;
            context.iohgatp.PPN = domain->s2_root >> VIOMMU_PAGE_SHIFT;
        } else {
            context.fsc.iosatp.MODE = IOSATP_Sv39;
            context.fsc.iosatp.PPN = domain->s1_root >> VIOMMU_PAGE_SHIFT;
            context.iohgatp.MODE = IOHGATP_Sv39x4;
            context.iohgatp.GSCID = domain->id & 0xffffU;
            context.iohgatp.PPN = domain->s2_root >> VIOMMU_PAGE_SHIFT;
            /* Before a relay attaches, the outer device submits GPAs and
             * must use only the G-stage.  A translated request with T2GPA
             * models that path; relay-owned requests remain untranslated
             * and therefore take the complete S1+S2 walk. */
            context.tc.EN_ATS = 1;
            context.tc.T2GPA = 1;
        }
        context.msiptp.MODE = MSIPTP_Off;
    }
    return shadow_write(iommu, address, &context, sizeof(context));
}

static int submit_commands(IoVirtioIommu *iommu, const command_t *commands,
                           uint32_t count)
{
    uint32_t head = 0;
    uint32_t status = 0;

    if (!count || count >= VIOMMU_CQ_ENTRIES) {
        return -EINVAL;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint64_t address = iommu->cq_address +
                           (uint64_t)iommu->cq_tail * sizeof(command_t);

        if (shadow_write(iommu, address, &commands[i], sizeof(commands[i]))) {
            return -EFAULT;
        }
        iommu->cq_tail = (iommu->cq_tail + 1) & (VIOMMU_CQ_ENTRIES - 1);
    }
    if (!io_refmodel_mmio_write(iommu->refmodel, CQT_OFFSET,
                                iommu->cq_tail) ||
        !io_refmodel_iommu_step(iommu->refmodel, (int)count) ||
        !io_refmodel_mmio_read(iommu->refmodel, CQH_OFFSET, &head) ||
        !io_refmodel_mmio_read(iommu->refmodel, CQCSR_OFFSET, &status)) {
        return -EIO;
    }
    if (head != iommu->cq_tail || (status & ((1U << 8) | (1U << 9) |
                                             (1U << 10)))) {
        return -EIO;
    }
    return 0;
}

static int invalidate_device_context(IoVirtioIommu *iommu,
                                     uint32_t endpoint)
{
    command_t commands[2] = { 0 };

    commands[0].iodir.opcode = IODIR;
    commands[0].iodir.func3 = INVAL_DDT;
    commands[0].iodir.dv = 1;
    commands[0].iodir.did = endpoint;
    commands[1].iofence.opcode = IOFENCE;
    commands[1].iofence.func3 = IOFENCE_C;
    commands[1].iofence.pr = 1;
    commands[1].iofence.pw = 1;
    return submit_commands(iommu, commands, 2);
}

static int invalidate_translation(IoVirtioIommu *iommu,
                                  const IoVirtioIommuDomain *domain,
                                  IoVirtioIommuStage stage)
{
    command_t commands[2] = { 0 };

    commands[0].iotinval.opcode = IOTINVAL;
    if (stage == IO_VIRTIO_IOMMU_STAGE_S1) {
        commands[0].iotinval.func3 = VMA;
        commands[0].iotinval.pscv = 1;
        commands[0].iotinval.pscid = domain->id & 0xfffffU;
        if (domain->mode == IO_VIRTIO_IOMMU_MODE_S1_S2) {
            commands[0].iotinval.gv = 1;
            commands[0].iotinval.gscid = domain->id & 0xffffU;
        }
    } else {
        commands[0].iotinval.func3 = GVMA;
        commands[0].iotinval.gv = 1;
        commands[0].iotinval.gscid = domain->id & 0xffffU;
    }
    commands[1].iofence.opcode = IOFENCE;
    commands[1].iofence.func3 = IOFENCE_C;
    commands[1].iofence.pr = 1;
    commands[1].iofence.pw = 1;
    return submit_commands(iommu, commands, 2);
}

static int shadow_load_u64(IoVirtioIommu *iommu, uint64_t address,
                           uint64_t *value)
{
    uint64_t raw;

    if (shadow_read(iommu, address, &raw, sizeof(raw))) {
        return -EFAULT;
    }
    *value = le64toh(raw);
    return 0;
}

static int shadow_store_u64(IoVirtioIommu *iommu, uint64_t address,
                           uint64_t value)
{
    uint64_t raw = htole64(value);

    return shadow_write(iommu, address, &raw, sizeof(raw));
}

static int set_page_mapping(IoVirtioIommu *iommu,
                            IoVirtioIommuDomain *domain,
                            IoVirtioIommuStage stage, uint64_t virt,
                            uint64_t phys, uint32_t flags);

static int page_table_leaf_address(IoVirtioIommu *iommu,
                                   IoVirtioIommuDomain *domain,
                                   IoVirtioIommuStage stage,
                                   uint64_t virtual_address, bool create,
                                   uint64_t *leaf_address)
{
    uint64_t table;
    uint32_t indexes[3];

    if (stage == IO_VIRTIO_IOMMU_STAGE_S1) {
        table = domain->s1_root;
        indexes[2] = (virtual_address >> 30) & 0x1ffU;
    } else {
        table = domain->s2_root;
        indexes[2] = (virtual_address >> 30) & 0x7ffU;
    }
    indexes[1] = (virtual_address >> 21) & 0x1ffU;
    indexes[0] = (virtual_address >> 12) & 0x1ffU;

    if (!table) {
        return -EINVAL;
    }
    for (int level = 2; level > 0; level--) {
        uint64_t pte_address = table + (uint64_t)indexes[level] * 8;
        uint64_t pte;

        if (shadow_load_u64(iommu, pte_address, &pte)) {
            return -EFAULT;
        }
        if (!(pte & VIOMMU_PTE_V)) {
            uint64_t next;

            if (!create) {
                return -ENOENT;
            }
            next = allocate_pages(iommu, 1, 1);
            if (!next) {
                return -ENOMEM;
            }
            if (stage == IO_VIRTIO_IOMMU_STAGE_S1 &&
                domain->mode == IO_VIRTIO_IOMMU_MODE_S1_S2 &&
                set_page_mapping(iommu, domain, IO_VIRTIO_IOMMU_STAGE_S2,
                                 next, next,
                                 IO_VIRTIO_IOMMU_MAP_READ |
                                 IO_VIRTIO_IOMMU_MAP_WRITE)) {
                return -ENOMEM;
            }
            pte = ((next >> VIOMMU_PAGE_SHIFT) << 10) | VIOMMU_PTE_V;
            if (shadow_store_u64(iommu, pte_address, pte)) {
                return -EFAULT;
            }
        } else if (pte & (VIOMMU_PTE_R | VIOMMU_PTE_W)) {
            return -EEXIST;
        }
        table = ((pte >> 10) & ((1ULL << 44) - 1)) << VIOMMU_PAGE_SHIFT;
    }
    *leaf_address = table + (uint64_t)indexes[0] * 8;
    return 0;
}

static int set_page_mapping(IoVirtioIommu *iommu,
                            IoVirtioIommuDomain *domain,
                            IoVirtioIommuStage stage, uint64_t virt,
                            uint64_t phys, uint32_t flags)
{
    uint64_t leaf_address;
    uint64_t old_pte;
    uint64_t pte;
    int ret;

    ret = page_table_leaf_address(iommu, domain, stage, virt, true,
                                  &leaf_address);
    if (ret) {
        return ret;
    }
    if (shadow_load_u64(iommu, leaf_address, &old_pte)) {
        return -EFAULT;
    }
    if (old_pte & VIOMMU_PTE_V) {
        return -EEXIST;
    }
    pte = ((phys >> VIOMMU_PAGE_SHIFT) << 10) |
          VIOMMU_PTE_V | VIOMMU_PTE_U | VIOMMU_PTE_A | VIOMMU_PTE_D;
    if (flags & IO_VIRTIO_IOMMU_MAP_READ) {
        pte |= VIOMMU_PTE_R;
    }
    if (flags & IO_VIRTIO_IOMMU_MAP_WRITE) {
        /* RISC-V reserves W=1,R=0. The software map list preserves write-only. */
        pte |= VIOMMU_PTE_R | VIOMMU_PTE_W;
    }
    return shadow_store_u64(iommu, leaf_address, pte);
}

static void clear_page_mapping(IoVirtioIommu *iommu,
                               IoVirtioIommuDomain *domain,
                               IoVirtioIommuStage stage, uint64_t virt)
{
    uint64_t leaf_address;

    if (!page_table_leaf_address(iommu, domain, stage, virt, false,
                                 &leaf_address)) {
        shadow_store_u64(iommu, leaf_address, 0);
    }
}

static bool stage_allowed(const IoVirtioIommuDomain *domain,
                          IoVirtioIommuStage stage)
{
    return domain->mode == IO_VIRTIO_IOMMU_MODE_S1_S2 ||
           (domain->mode == IO_VIRTIO_IOMMU_MODE_S1 &&
            stage == IO_VIRTIO_IOMMU_STAGE_S1) ||
           (domain->mode == IO_VIRTIO_IOMMU_MODE_S2 &&
            stage == IO_VIRTIO_IOMMU_STAGE_S2);
}

static IoVirtioIommuMapping *find_mapping(IoVirtioIommuDomain *domain,
                                          IoVirtioIommuStage stage,
                                          uint64_t address)
{
    IoVirtioIommuMapping *mapping;

    for (mapping = domain->mappings; mapping; mapping = mapping->next) {
        if (mapping->stage == stage && address >= mapping->virt_start &&
            address <= mapping->virt_end) {
            return mapping;
        }
    }
    return NULL;
}

static int validate_range(uint64_t virt_start, uint64_t virt_end,
                          uint64_t phys_start)
{
    uint64_t length;

    if ((virt_start & VIOMMU_PAGE_MASK) ||
        (phys_start & VIOMMU_PAGE_MASK) ||
        (virt_end & VIOMMU_PAGE_MASK) != VIOMMU_PAGE_MASK ||
        virt_end < virt_start) {
        return -ERANGE;
    }
    length = virt_end - virt_start + 1;
    if (!length || phys_start > UINT64_MAX - (length - 1)) {
        return -EOVERFLOW;
    }
    return 0;
}

static int initialize_refmodel(IoVirtioIommu *iommu)
{
    cqb_t cqb = { .raw = 0 };
    fqb_t fqb = { .raw = 0 };
    ddtp_t ddtp = { .raw = 0 };

    iommu->next_shadow_address = align_up(iommu->config.shadow_base,
                                          VIOMMU_PAGE_SIZE);
    iommu->refmodel = io_refmodel_iommu_open();
    if (!iommu->refmodel) {
        return -ENOMEM;
    }
    io_refmodel_iommu_set_trace(iommu->refmodel, iommu->config.trace);
    io_refmodel_translation_set_callbacks_v2(iommu->refmodel,
                                              translation_write_cb,
                                              translation_read_cb, iommu);
    iommu->ddt_address = allocate_pages(iommu, 1, 1);
    iommu->cq_address = allocate_pages(iommu, 1, 1);
    iommu->fq_address = allocate_pages(iommu, 1, 1);
    if (!iommu->ddt_address || !iommu->cq_address || !iommu->fq_address) {
        return -ENOMEM;
    }

    cqb.log2szm1 = 7;
    cqb.ppn = iommu->cq_address >> VIOMMU_PAGE_SHIFT;
    fqb.log2szm1 = 6;
    fqb.ppn = iommu->fq_address >> VIOMMU_PAGE_SHIFT;
    ddtp.iommu_mode = DDT_1LVL;
    ddtp.ppn = iommu->ddt_address >> VIOMMU_PAGE_SHIFT;
    if (!io_refmodel_mmio_write64(iommu->refmodel, CQB_OFFSET, cqb.raw) ||
        !io_refmodel_mmio_write(iommu->refmodel, CQCSR_OFFSET, 1) ||
        !io_refmodel_mmio_write64(iommu->refmodel, FQB_OFFSET, fqb.raw) ||
        !io_refmodel_mmio_write(iommu->refmodel, FQCSR_OFFSET, 1) ||
        !io_refmodel_mmio_write64(iommu->refmodel, DDTP_OFFSET, ddtp.raw)) {
        return -EIO;
    }
    iommu->cq_tail = 0;
    return 0;
}

IoVirtioIommu *io_virtio_iommu_create(const IoVirtioIommuConfig *config)
{
    IoVirtioIommu *iommu = calloc(1, sizeof(*iommu));

    if (!iommu) {
        return NULL;
    }
    if (config) {
        iommu->config = *config;
    }
    if (!iommu->config.max_endpoints) {
        iommu->config.max_endpoints = VIOMMU_DEFAULT_ENDPOINTS;
    }
    if (!iommu->config.shadow_base) {
        iommu->config.shadow_base =
            iommu->config.mode == IO_VIRTIO_IOMMU_MODE_S1_S2 ?
            VIOMMU_DEFAULT_COMBINED_SHADOW_BASE :
            VIOMMU_DEFAULT_SHADOW_BASE;
    }
    if (iommu->config.max_endpoints > VIOMMU_MAX_ENDPOINTS ||
        iommu->config.mode > IO_VIRTIO_IOMMU_MODE_S1_S2 ||
        initialize_refmodel(iommu)) {
        io_virtio_iommu_destroy(iommu);
        return NULL;
    }
    return iommu;
}

void io_virtio_iommu_destroy(IoVirtioIommu *iommu)
{
    if (!iommu) {
        return;
    }
    if (iommu->refmodel) {
        io_refmodel_iommu_close(iommu->refmodel);
    }
    free_domains(iommu);
    free_relays(iommu);
    free_pages(iommu);
    free(iommu);
}

int io_virtio_iommu_reset(IoVirtioIommu *iommu)
{
    if (!iommu) {
        return -EINVAL;
    }
    if (iommu->refmodel) {
        io_refmodel_iommu_close(iommu->refmodel);
        iommu->refmodel = NULL;
    }
    free_domains(iommu);
    free_relays(iommu);
    free_pages(iommu);
    iommu->ddt_address = 0;
    iommu->cq_address = 0;
    iommu->fq_address = 0;
    return initialize_refmodel(iommu);
}

int io_virtio_iommu_attach(IoVirtioIommu *iommu, uint32_t domain_id,
                           uint32_t endpoint, uint32_t flags,
                           IoVirtioIommuMode mode)
{
    IoVirtioIommuDomain *domain;
    IoVirtioIommuDomain *old_domain;
    bool created = false;
    int ret;

    if (!iommu || endpoint >= iommu->config.max_endpoints || flags ||
        mode != iommu->config.mode || !domain_id_valid(domain_id, mode)) {
        return -EINVAL;
    }
    domain = find_domain(iommu, domain_id);
    if (domain && domain->mode != mode) {
        return -EINVAL;
    }
    if (!domain) {
        domain = create_domain(iommu, domain_id, mode);
        if (!domain) {
            return -ENOMEM;
        }
        created = true;
    }

    /* In combined mode S-stage page-table addresses are GPAs.  Keep the
     * adapter-owned shadow pages reachable through the G-stage while the
     * refmodel walks the S-stage tables. */
    if (mode == IO_VIRTIO_IOMMU_MODE_S1_S2 && created &&
        set_page_mapping(iommu, domain, IO_VIRTIO_IOMMU_STAGE_S2,
                         domain->s1_root, domain->s1_root,
                         IO_VIRTIO_IOMMU_MAP_READ |
                         IO_VIRTIO_IOMMU_MAP_WRITE)) {
        remove_domain(iommu, domain);
        return -ENOMEM;
    }

    old_domain = iommu->endpoints[endpoint].domain;
    if (old_domain == domain) {
        return 0;
    }
    iommu->endpoints[endpoint].domain = domain;
    domain->endpoint_count++;
    ret = write_device_context(iommu, endpoint, domain);
    if (!ret) {
        ret = invalidate_device_context(iommu, endpoint);
    }
    if (ret) {
        iommu->endpoints[endpoint].domain = old_domain;
        domain->endpoint_count--;
        if (created) {
            remove_domain(iommu, domain);
        }
        return ret;
    }
    if (old_domain) {
        old_domain->endpoint_count--;
        if (!old_domain->endpoint_count) {
            remove_domain(iommu, old_domain);
        }
    }
    return 0;
}

int io_virtio_iommu_detach(IoVirtioIommu *iommu, uint32_t domain_id,
                           uint32_t endpoint)
{
    IoVirtioIommuDomain *domain;
    int ret;

    if (!iommu || endpoint >= iommu->config.max_endpoints) {
        return -EINVAL;
    }
    domain = iommu->endpoints[endpoint].domain;
    if (!domain) {
        return -ENOENT;
    }
    if (domain->id != domain_id) {
        return -EINVAL;
    }
    ret = write_device_context(iommu, endpoint, NULL);
    if (!ret) {
        ret = invalidate_device_context(iommu, endpoint);
    }
    if (ret) {
        return ret;
    }
    iommu->endpoints[endpoint].domain = NULL;
    domain->endpoint_count--;
    if (!domain->endpoint_count) {
        remove_domain(iommu, domain);
    }
    return 0;
}

int io_virtio_iommu_map_stage(IoVirtioIommu *iommu, uint32_t domain_id,
                              uint64_t virt_start, uint64_t virt_end,
                              uint64_t phys_start, uint32_t flags,
                              IoVirtioIommuStage stage)
{
    IoVirtioIommuDomain *domain;
    IoVirtioIommuMapping *mapping;
    uint64_t length;
    uint64_t done = 0;
    int ret;

    if (!iommu || stage > IO_VIRTIO_IOMMU_STAGE_S2 ||
        (flags & ~(IO_VIRTIO_IOMMU_MAP_READ |
                   IO_VIRTIO_IOMMU_MAP_WRITE |
                   IO_VIRTIO_IOMMU_MAP_MMIO)) ||
        !(flags & (IO_VIRTIO_IOMMU_MAP_READ |
                   IO_VIRTIO_IOMMU_MAP_WRITE))) {
        return -EINVAL;
    }
    ret = validate_range(virt_start, virt_end, phys_start);
    if (ret) {
        return ret;
    }
    domain = find_domain(iommu, domain_id);
    if (!domain) {
        return -ENOENT;
    }
    if (!stage_allowed(domain, stage)) {
        return -EINVAL;
    }
    for (mapping = domain->mappings; mapping; mapping = mapping->next) {
        if (mapping->stage == stage && virt_start <= mapping->virt_end &&
            virt_end >= mapping->virt_start) {
            return -EINVAL;
        }
    }
    mapping = calloc(1, sizeof(*mapping));
    if (!mapping) {
        return -ENOMEM;
    }
    length = virt_end - virt_start + 1;
    while (done < length) {
        ret = set_page_mapping(iommu, domain, stage, virt_start + done,
                               phys_start + done, flags);
        if (ret) {
            while (done) {
                done -= VIOMMU_PAGE_SIZE;
                clear_page_mapping(iommu, domain, stage, virt_start + done);
            }
            free(mapping);
            return ret;
        }
        done += VIOMMU_PAGE_SIZE;
    }
    mapping->virt_start = virt_start;
    mapping->virt_end = virt_end;
    mapping->phys_start = phys_start;
    mapping->flags = flags;
    mapping->stage = stage;
    mapping->next = domain->mappings;
    domain->mappings = mapping;
    ret = invalidate_translation(iommu, domain, stage);
    if (ret) {
        return ret;
    }
    return 0;
}

int io_virtio_iommu_unmap_stage(IoVirtioIommu *iommu, uint32_t domain_id,
                                uint64_t virt_start, uint64_t virt_end,
                                IoVirtioIommuStage stage)
{
    IoVirtioIommuDomain *domain;
    IoVirtioIommuMapping *mapping;
    IoVirtioIommuMapping **link;

    if (!iommu || stage > IO_VIRTIO_IOMMU_STAGE_S2 ||
        (virt_start & VIOMMU_PAGE_MASK) ||
        (virt_end & VIOMMU_PAGE_MASK) != VIOMMU_PAGE_MASK ||
        virt_end < virt_start) {
        return -ERANGE;
    }
    domain = find_domain(iommu, domain_id);
    if (!domain) {
        return -ENOENT;
    }
    if (!stage_allowed(domain, stage)) {
        return -EINVAL;
    }
    for (mapping = domain->mappings; mapping; mapping = mapping->next) {
        if (mapping->stage != stage || virt_start > mapping->virt_end ||
            virt_end < mapping->virt_start) {
            continue;
        }
        if (virt_start > mapping->virt_start || virt_end < mapping->virt_end) {
            return -ERANGE;
        }
    }
    for (uint64_t address = virt_start;; address += VIOMMU_PAGE_SIZE) {
        clear_page_mapping(iommu, domain, stage, address);
        if (address == virt_end - VIOMMU_PAGE_MASK) {
            break;
        }
    }
    link = &domain->mappings;
    while (*link) {
        mapping = *link;
        if (mapping->stage == stage && virt_start <= mapping->virt_start &&
            virt_end >= mapping->virt_end) {
            *link = mapping->next;
            free(mapping);
        } else {
            link = &mapping->next;
        }
    }
    return invalidate_translation(iommu, domain, stage);
}

int io_virtio_iommu_map(IoVirtioIommu *iommu, uint32_t domain,
                        uint64_t virt_start, uint64_t virt_end,
                        uint64_t phys_start, uint32_t flags,
                        IoVirtioIommuMode mode)
{
    IoVirtioIommuStage stage;

    if (!iommu || mode != iommu->config.mode) {
        return -EINVAL;
    }
    /* The guest-visible device owns the outer/host plane.  In combined mode
     * its standard MAP/UNMAP requests are therefore G-stage operations;
     * inner VS-stage requests use the separate relay API. */
    stage = (mode == IO_VIRTIO_IOMMU_MODE_S2 ||
             mode == IO_VIRTIO_IOMMU_MODE_S1_S2) ?
            IO_VIRTIO_IOMMU_STAGE_S2 : IO_VIRTIO_IOMMU_STAGE_S1;
    return io_virtio_iommu_map_stage(iommu, domain, virt_start, virt_end,
                                     phys_start, flags, stage);
}

int io_virtio_iommu_unmap(IoVirtioIommu *iommu, uint32_t domain,
                          uint64_t virt_start, uint64_t virt_end,
                          IoVirtioIommuMode mode)
{
    IoVirtioIommuStage stage;

    if (!iommu || mode != iommu->config.mode) {
        return -EINVAL;
    }
    stage = (mode == IO_VIRTIO_IOMMU_MODE_S2 ||
             mode == IO_VIRTIO_IOMMU_MODE_S1_S2) ?
            IO_VIRTIO_IOMMU_STAGE_S2 : IO_VIRTIO_IOMMU_STAGE_S1;
    return io_virtio_iommu_unmap_stage(iommu, domain, virt_start, virt_end,
                                       stage);
}

int io_virtio_iommu_translate(IoVirtioIommu *iommu, uint32_t endpoint,
                              uint64_t address, size_t length,
                              uint32_t access, uint64_t *translated,
                              uint64_t *address_mask)
{
    IoVirtioIommuDomain *domain;
    IoVirtioIommuMapping *mapping;
    IoVirtioIommuStage stage;
    iommu_request_context_t context = { 0 };
    IoVirtioIommuRelay *relay;
    bool relay_active = false;
    uint32_t permissions;

    if (!iommu || !length || !translated || !address_mask ||
        endpoint >= iommu->config.max_endpoints ||
        (access & ~(IO_VIRTIO_IOMMU_ACCESS_READ |
                    IO_VIRTIO_IOMMU_ACCESS_WRITE |
                    IO_VIRTIO_IOMMU_ACCESS_EXEC)) ||
        !(access & (IO_VIRTIO_IOMMU_ACCESS_READ |
                    IO_VIRTIO_IOMMU_ACCESS_WRITE |
                    IO_VIRTIO_IOMMU_ACCESS_EXEC))) {
        return -EINVAL;
    }
    domain = iommu->endpoints[endpoint].domain;
    if (!domain) {
        return -ENOENT;
    }
    if (domain->mode == IO_VIRTIO_IOMMU_MODE_S1_S2) {
        for (relay = iommu->relays; relay; relay = relay->next) {
            if (relay->endpoint == endpoint) {
                relay_active = true;
                break;
            }
        }
    }
    stage = (domain->mode == IO_VIRTIO_IOMMU_MODE_S2 ||
             (domain->mode == IO_VIRTIO_IOMMU_MODE_S1_S2 &&
              !relay_active)) ?
            IO_VIRTIO_IOMMU_STAGE_S2 : IO_VIRTIO_IOMMU_STAGE_S1;
    mapping = find_mapping(domain, stage, address);
    if (!mapping || (access & IO_VIRTIO_IOMMU_ACCESS_EXEC) ||
        ((access & IO_VIRTIO_IOMMU_ACCESS_READ) &&
         !(mapping->flags & IO_VIRTIO_IOMMU_MAP_READ)) ||
        ((access & IO_VIRTIO_IOMMU_ACCESS_WRITE) &&
         !(mapping->flags & IO_VIRTIO_IOMMU_MAP_WRITE))) {
        if (getenv("MY_VIRTIO_IOMMU_TRACE")) {
            fprintf(stderr, "viommu translate reject endpoint=%u domain=%u "
                    "addr=%#llx len=%zu stage=%u relay=%u mapping=%p "
                    "access=%#x\n", endpoint, domain->id,
                    (unsigned long long)address, length, stage,
                    relay_active, (void *)mapping, access);
        }
        return -EFAULT;
    }
    context.device_id = endpoint;
    context.is_translated = domain->mode == IO_VIRTIO_IOMMU_MODE_S1_S2 &&
                            !relay_active;
    if (!io_refmodel_iommu_translate(iommu->refmodel, address, length,
                                     &context, access, translated,
                                     address_mask, &permissions)) {
        if (getenv("MY_VIRTIO_IOMMU_TRACE")) {
            fprintf(stderr, "viommu translate refmodel fault endpoint=%u "
                    "domain=%u addr=%#llx len=%zu stage=%u relay=%u "
                    "translated-request=%u\n", endpoint, domain->id,
                    (unsigned long long)address, length, stage,
                    relay_active, context.is_translated);
        }
        return -EFAULT;
    }
    if (*address_mask > VIOMMU_PAGE_MASK) {
        *address_mask = VIOMMU_PAGE_MASK;
    }
    return 0;
}

int io_virtio_iommu_endpoint_domain(IoVirtioIommu *iommu, uint32_t endpoint,
                                    uint32_t *domain_id)
{
    if (!iommu || !domain_id || endpoint >= iommu->config.max_endpoints ||
        !iommu->endpoints[endpoint].domain) {
        return -ENOENT;
    }
    *domain_id = iommu->endpoints[endpoint].domain->id;
    return 0;
}

static IoVirtioIommuRelay *find_relay(IoVirtioIommu *iommu,
                                      uint64_t namespace_id,
                                      uint32_t inner_domain,
                                      uint32_t endpoint)
{
    IoVirtioIommuRelay *relay;

    for (relay = iommu->relays; relay; relay = relay->next) {
        if (relay->namespace_id == namespace_id &&
            relay->inner_domain == inner_domain &&
            relay->endpoint == endpoint) {
            return relay;
        }
    }
    return NULL;
}

int io_virtio_iommu_relay_attach(IoVirtioIommu *iommu, uint64_t namespace_id,
                                 uint32_t inner_domain, uint32_t endpoint)
{
    IoVirtioIommuRelay *relay;
    IoVirtioIommuRelay *other;
    uint32_t outer_domain;

    if (!iommu || !namespace_id || !inner_domain ||
        endpoint >= iommu->config.max_endpoints ||
        iommu->config.mode != IO_VIRTIO_IOMMU_MODE_S1_S2) {
        return -EINVAL;
    }
    if (io_virtio_iommu_endpoint_domain(iommu, endpoint, &outer_domain)) {
        return -ENOENT;
    }
    relay = find_relay(iommu, namespace_id, inner_domain, endpoint);
    if (relay) {
        return relay->outer_domain == outer_domain ? 0 : -EBUSY;
    }
    for (other = iommu->relays; other; other = other->next) {
        if (other->endpoint == endpoint &&
            (other->namespace_id != namespace_id ||
             other->inner_domain != inner_domain)) {
            return -EBUSY;
        }
    }
    relay = calloc(1, sizeof(*relay));
    if (!relay) {
        return -ENOMEM;
    }
    relay->namespace_id = namespace_id;
    relay->inner_domain = inner_domain;
    relay->endpoint = endpoint;
    relay->outer_domain = outer_domain;
    relay->next = iommu->relays;
    iommu->relays = relay;
    return 0;
}

int io_virtio_iommu_relay_detach(IoVirtioIommu *iommu, uint64_t namespace_id,
                                 uint32_t inner_domain, uint32_t endpoint)
{
    IoVirtioIommuRelay **link;

    if (!iommu) {
        return -EINVAL;
    }
    link = &iommu->relays;
    while (*link) {
        IoVirtioIommuRelay *relay = *link;

        if (relay->namespace_id == namespace_id &&
            relay->inner_domain == inner_domain &&
            relay->endpoint == endpoint) {
            IoVirtioIommuDomain *domain = find_domain(iommu,
                                                       relay->outer_domain);
            IoVirtioIommuMapping **mapping_link;
            bool cleared = false;
            int ret = 0;

            if (domain) {
                mapping_link = &domain->mappings;
                while (*mapping_link) {
                    IoVirtioIommuMapping *mapping = *mapping_link;
                    uint64_t address;

                    if (mapping->stage != IO_VIRTIO_IOMMU_STAGE_S1) {
                        mapping_link = &mapping->next;
                        continue;
                    }
                    for (address = mapping->virt_start; ;
                         address += VIOMMU_PAGE_SIZE) {
                        clear_page_mapping(iommu, domain,
                                           IO_VIRTIO_IOMMU_STAGE_S1,
                                           address);
                        if (address == mapping->virt_end -
                                       VIOMMU_PAGE_MASK) {
                            break;
                        }
                    }
                    *mapping_link = mapping->next;
                    free(mapping);
                    cleared = true;
                }
                if (cleared) {
                    ret = invalidate_translation(
                        iommu, domain, IO_VIRTIO_IOMMU_STAGE_S1);
                }
            }
            *link = relay->next;
            free(relay);
            return ret;
        }
        link = &relay->next;
    }
    return -ENOENT;
}

int io_virtio_iommu_relay_map(IoVirtioIommu *iommu, uint64_t namespace_id,
                              uint32_t inner_domain, uint32_t endpoint,
                              uint64_t virt_start, uint64_t virt_end,
                              uint64_t phys_start, uint32_t flags)
{
    IoVirtioIommuRelay *relay;

    if (!iommu) {
        return -EINVAL;
    }
    relay = find_relay(iommu, namespace_id, inner_domain, endpoint);
    if (!relay) {
        return -ENOENT;
    }
    return io_virtio_iommu_map_stage(iommu, relay->outer_domain,
                                     virt_start, virt_end, phys_start, flags,
                                     IO_VIRTIO_IOMMU_STAGE_S1);
}

int io_virtio_iommu_relay_unmap(IoVirtioIommu *iommu, uint64_t namespace_id,
                                uint32_t inner_domain, uint32_t endpoint,
                                uint64_t virt_start, uint64_t virt_end)
{
    IoVirtioIommuRelay *relay;

    if (!iommu) {
        return -EINVAL;
    }
    relay = find_relay(iommu, namespace_id, inner_domain, endpoint);
    if (!relay) {
        return -ENOENT;
    }
    return io_virtio_iommu_unmap_stage(iommu, relay->outer_domain,
                                       virt_start, virt_end,
                                       IO_VIRTIO_IOMMU_STAGE_S1);
}

int io_virtio_iommu_pop_fault(IoVirtioIommu *iommu,
                              IoVirtioIommuFault *fault)
{
    fault_rec_t record;
    uint32_t head;
    uint32_t tail;

    if (!iommu || !fault ||
        !io_refmodel_mmio_read(iommu->refmodel, FQH_OFFSET, &head) ||
        !io_refmodel_mmio_read(iommu->refmodel, FQT_OFFSET, &tail)) {
        return -EINVAL;
    }
    head &= VIOMMU_FQ_ENTRIES - 1;
    tail &= VIOMMU_FQ_ENTRIES - 1;
    if (head == tail) {
        return -ENOENT;
    }
    if (shadow_read(iommu, iommu->fq_address +
                    (uint64_t)head * sizeof(record), &record,
                    sizeof(record))) {
        return -EFAULT;
    }
    memset(fault, 0, sizeof(*fault));
    fault->cause = record.CAUSE;
    fault->endpoint = record.DID;
    fault->process_id = record.PID;
    fault->address = record.iotval;
    fault->address2 = record.iotval2;
    if (record.TTYP == UNTRANSLATED_WRITE_AMO_TRANSACTION ||
        record.TTYP == TRANSLATED_WRITE_AMO_TRANSACTION) {
        fault->access = IO_VIRTIO_IOMMU_ACCESS_WRITE;
    } else if (record.TTYP == UNTRANSLATED_READ_FOR_EXECUTE_TRANSACTION ||
               record.TTYP == TRANSLATED_READ_FOR_EXECUTE_TRANSACTION) {
        fault->access = IO_VIRTIO_IOMMU_ACCESS_EXEC;
    } else {
        fault->access = IO_VIRTIO_IOMMU_ACCESS_READ;
    }
    head = (head + 1) & (VIOMMU_FQ_ENTRIES - 1);
    if (!io_refmodel_mmio_write(iommu->refmodel, FQH_OFFSET, head)) {
        return -EIO;
    }
    return 0;
}
