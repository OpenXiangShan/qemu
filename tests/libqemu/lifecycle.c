/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include "libqemu/libqemu.h"
int main(int argc, char **argv)
{
    assert(argc == 2);
    void *d = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    assert(d);
    const LibQemuExports *(*get)(uint32_t, size_t) = dlsym(d, "libqemu_get_api");
    assert(get && !get(99, 0) && !get(1, 100000));
    const LibQemuExports *api = get(1, sizeof(*api));
    const char *args[] = {"api-check", "-M", "none", "-display", "none",
                          "-serial", "none", "-monitor", "none", "-S"};
    char *err = NULL;
    int code;
    assert(api->wait(&code, &err) == -1);
    api->error_free(err); err = NULL;
    assert(!api->start(sizeof(args)/sizeof(args[0]), args, &err));
    assert(api->start(sizeof(args)/sizeof(args[0]), args, &err) == -1);
    api->error_free(err); err = NULL;
    assert(!api->request_shutdown(&err));
    assert(!api->wait(&code, &err) && code == 0);
    assert(!api->request_shutdown(&err));
    assert(api->wait(&code, &err) == -1);
    api->error_free(err);
    puts("LIFECYCLE_PASS");
    return 0;
}
