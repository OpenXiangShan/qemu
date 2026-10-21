/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Freestanding RV64 firmware: no QEMU headers, handles, or host callbacks. */
#include <stdint.h>
#include "native-abi.h"

static volatile uint64_t arrived;
static volatile uint64_t configured;
static volatile uint64_t ready;
static volatile uint32_t global_ready;
static volatile uint32_t release;

static uint32_t u32(const void *base, unsigned off)
{
    const unsigned char *p = (const unsigned char *)base + off;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static uint64_t u64(const void *base, unsigned off)
{
    return u32(base, off) | (uint64_t)u32(base, off + 4) << 32;
}

static void fence(void)
{
    __asm__ volatile("fence rw,rw" ::: "memory");
}

static void park(void)
{
    for (;;) {
        __asm__ volatile("wfi");
    }
}

static void puts_uart(uint64_t uart, const char *p)
{
    while (*p) {
        while (!(*(volatile uint32_t *)(uart + 5 * 4) & 0x20)) {
        }
        *(volatile uint32_t *)uart = *p++;
    }
}

static void operations(const unsigned char *native, unsigned hart,
                       unsigned phase)
{
    unsigned count = u32(native, 4);
    const unsigned char *p = native + KMH_NATIVE_OPS_OFFSET;

    for (unsigned i = 0; i < count; i++, p += KMH_NATIVE_OP_SIZE) {
        uint64_t address = u64(p, 0), value = u64(p, 8);
        unsigned kind = p[20] | (unsigned)p[21] << 8;
        unsigned op_phase = p[22] | (unsigned)p[23] << 8;

        if (u32(p, 16) != hart || op_phase != phase) {
            continue;
        }
        switch (kind) {
        case KMH_NATIVE_MMIO32:
            *(volatile uint32_t *)address = value;
            break;
        case KMH_NATIVE_MMIO8:
            *(volatile uint8_t *)address = value;
            break;
        case KMH_NATIVE_MIREG:
            __asm__ volatile("csrw 0x350,%0; csrw 0x351,%1"
                             :: "r"(address), "r"(value) : "memory");
            break;
        case KMH_NATIVE_SIREG:
            __asm__ volatile("csrw 0x150,%0; csrw 0x151,%1"
                             :: "r"(address), "r"(value) : "memory");
            break;
        default:
            park();
        }
    }
    fence();
}

extern void kmh_native_return(uint64_t record) __attribute__((noreturn));

void kmh_native_main(unsigned hart)
{
    const unsigned char *dir = (const void *)KMH_NATIVE_DIRECTORY;
    const unsigned char *native = (const void *)u64(dir, 176);
    uint64_t mask = u64(dir, 72), bit = UINT64_C(1) << hart;
    unsigned leader = u32(dir, 68);
    unsigned char *record = (void *)(u64(dir, 96) + ((uint64_t)hart << 20));

    if (u32(dir, 8) != KMH_NATIVE_VERSION ||
        u32(native, 0) != KMH_NATIVE_MAGIC || !(mask & bit) ||
        u64(record, 0xecdb0) != 0xbeef || u64(record, 0xecdc0) > 1) {
        park();
    }
    __atomic_fetch_or(&arrived, bit, __ATOMIC_RELEASE);
    if (hart == leader) {
        while (__atomic_load_n(&arrived, __ATOMIC_ACQUIRE) != mask) {
        }
        /* All selected CPUs have stopped fetching the temporary entry. */
        const unsigned char *backup = (const void *)u64(dir, 144);
        volatile unsigned char *entry = (void *)KMH_NATIVE_ENTRY;
        for (unsigned i = 0; i < KMH_NATIVE_ENTRY_SIZE; i++) {
            entry[i] = backup[i];
        }
        fence();
        operations(native, KMH_NATIVE_GLOBAL, 0);
        __atomic_store_n(&global_ready, 1, __ATOMIC_RELEASE);
    } else {
        while (!__atomic_load_n(&global_ready, __ATOMIC_ACQUIRE)) {
        }
    }
    __asm__ volatile("fence.i" ::: "memory");
    operations(native, hart, 0);
    *(volatile uint64_t *)(0x19004000ULL + hart * 8) = u64(record, 0xecdd0);
    *(volatile uint32_t *)(0x19000000ULL + hart * 4) = u32(record, 0xecdf8);
    fence();
    __atomic_fetch_or(&configured, bit, __ATOMIC_RELEASE);
    if (hart == leader) {
        while (__atomic_load_n(&configured, __ATOMIC_ACQUIRE) != mask) {
        }
        puts_uart(u64(native, 8), "KMH_NATIVE_PLATFORM_RESTORED\r\n");
        operations(native, KMH_NATIVE_GLOBAL, 1);
        /* Restore the shared clock exactly once, after all compare registers. */
        *(volatile uint64_t *)0x1900bff8ULL = u64(record, 0xecdc8);
        fence();
        __atomic_store_n(&release, 1, __ATOMIC_RELEASE);
    } else {
        while (!__atomic_load_n(&release, __ATOMIC_ACQUIRE)) {
        }
    }
    operations(native, hart, 1);
    __atomic_fetch_or(&ready, bit, __ATOMIC_RELEASE);
    while (__atomic_load_n(&ready, __ATOMIC_ACQUIRE) != mask) {
    }
    kmh_native_return((uint64_t)record);
}
