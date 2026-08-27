/*
 * BOSC AXI4 traffic generator with DID/PID mapper.
 *
 * Copyright (c) 2026 Beijing Institute of Open Source Chip (BOSC)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_BOSC_AXI4_TRAFFIC_H
#define HW_MISC_BOSC_AXI4_TRAFFIC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_BOSC_AXI4_TRAFFIC "bosc-axi4-traffic"
OBJECT_DECLARE_SIMPLE_TYPE(BoscAxi4TrafficState, BOSC_AXI4_TRAFFIC)

#define BOSC_AXI4_TRAFFIC_MMIO_SIZE 0x1000

#endif /* HW_MISC_BOSC_AXI4_TRAFFIC_H */
