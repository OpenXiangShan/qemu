/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LIBQEMU_H
#define LIBQEMU_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIBQEMU_ABI_VERSION 1

typedef struct LibQemuObject LibQemuObject;
typedef struct LibQemuMemory LibQemuMemory;
typedef struct LibQemuAddressSpace LibQemuAddressSpace;
typedef struct LibQemuIRQ LibQemuIRQ;
typedef struct LibQemuTimer LibQemuTimer;

typedef enum LibQemuState {
    LIBQEMU_NEW, LIBQEMU_STARTING, LIBQEMU_ASSEMBLY, LIBQEMU_FINISHING,
    LIBQEMU_RUNNING, LIBQEMU_STOPPING, LIBQEMU_STOPPED,
} LibQemuState;

/* MMIO callbacks return zero on success, nonzero for a bus transaction error.
 * They run with BQL on a QEMU thread and may use memory_rw, irq_set and
 * request_shutdown. Other synchronous API calls from callbacks are rejected.
 * The opaque data and host RAM must remain valid until wait() returns.
 */
typedef int (*LibQemuReadFn)(void *, uint64_t, uint64_t *, unsigned);
typedef int (*LibQemuWriteFn)(void *, uint64_t, uint64_t, unsigned);
typedef void (*LibQemuIRQFn)(void *, int);
typedef void (*LibQemuTimerFn)(void *);

typedef enum LibQemuMemoryKind {
    LIBQEMU_CONTAINER, LIBQEMU_RAM, LIBQEMU_RAM_PTR,
    LIBQEMU_MMIO, LIBQEMU_ALIAS,
} LibQemuMemoryKind;

typedef struct LibQemuMemoryConfig {
    LibQemuMemoryKind kind;
    const char *name;
    uint64_t size;
    void *ram_ptr;
    LibQemuMemory *alias;
    uint64_t alias_offset;
    LibQemuReadFn read;
    LibQemuWriteFn write;
    void *opaque;
    unsigned min_access_size;
    unsigned max_access_size;
    bool big_endian;
} LibQemuMemoryConfig;

/* Core handles are borrowed except object_new/ref and property_get_str.
 * Unref owned objects during assembly; free strings with error_free.
 * Memory, IRQ, address-space and timer storage remains owned by the runtime.
 * All topology operations require ASSEMBLY. Runtime operations are identified
 * below. Pass only handles obtained from this instance, never stale pointers.
 */
typedef struct LibQemuCoreExports {
    uint32_t struct_size;
    LibQemuObject *(*object_new)(const char *type, char **errp);
    int (*object_ref)(LibQemuObject *, char **errp);
    int (*object_unref)(LibQemuObject *, char **errp);
    LibQemuObject *(*object_resolve)(const char *path, char **errp);
    int (*object_add_child)(LibQemuObject *, const char *, LibQemuObject *, char **);
    int (*property_set_str)(LibQemuObject *, const char *, const char *, char **);
    int (*property_set_bool)(LibQemuObject *, const char *, bool, char **);
    int (*property_set_int)(LibQemuObject *, const char *, int64_t, char **);
    int (*property_set_uint)(LibQemuObject *, const char *, uint64_t, char **);
    int (*property_set_link)(LibQemuObject *, const char *, LibQemuObject *, char **);
    char *(*property_get_str)(LibQemuObject *, const char *, char **);
    int (*property_get_bool)(LibQemuObject *, const char *, bool *, char **);
    int (*property_get_int)(LibQemuObject *, const char *, int64_t *, char **);
    int (*property_get_uint)(LibQemuObject *, const char *, uint64_t *, char **);
    LibQemuObject *(*property_get_link)(LibQemuObject *, const char *, char **);
    LibQemuObject *(*device_get_bus)(LibQemuObject *, const char *, char **);
    int (*device_realize)(LibQemuObject *, LibQemuObject *bus, char **);
    LibQemuMemory *(*sysbus_get_mmio)(LibQemuObject *, unsigned, char **);
    LibQemuMemory *(*system_memory)(char **);
    LibQemuMemory *(*memory_new)(const LibQemuMemoryConfig *, char **);
    LibQemuObject *(*memory_object)(LibQemuMemory *, char **);
    int (*memory_add)(LibQemuMemory *, uint64_t, LibQemuMemory *, int priority, char **);
    int (*memory_del)(LibQemuMemory *, LibQemuMemory *, char **);
    LibQemuAddressSpace *(*address_space_new)(LibQemuMemory *, const char *, char **);
    LibQemuAddressSpace *(*system_address_space)(char **);
    /* Runtime and callback-safe: write=false reads into buf; true writes buf. */
    int (*memory_rw)(LibQemuAddressSpace *, uint64_t, void *, size_t, bool write, char **);
    LibQemuIRQ *(*irq_new)(LibQemuIRQFn, void *, char **);
    LibQemuIRQ *(*gpio_in)(LibQemuObject *, const char *name, int index, char **);
    int (*gpio_connect)(LibQemuObject *, const char *name, int index, LibQemuIRQ *, char **);
    int (*sysbus_connect_irq)(LibQemuObject *, unsigned, LibQemuIRQ *, char **);
    int (*irq_set)(LibQemuIRQ *, int level, char **); /* Runtime/callback-safe. */
    LibQemuTimer *(*timer_new)(LibQemuTimerFn, void *, char **);
    int (*timer_mod)(LibQemuTimer *, int64_t deadline_ns, char **); /* Runtime. */
    int (*timer_del)(LibQemuTimer *, char **); /* Runtime. */
    int (*clock_get_ns)(int64_t *, char **); /* QEMU virtual clock; runtime. */
    int (*cpu_register_reset)(LibQemuObject *, char **);
} LibQemuCoreExports;

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
    /* Appended extension; the complete-machine prefix above stays compatible. */
    int (*init)(int argc, const char *const *argv, char **errp);
    int (*finish_qemu_init)(char **errp);
    LibQemuState (*get_state)(void);
    int (*pause)(char **errp);
    int (*resume)(char **errp);
    int (*system_reset)(char **errp);
    const LibQemuCoreExports *core;
} LibQemuExports;

/* Returns NULL for an incompatible version or an oversized client table. */
const LibQemuExports *libqemu_get_api(uint32_t version, size_t size);

#ifdef __cplusplus
}
#endif
#endif
