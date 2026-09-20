/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Usage: libqemu-run LIBRARY [--control-fd FD] [--split-init] QEMU-OPTIONS... */
#include <dlfcn.h>
#include <errno.h>
#include <stdbool.h>
#include <glib.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libqemu/libqemu.h"

static const LibQemuExports *api;
static int control_fd = -1;

static void *control(void *opaque)
{
    char byte;
    while (read(control_fd, &byte, 1) < 0) {
        if (errno != EINTR) {
            return NULL;
        }
    }
    api->request_shutdown(NULL);
    return NULL;
}

/* Exercise coexistence with a host-owned default GLib event loop. */
static void *host_loop(void *opaque)
{
    g_main_loop_run(opaque);
    return NULL;
}

int main(int argc, char **argv)
{
    const LibQemuExports *(*get_api)(uint32_t, size_t);
    void *library;
    char *error = NULL;
    int status, first = 2;
    bool split_init = false;
    pthread_t controller, loop_thread;
    GMainLoop *loop;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s LIBRARY [--control-fd FD] [--split-init] QEMU-OPTIONS...\n",
                argv[0]);
        return 2;
    }
    if (argc > 4 && !strcmp(argv[2], "--control-fd")) {
        control_fd = atoi(argv[3]);
        first = 4;
    }
    if (argc > first && !strcmp(argv[first], "--split-init")) {
        split_init = true;
        first++;
    }
    library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "%s\n", dlerror());
        return 2;
    }
    get_api = (const LibQemuExports *(*)(uint32_t, size_t))
              dlsym(library, "libqemu_get_api");
    if (!get_api || !(api = get_api(LIBQEMU_ABI_VERSION, sizeof(*api)))) {
        fprintf(stderr, "incompatible libqemu ABI\n");
        return 2;
    }
    loop = g_main_loop_new(NULL, false);
    if (pthread_create(&loop_thread, NULL, host_loop, loop)) {
        fprintf(stderr, "cannot create host GLib thread\n");
        return 2;
    }
    while (!g_main_loop_is_running(loop)) {
        g_thread_yield();
    }
    argv[first - 1] = (char *)"embedded-qemu";
    int (*init)(int, const char *const *, char **) =
        split_init ? api->init : api->start;
    if (init(argc - first + 1,
             (const char *const *)&argv[first - 1], &error) ||
        (split_init && api->finish_qemu_init(&error))) {
        fprintf(stderr, "%s\n", error);
        api->error_free(error);
        return 2;
    }
    puts("HOST_STARTED");
    fflush(stdout);
    if (control_fd >= 0) {
        if (pthread_create(&controller, NULL, control, NULL)) {
            fprintf(stderr, "cannot create control thread\n");
            return 2;
        }
    }
    if (api->wait(&status, &error)) {
        fprintf(stderr, "%s\n", error);
        api->error_free(error);
        return 2;
    }
    if (control_fd >= 0) {
        pthread_cancel(controller);
        pthread_join(controller, NULL);
    }
    g_main_loop_quit(loop);
    pthread_join(loop_thread, NULL);
    g_main_loop_unref(loop);
    printf("HOST_CONTINUES status=%d\n", status);
    fflush(stdout);
    if (control_fd >= 0) {
        char byte;
        /* Host remains alive for inspection until a second control byte. */
        while (read(control_fd, &byte, 1) < 0 && errno == EINTR) {
        }
    }
    /* The runtime is intentionally resident; do not dlclose(library). */
    return status;
}
