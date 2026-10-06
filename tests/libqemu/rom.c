/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Verify the host boot loader across initial reset and a subsequent reset. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libqemu/libqemu.h"

static char *error;
#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s: %s\n", #expr, error ? error : "failed"); \
        exit(2); \
    } \
} while (0)

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    CHECK(library);
    const LibQemuExports *(*get_api)(uint32_t, size_t) =
        (const LibQemuExports *(*)(uint32_t, size_t))dlsym(library, "libqemu_get_api");
    CHECK(get_api);
    const LibQemuExports *api = get_api(LIBQEMU_ABI_VERSION, sizeof(*api));
    CHECK(api && api->core && api->core->struct_size >= sizeof(*api->core));
    const LibQemuCoreExports *core = api->core;
    const char *qargv[] = { "rom-test", "-M", "none", "-m", "0", "-S",
                           "-display", "none", "-serial", "none",
                           "-monitor", "none", "-accel", "tcg" };
    CHECK(api->init(sizeof(qargv) / sizeof(qargv[0]), qargv, &error) == 0);
    LibQemuMemory *root = core->system_memory(&error);
    LibQemuAddressSpace *as = core->system_address_space(&error);
    CHECK(root && as);
    for (unsigned i = 0; i < 2; i++) {
        LibQemuMemoryConfig cfg = {
            .kind = i ? LIBQEMU_ROM : LIBQEMU_RAM,
            .name = i ? "reset-rom" : "reset-ram", .size = 4096,
        };
        LibQemuMemory *mr = core->memory_new(&cfg, &error);
        CHECK(mr && core->memory_add(root, 0x1000 + 0x1000 * i, mr, 0, &error) == 0);
        char payload[] = "boot";
        CHECK(core->rom_add_blob(cfg.name, 0x1000 + 0x1000 * i,
                                 payload, sizeof(payload), &error) == 0);
        memset(payload, 0, sizeof(payload)); /* loader must own a copy */
        char value[5] = { 1 };
        CHECK(core->memory_rw(as, 0x1000 + 0x1000 * i, value, 5, false, &error) == 0);
        CHECK(!memcmp(value, payload, 5)); /* deferred until finish/reset */
    }
    CHECK(core->rom_add_blob("unmapped", 0x5000, "boot", 5, &error) == -1);
    api->error_free(error); error = NULL;
    CHECK(core->rom_add_blob("cross-boundary", 0x1ffe, "boot", 5, &error) == -1);
    api->error_free(error); error = NULL;
    CHECK(api->finish_qemu_init(&error) == 0);
    for (unsigned i = 0; i < 2; i++) {
        char value[5];
        char edit[] = "edit";
        CHECK(core->memory_rw(as, 0x1000 + 0x1000 * i, value, 5, false, &error) == 0);
        CHECK(!memcmp(value, "boot", 5));
        int result = core->memory_rw(as, 0x1000 + 0x1000 * i,
                                     edit, 5, true, &error);
        CHECK(result == (i ? -1 : 0));
        api->error_free(error); error = NULL;
        CHECK(core->memory_rw(as, 0x1000 + 0x1000 * i, value, 5, false, &error) == 0);
        CHECK(!memcmp(value, i ? "boot" : "edit", 5));
    }
    CHECK(core->rom_add_blob("too-late", 0x1000, "boot", 5, &error) == -1);
    api->error_free(error); error = NULL;
    CHECK(api->system_reset(&error) == 0);
    /* system_reset queues a main-loop request; wait for its observable effect. */
    for (unsigned attempt = 0; attempt < 1000; attempt++) {
        char value[5];
        CHECK(core->memory_rw(as, 0x1000, value, 5, false, &error) == 0);
        if (!memcmp(value, "boot", 5)) {
            break;
        }
        CHECK(attempt < 999);
        struct timespec delay = { .tv_nsec = 1000000 };
        nanosleep(&delay, NULL);
    }
    for (unsigned i = 0; i < 2; i++) {
        char value[5];
        CHECK(core->memory_rw(as, 0x1000 + 0x1000 * i, value, 5, false, &error) == 0);
        CHECK(!memcmp(value, "boot", 5));
    }
    int status;
    CHECK(api->request_shutdown(&error) == 0);
    CHECK(api->wait(&status, &error) == 0 && status == 0);
    puts("ROM_RESET_PASS");
    return 0;
}
