/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef HW_MISC_DEVPROXY_IRQ_CONTROLLER_H
#define HW_MISC_DEVPROXY_IRQ_CONTROLLER_H

#include "qom/object.h"

#define TYPE_DEVPROXY_IRQ_CONTROLLER "devproxy-irq-controller"

OBJECT_DECLARE_SIMPLE_TYPE(DevProxyIRQControllerState,
                           DEVPROXY_IRQ_CONTROLLER)

#endif
