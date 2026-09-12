/* Test double for QEMU scheduling only: deliberately has no RTL or AME model. */
#define _GNU_SOURCE
#include "io_rtl_system_api.h"
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Shared with test_continuous.py: owner, steps, MMIO calls, close, wrong thread. */
struct io_rtl_system_handle { uint64_t *stats; };

static void check_owner(io_rtl_system_handle_t *h)
{
    if (h->stats[0] != (uint64_t)syscall(SYS_gettid)) {
        h->stats[4]++;
    }
}

io_rtl_system_handle_t *io_rtl_system_open(const io_rtl_system_config_t *config,
                                          const io_rtl_system_callbacks_t *cb)
{
    io_rtl_system_handle_t *h = calloc(1, sizeof(*h));
    int fd = open(getenv("ARENA_CLOCK_PROBE"), O_RDWR);
    assert(fd >= 0 && h);
    h->stats = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    assert(h->stats != MAP_FAILED);
    h->stats[0] = syscall(SYS_gettid);
    return h;
}

void io_rtl_system_close(io_rtl_system_handle_t *h)
{
    check_owner(h);
    h->stats[3]++;
    munmap(h->stats, 4096);
    free(h);
}

int io_rtl_system_reset(io_rtl_system_handle_t *h) { check_owner(h); return 0; }
int io_rtl_system_mmio_read(io_rtl_system_handle_t *h, uint64_t addr,
                           void *data, size_t size)
{
    check_owner(h);
    h->stats[2]++;
    memset(data, 0, size);
    return 0;
}
int io_rtl_system_mmio_write(io_rtl_system_handle_t *h, uint64_t addr,
                            const void *data, size_t size)
{
    check_owner(h);
    h->stats[2]++;
    return 0;
}
int io_rtl_system_gbus_read(io_rtl_system_handle_t *h, uint32_t a, uint32_t *v)
{ *v = 0; return 0; }
int io_rtl_system_gbus_write(io_rtl_system_handle_t *h, uint32_t a, uint32_t v)
{ return 0; }
int io_rtl_system_m_axi_next(io_rtl_system_handle_t *h, IoAxiTransaction *t)
{ return IO_RTL_SYSTEM_ERR_NO_TRANSACTION; }
int io_rtl_system_m_axi_complete(io_rtl_system_handle_t *h, const IoAxiTransaction *t)
{ return 0; }
int io_rtl_system_step(io_rtl_system_handle_t *h, uint64_t cycles)
{ check_owner(h); h->stats[1] += cycles; return 0; }
int io_rtl_system_poll(io_rtl_system_handle_t *h) { return 0; }
int io_rtl_system_needs_service(io_rtl_system_handle_t *h) { return 0; }
const char *io_rtl_system_last_error(io_rtl_system_handle_t *h) { return ""; }
