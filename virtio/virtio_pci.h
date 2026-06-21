#ifndef __VIRTIO_PCI_H__
#define __VIRTIO_PCI_H__

#include <stdint.h>
#include "virtio_wrapper.h"
#include "list.h"

struct virtio_emulator;

/* PCI capability IDs */
#define PCI_CAP_ID_VENDOR_SPEC		0x09

/* PCI Configuration Space Header */
struct pci_config_header {
	uint16_t vendor_id;
	uint16_t device_id;
	uint16_t command;
	uint16_t status;
	uint8_t  revision_id;
	uint8_t  prog_if;
	uint8_t  subclass;
	uint8_t  class_code;
	uint8_t  cache_line_size;
	uint8_t  latency_timer;
	uint8_t  header_type;
	uint8_t  bist;
	uint32_t bar[6];
	uint32_t cardbus_cis_ptr;
	uint16_t subsystem_vendor_id;
	uint16_t subsystem_id;
	uint32_t expansion_rom_base;
	uint8_t  capabilities_ptr;
	uint8_t  reserved[3];
	uint32_t expansion_rom_reserved;
	uint8_t  interrupt_line;
	uint8_t  interrupt_pin;
	uint8_t  min_gnt;
	uint8_t  max_lat;
} __attribute__((packed));

/* PCI Command Register Bits */
#define PCI_COMMAND_IO		0x0001
#define PCI_COMMAND_MEMORY	0x0002
#define PCI_COMMAND_MASTER	0x0004

/* PCI Status Register Bits */
#define PCI_STATUS_CAP_LIST	0x0010

/* PCI Header Type */
#define PCI_HEADER_TYPE_NORMAL	0x00

/* VirtIO PCI Subsystem Vendor ID (Red Hat) */
#define PCI_SUBVENDOR_ID_REDHAT	0x1af4

/* VirtIO PCI Device IDs */
#define PCI_DEVICE_ID_VIRTIO_NET	0x1000
#define PCI_DEVICE_ID_VIRTIO_BLOCK	0x1001
#define PCI_DEVICE_ID_VIRTIO_CONSOLE	0x1003
#define PCI_DEVICE_ID_VIRTIO_10_BASE	0x1040

/* PCI BAR helpers */
#define PCI_BASE_ADDRESS_SPACE_MEMORY	0x00000000
#define PCI_BASE_ADDRESS_MEM_MASK	0xfffffff0U

/* VirtIO PCI capability types */
#define VIRTIO_PCI_CAP_COMMON_CFG	1
#define VIRTIO_PCI_CAP_NOTIFY_CFG	2
#define VIRTIO_PCI_CAP_ISR_CFG		3
#define VIRTIO_PCI_CAP_DEVICE_CFG	4
#define VIRTIO_PCI_CAP_PCI_CFG		5

/* VirtIO PCI ISR bits */
#define VIRTIO_PCI_ISR_QUEUE		0x1
#define VIRTIO_PCI_ISR_CONFIG		0x2

struct virtio_pci_cap {
	uint8_t cap_vndr;
	uint8_t cap_next;
	uint8_t cap_len;
	uint8_t cfg_type;
	uint8_t bar;
	uint8_t id;
	uint8_t padding[2];
	uint32_t offset;
	uint32_t length;
} __attribute__((packed));

struct virtio_pci_notify_cap {
	struct virtio_pci_cap cap;
	uint32_t notify_off_multiplier;
} __attribute__((packed));

struct virtio_pci_common_cfg {
	uint32_t device_feature_select;
	uint32_t device_feature;
	uint32_t guest_feature_select;
	uint32_t guest_feature;
	uint16_t msix_config;
	uint16_t num_queues;
	uint8_t device_status;
	uint8_t config_generation;
	uint16_t queue_select;
	uint16_t queue_size;
	uint16_t queue_msix_vector;
	uint16_t queue_enable;
	uint16_t queue_notify_off;
	uint32_t queue_desc_lo;
	uint32_t queue_desc_hi;
	uint32_t queue_avail_lo;
	uint32_t queue_avail_hi;
	uint32_t queue_used_lo;
	uint32_t queue_used_hi;
} __attribute__((packed));

struct virtio_pci_dev_type_info {
	uint8_t class_code;
	uint8_t subclass;
	uint8_t prog_if;
	uint32_t device_cfg_len;
};

/* EP device config access callback */
typedef int (*virtio_pci_config_read_t)(uint32_t offset, void *dst, int len, void *priv);
typedef int (*virtio_pci_config_write_t)(uint32_t offset, void *src, int len, void *priv);

struct virtio_pci_ep_ops {
	virtio_pci_config_read_t  config_read;
	virtio_pci_config_write_t config_write;
	void *priv;
};

/* EP device handle */
typedef void *virtio_pci_ep_handle_t;

/* Create/Destroy EP device */
virtio_pci_ep_handle_t virtio_pci_ep_create(struct virtio_pci_ep_ops *ops);
void virtio_pci_ep_destroy(virtio_pci_ep_handle_t ep_handle);

/* Connect/Disconnect EP device to a specific ECAM port (BDF) */
int virtio_pci_ep_connect(virtio_handle_t ecam_handle, uint32_t bdf,
			  virtio_pci_ep_handle_t ep_handle);
int virtio_pci_ep_disconnect(virtio_handle_t ecam_handle, uint32_t bdf);

/* Internal ECAM lifecycle helpers used by the public wrapper */
virtio_handle_t virtio_pci_ecam_dev_create(const char *name, uint64_t ecam_base,
					   uint64_t ecam_size,
					   uint64_t bar_base, uint64_t bar_size,
					   struct libvirtio_ops *ops, void *priv);
int virtio_pci_ecam_dev_read(virtio_handle_t handle, uint64_t offset,
			     void *dst, int len);
int virtio_pci_ecam_dev_write(virtio_handle_t handle, uint64_t offset,
			      void *src, int len);
int virtio_pci_ecam_dev_bar_read(virtio_handle_t handle, uint64_t offset,
				 void *dst, int len);
int virtio_pci_ecam_dev_bar_write(virtio_handle_t handle, uint64_t offset,
				  void *src, int len, int *is_doorbell);

virtio_handle_t virtio_pci_dev_create(const char *name, virtio_handle_t ecam_handle,
				      uint32_t bdf, struct virtio_emulator *emu,
				      const struct virtio_pci_dev_type_info *info);

/* ECAM access (called by virtio_pci_ecam_read/write) */
int virtio_pci_ecam_access_read(virtio_handle_t handle, uint64_t offset,
				void *dst, int len);
int virtio_pci_ecam_access_write(virtio_handle_t handle, uint64_t offset,
				 void *src, int len);

#endif /* __VIRTIO_PCI_H__ */
