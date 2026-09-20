/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>
#include "libqemu/libqemu.h"

static const LibQemuExports *api;
static atomic_uint calls;
static atomic_bool stopping;

static void *reader(void *opaque)
{
    while (!atomic_load(&stopping)) {
        int64_t clock;
        char *error = NULL;
        if (api->core->clock_get_ns(&clock, &error)) {
            api->error_free(error);
            break;
        }
        atomic_fetch_add(&calls, 1);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    void *library;
    const LibQemuExports *(*get_api)(uint32_t, size_t);
    pthread_t readers[4];
    int status;
    const char *args[] = { "concurrency", "-M", "none", "-m", "0",
                          "-display", "none", "-serial", "none",
                          "-monitor", "none" };
    assert(argc == 2);
    library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    assert(library);
    get_api = dlsym(library, "libqemu_get_api");
    assert(get_api);
    api = get_api(LIBQEMU_ABI_VERSION, sizeof(*api));
    assert(api && !api->init(sizeof(args) / sizeof(args[0]), args, NULL));
    assert(!api->finish_qemu_init(NULL));
    for (int i = 0; i < 4; i++) {
        assert(!pthread_create(&readers[i], NULL, reader, NULL));
    }
    while (atomic_load(&calls) < 1000) {
        usleep(1000);
    }
    assert(!api->request_shutdown(NULL));
    assert(!api->wait(&status, NULL) && !status);
    atomic_store(&stopping, true);
    for (int i = 0; i < 4; i++) {
        pthread_join(readers[i], NULL);
    }
    assert(api->get_state() == LIBQEMU_STOPPED);
    printf("CONCURRENCY_PASS calls=%u\n", atomic_load(&calls));
    return 0;
}
