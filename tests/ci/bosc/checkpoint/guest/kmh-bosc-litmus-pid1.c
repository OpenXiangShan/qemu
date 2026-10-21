/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
/*
 * PID 1 used by the 2-die/32-core checkpoint validation.  The marker is
 * intercepted by QEMU after initramfs switch_root has installed this file as
 * the rootfs init.  A restored CPU therefore resumes at the next instruction
 * and starts the rootfs bash/litmus workload.
 */
#include <errno.h>
#include <linux/reboot.h>
#include <sys/reboot.h>
#include <sched.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *const shell_argv[] = {
    "/bin/bash", "-c",
    "set -eu; "
    "online=$(cat /sys/devices/system/cpu/online); "
    "printf 'LITMUS_32CORE_ONLINE=%s\\n' \"$online\"; "
    "test \"$online\" = 0-31; "
    "printf 'LITMUS_32CORE_ISOLATED='; cat /sys/devices/system/cpu/isolated; "
    "grep Cpus_allowed_list /proc/$$/status; "
    "cd /opt/package/litmus/bin/core32; "
    "echo LITMUS_32CORE_BEGIN; "
    "set -x; ./litmus -a 32 -st 1 -s 1k -r 1; set +x; "
    "echo LITMUS_32CORE_PASS",
    NULL,
};

int main(void)
{
    pid_t child;
    int status, result = 1;
    cpu_set_t cpus;

    __asm__ volatile(".word 0x0000006b" ::: "memory");

    dprintf(STDOUT_FILENO, "CHECKPOINT_ROOTFS_RESUMED pid=%d before bash\n",
            getpid());

    /* isolcpus keeps ordinary tasks on die0; the litmus runner deliberately
     * uses all 32 online application harts, so opt this PID1 into that mask. */
    CPU_ZERO(&cpus);
    for (unsigned int cpu = 0; cpu < 32; cpu++) {
        CPU_SET(cpu, &cpus);
    }
    if (sched_setaffinity(0, sizeof(cpus), &cpus) < 0) {
        perror("sched_setaffinity");
        goto shutdown;
    }

    child = fork();
    if (child == 0) {
        extern char **environ;
        execve(shell_argv[0], (char *const *)shell_argv, environ);
        perror("execve bash");
        _exit(127);
    }
    if (child > 0) {
        pid_t waited;

        do {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        if (waited < 0) {
            perror("waitpid");
        } else if (WIFEXITED(status)) {
            result = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            result = 128 + WTERMSIG(status);
            dprintf(STDERR_FILENO, "litmus bash killed by signal %d\n",
                    WTERMSIG(status));
        }
    } else {
        perror("fork");
    }

shutdown:
    dprintf(STDOUT_FILENO, "CHECKPOINT_WORKLOAD_EXIT=%d\n", result);
    sync();
    reboot(LINUX_REBOOT_CMD_POWER_OFF);
    perror("reboot poweroff");
    for (;;) {
        pause();
    }
}
