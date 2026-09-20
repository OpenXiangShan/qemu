/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/rcu.h"
#include "qemu/thread.h"
#include "system/system.h"
#include "system/runstate.h"
#include "system/replay.h"
#include "qapi/qapi-commands-control.h"
#include "libqemu/libqemu.h"

typedef enum LibState {
    LIB_NEW, LIB_STARTING, LIB_RUNNING, LIB_STOPPING, LIB_STOPPED,
} LibState;

static struct {
    GMutex lock;
    GCond changed;
    LibState state;
    QemuThread worker;
    QEMUBH *shutdown_bh;
    bool waiting;
    int status;
    int argc;
    char **argv;
} runtime;

static int api_error(char **errp, const char *message)
{
    if (errp) {
        *errp = g_strdup(message);
    }
    return -1;
}

static void shutdown_bh(void *opaque)
{
    qmp_quit(NULL);
}

static void *libqemu_worker(void *opaque)
{
    /* Kept resident with the stopped QEMU runtime and its AioContexts. */
    GMainContext *context = g_main_context_new();
    g_main_context_push_thread_default(context);
    qemu_lib_rcu_init();
    qemu_init(runtime.argc, runtime.argv);
    /* qemu_init returns with BQL/replay held by this same worker. */
    runtime.shutdown_bh = qemu_bh_new(shutdown_bh, NULL);
    g_mutex_lock(&runtime.lock);
    runtime.state = LIB_RUNNING;
    g_cond_broadcast(&runtime.changed);
    g_mutex_unlock(&runtime.lock);

    runtime.status = qemu_main_loop();
    g_mutex_lock(&runtime.lock);
    runtime.state = LIB_STOPPING;
    g_mutex_unlock(&runtime.lock);
    qemu_bh_delete(runtime.shutdown_bh);
    qemu_cleanup(runtime.status);
    qemu_run_exit_notifiers();
    bql_unlock();
    replay_mutex_unlock();
    rcu_unregister_thread();
    g_main_context_pop_thread_default(context);

    g_mutex_lock(&runtime.lock);
    runtime.state = LIB_STOPPED;
    g_cond_broadcast(&runtime.changed);
    g_mutex_unlock(&runtime.lock);
    return NULL;
}

static int libqemu_start(int argc, const char *const *argv, char **errp)
{
    int i;

    if (argc < 1 || !argv || !argv[0]) {
        return api_error(errp, "start requires argv[0]");
    }
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!arg) {
            return api_error(errp, "NULL QEMU argument");
        }
        /* These options intentionally have process-wide or split-init effects. */
        if (*arg != '-') {
            continue;
        }
        while (*arg == '-') {
            arg++;
        }
        if (!strcmp(arg, "daemonize") || !strcmp(arg, "runas") ||
            !strcmp(arg, "chroot") || !strcmp(arg, "preconfig") ||
            !strcmp(arg, "run-with")) {
            return api_error(errp, "unsupported process/preconfig option");
        }
    }
    g_mutex_lock(&runtime.lock);
    if (runtime.state != LIB_NEW) {
        g_mutex_unlock(&runtime.lock);
        return api_error(errp, "libqemu can only be initialized once");
    }
    runtime.argc = argc;
    runtime.argv = g_new0(char *, argc + 1);
    for (i = 0; i < argc; i++) {
        runtime.argv[i] = g_strdup(argv[i]);
    }
    runtime.state = LIB_STARTING;
    qemu_thread_create(&runtime.worker, "libqemu-main", libqemu_worker,
                       NULL, QEMU_THREAD_JOINABLE);
    while (runtime.state == LIB_STARTING) {
        g_cond_wait(&runtime.changed, &runtime.lock);
    }
    g_mutex_unlock(&runtime.lock);
    return 0;
}

static int libqemu_request_shutdown(char **errp)
{
    g_mutex_lock(&runtime.lock);
    if (runtime.state != LIB_RUNNING) {
        bool stopped = runtime.state >= LIB_STOPPING;
        g_mutex_unlock(&runtime.lock);
        return stopped ? 0 : api_error(errp, "QEMU has not completed start");
    }
    qemu_bh_schedule(runtime.shutdown_bh);
    g_mutex_unlock(&runtime.lock);
    return 0;
}

static int libqemu_wait(int *status, char **errp)
{
    if (!status) {
        return api_error(errp, "wait requires an exit status pointer");
    }
    g_mutex_lock(&runtime.lock);
    if (runtime.state < LIB_RUNNING || runtime.waiting) {
        g_mutex_unlock(&runtime.lock);
        return api_error(errp, "wait requires completed start and one waiter");
    }
    runtime.waiting = true;
    g_mutex_unlock(&runtime.lock);
    qemu_thread_join(&runtime.worker);
    *status = runtime.status;
    return 0;
}

static void libqemu_error_free(char *error)
{
    g_free(error);
}

static const LibQemuExports exports = {
    .abi_version = LIBQEMU_ABI_VERSION,
    .struct_size = sizeof(LibQemuExports),
    .start = libqemu_start,
    .request_shutdown = libqemu_request_shutdown,
    .wait = libqemu_wait,
    .error_free = libqemu_error_free,
};

const LibQemuExports *libqemu_get_api(uint32_t version, size_t size)
{
    return version == LIBQEMU_ABI_VERSION && size <= sizeof(exports) ?
           &exports : NULL;
}
