#include "io_scheduler.h"

#include <assert.h>
#include <string.h>

typedef struct TestSchedulerMemory {
    uint8_t bytes[256];
    uint8_t last_read[8];
    uint16_t completed[16];
    uint32_t completion_count;
} TestSchedulerMemory;

static int test_read(void *opaque, uint64_t addr, void *dst, uint32_t len)
{
    TestSchedulerMemory *mem = opaque;

    if (addr + len > sizeof(mem->bytes)) {
        return -1;
    }
    memcpy(dst, mem->bytes + addr, len);
    return (int)len;
}

static int test_write(void *opaque, uint64_t addr, const void *src,
                      uint32_t len)
{
    TestSchedulerMemory *mem = opaque;

    if (addr + len > sizeof(mem->bytes)) {
        return -1;
    }
    memcpy(mem->bytes + addr, src, len);
    return (int)len;
}

static void complete(void *opaque, const IoAxiTransaction *txn)
{
    TestSchedulerMemory *mem = opaque;

    assert(mem->completion_count < 16);
    mem->completed[mem->completion_count++] = txn->transaction_id;
    assert(txn->response == IO_AXI_RESPONSE_OKAY);
    if (txn->direction == IO_AXI_DIRECTION_IO2Q_READ) {
        assert(txn->data);
        memcpy(mem->last_read, txn->data, txn->beat_size);
    }
}

int main(void)
{
    TestSchedulerMemory mem = { .bytes = { 0, 1, 2, 3, 4, 5, 6, 7 } };
    IoScheduler *scheduler;
    IoAxiTransaction txn;
    uint8_t data[8] = { 0 };
    uint8_t strobe[8] = { 0xff, 0, 0xff, 0, 0xff, 0, 0xff, 0 };
    uint32_t completed;

    scheduler = io_scheduler_create(&(IoSchedulerConfig) {
        .opaque = &mem,
        .guest_read = test_read,
        .guest_write = test_write,
        .max_beat_bytes = 8,
        .outstanding_depth = 4,
        .async_enabled = true,
        .completion = complete,
    });
    assert(scheduler);

    io_axi_transaction_init(&txn, IO_AXI_DIRECTION_IO2Q_READ, 0, 4);
    txn.transaction_id = 1;
    txn.data = data;
    assert(io_scheduler_submit_async(scheduler, &txn) == IO_SYSTEM_OK);
    assert(io_scheduler_pending(scheduler) == 1);
    assert(io_scheduler_available_slots(scheduler) == 3);
    assert(io_scheduler_service(scheduler, 0, &completed) == IO_SYSTEM_OK);
    assert(completed == 1 && mem.completion_count == 1);
    assert(!memcmp(data, mem.bytes, 4));

    io_axi_transaction_init(&txn, IO_AXI_DIRECTION_IO2Q_READ, 4, 4);
    txn.transaction_id = 2;
    txn.data = NULL;
    assert(io_scheduler_submit_async(scheduler, &txn) == IO_SYSTEM_OK);
    assert(io_scheduler_service(scheduler, 1, &completed) == IO_SYSTEM_OK);
    assert(completed == 1 && mem.completion_count == 2);
    assert(!memcmp(mem.last_read, mem.bytes + 4, 4));

    memset(data, 0xaa, sizeof(data));
    io_axi_transaction_init(&txn, IO_AXI_DIRECTION_IO2Q_WRITE, 16, 8);
    txn.transaction_id = 3;
    txn.data = data;
    txn.byte_strobe = strobe;
    assert(io_scheduler_submit_async(scheduler, &txn) == IO_SYSTEM_OK);
    assert(io_scheduler_service(scheduler, 1, &completed) == IO_SYSTEM_OK);
    assert(completed == 1);
    assert(mem.bytes[16] == 0xaa && mem.bytes[17] == 0);
    assert(mem.bytes[18] == 0xaa && mem.bytes[19] == 0);

    for (uint16_t id = 4; id < 8; id++) {
        io_axi_transaction_init(&txn, IO_AXI_DIRECTION_IO2Q_WRITE,
                                id * 2, 1);
        txn.transaction_id = id;
        txn.data = data;
        assert(io_scheduler_submit_async(scheduler, &txn) == IO_SYSTEM_OK);
    }
    io_axi_transaction_init(&txn, IO_AXI_DIRECTION_IO2Q_WRITE, 32, 1);
    txn.transaction_id = 8;
    txn.data = data;
    assert(io_scheduler_submit_async(scheduler, &txn) == IO_SYSTEM_ERR_BUSY);
    assert(io_scheduler_service(scheduler, 2, &completed) == IO_SYSTEM_OK);
    assert(completed == 2);
    assert(mem.completed[3] == 4 && mem.completed[4] == 5);
    assert(io_scheduler_flush(scheduler) == IO_SYSTEM_OK);
    assert(!io_scheduler_needs_service(scheduler));

    io_scheduler_destroy(scheduler);

    scheduler = io_scheduler_create(&(IoSchedulerConfig) {
        .opaque = &mem,
        .guest_read = test_read,
        .guest_write = test_write,
        .max_beat_bytes = 8,
        .outstanding_depth = 1,
        .async_enabled = false,
        .completion = complete,
    });
    assert(scheduler);
    mem.completion_count = 0;
    io_axi_transaction_init(&txn, IO_AXI_DIRECTION_IO2Q_WRITE, 32, 1);
    txn.transaction_id = 9;
    txn.data = data;
    assert(io_scheduler_submit_async(scheduler, &txn) == IO_SYSTEM_OK);
    assert(io_scheduler_pending(scheduler) == 0);
    assert(mem.completion_count == 1 && mem.completed[0] == 9);
    io_scheduler_destroy(scheduler);

    scheduler = io_scheduler_create(&(IoSchedulerConfig) {
        .opaque = &mem,
        .guest_read = test_read,
        .guest_write = test_write,
        .max_beat_bytes = 8,
        .outstanding_depth = 1,
        .async_enabled = false,
    });
    assert(scheduler);
    io_axi_transaction_init(&txn, IO_AXI_DIRECTION_IO2Q_READ, 254, 4);
    txn.data = data;
    assert(io_scheduler_submit_sync(scheduler, &txn) == IO_SYSTEM_ERR_IO);
    assert(txn.response == IO_AXI_RESPONSE_SLVERR);
    io_scheduler_destroy(scheduler);
    return 0;
}
