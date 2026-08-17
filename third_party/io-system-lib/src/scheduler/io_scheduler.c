#include "io_scheduler.h"

#include <stdlib.h>
#include <string.h>

typedef struct IoSchedulerEntry {
    IoAxiTransaction txn;
    uint8_t data[IO_AXI_MAX_BEAT_BYTES];
    uint8_t strobe[IO_AXI_MAX_BEAT_BYTES];
    uint8_t *caller_data;
    size_t size;
} IoSchedulerEntry;

struct IoScheduler {
    IoSchedulerConfig config;
    IoSchedulerEntry *entries;
    uint32_t capacity;
    uint32_t head;
    uint32_t count;
};

static size_t io_scheduler_txn_size(const IoAxiTransaction *txn)
{
    if (!txn) {
        return 0;
    }
    return (size_t)txn->beat_count * txn->beat_size;
}

static bool io_scheduler_valid(const IoScheduler *scheduler,
                               const IoAxiTransaction *txn,
                               size_t *size)
{
    size_t total = io_scheduler_txn_size(txn);

    if (!scheduler || !txn || !txn->beat_count || !txn->beat_size ||
        total > IO_AXI_MAX_BEAT_BYTES ||
        total > scheduler->config.max_beat_bytes ||
        (txn->direction != IO_AXI_DIRECTION_IO2Q_READ &&
         txn->direction != IO_AXI_DIRECTION_IO2Q_WRITE)) {
        return false;
    }
    if (txn->direction == IO_AXI_DIRECTION_IO2Q_WRITE && !txn->data) {
        return false;
    }
    if (size) {
        *size = total;
    }
    return true;
}

static IoSystemStatus io_scheduler_execute(IoScheduler *scheduler,
                                           IoSchedulerEntry *entry,
                                           IoAxiTransaction *txn)
{
    IoSystemStatus status = IO_SYSTEM_OK;
    size_t i;

    if (!io_scheduler_valid(scheduler, txn, &entry->size)) {
        txn->response = IO_AXI_RESPONSE_SLVERR;
        return IO_SYSTEM_ERR_INVALID;
    }

    if (txn->direction == IO_AXI_DIRECTION_IO2Q_READ) {
        memset(entry->data, 0xff, entry->size);
        if (!scheduler->config.guest_read ||
            scheduler->config.guest_read(scheduler->config.opaque,
                                         txn->address, entry->data,
                                         (uint32_t)entry->size) !=
                (int)entry->size) {
            status = IO_SYSTEM_ERR_IO;
            txn->response = IO_AXI_RESPONSE_SLVERR;
        } else {
            txn->response = IO_AXI_RESPONSE_OKAY;
        }
        if (entry->caller_data && entry->size) {
            memcpy(entry->caller_data, entry->data, entry->size);
        }
    } else {
        const uint8_t *strobe = txn->byte_strobe;

        if (!scheduler->config.guest_write) {
            status = IO_SYSTEM_ERR_UNSUPPORTED;
        } else if (!strobe) {
            if (scheduler->config.guest_write(scheduler->config.opaque,
                                              txn->address, entry->data,
                                              (uint32_t)entry->size) !=
                (int)entry->size) {
                status = IO_SYSTEM_ERR_IO;
            }
        } else {
            for (i = 0; i < entry->size; ) {
                size_t start;

                while (i < entry->size && !strobe[i]) {
                    i++;
                }
                start = i;
                while (i < entry->size && strobe[i]) {
                    i++;
                }
                if (i != start && scheduler->config.guest_write(
                        scheduler->config.opaque, txn->address + start,
                        entry->data + start, (uint32_t)(i - start)) !=
                    (int)(i - start)) {
                    status = IO_SYSTEM_ERR_IO;
                    break;
                }
            }
        }
        txn->response = status == IO_SYSTEM_OK ? IO_AXI_RESPONSE_OKAY :
                        IO_AXI_RESPONSE_SLVERR;
    }

    txn->data = entry->size ? entry->data : NULL;
    return status;
}

static IoSystemStatus io_scheduler_submit_now(IoScheduler *scheduler,
                                               IoAxiTransaction *txn)
{
    IoSchedulerEntry entry;
    IoSystemStatus status;
    IoAxiTransaction callback_txn;
    uint8_t *caller_data = (uint8_t *)txn->data;

    memset(&entry, 0, sizeof(entry));
    entry.caller_data = caller_data;
    if (io_scheduler_valid(scheduler, txn, &entry.size) &&
        txn->direction == IO_AXI_DIRECTION_IO2Q_WRITE) {
        memcpy(entry.data, txn->data, entry.size);
        if (txn->byte_strobe) {
            memcpy(entry.strobe, txn->byte_strobe, entry.size);
        } else {
            memset(entry.strobe, 0xff, entry.size);
        }
        entry.txn.byte_strobe = entry.strobe;
    }
    entry.txn = *txn;
    if (txn->direction == IO_AXI_DIRECTION_IO2Q_WRITE) {
        entry.txn.data = entry.data;
        entry.txn.byte_strobe = entry.strobe;
    }
    status = io_scheduler_execute(scheduler, &entry, &entry.txn);
    callback_txn = entry.txn;
    if (txn->direction == IO_AXI_DIRECTION_IO2Q_READ) {
        callback_txn.data = caller_data ? caller_data : entry.data;
    }
    *txn = callback_txn;
    if (scheduler->config.completion) {
        scheduler->config.completion(scheduler->config.opaque, &callback_txn);
    }
    return status;
}

