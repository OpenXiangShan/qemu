#ifndef IO_DWC_PCIE_H
#define IO_DWC_PCIE_H

#include <stdint.h>

#include "io_aplic.h"
#include "io_system.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IoDwcPcie IoDwcPcie;

typedef struct IoDwcPcieConfig {
    const char *name;
    uint64_t dbi_base;
    uint64_t dbi_size;
    uint64_t ecam_base;
    uint64_t ecam_size;
    uint64_t bar_base;
    uint64_t bar_size;
    uint32_t msi_irq;
    uint32_t inta_irq;
    uint32_t intb_irq;
    uint32_t intc_irq;
    uint32_t intd_irq;
    const char *root_bus_name;
    const char *secondary_bus_name;
    const char *root_bus_path;
} IoDwcPcieConfig;

IoDwcPcie *io_dwc_pcie_create(IoSystem *system,
                              const IoDwcPcieConfig *config,
                              IoAplic *irq_parent);
void io_dwc_pcie_destroy(IoDwcPcie *pcie);
IoSystemStatus io_dwc_pcie_reset(IoDwcPcie *pcie);

#ifdef __cplusplus
}
#endif

#endif
