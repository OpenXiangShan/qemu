#ifndef __MY_VIRTIO_IOMMU_H__
#define __MY_VIRTIO_IOMMU_H__

#include <stdint.h>

#include "virtio_wrapper.h"

#define VIRTIO_IOMMU_F_INPUT_RANGE       0
#define VIRTIO_IOMMU_F_DOMAIN_RANGE      1
#define VIRTIO_IOMMU_F_MAP_UNMAP         2
#define VIRTIO_IOMMU_F_BYPASS            3
#define VIRTIO_IOMMU_F_PROBE             4
#define VIRTIO_IOMMU_F_MMIO              5
#define VIRTIO_IOMMU_F_BYPASS_CONFIG     6

#define VIRTIO_IOMMU_T_ATTACH             0x01
#define VIRTIO_IOMMU_T_DETACH             0x02
#define VIRTIO_IOMMU_T_MAP                0x03
#define VIRTIO_IOMMU_T_UNMAP              0x04
#define VIRTIO_IOMMU_T_PROBE              0x05

#define VIRTIO_IOMMU_S_OK                 0x00
#define VIRTIO_IOMMU_S_IOERR              0x01
#define VIRTIO_IOMMU_S_UNSUPP             0x02
#define VIRTIO_IOMMU_S_DEVERR             0x03
#define VIRTIO_IOMMU_S_INVAL              0x04
#define VIRTIO_IOMMU_S_RANGE              0x05
#define VIRTIO_IOMMU_S_NOENT              0x06
#define VIRTIO_IOMMU_S_FAULT              0x07
#define VIRTIO_IOMMU_S_NOMEM              0x08

#define VIRTIO_IOMMU_ATTACH_F_BYPASS      (1U << 0)
#define VIRTIO_IOMMU_MAP_F_READ           (1U << 0)
#define VIRTIO_IOMMU_MAP_F_WRITE          (1U << 1)
#define VIRTIO_IOMMU_MAP_F_MMIO           (1U << 2)
#define VIRTIO_IOMMU_MAP_F_MASK           7U

#define VIRTIO_IOMMU_FAULT_R_UNKNOWN      0
#define VIRTIO_IOMMU_FAULT_R_DOMAIN       1
#define VIRTIO_IOMMU_FAULT_R_MAPPING      2
#define VIRTIO_IOMMU_FAULT_F_READ         (1U << 0)
#define VIRTIO_IOMMU_FAULT_F_WRITE        (1U << 1)
#define VIRTIO_IOMMU_FAULT_F_EXEC         (1U << 2)
#define VIRTIO_IOMMU_FAULT_F_ADDRESS      (1U << 8)

#define VIRTIO_IOMMU_REQUEST_VQ           0
#define VIRTIO_IOMMU_EVENT_VQ             1
#define VIRTIO_IOMMU_NUM_VQS              2
#define VIRTIO_IOMMU_QUEUE_SIZE           128
#define VIRTIO_IOMMU_PROBE_SIZE           0

struct virtio_iommu_range_64 {
	uint64_t start;
	uint64_t end;
} __attribute__((packed));

struct virtio_iommu_range_32 {
	uint32_t start;
	uint32_t end;
} __attribute__((packed));

struct virtio_iommu_config {
	uint64_t page_size_mask;
	struct virtio_iommu_range_64 input_range;
	struct virtio_iommu_range_32 domain_range;
	uint32_t probe_size;
	uint8_t bypass;
	uint8_t reserved[3];
} __attribute__((packed));

struct virtio_iommu_req_head {
	uint8_t type;
	uint8_t reserved[3];
} __attribute__((packed));

struct virtio_iommu_req_tail {
	uint8_t status;
	uint8_t reserved[3];
} __attribute__((packed));

struct virtio_iommu_req_attach {
	struct virtio_iommu_req_head head;
	uint32_t domain;
	uint32_t endpoint;
	uint32_t flags;
	uint8_t reserved[4];
	struct virtio_iommu_req_tail tail;
} __attribute__((packed));

struct virtio_iommu_req_detach {
	struct virtio_iommu_req_head head;
	uint32_t domain;
	uint32_t endpoint;
	uint8_t reserved[8];
	struct virtio_iommu_req_tail tail;
} __attribute__((packed));

struct virtio_iommu_req_map {
	struct virtio_iommu_req_head head;
	uint32_t domain;
	uint64_t virt_start;
	uint64_t virt_end;
	uint64_t phys_start;
	uint32_t flags;
	struct virtio_iommu_req_tail tail;
} __attribute__((packed));

struct virtio_iommu_req_unmap {
	struct virtio_iommu_req_head head;
	uint32_t domain;
	uint64_t virt_start;
	uint64_t virt_end;
	uint8_t reserved[4];
	struct virtio_iommu_req_tail tail;
} __attribute__((packed));

struct virtio_iommu_req_probe {
	struct virtio_iommu_req_head head;
	uint32_t endpoint;
	uint8_t reserved[64];
} __attribute__((packed));

struct virtio_iommu_fault {
	uint8_t reason;
	uint8_t reserved[3];
	uint32_t flags;
	uint32_t endpoint;
	uint8_t reserved2[4];
	uint64_t address;
} __attribute__((packed));

struct virtio_emulator *virtio_iommu_emulator_create(void);

int virtio_iommu_translate(virtio_handle_t iommu, uint32_t endpoint,
			   uint64_t addr, uint32_t len, uint32_t access,
			   uint64_t *translated, uint64_t *addr_mask);
int virtio_iommu_report_fault(virtio_handle_t iommu, uint32_t endpoint,
			      uint64_t addr, uint32_t access);

#define VIRTIO_IOMMU_WORK_REQUEST (1U << VIRTIO_IOMMU_REQUEST_VQ)
#define VIRTIO_IOMMU_WORK_EVENT   (1U << VIRTIO_IOMMU_EVENT_VQ)
#define VIRTIO_IOMMU_FAULT_BACKLOG 128U

/* Optional backend scheduling, not a change to the guest wire protocol.
 * A non-NULL notifier opts into deferred mode: translate/report_fault never
 * access a guest VQ. The notifier MUST only schedule work, never drain it
 * inline. Run virtio_process_req() and drain_faults() in the IOMMU's own
 * serialized execution context, which may yield through guest_mem_*().
 * All library calls still belong to one host thread. Reset, destroy and
 * changing this hook require quiesced IOMMU and endpoint callbacks.
 * NULL keeps the legacy synchronous behavior. The hook survives reset.
 */
typedef void (*virtio_iommu_work_notify_t)(void *opaque, uint32_t work);
int virtio_iommu_set_deferred(virtio_handle_t iommu,
			    virtio_iommu_work_notify_t notify, void *opaque);
/* At most budget descriptors per call. No-buffer faults remain queued until
 * an Event VQ kick or new fault. A full backlog drops the newest fault.
 */
int virtio_iommu_drain_faults(virtio_handle_t iommu, unsigned int budget);
struct virtio_iommu_fault_stats {
	uint32_t pending;
	uint64_t dropped;
};
int virtio_iommu_get_fault_stats(virtio_handle_t iommu,
				 struct virtio_iommu_fault_stats *stats);

#endif
