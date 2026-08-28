#ifndef __VIRTIO_WRAPPER_H__
#define __VIRTIO_WRAPPER_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#define VIRTIO_EMU_NAME_BLK     "virtio_blk"
#define VIRTIO_EMU_NAME_NET     "virtio_net"
#define VIRTIO_EMU_NAME_CONSOLE "virtio_console"
#define VIRTIO_EMU_NAME_GPU     "virtio_gpu"
#define VIRTIO_EMU_NAME_KEYBOARD "virtio_keyboard"
#define VIRTIO_EMU_NAME_MOUSE   "virtio_mouse"
#define VIRTIO_EMU_NAME_TABLET  "virtio_tablet"
#define VIRTIO_EMU_NAME_GENIRQ  "virtio_genirq"

typedef void *virtio_handle_t;

enum {
	VIRTIO_GBUS_CSR_MAGIC = 0x0000,
	VIRTIO_GBUS_CSR_VERSION = 0x0004,
	VIRTIO_GBUS_CSR_UPDATE_SEQ = 0x0008,
	VIRTIO_GBUS_CSR_STATUS = 0x0010,
	VIRTIO_GBUS_CSR_DRIVER_FEATURES_0 = 0x0014,
	VIRTIO_GBUS_CSR_DRIVER_FEATURES_1 = 0x0018,
	VIRTIO_GBUS_CSR_GUEST_PAGE_SIZE = 0x001c,
	VIRTIO_GBUS_CSR_RESET_SEQ = 0x0020,
	VIRTIO_GBUS_CSR_TRANSPORT_FEATURES_0 = 0x0024,
	VIRTIO_GBUS_CSR_TRANSPORT_FEATURES_1 = 0x0028,
	VIRTIO_GBUS_CSR_BLK_CAPACITY_LOW = 0x0040,
	VIRTIO_GBUS_CSR_BLK_CAPACITY_HIGH = 0x0044,
	VIRTIO_GBUS_CSR_BLK_SEG_MAX = 0x0048,
	VIRTIO_GBUS_CSR_BLK_SIZE = 0x004c,
	VIRTIO_GBUS_CSR_NET_MAC_LOW = 0x0060,
	VIRTIO_GBUS_CSR_NET_MAC_HIGH = 0x0064,
	VIRTIO_GBUS_CSR_NET_STATUS = 0x0068,
	VIRTIO_GBUS_CSR_NET_MAX_QUEUE_PAIRS = 0x006c,
	VIRTIO_GBUS_CSR_QUEUE_BASE = 0x0100,
	VIRTIO_GBUS_CSR_QUEUE_STRIDE = 0x0040,
	VIRTIO_GBUS_CSR_QUEUE_NUM = 0x0000,
	VIRTIO_GBUS_CSR_QUEUE_ALIGN = 0x0004,
	VIRTIO_GBUS_CSR_QUEUE_PFN = 0x0008,
	VIRTIO_GBUS_CSR_QUEUE_READY = 0x000c,
	VIRTIO_GBUS_CSR_QUEUE_NOTIFY_SEQ = 0x0010,
	VIRTIO_GBUS_CSR_QUEUE_DESC_LOW = 0x0014,
	VIRTIO_GBUS_CSR_QUEUE_DESC_HIGH = 0x0018,
	VIRTIO_GBUS_CSR_QUEUE_AVAIL_LOW = 0x001c,
	VIRTIO_GBUS_CSR_QUEUE_AVAIL_HIGH = 0x0020,
	VIRTIO_GBUS_CSR_QUEUE_USED_LOW = 0x0024,
	VIRTIO_GBUS_CSR_QUEUE_USED_HIGH = 0x0028,
	VIRTIO_GBUS_CSR_HOST_IRQ_SET = 0x0200,
};

#define VIRTIO_GBUS_MAGIC	0x73756267U /* "gbus" little-endian */
#define VIRTIO_GBUS_VERSION	2U
#define VIRTIO_GBUS_HOST_IRQ_VRING	(1U << 0)
#define VIRTIO_GBUS_HOST_IRQ_CONFIG	(1U << 1)

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

