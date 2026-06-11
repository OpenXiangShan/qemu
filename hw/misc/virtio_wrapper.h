#ifndef __VIRTIO_WRAPPER_H__
#define __VIRTIO_WRAPPER_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#define VIRTIO_EMU_NAME_BLK     "virtio_blk"
#define VIRTIO_EMU_NAME_NET     "virtio_net"
#define VIRTIO_EMU_NAME_CONSOLE "virtio_console"

typedef void *virtio_handle_t;

enum {
	MY_BLK_REQ_READ = 0,
	MY_BLK_REQ_WRITE,
	MY_BLK_REQ_FLUSH
};

struct libvirtio_blk_ops {
	int (*submit_blk_io)(uint64_t sector, void *buf, int len, uint8_t flags, void *priv);
	int (*get_blk_capacity)(void *priv);
};

struct libvirtio_net_ops {
	void (*set_mac)(uint8_t *mac, void *priv);
	int  (*ctrl_mq)(int vq_pairs, void *priv);
	int  (*read_tap)(uint64_t offset, void *buf, int len, void *priv);
	int  (*write_tap)(uint64_t offset, void *buf, int len, void *priv);
};

struct libvirtio_console_ops {
	int (*send)(void *buf, int len, void *priv);
};

struct libvirtio_ops {
	int      (*vprint)(const char *fmt, va_list ap) __attribute__((format(printf, 1, 0)));
	uint64_t (*mm_alloc)(int size);
	void     (*mm_free)(uint64_t addr, int size);
	int      (*map)(uint64_t gphys_addr, uint64_t gphys_size,
			uint64_t *hphys_addr, uint64_t *hphys_size);
	int      (*guest_mem_read)(uint64_t gpa, void *dst, uint32_t len);
	int      (*guest_mem_write)(uint64_t gpa, void *src, uint32_t len);

	int      (*set_irq)(void *pirv);

	struct   libvirtio_blk_ops blk_ops;
	struct   libvirtio_net_ops net_ops;
	struct   libvirtio_console_ops console_ops;
};

int virtio_receive(virtio_handle_t handle, void *buf, int len);

void virtio_process_req(virtio_handle_t handle);

void virtio_mmio_show_all(void);
int virtio_mmio_read(virtio_handle_t handle, uint64_t addr,
		     uint32_t *val, int len);
int virtio_mmio_write(virtio_handle_t handle, uint64_t addr, uint32_t val,
		      int len, int *is_doorbell);
virtio_handle_t virtio_mmio_create(const char *name, uint64_t start, int len,
				   struct libvirtio_ops *ops, void *priv);

#endif
