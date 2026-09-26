/*
 * BOSC Kunminghu APLIC interrupt-source test device.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_KMH_BOSC_APLIC_TEST_H
#define HW_MISC_KMH_BOSC_APLIC_TEST_H

#include "qemu/typedefs.h"

#define KMH_BOSC_APLIC_TEST_NUM_IRQS       192
#define KMH_BOSC_APLIC_TEST_MMIO_BASE      0x0409014cULL
#define KMH_BOSC_APLIC_TEST_MMIO_SIZE      0x4
#define KMH_BOSC_APLIC_TEST_CLEAR_OFFSET   0x0
#define KMH_BOSC_APLIC_TEST_RAISE_ALL      UINT32_MAX

void kmh_bosc_aplic_test_create(DeviceState *aplic, hwaddr addr);

#endif