struct libvirtio_gpu_ops {
	int (*submit_ctrl)(void *cmd, int cmd_len, void *resp, int resp_cap,
			   int *resp_len, void *priv);
	int (*submit_cursor)(void *cmd, int cmd_len, void *resp, int resp_cap,
			     int *resp_len, void *priv);
};

struct libvirtio_input_ops {
	int (*status)(void *event, int len, void *priv);
};

struct libvirtio_genirq_ops {
	int (*send_msi)(uint64_t addr, uint32_t data, void *priv);
};

struct libvirtio_ops {
	int      (*vprint)(const char *fmt, va_list ap) __attribute__((format(printf, 1, 0)));
	uint64_t (*mm_alloc)(int size);
	void     (*mm_free)(uint64_t addr, int size);
	int      (*map)(uint64_t gphys_addr, uint64_t gphys_size,
			uint64_t *hphys_addr, uint64_t *hphys_size, void *priv);
	int      (*guest_mem_read)(uint64_t gpa, void *dst, uint32_t len, void *priv);
	int      (*guest_mem_write)(uint64_t gpa, void *src, uint32_t len, void *priv);

	int      (*set_irq)(void *pirv);

	struct   libvirtio_blk_ops blk_ops;
	struct   libvirtio_net_ops net_ops;
	struct   libvirtio_console_ops console_ops;
	struct   libvirtio_gpu_ops gpu_ops;
	struct   libvirtio_input_ops input_ops;
	struct   libvirtio_genirq_ops genirq_ops;
};

struct virtio_gbus_ops {
	int (*read)(void *opaque, uint32_t addr, uint32_t *val);
	int (*write)(void *opaque, uint32_t addr, uint32_t val);
};

struct virtio_mmio_options {
	bool packed;
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
virtio_handle_t virtio_mmio_create_ex(const char *name, uint64_t start, int len,
				      struct libvirtio_ops *ops, void *priv,
				      const struct virtio_mmio_options *opts);
virtio_handle_t virtio_gbus_create(const char *name, uint64_t start, int len,
				   struct libvirtio_ops *ops, void *priv,
				   const struct virtio_gbus_ops *gbus_ops,
				   void *gbus_opaque);
virtio_handle_t virtio_gbus_create_ex(const char *name, uint64_t start, int len,
				      struct libvirtio_ops *ops, void *priv,
				      const struct virtio_gbus_ops *gbus_ops,
				      void *gbus_opaque,
				      const struct virtio_mmio_options *opts);
int virtio_gbus_poll(virtio_handle_t handle);

/* PCIe ECAM access interface */
#define VIRTIO_PCI_BDF(bus, dev, func)	(((bus) << 8) | ((dev) << 3) | (func))
#define VIRTIO_PCI_BDF_BUS(bdf)		(((bdf) >> 8) & 0xff)
#define VIRTIO_PCI_BDF_DEV(bdf)		(((bdf) >> 3) & 0x1f)
#define VIRTIO_PCI_BDF_FUNC(bdf)	((bdf) & 0x07)

/* ECAM offset encoding: offset = (bus << 20) | (dev << 15) | (func << 12) | reg */
#define VIRTIO_PCI_ECAM_OFFSET(bdf, reg) \
	((VIRTIO_PCI_BDF_BUS(bdf) << 20) | \
	 (VIRTIO_PCI_BDF_DEV(bdf) << 15) | \
	 (VIRTIO_PCI_BDF_FUNC(bdf) << 12) | \
	 ((reg) & 0xfff))

virtio_handle_t virtio_pci_ecam_create(const char *name, uint64_t ecam_base,
				       uint64_t ecam_size,
				       uint64_t bar_base, uint64_t bar_size,
				       struct libvirtio_ops *ops, void *priv);
int virtio_pci_ecam_read(virtio_handle_t handle, uint64_t offset,
			 void *dst, int len);
int virtio_pci_ecam_write(virtio_handle_t handle, uint64_t offset,
			  void *src, int len);
virtio_handle_t virtio_pci_create(const char *name, virtio_handle_t ecam_handle,
				  uint32_t bdf);
int virtio_pci_bar_read(virtio_handle_t ecam_handle,
			uint64_t offset, void *dst, int len);
int virtio_pci_bar_write(virtio_handle_t ecam_handle,
			 uint64_t offset, void *src, int len,
			 int *is_doorbell);

#endif
