/* BOSC per-die APLIC inputs, matching the intr.png hardware signal table. */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_RISCV_KMH_BOSC_IRQ_H
#define HW_RISCV_KMH_BOSC_IRQ_H

enum {
    KMH_BOSC_UART0_IRQ = 32,
    KMH_BOSC_UART1_IRQ = 33,
    KMH_BOSC_QSPI_IRQ = 34,
    KMH_BOSC_I2C0_IRQ = 35,
    KMH_BOSC_I2C1_IRQ = 36,
    KMH_BOSC_GPIO_IRQ = 37,
    KMH_BOSC_SD_IRQ = 38,
    KMH_BOSC_SD_WAKEUP_IRQ = 39,
    KMH_BOSC_DMAC_IRQ = 40,
    KMH_BOSC_WDT_IRQ = 41,
    KMH_BOSC_I2S_IRQ = 42,
    /* Inputs 43..63 are tied low in hardware. */
    KMH_BOSC_IOPMP_PCIE2_IRQ = 64,
    KMH_BOSC_IOPMP_IOMMU0_IRQ = 65,
    KMH_BOSC_IOPMP_PCIE1_IRQ = 66,
    KMH_BOSC_IOPMP_PCIE0_IRQ = 67,
    KMH_BOSC_IOATS0_EVENT_IRQ = 68,
    KMH_BOSC_IOATS0_RAS_IRQ = 69,
    KMH_BOSC_IOMMU_IRQ = 70,
    KMH_BOSC_PCIE0_MCTP_IRQ = 71,
    KMH_BOSC_PCIE0_HPM_IRQ = 72,
    KMH_BOSC_PCIE0_RAS_IRQ = 73,
    KMH_BOSC_PCIE0_AER_IRQ = 74,
    KMH_BOSC_PCIE0_SII_IRQ = 75,
    KMH_BOSC_PCIE0_EP_IRQ = 76,
    KMH_BOSC_PCIE_IRQ_STRIDE = 6,
};

static inline unsigned kmh_bosc_pcie_ep_irq(unsigned port)
{
    return KMH_BOSC_PCIE0_EP_IRQ + port * KMH_BOSC_PCIE_IRQ_STRIDE;
}

static inline unsigned kmh_bosc_pcie_sii_irq(unsigned port)
{
    return KMH_BOSC_PCIE0_SII_IRQ + port * KMH_BOSC_PCIE_IRQ_STRIDE;
}

#endif
