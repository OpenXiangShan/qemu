/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A complete CPU + RAM + host MMIO + IRQ SoC assembled through the public ABI.
 * Usage: libqemu-assemble LIBRARY [cancel|running]
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libqemu/libqemu.h"

static const LibQemuExports *api;
static const LibQemuCoreExports *core;
static LibQemuIRQ *software_irq, *host_irq;
static LibQemuAddressSpace *guest_as;
static atomic_uint reads, writes, irq_count, timer_count;
static bool ready, reentry_rejected;
static char *error;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s: %s\n", #expr, error ? error : "failed"); \
        exit(2); \
    } \
} while (0)

static int read_register(void *opaque, uint64_t addr, uint64_t *value, unsigned size)
{
    reads++;
    if (addr || size != 4) {
        return -1;
    }
    *value = ready ? 0x1234 : 0;
    return 0;
}

static int write_register(void *opaque, uint64_t addr, uint64_t value, unsigned size)
{
    char *callback_error = NULL;
    if (addr != 4 || size != 4 || value != 0x1234) {
        return -1;
    }
    writes++;
    reentry_rejected = api->pause(&callback_error) == -1;
    api->error_free(callback_error);
    uint8_t one[4] = { 1, 0, 0, 0 };
    CHECK(core->memory_rw(guest_as, 0x21000000, one, sizeof(one), true, &error) == 0);
    CHECK(core->irq_set(host_irq, 1, &error) == 0);
    return 0;
}

static void host_irq_handler(void *opaque, int level)
{
    if (level) {
        irq_count++;
    }
}

static void timer_handler(void *opaque)
{
    timer_count++;
    ready = true;
}

