/*
 * DesignWare PCIe C-model frontend with QEMU PCI endpoint backend.
 *
 * Guest-visible DBI/iATU/ECAM/BAR windows are owned by io-system-lib.  The
 * QEMU backend below only supplies a private PCIe bus so existing QEMU PCI
 * endpoints such as nvme can be reused while their DMA still goes through the
 * io-system IO2Q callbacks.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/queue.h"
#include "hw/pci/msi.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_host.h"
#include "hw/pci/pci_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "monitor/qdev.h"

#include "io_cmodel_internal.h"
#include "io_dwc_pcie.h"

#define IO_DWC_PCIE_PORT_LINK_CONTROL          0x710
#define IO_DWC_PCIE_PORT_DEBUG1                0x72c
#define IO_DWC_PCIE_PORT_DEBUG1_LINK_UP        BIT(4)
#define IO_DWC_PCIE_LINK_WIDTH_SPEED_CONTROL   0x80c
#define IO_DWC_PCIE_PORT_LOGIC_SPEED_CHANGE    BIT(17)
#define IO_DWC_PCIE_MSI_ADDR_LO                0x820
#define IO_DWC_PCIE_MSI_ADDR_HI                0x824
#define IO_DWC_PCIE_MSI_INTR0_ENABLE           0x828
#define IO_DWC_PCIE_MSI_INTR0_MASK             0x82c
#define IO_DWC_PCIE_MSI_INTR0_STATUS           0x830
#define IO_DWC_PCIE_VERSION_NUMBER             0x8f8
#define IO_DWC_PCIE_VERSION_TYPE               0x8fc
#define IO_DWC_PCIE_VERSION_540A               0x3534302a

#define IO_DWC_PCIE_ATU_VIEWPORT               0x900
#define IO_DWC_PCIE_ATU_REGION_INBOUND         BIT(31)
#define IO_DWC_PCIE_ATU_CR1                    0x904
#define IO_DWC_PCIE_ATU_CR2                    0x908
#define IO_DWC_PCIE_ATU_LOWER_BASE             0x90c
#define IO_DWC_PCIE_ATU_UPPER_BASE             0x910
#define IO_DWC_PCIE_ATU_LIMIT                  0x914
#define IO_DWC_PCIE_ATU_LOWER_TARGET           0x918
#define IO_DWC_PCIE_ATU_UPPER_TARGET           0x91c
#define IO_DWC_PCIE_ATU_UPPER_LIMIT            0x924
#define IO_DWC_PCIE_ATU_TYPE_MASK              0x1f
#define IO_DWC_PCIE_ATU_TYPE_MEM               0x0
#define IO_DWC_PCIE_ATU_TYPE_CFG0              0x4
#define IO_DWC_PCIE_ATU_TYPE_CFG1              0x5
#define IO_DWC_PCIE_ATU_ENABLE                 BIT(31)
#define IO_DWC_PCIE_ATU_CFG_SHIFT_MODE_ENABLE  BIT(28)

#define IO_DWC_PCIE_VIEWPORT_OUTBOUND          0
#define IO_DWC_PCIE_VIEWPORT_INBOUND           1
#define IO_DWC_PCIE_NUM_VIEWPORTS              4

#define TYPE_IO_DWC_PCIE_HOST_QEMU             "io-system-dwc-pcie-host"
#define TYPE_IO_DWC_PCIE_ROOT_QEMU             "io-system-dwc-pcie-root"

OBJECT_DECLARE_SIMPLE_TYPE(IoDwcPcieHostQemu, IO_DWC_PCIE_HOST_QEMU)
OBJECT_DECLARE_SIMPLE_TYPE(IoDwcPcieRootQemu, IO_DWC_PCIE_ROOT_QEMU)

typedef struct IoDwcPcieViewport {
    uint64_t base;
    uint64_t target;
    uint64_t limit;
    uint64_t upper_limit;
    uint32_t cr[2];
} IoDwcPcieViewport;

typedef struct IoDwcPcieMsi {
    uint64_t base;
    uint32_t enable;
    uint32_t mask;
    uint32_t status;
} IoDwcPcieMsi;

typedef struct IoDwcPcieDmaSpace {
    struct IoDwcPcie *pcie;
    PCIBus *bus;
    int devfn;
    MemoryRegion dma_mr;
    AddressSpace dma_as;
    QLIST_ENTRY(IoDwcPcieDmaSpace) link;
} IoDwcPcieDmaSpace;

struct IoDwcPcieHostQemu {
    PCIExpressHost parent_obj;
};

struct IoDwcPcieRootQemu {
    PCIBridge parent_obj;
    char *sec_bus_name;
};

struct IoDwcPcie {
    IoSystem *system;
    IoAplic *irq_parent;
    IoDwcPcieConfig config;

    DeviceState *owner;
    PCIBus *root_bus;
    PCIDevice *root_port;

    MemoryRegion pci_memory;
    MemoryRegion pci_io;
    AddressSpace pci_memory_as;

    QLIST_HEAD(, IoDwcPcieDmaSpace) dma_spaces;

    uint32_t atu_viewport;
    IoDwcPcieViewport viewports[2][IO_DWC_PCIE_NUM_VIEWPORTS];
    IoDwcPcieMsi msi;
};

static const MemoryRegionOps io_dwc_pcie_dma_ops;

static uint64_t io_dwc_pcie_load_le(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint64_t value = 0;

    for (size_t i = 0; i < size && i < sizeof(value); i++) {
        value |= (uint64_t)bytes[i] << (8 * i);
    }

    return value;
}

static void io_dwc_pcie_store_le(void *data, uint64_t value, size_t size)
{
    uint8_t *bytes = data;

    for (size_t i = 0; i < size; i++) {
        bytes[i] = (uint8_t)(value >> (8 * i));
    }
}

static char *io_dwc_pcie_dup_name(const char *name, const char *fallback)
{
    return g_strdup(name && *name ? name : fallback);
}

static uint32_t io_dwc_pcie_requester_id(PCIBus *bus, int devfn)
{
    return ((uint32_t)pci_bus_num(bus) << 8) | (uint32_t)(devfn & 0xff);
}

static void io_dwc_pcie_root_realize(PCIDevice *dev, Error **errp)
{
    IoDwcPcieRootQemu *root = IO_DWC_PCIE_ROOT_QEMU(dev);
    PCIBridge *br = PCI_BRIDGE(dev);
    Error *local_err = NULL;

    br->bus_name = root->sec_bus_name && *root->sec_bus_name ?
                   root->sec_bus_name : "io-system-pcie";

    pci_set_word(dev->config + PCI_COMMAND,
                 PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    pci_config_set_interrupt_pin(dev->config, 1);
    pci_bridge_initfn(dev, TYPE_PCIE_BUS);
    pcie_port_init_reg(dev);

    pcie_cap_init(dev, 0x70, PCI_EXP_TYPE_ROOT_PORT, 0, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        pci_bridge_exitfn(dev);
        return;
    }

    msi_nonbroken = true;
    msi_init(dev, 0x50, 32, true, true, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        pcie_cap_exit(dev);
        pci_bridge_exitfn(dev);
    }
}

static const Property io_dwc_pcie_root_props[] = {
    DEFINE_PROP_STRING("sec-bus-name", IoDwcPcieRootQemu, sec_bus_name),
};

static void io_dwc_pcie_root_class_init(ObjectClass *klass, const void *data)
{
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    pc->vendor_id = PCI_VENDOR_ID_SYNOPSYS;
    pc->device_id = 0xabcd;
    pc->revision = 0;
    pc->class_id = PCI_CLASS_BRIDGE_PCI;
    pc->realize = io_dwc_pcie_root_realize;
    pc->config_write = pci_bridge_write_config;
    pc->exit = pci_bridge_exitfn;
    device_class_set_legacy_reset(dc, pci_bridge_reset);
    device_class_set_props(dc, io_dwc_pcie_root_props);
    dc->user_creatable = false;
}

static AddressSpace *io_dwc_pcie_iommu_as(PCIBus *bus, void *opaque,
                                          int devfn)
{
    IoDwcPcie *pcie = opaque;
    IoDwcPcieDmaSpace *space;
    uint32_t requester_id;
    char name[64];

    requester_id = io_dwc_pcie_requester_id(bus, devfn);
    QLIST_FOREACH(space, &pcie->dma_spaces, link) {
        if (space->bus == bus && space->devfn == devfn) {
            return &space->dma_as;
        }
    }

    space = g_new0(IoDwcPcieDmaSpace, 1);
    space->pcie = pcie;
    space->bus = bus;
    space->devfn = devfn;
    snprintf(name, sizeof(name), "io-system-pcie-dma-%02x:%02x.%x",
             pci_bus_num(bus), PCI_SLOT(devfn), PCI_FUNC(devfn));
    memory_region_init_io(&space->dma_mr, OBJECT(pcie->owner),
                          &io_dwc_pcie_dma_ops, space, name, UINT64_MAX);
    address_space_init(&space->dma_as, &space->dma_mr, name);
    QLIST_INSERT_HEAD(&pcie->dma_spaces, space, link);
    return &space->dma_as;
}

static const PCIIOMMUOps io_dwc_pcie_iommu_ops = {
    .get_address_space = io_dwc_pcie_iommu_as,
};

static void io_dwc_pcie_set_irq(void *opaque, int irq_num, int level)
{
    IoDwcPcie *pcie = opaque;
    static const uint32_t irq_index_to_config_offset[PCI_NUM_PINS] = {
        offsetof(IoDwcPcieConfig, inta_irq),
        offsetof(IoDwcPcieConfig, intb_irq),
        offsetof(IoDwcPcieConfig, intc_irq),
        offsetof(IoDwcPcieConfig, intd_irq),
    };
    const uint8_t *base = (const uint8_t *)&pcie->config;
    uint32_t source;

    if (irq_num < 0 || irq_num >= PCI_NUM_PINS || !pcie->irq_parent) {
        return;
    }

    source = *(const uint32_t *)(base + irq_index_to_config_offset[irq_num]);
    if (source == IO_MANIFEST_NO_IRQ) {
        return;
    }

    io_aplic_irq_line_set(pcie->irq_parent, source, level > 0);
}

static uint64_t io_dwc_pcie_dma_read(void *opaque, hwaddr addr, unsigned size)
{
    IoDwcPcieDmaSpace *space = opaque;
    IoDwcPcie *pcie = space->pcie;
    IoSystemDmaAttrs attrs = {
        .requester_id = io_dwc_pcie_requester_id(space->bus, space->devfn),
    };
    uint8_t data[8] = { 0xff };

    if (!size || size > sizeof(data) ||
        io_system_dma_read(pcie->system, &attrs, addr, data, size) !=
        IO_SYSTEM_OK) {
        return UINT64_MAX;
    }

    return io_dwc_pcie_load_le(data, size);
}

static void io_dwc_pcie_raise_internal_msi(IoDwcPcie *pcie, uint32_t vector)
{
    uint32_t bit;

    if (vector >= 32) {
        return;
    }

    bit = BIT(vector);
    pcie->msi.status |= bit & pcie->msi.enable;
    if ((pcie->msi.status & ~pcie->msi.mask) && pcie->irq_parent &&
        pcie->config.msi_irq != IO_MANIFEST_NO_IRQ) {
        io_aplic_irq_line_set(pcie->irq_parent, pcie->config.msi_irq, true);
    }
}

static void io_dwc_pcie_dma_write(void *opaque, hwaddr addr,
                                  uint64_t value, unsigned size)
{
    IoDwcPcieDmaSpace *space = opaque;
    IoDwcPcie *pcie = space->pcie;
    IoSystemDmaAttrs attrs = {
        .requester_id = io_dwc_pcie_requester_id(space->bus, space->devfn),
    };
    uint8_t data[8];

    if (!size || size > sizeof(data)) {
        return;
    }

    io_dwc_pcie_store_le(data, value, size);

    if (size == 4 && pcie->msi.enable && addr == pcie->msi.base) {
        io_dwc_pcie_raise_internal_msi(pcie, (uint32_t)value);
        return;
    }

    (void)io_system_dma_write(pcie->system, &attrs, addr, data, size);
}

static const MemoryRegionOps io_dwc_pcie_dma_ops = {
    .read = io_dwc_pcie_dma_read,
    .write = io_dwc_pcie_dma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static IoDwcPcieViewport *io_dwc_pcie_current_viewport(IoDwcPcie *pcie)
{
    uint32_t idx = pcie->atu_viewport & (IO_DWC_PCIE_NUM_VIEWPORTS - 1);
    uint32_t dir = (pcie->atu_viewport & IO_DWC_PCIE_ATU_REGION_INBOUND) ?
                   IO_DWC_PCIE_VIEWPORT_INBOUND :
                   IO_DWC_PCIE_VIEWPORT_OUTBOUND;

    return &pcie->viewports[dir][idx];
}

static uint64_t io_dwc_pcie_viewport_limit(const IoDwcPcieViewport *viewport)
{
    return (viewport->base & ~(uint64_t)UINT32_MAX) |
           (uint32_t)viewport->limit;
}

static bool io_dwc_pcie_dwc_reg_read32(IoDwcPcie *pcie, uint32_t offset,
                                       uint32_t *value)
{
    IoDwcPcieViewport *viewport = io_dwc_pcie_current_viewport(pcie);

    switch (offset) {
    case IO_DWC_PCIE_VERSION_NUMBER:
    case IO_DWC_PCIE_VERSION_TYPE:
        *value = IO_DWC_PCIE_VERSION_540A;
        return true;
    case IO_DWC_PCIE_PORT_LINK_CONTROL:
        *value = 0xdeadbeef;
        return true;
    case IO_DWC_PCIE_LINK_WIDTH_SPEED_CONTROL:
        *value = IO_DWC_PCIE_PORT_LOGIC_SPEED_CHANGE;
        return true;
    case IO_DWC_PCIE_PORT_DEBUG1:
        *value = IO_DWC_PCIE_PORT_DEBUG1_LINK_UP;
        return true;
    case IO_DWC_PCIE_MSI_ADDR_LO:
        *value = (uint32_t)pcie->msi.base;
        return true;
    case IO_DWC_PCIE_MSI_ADDR_HI:
        *value = (uint32_t)(pcie->msi.base >> 32);
        return true;
    case IO_DWC_PCIE_MSI_INTR0_ENABLE:
        *value = pcie->msi.enable;
        return true;
    case IO_DWC_PCIE_MSI_INTR0_MASK:
        *value = pcie->msi.mask;
        return true;
    case IO_DWC_PCIE_MSI_INTR0_STATUS:
        *value = pcie->msi.status;
        return true;
    case IO_DWC_PCIE_ATU_VIEWPORT:
        *value = pcie->atu_viewport;
        return true;
    case IO_DWC_PCIE_ATU_CR1:
        *value = viewport->cr[0];
        return true;
    case IO_DWC_PCIE_ATU_CR2:
        *value = viewport->cr[1];
        return true;
    case IO_DWC_PCIE_ATU_LOWER_BASE:
        *value = (uint32_t)viewport->base;
        return true;
    case IO_DWC_PCIE_ATU_UPPER_BASE:
        *value = (uint32_t)(viewport->base >> 32);
        return true;
    case IO_DWC_PCIE_ATU_LIMIT:
        *value = (uint32_t)viewport->limit;
        return true;
    case IO_DWC_PCIE_ATU_LOWER_TARGET:
        *value = (uint32_t)viewport->target;
        return true;
    case IO_DWC_PCIE_ATU_UPPER_TARGET:
        *value = (uint32_t)(viewport->target >> 32);
        return true;
    case IO_DWC_PCIE_ATU_UPPER_LIMIT:
        *value = (uint32_t)viewport->upper_limit;
        return true;
    default:
        return false;
    }
}

static void io_dwc_pcie_msi_irq_update(IoDwcPcie *pcie)
{
    if (!pcie->irq_parent || pcie->config.msi_irq == IO_MANIFEST_NO_IRQ) {
        return;
    }

    io_aplic_irq_line_set(pcie->irq_parent, pcie->config.msi_irq,
                          !!(pcie->msi.status & ~pcie->msi.mask));
}

static bool io_dwc_pcie_dwc_reg_write32(IoDwcPcie *pcie, uint32_t offset,
                                        uint32_t value)
{
    IoDwcPcieViewport *viewport = io_dwc_pcie_current_viewport(pcie);

    switch (offset) {
    case IO_DWC_PCIE_PORT_LINK_CONTROL:
    case IO_DWC_PCIE_LINK_WIDTH_SPEED_CONTROL:
    case IO_DWC_PCIE_PORT_DEBUG1:
    case IO_DWC_PCIE_VERSION_NUMBER:
    case IO_DWC_PCIE_VERSION_TYPE:
        return true;
    case IO_DWC_PCIE_MSI_ADDR_LO:
        pcie->msi.base = (pcie->msi.base & ~(uint64_t)UINT32_MAX) | value;
        return true;
    case IO_DWC_PCIE_MSI_ADDR_HI:
        pcie->msi.base = (pcie->msi.base & UINT32_MAX) |
                         ((uint64_t)value << 32);
        return true;
    case IO_DWC_PCIE_MSI_INTR0_ENABLE:
        pcie->msi.enable = value;
        io_dwc_pcie_msi_irq_update(pcie);
        return true;
    case IO_DWC_PCIE_MSI_INTR0_MASK:
        pcie->msi.mask = value;
        io_dwc_pcie_msi_irq_update(pcie);
        return true;
    case IO_DWC_PCIE_MSI_INTR0_STATUS:
        pcie->msi.status ^= value;
        io_dwc_pcie_msi_irq_update(pcie);
        return true;
    case IO_DWC_PCIE_ATU_VIEWPORT:
        pcie->atu_viewport = value & (IO_DWC_PCIE_ATU_REGION_INBOUND |
                                      (IO_DWC_PCIE_NUM_VIEWPORTS - 1));
        return true;
    case IO_DWC_PCIE_ATU_CR1:
        viewport->cr[0] = value;
        return true;
    case IO_DWC_PCIE_ATU_CR2:
        viewport->cr[1] = value;
        return true;
    case IO_DWC_PCIE_ATU_LOWER_BASE:
        viewport->base = (viewport->base & ~(uint64_t)UINT32_MAX) | value;
        return true;
    case IO_DWC_PCIE_ATU_UPPER_BASE:
        viewport->base = (viewport->base & UINT32_MAX) |
                         ((uint64_t)value << 32);
        return true;
    case IO_DWC_PCIE_ATU_LIMIT:
        viewport->limit = (viewport->limit & ~(uint64_t)UINT32_MAX) | value;
        return true;
    case IO_DWC_PCIE_ATU_LOWER_TARGET:
        viewport->target = (viewport->target & ~(uint64_t)UINT32_MAX) | value;
        return true;
    case IO_DWC_PCIE_ATU_UPPER_TARGET:
        viewport->target = (viewport->target & UINT32_MAX) |
                           ((uint64_t)value << 32);
        return true;
    case IO_DWC_PCIE_ATU_UPPER_LIMIT:
        viewport->upper_limit = value;
        return true;
    default:
        return false;
    }
}

static uint32_t io_dwc_pcie_dbi_read32(IoDwcPcie *pcie, uint32_t offset)
{
    uint32_t value;

    if (io_dwc_pcie_dwc_reg_read32(pcie, offset, &value)) {
        return value;
    }

    if (!pcie->root_port) {
        return UINT32_MAX;
    }

    return pci_host_config_read_common(pcie->root_port, offset,
                                       pci_config_size(pcie->root_port), 4);
}

static void io_dwc_pcie_dbi_write32(IoDwcPcie *pcie, uint32_t offset,
                                    uint32_t value)
{
    if (io_dwc_pcie_dwc_reg_write32(pcie, offset, value)) {
        return;
    }

    if (!pcie->root_port) {
        return;
    }

    pci_host_config_write_common(pcie->root_port, offset,
                                 pci_config_size(pcie->root_port),
                                 value, 4);
}

static IoSystemStatus io_dwc_pcie_dbi_read(void *opaque, uint64_t addr,
                                           void *data, size_t size)
{
    IoDwcPcie *pcie = opaque;
    uint32_t offset;
    uint32_t aligned;
    uint32_t value;
    unsigned shift;
    uint64_t mask;

    if (!data || !size || size > 4 || addr < pcie->config.dbi_base) {
        return IO_SYSTEM_ERR_INVALID;
    }

    offset = (uint32_t)(addr - pcie->config.dbi_base);
    if (offset + size > pcie->config.dbi_size) {
        return IO_SYSTEM_ERR_UNMAPPED;
    }

    aligned = offset & ~3u;
    shift = (offset & 3u) * 8u;
    mask = size == 4 ? UINT32_MAX : ((1ULL << (size * 8u)) - 1u);
    value = io_dwc_pcie_dbi_read32(pcie, aligned);
    io_dwc_pcie_store_le(data, (value >> shift) & mask, size);
    return IO_SYSTEM_OK;
}

static IoSystemStatus io_dwc_pcie_dbi_write(void *opaque, uint64_t addr,
                                            const void *data, size_t size)
{
    IoDwcPcie *pcie = opaque;
    uint32_t offset;
    uint32_t aligned;
    uint32_t old_value;
    uint32_t new_value;
    uint64_t mask;
    unsigned shift;

    if (!data || !size || size > 4 || addr < pcie->config.dbi_base) {
        return IO_SYSTEM_ERR_INVALID;
    }

    offset = (uint32_t)(addr - pcie->config.dbi_base);
    if (offset + size > pcie->config.dbi_size) {
        return IO_SYSTEM_ERR_UNMAPPED;
    }

    aligned = offset & ~3u;
    shift = (offset & 3u) * 8u;
    mask = size == 4 ? UINT32_MAX : ((1ULL << (size * 8u)) - 1u);
    old_value = io_dwc_pcie_dbi_read32(pcie, aligned);
    new_value = (old_value & ~(uint32_t)(mask << shift)) |
                (uint32_t)((io_dwc_pcie_load_le(data, size) & mask) << shift);
    io_dwc_pcie_dbi_write32(pcie, aligned, new_value);
    return IO_SYSTEM_OK;
}

static PCIDevice *io_dwc_pcie_find_ecam_device(IoDwcPcie *pcie,
                                               uint8_t bus, uint8_t devfn)
{
    if (!pcie->root_bus) {
        return NULL;
    }

    if (!bus) {
        if (PCI_SLOT(devfn) > 0 || PCI_FUNC(devfn) > 0) {
            return NULL;
        }
        return pcie->root_port;
    }

    return pci_find_device(pcie->root_bus, bus, devfn);
}

static bool io_dwc_pcie_is_cfg_viewport(const IoDwcPcieViewport *viewport)
{
    uint32_t type = viewport->cr[0] & IO_DWC_PCIE_ATU_TYPE_MASK;

    return (viewport->cr[1] & IO_DWC_PCIE_ATU_ENABLE) &&
           (type == IO_DWC_PCIE_ATU_TYPE_CFG0 ||
            type == IO_DWC_PCIE_ATU_TYPE_CFG1);
}

static bool io_dwc_pcie_decode_cfg_viewport(IoDwcPcie *pcie, uint64_t addr,
                                            uint8_t *bus, uint8_t *devfn,
                                            uint32_t *where)
{
    for (size_t i = 0; i < IO_DWC_PCIE_NUM_VIEWPORTS; i++) {
        IoDwcPcieViewport *viewport =
            &pcie->viewports[IO_DWC_PCIE_VIEWPORT_OUTBOUND][i];
        uint64_t limit = io_dwc_pcie_viewport_limit(viewport);

        if (!io_dwc_pcie_is_cfg_viewport(viewport)) {
            continue;
        }
        if (addr < viewport->base || addr > limit) {
            continue;
        }

        if (viewport->cr[1] & IO_DWC_PCIE_ATU_CFG_SHIFT_MODE_ENABLE) {
            hwaddr ecam_addr = addr & (PCIE_MMCFG_SIZE_MAX - 1);

            *bus = PCIE_MMCFG_BUS(ecam_addr);
            *devfn = PCIE_MMCFG_DEVFN(ecam_addr);
            *where = PCIE_MMCFG_CONFOFFSET(ecam_addr);
        } else {
            *bus = (viewport->target >> 24) & 0xff;
            *devfn = (viewport->target >> 16) & 0xff;
            *where = addr - viewport->base;
        }
        return true;
    }

    return false;
}

static IoSystemStatus io_dwc_pcie_ecam_read(void *opaque, uint64_t addr,
                                            void *data, size_t size)
{
    IoDwcPcie *pcie = opaque;
    uint64_t offset;
    uint8_t bus;
    uint8_t devfn;
    uint32_t where;
    PCIDevice *dev;
    uint32_t value;
    bool cfg_viewport;

    if (!data || !size || size > 4 || addr < pcie->config.ecam_base) {
        return IO_SYSTEM_ERR_INVALID;
    }

    offset = addr - pcie->config.ecam_base;
    if (offset + size > pcie->config.ecam_size) {
        return IO_SYSTEM_ERR_UNMAPPED;
    }

    cfg_viewport = io_dwc_pcie_decode_cfg_viewport(pcie, addr, &bus, &devfn,
                                                   &where);
    if (!cfg_viewport) {
        bus = (offset >> 20) & 0xff;
        devfn = (offset >> 12) & 0xff;
        where = offset & 0xfff;
    }

    if (!cfg_viewport && !bus && !devfn) {
        return io_dwc_pcie_dbi_read(pcie, pcie->config.dbi_base + where,
                                    data, size);
    }

    dev = io_dwc_pcie_find_ecam_device(pcie, bus, devfn);
    if (!dev) {
        memset(data, 0xff, size);
        return IO_SYSTEM_OK;
    }

    where &= pci_config_size(dev) - 1;
    value = pci_host_config_read_common(dev, where, pci_config_size(dev),
                                        (uint32_t)size);
    io_dwc_pcie_store_le(data, value, size);
    return IO_SYSTEM_OK;
}

static IoSystemStatus io_dwc_pcie_ecam_write(void *opaque, uint64_t addr,
                                             const void *data, size_t size)
{
    IoDwcPcie *pcie = opaque;
    uint64_t offset;
    uint8_t bus;
    uint8_t devfn;
    uint32_t where;
    PCIDevice *dev;
    bool cfg_viewport;

    if (!data || !size || size > 4 || addr < pcie->config.ecam_base) {
        return IO_SYSTEM_ERR_INVALID;
    }

    offset = addr - pcie->config.ecam_base;
    if (offset + size > pcie->config.ecam_size) {
        return IO_SYSTEM_ERR_UNMAPPED;
    }

    cfg_viewport = io_dwc_pcie_decode_cfg_viewport(pcie, addr, &bus, &devfn,
                                                   &where);
    if (!cfg_viewport) {
        bus = (offset >> 20) & 0xff;
        devfn = (offset >> 12) & 0xff;
        where = offset & 0xfff;
    }

    if (!cfg_viewport && !bus && !devfn) {
        return io_dwc_pcie_dbi_write(pcie, pcie->config.dbi_base + where,
                                     data, size);
    }

    dev = io_dwc_pcie_find_ecam_device(pcie, bus, devfn);
    if (!dev) {
        return IO_SYSTEM_OK;
    }

    where &= pci_config_size(dev) - 1;
    pci_host_config_write_common(dev, where, pci_config_size(dev),
                                 (uint32_t)io_dwc_pcie_load_le(data, size),
                                 (uint32_t)size);
    return IO_SYSTEM_OK;
}

static bool io_dwc_pcie_bar_translate(IoDwcPcie *pcie, uint64_t cpu_addr,
                                      uint64_t *pci_addr)
{
    for (size_t i = 0; i < IO_DWC_PCIE_NUM_VIEWPORTS; i++) {
        IoDwcPcieViewport *viewport =
            &pcie->viewports[IO_DWC_PCIE_VIEWPORT_OUTBOUND][i];
        uint64_t limit = io_dwc_pcie_viewport_limit(viewport);

        if (!(viewport->cr[1] & IO_DWC_PCIE_ATU_ENABLE)) {
            continue;
        }
        if ((viewport->cr[0] & IO_DWC_PCIE_ATU_TYPE_MASK) !=
            IO_DWC_PCIE_ATU_TYPE_MEM) {
            continue;
        }
        if (cpu_addr < viewport->base || cpu_addr > limit) {
            continue;
        }

        *pci_addr = viewport->target + (cpu_addr - viewport->base);
        return true;
    }

    return false;
}

static IoSystemStatus io_dwc_pcie_bar_access(IoDwcPcie *pcie, uint64_t addr,
                                             void *data, size_t size,
                                             bool write)
{
    uint64_t pci_addr;
    MemTxResult result;

    if (!data || !size || size > 8 || addr < pcie->config.bar_base) {
        return IO_SYSTEM_ERR_INVALID;
    }
    if (addr + size > pcie->config.bar_base + pcie->config.bar_size) {
        return IO_SYSTEM_ERR_UNMAPPED;
    }
    if (!io_dwc_pcie_bar_translate(pcie, addr, &pci_addr)) {
        if (!write) {
            memset(data, 0xff, size);
        }
        return IO_SYSTEM_ERR_UNMAPPED;
    }

    result = address_space_rw(&pcie->pci_memory_as, pci_addr,
                              MEMTXATTRS_UNSPECIFIED, data, size, write);
    return result == MEMTX_OK ? IO_SYSTEM_OK : IO_SYSTEM_ERR_IO;
}

static IoSystemStatus io_dwc_pcie_bar_read(void *opaque, uint64_t addr,
                                           void *data, size_t size)
{
    return io_dwc_pcie_bar_access(opaque, addr, data, size, false);
}

static IoSystemStatus io_dwc_pcie_bar_write(void *opaque, uint64_t addr,
                                            const void *data, size_t size)
{
    return io_dwc_pcie_bar_access(opaque, addr, (void *)data, size, true);
}

IoSystemStatus io_dwc_pcie_reset(IoDwcPcie *pcie)
{
    if (!pcie) {
        return IO_SYSTEM_ERR_INVALID;
    }

    pcie->atu_viewport = 0;
    memset(pcie->viewports, 0, sizeof(pcie->viewports));
    memset(&pcie->msi, 0, sizeof(pcie->msi));
    return IO_SYSTEM_OK;
}

static void io_dwc_pcie_register_windows(IoDwcPcie *pcie, Error **errp)
{
    IoSystemStatus status;

    status = io_cmodel_register_mmio_window(pcie->system, "dwc-dbi",
                                            pcie->config.dbi_base,
                                            pcie->config.dbi_size,
                                            io_dwc_pcie_dbi_read,
                                            io_dwc_pcie_dbi_write, pcie);
    if (status != IO_SYSTEM_OK) {
        error_setg(errp, "failed to register DWC DBI window: %d", status);
        return;
    }

    status = io_cmodel_register_mmio_window(pcie->system, "pcie-ecam",
                                            pcie->config.ecam_base,
                                            pcie->config.ecam_size,
                                            io_dwc_pcie_ecam_read,
                                            io_dwc_pcie_ecam_write, pcie);
    if (status != IO_SYSTEM_OK) {
        error_setg(errp, "failed to register PCIe ECAM window: %d", status);
        return;
    }

    status = io_cmodel_register_mmio_window(pcie->system, "pcie-bar",
                                            pcie->config.bar_base,
                                            pcie->config.bar_size,
                                            io_dwc_pcie_bar_read,
                                            io_dwc_pcie_bar_write, pcie);
    if (status != IO_SYSTEM_OK) {
        error_setg(errp, "failed to register PCIe BAR window: %d", status);
    }
}

IoDwcPcie *io_dwc_pcie_create(IoSystem *system,
                              const IoDwcPcieConfig *config,
                              IoAplic *irq_parent)
{
    IoDwcPcie *pcie;
    DeviceState *root_dev;
    Error *local_err = NULL;
    char *owner_id;
    const char *name;
    const char *root_bus_name;
    const char *secondary_bus_name;
    const char *assigned_id;

    if (!system || !config || !config->dbi_size || !config->ecam_size ||
        !config->bar_size) {
        return NULL;
    }

    pcie = g_new0(IoDwcPcie, 1);
    pcie->system = system;
    pcie->irq_parent = irq_parent;
    pcie->config = *config;
    QLIST_INIT(&pcie->dma_spaces);
    pcie->config.name = io_dwc_pcie_dup_name(config->name, "dwc-pcie");
    pcie->config.root_bus_name =
        io_dwc_pcie_dup_name(config->root_bus_name, "io-system-pcie-root");
    pcie->config.secondary_bus_name =
        io_dwc_pcie_dup_name(config->secondary_bus_name, "io-system-pcie");
    pcie->config.root_bus_path =
        io_dwc_pcie_dup_name(config->root_bus_path, "0000:00");

    name = pcie->config.name;
    root_bus_name = pcie->config.root_bus_name;
    secondary_bus_name = pcie->config.secondary_bus_name;

    owner_id = g_strdup_printf("%s-host", name);
    pcie->owner = qdev_new(TYPE_IO_DWC_PCIE_HOST_QEMU);
    assigned_id = qdev_set_id(pcie->owner, owner_id, &local_err);
    if (!assigned_id) {
        error_report_err(local_err);
        io_dwc_pcie_destroy(pcie);
        return NULL;
    }
    sysbus_realize(SYS_BUS_DEVICE(pcie->owner), &error_fatal);

    memory_region_init(&pcie->pci_memory, OBJECT(pcie->owner),
                       "io-system-pcie-memory", UINT64_MAX);
    memory_region_init(&pcie->pci_io, OBJECT(pcie->owner),
                       "io-system-pcie-io", 0x10000);
    address_space_init(&pcie->pci_memory_as, &pcie->pci_memory,
                       "io-system-pcie-memory-as");

    pcie->root_bus = pci_register_root_bus(pcie->owner, root_bus_name,
                                           io_dwc_pcie_set_irq,
                                           pci_swizzle_map_irq_fn,
                                           pcie, &pcie->pci_memory,
                                           &pcie->pci_io, 0, PCI_NUM_PINS,
                                           TYPE_PCIE_BUS);
    PCI_HOST_BRIDGE(pcie->owner)->bus = pcie->root_bus;
    pcie->root_bus->flags |= PCI_BUS_EXTENDED_CONFIG_SPACE;
    pci_setup_iommu(pcie->root_bus, &io_dwc_pcie_iommu_ops, pcie);

    root_dev = qdev_new(TYPE_IO_DWC_PCIE_ROOT_QEMU);
    assigned_id = qdev_set_id(root_dev, g_strdup(secondary_bus_name),
                              &local_err);
    if (!assigned_id) {
        error_report_err(local_err);
        io_dwc_pcie_destroy(pcie);
        return NULL;
    }
    qdev_prop_set_int32(root_dev, "addr", PCI_DEVFN(0, 0));
    qdev_prop_set_string(root_dev, "sec-bus-name", secondary_bus_name);
    qdev_realize(root_dev, BUS(pcie->root_bus), &error_fatal);
    pcie->root_port = PCI_DEVICE(root_dev);

    io_dwc_pcie_reset(pcie);
    io_dwc_pcie_register_windows(pcie, &local_err);
    if (local_err) {
        error_report_err(local_err);
        io_dwc_pcie_destroy(pcie);
        return NULL;
    }

    return pcie;
}

void io_dwc_pcie_destroy(IoDwcPcie *pcie)
{
    if (!pcie) {
        return;
    }

    g_free((char *)pcie->config.name);
    g_free((char *)pcie->config.root_bus_name);
    g_free((char *)pcie->config.secondary_bus_name);
    g_free((char *)pcie->config.root_bus_path);
    g_free(pcie);
}

static const TypeInfo io_dwc_pcie_qemu_types[] = {
    {
        .name = TYPE_IO_DWC_PCIE_HOST_QEMU,
        .parent = TYPE_PCIE_HOST_BRIDGE,
        .instance_size = sizeof(IoDwcPcieHostQemu),
    }, {
        .name = TYPE_IO_DWC_PCIE_ROOT_QEMU,
        .parent = TYPE_PCI_BRIDGE,
        .instance_size = sizeof(IoDwcPcieRootQemu),
        .class_init = io_dwc_pcie_root_class_init,
        .interfaces = (const InterfaceInfo[]) {
            { INTERFACE_PCIE_DEVICE },
            { }
        },
    },
};

DEFINE_TYPES(io_dwc_pcie_qemu_types)
