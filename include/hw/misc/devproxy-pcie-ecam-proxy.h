/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef HW_MISC_DEVPROXY_PCIE_ECAM_PROXY_H
#define HW_MISC_DEVPROXY_PCIE_ECAM_PROXY_H

#include "qom/object.h"

#define TYPE_DEVPROXY_PCIE_ECAM_PROXY "devproxy-pcie-ecam-proxy"

OBJECT_DECLARE_SIMPLE_TYPE(DevProxyPcieEcamProxyState,
                           DEVPROXY_PCIE_ECAM_PROXY)

#endif
