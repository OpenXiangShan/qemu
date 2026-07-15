/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef HW_MISC_DEVPROXY_MSI_CONTROLLER_H
#define HW_MISC_DEVPROXY_MSI_CONTROLLER_H

#include "qom/object.h"

#define TYPE_DEVPROXY_MSI_CONTROLLER "devproxy-msi-controller"

OBJECT_DECLARE_SIMPLE_TYPE(DevProxyMSIControllerState,
                           DEVPROXY_MSI_CONTROLLER)

#endif
