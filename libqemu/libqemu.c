/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/rcu.h"
#include "qemu/thread.h"
#include "system/system.h"
#include "system/runstate.h"
#include "system/replay.h"
#include "qapi/qapi-commands-control.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/error.h"
#include "libqemu/libqemu.h"
#include "libqemu/internal.h"

typedef struct LibCall {
    int (*fn)(void *, char **);
    void *opaque;
    bool assembly;
    bool done;
    int result;
    char *error;
    GCond completed;
} LibCall;

static __thread unsigned callback_depth;

static struct {
    GMutex lock;
    GCond changed;
    LibQemuState state;
    QemuThread worker;
    QEMUBH *shutdown_bh;
    QEMUBH *dispatch_bh;
    GQueue calls;
    bool deferred;
    bool waiting;
    int status;
    int argc;
    char **argv;
} runtime;

bool libqemu_deferred_init(void)
{
    return runtime.deferred;
}

void libqemu_callback_enter(void)
{
    callback_depth++;
}

void libqemu_callback_leave(void)
{
    callback_depth--;
}

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

static bool callable(bool assembly)
{
    return runtime.state == LIBQEMU_ASSEMBLY ||
           (!assembly && runtime.state == LIBQEMU_RUNNING);
}

static void dispatch_bh(void *opaque)
{
    LibCall *call;
    unsigned budget = 64;

    g_mutex_lock(&runtime.lock);
    while (budget-- && (call = g_queue_pop_head(&runtime.calls))) {
        bool allowed = callable(call->assembly);
        g_mutex_unlock(&runtime.lock);
        call->result = allowed ? call->fn(call->opaque, &call->error) :
                       api_error(&call->error, "operation not valid in this phase");
        g_mutex_lock(&runtime.lock);
        call->done = true;
        g_cond_signal(&call->completed);
    }
    /* Let runstate/shutdown and device I/O progress under host API load. */
    if (!g_queue_is_empty(&runtime.calls)) {
        qemu_bh_schedule(runtime.dispatch_bh);
    }
    g_mutex_unlock(&runtime.lock);
}

int libqemu_call(int (*fn)(void *, char **), void *opaque, bool assembly,
                bool callback_safe, char **errp)
{
    LibCall call = { .fn = fn, .opaque = opaque, .assembly = assembly };

    g_mutex_lock(&runtime.lock);
    if (!callable(assembly) &&
        !(callback_depth && callback_safe &&
          runtime.state == LIBQEMU_FINISHING)) {
        g_mutex_unlock(&runtime.lock);
        return api_error(errp, "operation not valid in this phase");
    }
    if (callback_depth || bql_locked()) {
        g_mutex_unlock(&runtime.lock);
        if (!callback_safe) {
            return api_error(errp, "synchronous API cannot run from a QEMU callback");
        }
        assert(bql_locked());
        return fn(opaque, errp);
    }
    g_queue_push_tail(&runtime.calls, &call);
    qemu_bh_schedule(runtime.dispatch_bh);
    while (!call.done) {
        g_cond_wait(&call.completed, &runtime.lock);
    }
    g_mutex_unlock(&runtime.lock);
    g_cond_clear(&call.completed);
    if (errp) {
        *errp = call.error;
    } else {
        g_free(call.error);
    }
    return call.result;
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
    runtime.dispatch_bh = qemu_bh_new(dispatch_bh, NULL);
    g_mutex_lock(&runtime.lock);
    runtime.state = runtime.deferred ? LIBQEMU_ASSEMBLY : LIBQEMU_RUNNING;
    g_cond_broadcast(&runtime.changed);
    g_mutex_unlock(&runtime.lock);

    runtime.status = qemu_main_loop();
    g_mutex_lock(&runtime.lock);
    runtime.state = LIBQEMU_STOPPING;
    while (!g_queue_is_empty(&runtime.calls)) {
        LibCall *call = g_queue_pop_head(&runtime.calls);
        call->result = api_error(&call->error, "QEMU is stopping");
        call->done = true;
        g_cond_signal(&call->completed);
    }
    g_mutex_unlock(&runtime.lock);
    qemu_bh_delete(runtime.shutdown_bh);
    qemu_bh_delete(runtime.dispatch_bh);
    qemu_cleanup(runtime.status);
    libqemu_core_cleanup();
    qemu_run_exit_notifiers();
    bql_unlock();
    replay_mutex_unlock();
    rcu_unregister_thread();
    g_main_context_pop_thread_default(context);

    g_mutex_lock(&runtime.lock);
    runtime.state = LIBQEMU_STOPPED;
    g_cond_broadcast(&runtime.changed);
    g_mutex_unlock(&runtime.lock);
    return NULL;
}

