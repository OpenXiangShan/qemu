/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host-side Kunminghu composition using the typed compose modules. */
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libqemu/compose.h"

static const LibQemuExports *api;
static int control_fd = -1;

static void *control(void *opaque)
{
    char byte;
    ssize_t ret;

    (void)opaque;
    do {
        ret = read(control_fd, &byte, 1);
    } while (ret < 0 && errno == EINTR);
    api->request_shutdown(NULL);
    return NULL;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s LIBRARY FW_DYNAMIC Image BLK_IMAGE NVME_IMAGE "
            "[--control-fd FD] [--hold-after-wait] "
            "[--append BOOTARGS] [--dump-dtb FILE] "
            "[--devices none|blk|nvme|blk-nvme] [--profile-builder]\n", program);
}

/* Board assembly stays explicit; each call owns QOM, wiring and DT metadata.
 * kmh_soc_build() provides the same sequence as a convenience function.
 */
static int assemble(LibQemuCompose *c, const KmhProfile *p,
                    LibQemuSoc **out, char **errp)
{
    LibQemuSoc *soc;
    LibQemuCpu *cpu;
    LibQemuAia *aia;
    LibQemuPcie *pcie;

    if (libqemu_soc_new(c, &p->soc, &soc, errp) ||
        libqemu_soc_add_memory(soc, &p->dram, errp) ||
        libqemu_soc_add_memory(soc, &p->rom, errp) ||
        libqemu_soc_add_memory(soc, &p->sram, errp) ||
        libqemu_soc_add_memory(soc, &p->flash, errp) ||
        libqemu_riscv_cpu_create(soc, &p->cpu, &cpu, errp) ||
        libqemu_riscv_aia_create(soc, cpu, &p->aia, &aia, errp) ||
        libqemu_riscv_aclint_create(soc, cpu, &p->aclint, errp) ||
        (p->uart_enabled && libqemu_uart_create(soc, &p->uart, aia, errp)) ||
        (p->blk_enabled && libqemu_my_virtio_blk_create(soc, &p->blk, aia, errp)) ||
        (p->pcie_enabled && libqemu_dwc_create(soc, &p->pcie, aia, &pcie, errp)) ||
        (p->pcie_enabled && p->nvme_enabled &&
         libqemu_nvme_attach(pcie, &p->nvme, errp)) ||
        (p->finisher_enabled && libqemu_finisher_create(soc, &p->finisher, errp))) {
        return -1;
    }
    *out = soc;
    return 0;
}

