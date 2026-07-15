/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"

#include "qemu/guest-random.h"
#include "qemu/main-loop.h"
#include "qemu/rcu.h"
#include "accel/accel-cpu-ops.h"
#include "accel/devproxy/devproxy.h"
#include "system/cpus.h"

static void *devproxy_vcpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;
    int r;

    rcu_register_thread();

    bql_lock();
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id = qemu_get_thread_id();
    current_cpu = cpu;

    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    do {
        if (cpu_can_run(cpu)) {
            r = devproxy_cpu_exec(cpu);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(cpu);
            }
        }
        qemu_wait_io_event(cpu);
    } while (!cpu->unplug || cpu_can_run(cpu));

    cpu_thread_signal_destroyed(cpu);
    bql_unlock();
    rcu_unregister_thread();
    return NULL;
}

static void devproxy_start_vcpu_thread(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];

    snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/DEVPROXY",
             cpu->cpu_index);
    qemu_thread_create(cpu->thread, thread_name, devproxy_vcpu_thread_fn,
                       cpu, QEMU_THREAD_JOINABLE);
}

static bool devproxy_vcpu_thread_is_idle(CPUState *cpu)
{
    return true;
}

static bool devproxy_cpus_are_resettable(void)
{
    return true;
}

static void devproxy_accel_ops_class_init(ObjectClass *oc, const void *data)
{
    AccelOpsClass *ops = ACCEL_OPS_CLASS(oc);

    ops->create_vcpu_thread = devproxy_start_vcpu_thread;
    ops->kick_vcpu_thread = devproxy_kick_vcpu;
    ops->cpu_thread_is_idle = devproxy_vcpu_thread_is_idle;
    ops->cpus_are_resettable = devproxy_cpus_are_resettable;
    ops->handle_interrupt = generic_handle_interrupt;
}

static const TypeInfo devproxy_accel_ops_type = {
    .name = ACCEL_OPS_NAME("devproxy"),
    .parent = TYPE_ACCEL_OPS,
    .class_init = devproxy_accel_ops_class_init,
    .abstract = true,
};

static void devproxy_accel_ops_register_types(void)
{
    type_register_static(&devproxy_accel_ops_type);
}

type_init(devproxy_accel_ops_register_types);