int main(int argc, char **argv)
{
    void *library;
    const LibQemuExports *(*get_api)(uint32_t, size_t);
    bool running = argc > 2 && !strcmp(argv[2], "running");
    const char *qargv[] = { "assembled-qemu", "-M", "none", "-m", "0",
                           "-accel", "tcg,thread=single", "-display", "none",
                           "-serial", "none", "-monitor", "none", "-S" };
    LibQemuObject *machine, *cpu, *finisher, *root_memory, *swi;
    LibQemuMemory *root, *ram, *mmio, *alias;
    LibQemuAddressSpace *as;
    LibQemuTimer *timer;
    int status;
    int64_t now;
    uint64_t resetvec;
    bool realized;
    uint32_t program[] = {
        0x200002b7, 0x0002a303, 0xfe030ee3, 0x000013b7,
        0x2343839b, 0x02731463, 0x0062a223, 0x344023f3,
        0x0083f393, 0x00038c63, 0x001002b7, 0x00005337,
        0x5553031b, 0x0062a023, 0x0000006f, 0x001002b7,
        0x00013337, 0x3333031b, 0x0062a023, 0xff1ff06f,
    };
    uint8_t code[sizeof(program)], readback[sizeof(program)];
    LibQemuMemoryConfig cfg = { .kind = LIBQEMU_RAM, .name = "host-ram",
                                .size = 0x10000 };

    CHECK(argc >= 2);
    library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "%s\n", dlerror());
        return 2;
    }
    get_api = (const LibQemuExports *(*)(uint32_t, size_t))
              dlsym(library, "libqemu_get_api");
    CHECK(get_api);
    api = get_api(LIBQEMU_ABI_VERSION, sizeof(*api));
    CHECK(api && api->core && api->core->struct_size >= sizeof(*core));
    core = api->core;
    CHECK(api->init(sizeof(qargv) / sizeof(qargv[0]) - running, qargv, &error) == 0);
    CHECK(api->get_state() == LIBQEMU_ASSEMBLY);
    if (argc > 2 && !strcmp(argv[2], "cancel")) {
        CHECK(api->request_shutdown(&error) == 0);
        CHECK(api->wait(&status, &error) == 0 && status == 0);
        CHECK(api->get_state() == LIBQEMU_STOPPED);
        puts("ASSEMBLY_CANCEL_PASS");
        return 0;
    }
    machine = core->object_resolve("/machine", &error);
    root = core->system_memory(&error);
    root_memory = core->memory_object(root, &error);
    as = core->system_address_space(&error);
    CHECK(machine && root && root_memory && as);
    CHECK(!core->object_new("no-such-type", &error));
    api->error_free(error); error = NULL;
    /* A second address space backed by caller-owned RAM. */
    static uint8_t host_ram[4096] __attribute__((aligned(4096)));
    LibQemuMemoryConfig external = {
        .kind = LIBQEMU_RAM_PTR, .name = "external-ram", .size = sizeof(host_ram),
        .ram_ptr = host_ram,
    };
    LibQemuMemory *external_mr = core->memory_new(&external, &error);
    LibQemuAddressSpace *external_as =
        core->address_space_new(external_mr, "external-as", &error);
    uint8_t byte = 0x5a;
    CHECK(external_mr && external_as);
    CHECK(core->memory_rw(external_as, 16, &byte, 1, true, &error) == 0);
    CHECK(host_ram[16] == 0x5a);
    ram = core->memory_new(&cfg, &error);
    CHECK(ram && core->memory_add(root, 0x80000000, ram, 0, &error) == 0);
    cfg.kind = LIBQEMU_ALIAS;
    cfg.name = "ram-alias";
    cfg.alias = ram;
    alias = core->memory_new(&cfg, &error);
    CHECK(alias && core->memory_add(root, 0x90000000, alias, 0, &error) == 0);
    for (unsigned i = 0; i < sizeof(program) / sizeof(program[0]); i++) {
        for (unsigned j = 0; j < 4; j++) {
            code[4 * i + j] = program[i] >> (j * 8);
        }
    }
    CHECK(core->memory_rw(as, 0x80000000, code, sizeof(code), true, &error) == 0);
    CHECK(core->memory_rw(as, 0x90000000, readback, sizeof(readback), false, &error) == 0);
    CHECK(!memcmp(code, readback, sizeof(code)));
    CHECK(core->memory_del(root, alias, &error) == 0);

    cpu = core->object_new("rv64-riscv-cpu", &error);
    CHECK(cpu && core->object_add_child(machine, "host-cpu", cpu, &error) == 0);
    CHECK(core->property_set_uint(cpu, "resetvec", 0x80000000, &error) == 0);
    CHECK(core->property_get_uint(cpu, "resetvec", &resetvec, &error) == 0);
    CHECK(resetvec == 0x80000000);
    CHECK(core->property_set_link(cpu, "memory", root_memory, &error) == 0);
    CHECK(core->property_get_link(cpu, "memory", &error) == root_memory);
    CHECK(core->property_get_bool(cpu, "realized", &realized, &error) == 0);
    CHECK(!realized);
    CHECK(core->cpu_register_reset(cpu, &error) == 0);
    CHECK(core->device_realize(cpu, NULL, &error) == 0);
    CHECK(core->property_get_bool(cpu, "realized", &realized, &error) == 0);
    CHECK(realized);
    software_irq = core->gpio_in(cpu, NULL, 3, &error);
    host_irq = core->irq_new(host_irq_handler, NULL, &error);
    CHECK(software_irq && host_irq);
    swi = core->object_new("riscv.aclint.swi", &error);
    CHECK(swi && core->device_realize(swi, NULL, &error) == 0);
    CHECK(core->gpio_connect(swi, NULL, 0, software_irq, &error) == 0);
    CHECK(core->memory_add(root, 0x21000000, core->sysbus_get_mmio(swi, 0, &error),
                          0, &error) == 0);
    CHECK(core->object_unref(swi, &error) == 0);
    guest_as = as;
    finisher = core->object_new("riscv.sifive.test", &error);
    CHECK(finisher && core->device_realize(finisher, NULL, &error) == 0);
    CHECK(core->memory_add(root, 0x100000, core->sysbus_get_mmio(finisher, 0, &error),
                           0, &error) == 0);
    cfg = (LibQemuMemoryConfig) {
        .kind = LIBQEMU_MMIO, .name = "host-registers", .size = 0x1000,
        .read = read_register, .write = write_register,
        .min_access_size = 4, .max_access_size = 4,
    };
    mmio = core->memory_new(&cfg, &error);
    CHECK(mmio && core->memory_add(root, 0x20000000, mmio, 0, &error) == 0);
    timer = core->timer_new(timer_handler, NULL, &error);
    CHECK(timer && core->clock_get_ns(&now, &error) == 0);
    CHECK(core->timer_mod(timer, now, &error) == 0);
    CHECK(core->timer_del(timer, &error) == 0);
    CHECK(core->timer_mod(timer, now + 1000000, &error) == 0);
    CHECK(core->object_unref(cpu, &error) == 0);
    CHECK(core->object_unref(finisher, &error) == 0);
    CHECK(reads == 0 && writes == 0 && timer_count == 0);
    CHECK(api->finish_qemu_init(&error) == 0);
    if (!running) {
        CHECK(reads == 0 && timer_count == 0);
        CHECK(api->finish_qemu_init(&error) == -1);
        api->error_free(error); error = NULL;
        CHECK(!core->object_new("container", &error));
        api->error_free(error); error = NULL;
        CHECK(api->pause(&error) == 0);
        CHECK(api->resume(&error) == 0);
    }
    CHECK(api->wait(&status, &error) == 0 && status == 0);
    CHECK(reads > 0 && writes == 1 && irq_count == 1 && timer_count == 1);
    CHECK(reentry_rejected && api->get_state() == LIBQEMU_STOPPED);
    unsigned completed_reads = reads;
    struct timespec settle = { .tv_nsec = 20000000 };
    nanosleep(&settle, NULL);
    CHECK(reads == completed_reads && writes == 1 && irq_count == 1 &&
          timer_count == 1);
    CHECK(core->memory_rw(as, 0x80000000, code, sizeof(code), false, &error) == -1);
    api->error_free(error);
    printf("ASSEMBLY_PASS reads=%u writes=%u irq=%u timer=%u HOST_CONTINUES\n",
           reads, writes, irq_count, timer_count);
    return 0;
}