IoScheduler *io_scheduler_create(const IoSchedulerConfig *config)
{
    IoScheduler *scheduler;

    if (!config) {
        return NULL;
    }
    scheduler = calloc(1, sizeof(*scheduler));
    if (!scheduler) {
        return NULL;
    }
    scheduler->config = *config;
    if (!scheduler->config.max_beat_bytes ||
        scheduler->config.max_beat_bytes > IO_AXI_MAX_BEAT_BYTES) {
        scheduler->config.max_beat_bytes = IO_AXI_MAX_BEAT_BYTES;
    }
    scheduler->capacity = config->outstanding_depth ?
                          config->outstanding_depth : 1;
    scheduler->entries = calloc(scheduler->capacity,
                                 sizeof(scheduler->entries[0]));
    if (!scheduler->entries) {
        free(scheduler);
        return NULL;
    }
    return scheduler;
}

void io_scheduler_destroy(IoScheduler *scheduler)
{
    if (!scheduler) {
        return;
    }
    free(scheduler->entries);
    free(scheduler);
}

void io_scheduler_reset(IoScheduler *scheduler)
{
    if (scheduler) {
        scheduler->head = 0;
        scheduler->count = 0;
        memset(scheduler->entries, 0,
               scheduler->capacity * sizeof(scheduler->entries[0]));
    }
}

IoSystemStatus io_scheduler_submit_sync(IoScheduler *scheduler,
                                        IoAxiTransaction *txn)
{
    if (!scheduler || !txn) {
        return IO_SYSTEM_ERR_INVALID;
    }
    return io_scheduler_submit_now(scheduler, txn);
}

IoSystemStatus io_scheduler_submit_async(IoScheduler *scheduler,
                                         const IoAxiTransaction *txn)
{
    IoSchedulerEntry *entry;
    size_t size;

    if (!scheduler || !txn || !io_scheduler_valid(scheduler, txn, &size)) {
        return IO_SYSTEM_ERR_INVALID;
    }
    if (!scheduler->config.async_enabled) {
        IoAxiTransaction now = *txn;
        return io_scheduler_submit_now(scheduler, &now);
    }
    if (scheduler->count >= scheduler->capacity) {
        return IO_SYSTEM_ERR_BUSY;
    }

    entry = &scheduler->entries[(scheduler->head + scheduler->count) %
                                scheduler->capacity];
    memset(entry, 0, sizeof(*entry));
    entry->txn = *txn;
    entry->size = size;
    entry->caller_data = (uint8_t *)txn->data;
    if (size && txn->direction == IO_AXI_DIRECTION_IO2Q_WRITE) {
        memcpy(entry->data, txn->data, size);
        if (txn->byte_strobe) {
            memcpy(entry->strobe, txn->byte_strobe, size);
        } else {
            memset(entry->strobe, 0xff, size);
        }
    }
    entry->txn.data = entry->data;
    entry->txn.byte_strobe = entry->strobe;
    scheduler->count++;
    return IO_SYSTEM_OK;
}

IoSystemStatus io_scheduler_service(IoScheduler *scheduler, uint32_t budget,
                                    uint32_t *completed)
{
    uint32_t done = 0;

    if (!scheduler) {
        return IO_SYSTEM_ERR_INVALID;
    }
    while (scheduler->count && (!budget || done < budget)) {
        IoSchedulerEntry *entry = &scheduler->entries[scheduler->head];
        IoSystemStatus status;
        IoAxiTransaction callback_txn;

        status = io_scheduler_execute(scheduler, entry, &entry->txn);
        callback_txn = entry->txn;
        if (callback_txn.direction == IO_AXI_DIRECTION_IO2Q_READ) {
            callback_txn.data = entry->caller_data ? entry->caller_data :
                                entry->data;
        }
        if (scheduler->config.completion) {
            scheduler->config.completion(scheduler->config.opaque,
                                         &callback_txn);
        }
        memset(entry, 0, sizeof(*entry));
        scheduler->head = (scheduler->head + 1) % scheduler->capacity;
        scheduler->count--;
        done++;
        if (status != IO_SYSTEM_OK) {
            if (completed) {
                *completed = done;
            }
            return status;
        }
    }
    if (completed) {
        *completed = done;
    }
    return IO_SYSTEM_OK;
}

IoSystemStatus io_scheduler_flush(IoScheduler *scheduler)
{
    return io_scheduler_service(scheduler, 0, NULL);
}

uint32_t io_scheduler_available_slots(const IoScheduler *scheduler)
{
    return scheduler && scheduler->count < scheduler->capacity ?
           scheduler->capacity - scheduler->count : 0;
}

uint32_t io_scheduler_pending(const IoScheduler *scheduler)
{
    return scheduler ? scheduler->count : 0;
}

bool io_scheduler_needs_service(const IoScheduler *scheduler)
{
    return io_scheduler_pending(scheduler) != 0;
}
