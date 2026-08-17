#ifndef IO_SCHEDULER_H
#define IO_SCHEDULER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "io_axi.h"
#include "io_system.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*IoSchedulerGuestRead)(void *opaque, uint64_t gpa,
                                    void *dst, uint32_t len);
typedef int (*IoSchedulerGuestWrite)(void *opaque, uint64_t gpa,
                                     const void *src, uint32_t len);
typedef void (*IoSchedulerCompletion)(void *opaque,
                                      const IoAxiTransaction *txn);

typedef struct IoSchedulerConfig {
    void *opaque;
    IoSchedulerGuestRead guest_read;
    IoSchedulerGuestWrite guest_write;
    uint32_t max_beat_bytes;
    uint32_t outstanding_depth;
    bool async_enabled;
    IoSchedulerCompletion completion;
} IoSchedulerConfig;

typedef struct IoScheduler IoScheduler;

IoScheduler *io_scheduler_create(const IoSchedulerConfig *config);
void io_scheduler_destroy(IoScheduler *scheduler);
void io_scheduler_reset(IoScheduler *scheduler);

IoSystemStatus io_scheduler_submit_async(IoScheduler *scheduler,
                                         const IoAxiTransaction *txn);
IoSystemStatus io_scheduler_submit_sync(IoScheduler *scheduler,
                                        IoAxiTransaction *txn);
IoSystemStatus io_scheduler_service(IoScheduler *scheduler, uint32_t budget,
                                    uint32_t *completed);
IoSystemStatus io_scheduler_flush(IoScheduler *scheduler);

uint32_t io_scheduler_available_slots(const IoScheduler *scheduler);
uint32_t io_scheduler_pending(const IoScheduler *scheduler);
bool io_scheduler_needs_service(const IoScheduler *scheduler);

#ifdef __cplusplus
}
#endif

#endif
