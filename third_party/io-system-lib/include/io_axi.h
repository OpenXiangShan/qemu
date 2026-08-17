#ifndef IO_AXI_H
#define IO_AXI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IO_AXI_MAX_BEAT_BYTES 64

typedef enum IoAxiDirection {
    IO_AXI_DIRECTION_Q2IO_READ = 0,
    IO_AXI_DIRECTION_Q2IO_WRITE,
    IO_AXI_DIRECTION_IO2Q_READ,
    IO_AXI_DIRECTION_IO2Q_WRITE,
} IoAxiDirection;

typedef enum IoAxiBurstType {
    IO_AXI_BURST_FIXED = 0,
    IO_AXI_BURST_INCR,
    IO_AXI_BURST_WRAP,
} IoAxiBurstType;

typedef enum IoAxiResponse {
    IO_AXI_RESPONSE_OKAY = 0,
    IO_AXI_RESPONSE_EXOKAY,
    IO_AXI_RESPONSE_SLVERR,
    IO_AXI_RESPONSE_DECERR,
} IoAxiResponse;

typedef struct IoAxiTransaction {
    IoAxiDirection direction;
    uint16_t transaction_id;
    uint64_t address;
    uint16_t beat_count;
    uint8_t beat_size;
    IoAxiBurstType burst_type;
    const uint8_t *data;
    const uint8_t *byte_strobe;
    bool last;
    IoAxiResponse response;
    uint32_t attributes;
    uint16_t requester_id;
    uint32_t pasid;
    bool has_pasid;
    bool translated;
} IoAxiTransaction;

typedef struct IoAxiBeatTrace {
    const char *port;
    const char *channel;
    uint16_t transaction_id;
    uint64_t address;
    uint16_t beat_index;
    uint8_t beat_size;
    uint8_t data[IO_AXI_MAX_BEAT_BYTES];
    uint8_t strobe[IO_AXI_MAX_BEAT_BYTES];
    IoAxiResponse response;
} IoAxiBeatTrace;

static inline void io_axi_transaction_init(IoAxiTransaction *txn,
                                           IoAxiDirection direction,
                                           uint64_t address,
                                           uint8_t beat_size)
{
    txn->direction = direction;
    txn->transaction_id = 0;
    txn->address = address;
    txn->beat_count = 1;
    txn->beat_size = beat_size;
    txn->burst_type = IO_AXI_BURST_INCR;
    txn->data = NULL;
    txn->byte_strobe = NULL;
    txn->last = true;
    txn->response = IO_AXI_RESPONSE_OKAY;
    txn->attributes = 0;
    txn->requester_id = 0;
    txn->pasid = 0;
    txn->has_pasid = false;
    txn->translated = false;
}

#ifdef __cplusplus
}
#endif

#endif
