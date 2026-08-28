#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdint.h>
#include "list.h"
#include "virtio.h"

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t
#define s8 int8_t
#define s16 int16_t
#define s32 int32_t
#define s64 int64_t

#define USHRT_MAX	((u16)(~0U))
#define SHRT_MAX	((s16)(USHRT_MAX>>1))
#define SHRT_MIN	((s16)(-SHRT_MAX - 1))
#define INT_MAX		((int)(~0U>>1))
#define INT_MIN		(-INT_MAX - 1)
#define UINT_MAX	(~0U)
#define LONG_MAX	((long)(~0UL>>1))
#define LONG_MIN	(-LONG_MAX - 1)
#define ULONG_MAX	(~0UL)
#define LLONG_MAX	((long long)(~0ULL>>1))
#define LLONG_MIN	(-LLONG_MAX - 1)
#define ULLONG_MAX	(~0ULL)

extern u32 do_udiv32(u32 dividend, u32 divisor, u32 * remainder);
static inline u32 umod32(u32 value, u32 divisor)
{
	u32 r;
	do_udiv32(value, divisor, &r);
	return r;
}

int memory_region_is_overlay(unsigned long start, unsigned long end,
				    unsigned long new_start,
				    unsigned long new_end);

/* virtio blk ops */
int libvirtio_get_blk_capacity(struct virtio_device *dev);
int libvirtio_submit_blk_io(struct virtio_device *dev,
			    uint64_t sector, void *buf, int len,
			    uint8_t flags);
#define my_get_blk_capacity     libvirtio_get_blk_capacity
#define my_submit_blk_request   libvirtio_submit_blk_io

/* virtio net ops */
int libvirtio_net_read_tap(struct virtio_device *dev, uint64_t offset,
			   void *buf, int len);
int libvirtio_net_write_tap(struct virtio_device *dev, uint64_t offset,
			    void *buf, int len);
int libvirtio_net_ctrl_mq(struct virtio_device *dev, int vq_pairs);
int libvirtio_net_set_mac(struct virtio_device *dev, uint8_t *mac);
#define my_net_read_tap         libvirtio_net_read_tap
#define my_net_write_tap        libvirtio_net_write_tap
#define my_net_ctrl_mq          libvirtio_net_ctrl_mq
#define my_net_set_mac          libvirtio_net_set_mac

/* virtio console ops */
int libvirtio_console_send(struct virtio_device *dev, void *buf, int len);
#define my_console_send         libvirtio_console_send

/* virtio gpu ops */
int libvirtio_gpu_submit_ctrl(struct virtio_device *dev, void *cmd,
			      int cmd_len, void *resp, int resp_cap,
			      int *resp_len);
int libvirtio_gpu_submit_cursor(struct virtio_device *dev, void *cmd,
				int cmd_len, void *resp, int resp_cap,
				int *resp_len);
#define my_gpu_submit_ctrl      libvirtio_gpu_submit_ctrl
#define my_gpu_submit_cursor    libvirtio_gpu_submit_cursor

/* virtio input ops */
int libvirtio_input_status(struct virtio_device *dev, void *event, int len);
#define my_input_status         libvirtio_input_status

/* virtio genirq ops */
int libvirtio_genirq_send_msi(struct virtio_device *dev, uint64_t addr,
				     uint32_t data);
#define my_genirq_send_msi      libvirtio_genirq_send_msi

/* generic ops */
int libvirtio_gphys_read(struct virtio_device *dev, uint64_t gpa, void *dst, uint32_t len);
int libvirtio_gphys_write(struct virtio_device *dev, uint64_t gpa, void *src, uint32_t len);
int libvirtio_gphys_map(struct virtio_device *dev,
			uint64_t gphys_addr, uint64_t gphys_size,
			uint64_t *hphys_addr, uint64_t *hphys_size);
void libvirtio_print(struct virtio_device *dev, const char *fmt, ...);
uint64_t libvirtio_alloc(struct virtio_device *dev, int size);
uint64_t libvirtio_zalloc(struct virtio_device *dev, int size);
void libvirtio_free(struct virtio_device *dev, uint64_t addr, int size);
int libvirtio_set_irq(struct virtio_device *dev);
#define my_guest_physical_read  libvirtio_gphys_read
#define my_guest_physical_write libvirtio_gphys_write
#define my_guest_physical_map   libvirtio_gphys_map
#define my_alloc                libvirtio_alloc
#define my_zalloc               libvirtio_zalloc
#define my_free                 libvirtio_free
#define my_print                libvirtio_print
#define my_set_irq              libvirtio_set_irq

#endif
