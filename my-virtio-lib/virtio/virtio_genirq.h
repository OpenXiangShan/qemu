#ifndef __VIRTIO_GENIRQ_H__
#define __VIRTIO_GENIRQ_H__

#include <stdint.h>
#include "virtio.h"

#define VIRTIO_GENIRQ_VERSION 1
#define VIRTIO_GENIRQ_QUEUE_SIZE 128
#define VIRTIO_GENIRQ_NUM_QUEUES 1
#define VIRTIO_GENIRQ_CMD_QUEUE 0
#define VIRTIO_GENIRQ_MAX_TARGETS 64
#define VIRTIO_GENIRQ_MAX_OPS 512
#define VIRTIO_GENIRQ_MAX_REPEAT 1000000U
#define VIRTIO_GENIRQ_REQ_MAGIC 0x51524947U /* "GIRQ" */

enum virtio_genirq_cmd {
	VIRTIO_GENIRQ_CMD_RUN = 1,
	VIRTIO_GENIRQ_CMD_GET_STATS = 2,
	VIRTIO_GENIRQ_CMD_RESET_STATS = 3,
};

enum virtio_genirq_op_type {
	VIRTIO_GENIRQ_OP_SEND = 1,
	VIRTIO_GENIRQ_OP_SEND_RANGE = 2,
	VIRTIO_GENIRQ_OP_DELAY_NS = 3,
	VIRTIO_GENIRQ_OP_RANDOM = 4,
};

enum virtio_genirq_status {
	VIRTIO_GENIRQ_STATUS_OK = 0,
	VIRTIO_GENIRQ_STATUS_INVALID = 1,
	VIRTIO_GENIRQ_STATUS_UNSUPPORTED = 2,
	VIRTIO_GENIRQ_STATUS_IOERR = 3,
};

#define VIRTIO_GENIRQ_RAW_F_DATA_INC (1U << 0)
#define VIRTIO_GENIRQ_RAW_KICK_SEND 1U

struct virtio_genirq_config {
	uint32_t version;
	uint32_t max_targets;
	uint32_t max_ops;
	uint32_t max_repeat;
	uint64_t total_sends;
	uint64_t total_errors;
	uint32_t raw_status;
	uint32_t raw_flags;
	uint64_t raw_addr;
	uint32_t raw_data;
	uint32_t raw_count;
	uint32_t raw_stride;
	uint32_t raw_kick;
} __attribute__((packed));

struct virtio_genirq_req_hdr {
	uint32_t magic;
	uint16_t version;
	uint16_t opcode;
	uint32_t flags;
	uint32_t target_count;
	uint32_t op_count;
	uint64_t seq;
} __attribute__((packed));

struct virtio_genirq_target {
	uint64_t addr;
	uint32_t data;
	uint32_t flags;
} __attribute__((packed));

struct virtio_genirq_op {
	uint16_t type;
	uint16_t target;
	uint32_t count;
	uint64_t arg0;
	uint64_t arg1;
} __attribute__((packed));

struct virtio_genirq_resp {
	uint32_t status;
	uint32_t error_op;
	uint64_t seq;
	uint64_t sends;
	uint64_t errors;
} __attribute__((packed));

struct virtio_emulator *virtio_genirq_emulator_create(void);

#endif
