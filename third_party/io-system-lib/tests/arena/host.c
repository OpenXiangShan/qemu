/* Bare-metal QEMU controller for the unchanged AME instruction sequence. */
#include <stdint.h>

#ifndef TEST_MODE
#define TEST_MODE 0 /* 0=poll, 1=no-poll interval, 2=WFI interval */
#endif
#ifndef TEST_CASE
#define TEST_CASE 0
#endif
#ifndef QUIET_TICKS
#define QUIET_TICKS 30000000ULL /* native CLINT: 1 MHz, no RA MMIO for 30 s */
#endif
#ifndef E6_ILLEGAL
#define E6_ILLEGAL 0
#endif

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG64(a) (*(volatile uint64_t *)(uintptr_t)(a))
#define E6_DONE 0x50000120UL
#define NH_RESULT 0x50000140UL
#define E6_PHASE 0x50000160UL
#define MTIME 0x3800bff8UL
#define MTIMECMP 0x38004000UL
#define UART 0x310b0000UL

extern void e6_start(void);
extern volatile int32_t matrix_o[6];
extern const int32_t expected_o[6];
extern const volatile uint8_t guard_before[64], guard_after[64];

static void fence(void)
{
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
}

static void putc(char c)
{
    while (!(REG32(UART + 0x14) & 0x20)) {
    }
    REG32(UART) = (uint8_t)c;
}

static void puts(const char *s)
{
    while (*s) {
        putc(*s++);
    }
}

static void hex(uint64_t value)
{
    puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        putc("0123456789abcdef"[(value >> i) & 15]);
    }
}

static void stop(void) __attribute__((noreturn));
static void stop(void)
{
    __asm__ volatile ("csrw mie, zero");
    for (;;) {
        __asm__ volatile ("wfi");
    }
}

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason)
{
    REG32(NH_RESULT) = 0xee;
    fence();
    puts("ARENA FAIL reason=");
    puts(reason);
    puts(" phase=");
    hex(REG32(E6_PHASE));
    puts(" done=");
    hex(REG32(E6_DONE));
    putc('\n');
    stop();
}

void nh_trap_report(uint64_t cause, uint64_t pc, uint64_t value)
{
    puts("ARENA FAIL reason=host-trap cause=");
    hex(cause);
    puts(" pc=");
    hex(pc);
    puts(" value=");
    hex(value);
    putc('\n');
    stop();
}

void nh_main(void)
{
    /* QEMU 16550 register shift 2, THR at offset zero. */
    REG32(UART + 4) = 0;
    REG32(UART + 12) = 3;
    puts("ARENA START mode=");
    hex(TEST_MODE);
    puts(" case=");
    hex(TEST_CASE);
    putc('\n');

    REG32(E6_DONE) = 0;
    REG32(NH_RESULT) = 0;
    REG32(E6_PHASE) = 0;
    REG32(0x50000100UL) = (uintptr_t)e6_start;
    REG32(0x50000060UL) = 0;
    REG32(0x50000050UL) = 1;
    fence();
    REG32(0x50000060UL) = 1;
    fence();

#if TEST_MODE != 0
    puts(TEST_MODE == 2 ? "ARENA QUIET WFI BEGIN\n" :
                         "ARENA QUIET NOPOLL BEGIN\n");
    uint64_t quiet_end = REG64(MTIME) + QUIET_TICKS;
#if TEST_MODE == 2
    REG64(MTIMECMP) = quiet_end;
    fence();
    /* MIE stays zero: WFI wakes on enabled MTIP, without taking a trap. */
    __asm__ volatile ("csrs mie, %0" :: "r"(128) : "memory");
    while (REG64(MTIME) < quiet_end) {
        __asm__ volatile ("wfi");
    }
    __asm__ volatile ("csrw mie, zero");
    REG64(MTIMECMP) = UINT64_MAX;
#else
    while (REG64(MTIME) < quiet_end) {
        __asm__ volatile ("nop");
    }
#endif
    puts("ARENA QUIET END\n");
    /* The first RA read after the quiet interval must already see completion. */
    if (!REG32(E6_DONE)) {
        fail("no-progress-without-mmio");
    }
#endif

    uint64_t deadline = REG64(MTIME) + 300000000ULL;
    while (!REG32(E6_DONE)) {
        if (REG64(MTIME) >= deadline) {
            fail("e6-timeout");
        }
    }
    uint32_t done = REG32(E6_DONE);
#if E6_ILLEGAL
    if (done == 0xee && REG32(E6_PHASE) == 0xe002) {
        puts("ARENA EXPECTED-FAIL reason=e6-illegal phase=0xe002\n");
        stop();
    }
    fail("illegal-instruction-not-detected");
#endif
    if (done != 1) {
        fail("e6-trap");
    }

    fence();
    for (unsigned i = 0; i < 6; i++) {
        int32_t actual = matrix_o[i];
        puts("ARENA RESULT index=");
        hex(i);
        puts(" actual=");
        hex((int64_t)actual);
        puts(" expected=");
        hex((int64_t)expected_o[i]);
        putc('\n');
        if (actual != expected_o[i]) {
            fail("result-mismatch");
        }
    }
    for (unsigned i = 0; i < 64; i++) {
        if (guard_before[i] != 0xa5 || guard_after[i] != 0xa5) {
            fail("output-guard-corrupted");
        }
    }

    REG32(NH_RESULT) = 0xff;
    fence();
    deadline = REG64(MTIME) + 30000000ULL;
    while (REG32(E6_PHASE) != 0xff) {
        if (REG64(MTIME) >= deadline) {
            fail("verdict-not-acknowledged");
        }
    }
    puts("ARENA PASS results=6 guards=128 ack=0xff\n");
    stop();
}