static int libqemu_launch(int argc, const char *const *argv, bool deferred,
                         char **errp)
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
    if (runtime.state != LIBQEMU_NEW) {
        g_mutex_unlock(&runtime.lock);
        return api_error(errp, "libqemu can only be initialized once");
    }
    runtime.argc = argc;
    runtime.argv = g_new0(char *, argc + 1);
    for (i = 0; i < argc; i++) {
        runtime.argv[i] = g_strdup(argv[i]);
    }
    runtime.deferred = deferred;
    runtime.state = LIBQEMU_STARTING;
    qemu_thread_create(&runtime.worker, "libqemu-main", libqemu_worker,
                       NULL, QEMU_THREAD_JOINABLE);
    while (runtime.state == LIBQEMU_STARTING) {
        g_cond_wait(&runtime.changed, &runtime.lock);
    }
    g_mutex_unlock(&runtime.lock);
    return 0;
}

static int libqemu_start(int argc, const char *const *argv, char **errp)
{
    return libqemu_launch(argc, argv, false, errp);
}

static int libqemu_init(int argc, const char *const *argv, char **errp)
{
    return libqemu_launch(argc, argv, true, errp);
}

static int finish_init(void *opaque, char **errp)
{
    Error *err = NULL;

    g_mutex_lock(&runtime.lock);
    runtime.state = LIBQEMU_FINISHING;
    g_mutex_unlock(&runtime.lock);
    qemu_lib_finish_init(&err);
    if (err) {
        int ret = api_error(errp, error_get_pretty(err));
        error_free(err);
        qmp_quit(NULL);
        return ret;
    }
    g_mutex_lock(&runtime.lock);
    runtime.state = LIBQEMU_RUNNING;
    g_mutex_unlock(&runtime.lock);
    return 0;
}

static int libqemu_finish_init(char **errp)
{
    return libqemu_call(finish_init, NULL, true, false, errp);
}

static LibQemuState libqemu_get_state(void)
{
    LibQemuState state;
    g_mutex_lock(&runtime.lock);
    state = runtime.state;
    g_mutex_unlock(&runtime.lock);
    return state;
}

static int control_vm(void *opaque, char **errp)
{
    Error *err = NULL;
    if (libqemu_get_state() != LIBQEMU_RUNNING) {
        return api_error(errp, "VM control requires completed initialization");
    }
    switch ((uintptr_t)opaque) {
    case 0:
        qmp_stop(&err);
        break;
    case 1:
        qmp_cont(&err);
        break;
    case 2:
        qemu_system_reset_request(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
        break;
    }
    if (err) {
        int ret = api_error(errp, error_get_pretty(err));
        error_free(err);
        return ret;
    }
    return 0;
}

static int libqemu_pause(char **errp)
{
    return libqemu_call(control_vm, (void *)0, false, false, errp);
}

static int libqemu_resume(char **errp)
{
    return libqemu_call(control_vm, (void *)1, false, false, errp);
}

static int libqemu_reset(char **errp)
{
    return libqemu_call(control_vm, (void *)2, false, false, errp);
}

static int libqemu_request_shutdown(char **errp)
{
    g_mutex_lock(&runtime.lock);
    if (runtime.state != LIBQEMU_RUNNING && runtime.state != LIBQEMU_ASSEMBLY &&
        runtime.state != LIBQEMU_FINISHING) {
        bool stopped = runtime.state >= LIBQEMU_STOPPING;
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
    if (callback_depth || bql_locked() ||
        runtime.state < LIBQEMU_ASSEMBLY || runtime.waiting) {
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
    .init = libqemu_init,
    .finish_qemu_init = libqemu_finish_init,
    .get_state = libqemu_get_state,
    .pause = libqemu_pause,
    .resume = libqemu_resume,
    .system_reset = libqemu_reset,
    .core = &libqemu_core_exports,
};

const LibQemuExports *libqemu_get_api(uint32_t version, size_t size)
{
    return version == LIBQEMU_ABI_VERSION && size <= sizeof(exports) ?
           &exports : NULL;
}