int main(int argc, char **argv)
{
    const LibQemuExports *(*get_api)(uint32_t, size_t);
    LibQemuCompose *compose = NULL;
    LibQemuSoc *soc = NULL;
    LibQemuDtb *dtb = NULL;
    KmhProfile profile;
    LibQemuComposeBackend backend;
    LibQemuComposeChardev chardev = { "uart0", LIBQEMU_CHAR_STDIO };
    LibQemuComposeLaunch launch;
    void *library;
    pthread_t controller;
    char *error = NULL;
    const char *bootargs = NULL;
    const char *dump_dtb = NULL;
    const char *devices = "blk-nvme";
    bool profile_builder = false;
    bool controller_started = false;
    bool hold_after_wait = false;
    int status;

    if (argc < 6) {
        usage(argv[0]);
        return 2;
    }
    for (int i = 6; i < argc; i++) {
        if (!strcmp(argv[i], "--control-fd") && i + 1 < argc) {
            char *end;
            const char *value = argv[++i];
            errno = 0;
            long fd = strtol(value, &end, 10);
            if (errno || end == value || *end || fd < 3 || fd > INT_MAX ||
                fcntl((int)fd, F_GETFD) == -1) {
                fprintf(stderr, "control-fd must be an open descriptor >= 3\n");
                return 2;
            }
            control_fd = fd;
        } else if (!strcmp(argv[i], "--append") && i + 1 < argc) {
            bootargs = argv[++i];
        } else if (!strcmp(argv[i], "--dump-dtb") && i + 1 < argc) {
            dump_dtb = argv[++i];
        } else if (!strcmp(argv[i], "--hold-after-wait")) {
            hold_after_wait = true;
        } else if (!strcmp(argv[i], "--devices") && i + 1 < argc) {
            devices = argv[++i];
            if (strcmp(devices, "none") && strcmp(devices, "blk") &&
                strcmp(devices, "nvme") && strcmp(devices, "blk-nvme")) {
                usage(argv[0]);
                return 2;
            }
        } else if (!strcmp(argv[i], "--profile-builder")) {
            profile_builder = true;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (hold_after_wait && control_fd < 0) {
        fprintf(stderr, "--hold-after-wait requires --control-fd\n");
        return 2;
    }

    library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "%s\n", dlerror());
        return 2;
    }
    get_api = (const LibQemuExports *(*)(uint32_t, size_t))
        dlsym(library, "libqemu_get_api");
    api = get_api ? get_api(LIBQEMU_ABI_VERSION, sizeof(*api)) : NULL;
    if (!api) {
        fprintf(stderr, "incompatible libqemu ABI\n");
        return 2;
    }

    kmh_profile_init_defaults(&profile);
    profile.blk_enabled = !strcmp(devices, "blk") || !strcmp(devices, "blk-nvme");
    profile.pcie_enabled = profile.nvme_enabled =
        !strcmp(devices, "nvme") || !strcmp(devices, "blk-nvme");
    backend = (LibQemuComposeBackend) {
        .id = "nvmedisk", .path = argv[5], .format = "raw",
    };
    launch = (LibQemuComposeLaunch) {
        .accel = "tcg", .smp = 1, .backends = &backend,
        .backend_count = profile.nvme_enabled ? 1 : 0,
        .chardevs = &chardev, .chardev_count = 1,
    };
    if (libqemu_compose_init(api, &launch, &compose, &error)) {
        fprintf(stderr, "%s\n", error ? error : "compose init failed");
        libqemu_compose_error_free(error);
        return 2;
    }

    profile.blk.image = argv[4];
    profile.nvme.backend_id = backend.id;
    profile.boot.firmware = argv[2];
    profile.boot.kernel = argv[3];
    if (bootargs) {
        profile.boot.bootargs = bootargs;
    }
    int (*builder)(LibQemuCompose *, const KmhProfile *, LibQemuSoc **, char **) =
        profile_builder ? kmh_soc_build : assemble;
    if (builder(compose, &profile, &soc, &error) ||
        libqemu_soc_build_dtb(soc, profile.boot.bootargs, &dtb, &error) ||
        (dump_dtb && libqemu_dtb_write(dtb, dump_dtb, &error)) ||
        libqemu_riscv_boot_load(soc, &profile.boot, dtb, &error)) {
        fprintf(stderr, "%s\n", error ? error : "compose build failed");
        libqemu_compose_error_free(error);
        api->request_shutdown(NULL);
        api->wait(&status, NULL);
        libqemu_dtb_free(dtb);
        libqemu_compose_free(compose);
        return 2;
    }
    libqemu_dtb_free(dtb);
    if (api->finish_qemu_init(&error)) {
        fprintf(stderr, "%s\n", error ? error : "finish init failed");
        api->error_free(error);
        api->request_shutdown(NULL);
        api->wait(&status, NULL);
        libqemu_compose_free(compose);
        return 2;
    }
    printf("HOST_ASSEMBLED Kunminghu: devices=%s builder=%s\n", devices,
           profile_builder ? "profile" : "explicit");
    fflush(stdout);

    if (control_fd >= 0) {
        if (pthread_create(&controller, NULL, control, NULL)) {
            fprintf(stderr, "cannot create control thread\n");
            api->request_shutdown(NULL);
        } else {
            controller_started = true;
        }
    }
    if (api->wait(&status, &error)) {
        fprintf(stderr, "%s\n", error ? error : "wait failed");
        api->error_free(error);
        libqemu_compose_free(compose);
        return 2;
    }
    if (controller_started) {
        pthread_cancel(controller);
        pthread_join(controller, NULL);
    }
    libqemu_compose_free(compose);
    printf("HOST_CONTINUES status=%d\n", status);
    fflush(stdout);
    if (controller_started && hold_after_wait) {
        char byte;
        while (read(control_fd, &byte, 1) < 0 && errno == EINTR) {
            /* Retry until the controller acknowledges or closes the pipe. */
        }
    }
    return status;
}
