/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LIBQEMU_INTERNAL_H
#define LIBQEMU_INTERNAL_H

#include "libqemu/libqemu.h"

bool libqemu_deferred_init(void);
/* The callback runs with BQL on the main worker. Return -1 on API failure. */
int libqemu_call(int (*fn)(void *, char **), void *opaque, bool assembly,
                bool callback_safe, char **errp);
void libqemu_callback_enter(void);
void libqemu_callback_leave(void);
void libqemu_core_cleanup(void);
extern const LibQemuCoreExports libqemu_core_exports;

#endif
