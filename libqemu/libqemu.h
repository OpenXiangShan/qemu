/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LIBQEMU_H
#define LIBQEMU_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIBQEMU_ABI_VERSION 1

/* One runtime per process, resident until process exit. Never dlclose().
 * All functions return 0 on success, -1 on API failure unless documented.
 * errp may be NULL; otherwise pass a NULL-initialized string and release
 * the returned error with error_free(). Deep QEMU fatal errors still exit.
 * Calls may come from host threads, never from signal handlers.
 */
typedef struct LibQemuExports {
    uint32_t abi_version;
    uint32_t struct_size;
    int (*start)(int argc, const char *const *argv, char **errp);
    int (*request_shutdown)(char **errp);
    /* Exactly one caller may wait. Returns after the main worker is joined. */
    int (*wait)(int *exit_status, char **errp);
    void (*error_free)(char *error);
} LibQemuExports;

/* Returns NULL for an incompatible version or an oversized client table. */
const LibQemuExports *libqemu_get_api(uint32_t version, size_t size);

#ifdef __cplusplus
}
#endif
#endif
