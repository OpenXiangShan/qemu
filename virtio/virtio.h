#ifndef __VIRTIO_H__
#define __VIRTIO_H__

#include <stdint.h>
#include <stdbool.h>
#include "virtio_ring.h"
#include "list.h"

struct virtio_emulator;

#define VIRTIO_OK		0
#define VIRTIO_EFAIL		-1
#define VIRTIO_EUNKNOWN		-2
#define VIRTIO_ENOTAVAIL	-3
#define VIRTIO_EALREADY		-4
#define VIRTIO_EINVALID		-5
#define VIRTIO_EOVERFLOW	-6
#define VIRTIO_ENOMEM		-7
#define VIRTIO_ENODEV		-8
#define VIRTIO_EBUSY		-9
#define VIRTIO_EEXIST		-10
#define VIRTIO_ETIMEDOUT	-11
#define VIRTIO_EACCESS		-12
#define VIRTIO_ENOEXEC		-13
#define VIRTIO_ENOENT		-14
#define VIRTIO_ENOSYS		-15
#define VIRTIO_EIO		-16
#define VIRTIO_ETIME		-17
#define VIRTIO_ERANGE		-18
#define VIRTIO_EILSEQ		-19
#define VIRTIO_EOPNOTSUPP	-20
#define VIRTIO_ENOSPC		-21
#define VIRTIO_ENODATA		-22
#define VIRTIO_EFAULT		-23
#define VIRTIO_ENXIO		-24
#define VIRTIO_EPROTONOSUPPORT	-25
#define VIRTIO_EPROBE_DEFER	-26
#define VIRTIO_ESHUTDOWN	-27
#define VIRTIO_EREMOTEIO	-28
#define VIRTIO_EINPROGRESS	-29
#define VIRTIO_EROFS		-30	/* Read-only file system */
#define VIRTIO_EBADMSG		-31	/* Not a data message */
#define VIRTIO_EUCLEAN		-32	/* Structure needs cleaning */
#define VIRTIO_ENOTSUPP		-33
#define VIRTIO_EAGAIN		-34
#define VIRTIO_EPROTO		-35	/* Protocol error */

struct virtio_device;

struct addr_trans_pair {
	struct list_head list;
	uint64_t gphys;
	uint64_t hphys;
	uint64_t size;
};

struct virtio_notify {
	const char *name;
	int (*notify)(struct virtio_device *dev, uint32_t vq);
};

struct virtio_iovec {
	/* Address (guest-physical). */
	uint64_t addr;
	/* Length. */
	uint32_t len;
	/* The flags as indicated above. */
	uint16_t flags;
};

struct virtio_queue {
	/* The last_avail_idx field is an index to ->ring of struct vring_avail.
	   It's where we assume the next request index is at.  */
	uint16_t		last_avail_idx;
	uint16_t		last_used_signalled;

	struct vring		vring;

	uint32_t		desc_count;
	uint32_t		align;
	uint64_t		guest_pfn;
	uint64_t		guest_page_size;
	uint64_t		guest_addr;
	uint64_t		host_addr;
	uint64_t		total_size;

	struct virtio_device *vdev;
};

struct virtio_device_id {
	uint32_t type;
};

struct virtio_device {
	char name[64];
	struct list_head addr_trans_tables;
	struct virtio_emulator *emu;
	void *emu_data;

	struct virtio_notify *vn;
	void *vn_data;
};

struct virtio_emulator {
	const char *name;
	const struct virtio_device_id *id_table;

	/* VirtIO operations */
	uint64_t (*get_host_features) (struct virtio_device *dev);
	void (*set_guest_features) (struct virtio_device *dev,
				    uint32_t select, uint32_t features);
	int (*init_vq) (struct virtio_device *dev, uint32_t vq, uint32_t page_size,
			uint32_t align, uint32_t pfn);
	int (*init_vq_addr) (struct virtio_device *dev, uint32_t vq,
			     uint64_t desc_addr, uint64_t avail_addr,
			     uint64_t used_addr, uint32_t size);
	int (*get_pfn_vq) (struct virtio_device *dev, uint32_t vq);
	int (*get_size_vq) (struct virtio_device *dev, uint32_t vq);
	int (*set_size_vq) (struct virtio_device *dev, uint32_t vq, int size);
	int (*notify_vq) (struct virtio_device *dev, uint32_t vq);
	void (*status_changed) (struct virtio_device *dev,
				uint32_t new_status);

	/* Emulator operations */
	int (*read_config)(struct virtio_device *dev,
			   uint32_t offset, void *dst, uint32_t dst_len);
	int (*write_config)(struct virtio_device *dev,
			    uint32_t offset, void *src, uint32_t src_len);
	int (*reset)(struct virtio_device *dev);
	int  (*connect)(struct virtio_device *dev,
			struct virtio_emulator *emu);
	void (*disconnect)(struct virtio_device *dev);
};

uint64_t virtio_get_gphys_from_hphys(struct virtio_device *dev, uint64_t hphys);
uint64_t virtio_get_hphys_from_gphys(struct virtio_device *dev, uint64_t gphys);

unsigned int virtio_queue_desc_count(struct virtio_queue *vq);
unsigned int virtio_queue_align(struct virtio_queue *vq);
uint64_t virtio_queue_guest_pfn(struct virtio_queue *vq);
uint64_t virtio_queue_guest_page_size(struct virtio_queue *vq);
uint64_t virtio_queue_guest_addr(struct virtio_queue *vq);
uint64_t virtio_queue_host_addr(struct virtio_queue *vq);
uint64_t virtio_queue_total_size(struct virtio_queue *vq);
unsigned int virtio_queue_max_desc(struct virtio_queue *vq);
void virtio_queue_set_avail_event(struct virtio_queue *vq);
void virtio_queue_set_used_elem(struct virtio_queue *vq,
				uint32_t head, uint32_t len);
int virtio_queue_get_desc(struct virtio_queue *vq, unsigned short indx,
			  struct vring_desc *desc);
unsigned short virtio_queue_pop(struct virtio_queue *vq);
bool virtio_queue_available(struct virtio_queue *vq);
bool virtio_queue_should_signal(struct virtio_queue *vq);
int virtio_queue_setup(struct virtio_device *dev, struct virtio_queue *vq,
		       uint64_t guest_pfn, uint64_t guest_page_size,
		       uint32_t desc_count, uint32_t align);
int virtio_queue_setup_split(struct virtio_device *dev, struct virtio_queue *vq,
			     uint64_t desc_addr, uint64_t avail_addr,
			     uint64_t used_addr, uint32_t desc_count);
int virtio_queue_get_head_iovec(struct virtio_queue *vq,
				uint16_t head, struct virtio_iovec *iov,
				uint32_t *ret_iov_cnt, uint32_t *ret_total_len,
				uint16_t *ret_head);
int virtio_queue_get_iovec(struct virtio_queue *vq,
			   struct virtio_iovec *iov,
			   uint32_t *ret_iov_cnt, uint32_t *ret_total_len,
			   uint16_t *ret_head);
uint32_t virtio_iovec_to_buf_read(struct virtio_device *dev,
				  struct virtio_iovec *iov,
				  uint32_t iov_cnt, void *buf,
				  uint32_t buf_len);
uint32_t virtio_buf_to_iovec_write(struct virtio_device *dev,
				   struct virtio_iovec *iov,
				   uint32_t iov_cnt, void *buf,
				   uint32_t buf_len);

#endif
