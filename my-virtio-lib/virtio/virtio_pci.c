/*
 * VirtIO PCI ECAM Emulator
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "virtio_wrapper.h"
#include "virtio_mmio.h"
#include "virtio_pci.h"
#include "virtio_config.h"
#include "utils.h"

/* ECAM register offset helpers */
#define ECAM_BUS_SHIFT		20
#define ECAM_DEV_SHIFT		15
#define ECAM_FUNC_SHIFT		12
#define ECAM_REG_MASK		0xfff

#define VIRTIO_PCI_CONFIG_SPACE_SIZE		256
#define VIRTIO_PCI_BAR0				0
#define VIRTIO_PCI_BAR0_SIZE			0x4000
#define VIRTIO_PCI_COMMON_CFG_OFFSET		0x0000
#define VIRTIO_PCI_COMMON_CFG_SIZE		0x1000
#define VIRTIO_PCI_ISR_CFG_OFFSET		0x1000
#define VIRTIO_PCI_ISR_CFG_SIZE			0x1000
#define VIRTIO_PCI_DEVICE_CFG_OFFSET		0x2000
#define VIRTIO_PCI_DEVICE_CFG_SIZE		0x1000
#define VIRTIO_PCI_NOTIFY_CFG_OFFSET		0x3000
#define VIRTIO_PCI_NOTIFY_OFF_MULTIPLIER	4

#define VIRTIO_PCI_COMMON_CAP_OFFSET		0x40
#define VIRTIO_PCI_ISR_CAP_OFFSET		0x50
#define VIRTIO_PCI_DEVICE_CAP_OFFSET		0x60
#define VIRTIO_PCI_NOTIFY_CAP_OFFSET		0x70

#define VIRTIO_PCI_QUEUE_MAX			8
#define VIRTIO_PCI_QUEUE_PAGE_SIZE		4096
#define VIRTIO_PCI_QUEUE_ALIGN			4096

struct virtio_pci_queue_state {
	uint16_t size;
	uint16_t msix_vector;
	uint16_t enabled;
	uint64_t desc;
	uint64_t avail;
	uint64_t used;
};

struct virtio_pci_bar_state {
	uint32_t size;
	bool probe;
};

struct virtio_pci_dev {
	struct virtio_mmio_dev mdev;
	struct pci_config_header header;
	uint8_t config_space[VIRTIO_PCI_CONFIG_SPACE_SIZE];
	virtio_pci_ep_handle_t ep_handle;
	virtio_handle_t ecam_handle;
	uint32_t bdf;
	struct virtio_pci_dev_type_info info;
	uint16_t num_queues;
	uint32_t device_feature_select;
	uint32_t guest_feature_select;
	uint64_t guest_features;
	uint16_t msix_config;
	uint8_t device_status;
	uint8_t config_generation;
	uint8_t isr_status;
	uint16_t queue_select;
	struct virtio_pci_queue_state queues[VIRTIO_PCI_QUEUE_MAX];
	struct virtio_pci_bar_state bars[6];
};

/* EP device entry (attached to ECAM) */
struct virtio_pci_ep_device {
	struct list_head list;
	uint32_t bdf;
	struct virtio_pci_ep_ops ops;
	struct virtio_pci_dev *pdev;
};

/* EP device handle structure */
struct virtio_pci_ep {
	struct virtio_pci_ep_ops ops;
};

/* ECAM device instance */
struct virtio_pci_ecam_dev {
	struct list_head list;
	struct list_head ep_device_list;
	uint64_t ecam_base;
	uint64_t ecam_size;
	uint64_t bar_base;
	uint64_t bar_size;
	struct libvirtio_ops *ops;
	void *priv;
};

static LIST_HEAD(ecam_dev_list);

/* Extract BDF from ECAM offset */
static inline uint32_t ecam_offset_to_bdf(uint64_t offset)
{
	uint32_t bus = (offset >> ECAM_BUS_SHIFT) & 0xff;
	uint32_t dev = (offset >> ECAM_DEV_SHIFT) & 0x1f;
	uint32_t func = (offset >> ECAM_FUNC_SHIFT) & 0x07;

	return VIRTIO_PCI_BDF(bus, dev, func);
}

/* Extract register offset from ECAM offset */
static inline uint32_t ecam_offset_to_reg(uint64_t offset)
{
	return offset & ECAM_REG_MASK;
}

static struct virtio_pci_ecam_dev *find_ecam_dev(virtio_handle_t handle)
{
	struct virtio_pci_ecam_dev *ecam;

	list_for_each_entry(ecam, &ecam_dev_list, list) {
		if ((virtio_handle_t)(uintptr_t)ecam == handle)
			return ecam;
	}

	return NULL;
}

static struct virtio_pci_ep_device *find_ep_device(struct virtio_pci_ecam_dev *ecam,
						   uint32_t bdf)
{
	struct virtio_pci_ep_device *ep;

	list_for_each_entry(ep, &ecam->ep_device_list, list) {
		if (ep->bdf == bdf)
			return ep;
	}

	return NULL;
}

static bool is_bdf_occupied(struct virtio_pci_ecam_dev *ecam, uint32_t bdf)
{
	return find_ep_device(ecam, bdf) != NULL;
}

static uint16_t virtio_pci_modern_device_id(struct virtio_pci_dev *pdev)
{
	return PCI_DEVICE_ID_VIRTIO_10_BASE + pdev->mdev.dev.emu->id_table[0].type;
}

static uint32_t virtio_pci_host_features(struct virtio_pci_dev *pdev, uint32_t select)
{
	uint64_t features = 0;

	if (pdev->mdev.dev.emu && pdev->mdev.dev.emu->get_host_features)
		features = pdev->mdev.dev.emu->get_host_features(&pdev->mdev.dev);

	features |= (1ULL << VMM_VIRTIO_F_VERSION_1);

	return select ? (uint32_t)(features >> 32) : (uint32_t)features;
}

static uint16_t virtio_pci_detect_num_queues(struct virtio_pci_dev *pdev)
{
	uint16_t count = 0;
	uint16_t i;

	if (!pdev->mdev.dev.emu || !pdev->mdev.dev.emu->get_size_vq)
		return 0;

	for (i = 0; i < VIRTIO_PCI_QUEUE_MAX; i++) {
		if (pdev->mdev.dev.emu->get_size_vq(&pdev->mdev.dev, i) <= 0)
			break;
		count++;
	}

	return count;
}

static uint32_t virtio_pci_bar_size_mask(uint32_t size)
{
	if (!size)
		return 0;

	return ~(size - 1U) & PCI_BASE_ADDRESS_MEM_MASK;
}

static uint32_t virtio_pci_bar_probe_value(struct virtio_pci_dev *pdev, uint32_t bar)
{
	return virtio_pci_bar_size_mask(pdev->bars[bar].size);
}

static void virtio_pci_init_cap(struct virtio_pci_cap *cap, uint8_t next,
				uint8_t cfg_type, uint8_t bar,
				uint32_t offset, uint32_t length)
{
	memset(cap, 0, sizeof(*cap));
	cap->cap_vndr = PCI_CAP_ID_VENDOR_SPEC;
	cap->cap_next = next;
	cap->cap_len = sizeof(*cap);
	cap->cfg_type = cfg_type;
	cap->bar = bar;
	cap->offset = offset;
	cap->length = length;
}

static void virtio_pci_init_caps(struct virtio_pci_dev *pdev)
{
	struct virtio_pci_cap cap;
	struct virtio_pci_notify_cap notify_cap;
	uint32_t notify_len = pdev->num_queues * VIRTIO_PCI_NOTIFY_OFF_MULTIPLIER;

	if (!notify_len)
		notify_len = VIRTIO_PCI_NOTIFY_OFF_MULTIPLIER;

	memset(pdev->config_space, 0, sizeof(pdev->config_space));

	virtio_pci_init_cap(&cap, VIRTIO_PCI_ISR_CAP_OFFSET,
			    VIRTIO_PCI_CAP_COMMON_CFG, VIRTIO_PCI_BAR0,
			    VIRTIO_PCI_COMMON_CFG_OFFSET,
			    sizeof(struct virtio_pci_common_cfg));
	memcpy(&pdev->config_space[VIRTIO_PCI_COMMON_CAP_OFFSET], &cap, sizeof(cap));

	virtio_pci_init_cap(&cap, VIRTIO_PCI_DEVICE_CAP_OFFSET,
			    VIRTIO_PCI_CAP_ISR_CFG, VIRTIO_PCI_BAR0,
			    VIRTIO_PCI_ISR_CFG_OFFSET, 1);
	memcpy(&pdev->config_space[VIRTIO_PCI_ISR_CAP_OFFSET], &cap, sizeof(cap));

	virtio_pci_init_cap(&cap, VIRTIO_PCI_NOTIFY_CAP_OFFSET,
			    VIRTIO_PCI_CAP_DEVICE_CFG, VIRTIO_PCI_BAR0,
			    VIRTIO_PCI_DEVICE_CFG_OFFSET, pdev->info.device_cfg_len);
	memcpy(&pdev->config_space[VIRTIO_PCI_DEVICE_CAP_OFFSET], &cap, sizeof(cap));

	memset(&notify_cap, 0, sizeof(notify_cap));
	virtio_pci_init_cap(&notify_cap.cap, 0, VIRTIO_PCI_CAP_NOTIFY_CFG,
			    VIRTIO_PCI_BAR0, VIRTIO_PCI_NOTIFY_CFG_OFFSET,
			    notify_len);
	notify_cap.cap.cap_len = sizeof(notify_cap);
	notify_cap.notify_off_multiplier = VIRTIO_PCI_NOTIFY_OFF_MULTIPLIER;
	memcpy(&pdev->config_space[VIRTIO_PCI_NOTIFY_CAP_OFFSET],
	       &notify_cap, sizeof(notify_cap));
}

static void virtio_pci_refresh_queue_sizes(struct virtio_pci_dev *pdev)
{
	uint16_t i;

	for (i = 0; i < pdev->num_queues; i++) {
		if (pdev->mdev.dev.emu && pdev->mdev.dev.emu->get_size_vq)
			pdev->queues[i].size =
				pdev->mdev.dev.emu->get_size_vq(&pdev->mdev.dev, i);
	}
}

static void virtio_pci_init_transport(struct virtio_pci_dev *pdev)
{
	memset(pdev->queues, 0, sizeof(pdev->queues));
	memset(pdev->bars, 0, sizeof(pdev->bars));

	pdev->num_queues = virtio_pci_detect_num_queues(pdev);
	pdev->bars[VIRTIO_PCI_BAR0].size = VIRTIO_PCI_BAR0_SIZE;
	pdev->queue_select = 0;
	pdev->config_generation = 0;
	pdev->isr_status = 0;

	virtio_pci_refresh_queue_sizes(pdev);
	virtio_pci_init_caps(pdev);
}

static void virtio_pci_reset_transport(struct virtio_pci_dev *pdev)
{
	uint16_t i;

	pdev->device_feature_select = 0;
	pdev->guest_feature_select = 0;
	pdev->guest_features = 0;
	pdev->mdev.dev.guest_features = 0;
	pdev->msix_config = 0xffff;
	pdev->device_status = 0;
	pdev->config_generation = 0;
	pdev->isr_status = 0;
	pdev->queue_select = 0;

	for (i = 0; i < VIRTIO_PCI_QUEUE_MAX; i++) {
		pdev->queues[i].msix_vector = 0xffff;
		pdev->queues[i].enabled = 0;
		pdev->queues[i].desc = 0;
		pdev->queues[i].avail = 0;
		pdev->queues[i].used = 0;
	}

	virtio_pci_refresh_queue_sizes(pdev);
}

static int virtio_pci_init_queue(struct virtio_pci_dev *pdev,
				 struct virtio_pci_queue_state *vq,
				 uint16_t queue_sel)
{
	if (!pdev || !vq || !vq->enabled)
		return 0;

	if (!pdev->mdev.dev.emu)
		return 0;

	if (!vq->size || !vq->desc || !vq->avail || !vq->used)
		return -1;

	if (pdev->mdev.dev.emu->init_vq_addr)
		return pdev->mdev.dev.emu->init_vq_addr(&pdev->mdev.dev,
							queue_sel,
							vq->desc,
							vq->avail,
							vq->used,
							vq->size);

	if (pdev->mdev.dev.emu->init_vq) {
		uint64_t avail_expected;
		uint64_t used_expected;
		uint64_t desc_limit;
		uint64_t pfn;

		if (vq->desc & (VIRTIO_PCI_QUEUE_PAGE_SIZE - 1))
			return -1;

		desc_limit = vq->desc +
			     (uint64_t)vq->size * sizeof(struct vring_desc);
		avail_expected = desc_limit;
		used_expected = vq->avail + offsetof(struct vring_avail, ring[vq->size]);
		used_expected = (used_expected + VIRTIO_PCI_QUEUE_ALIGN - 1) &
				~(uint64_t)(VIRTIO_PCI_QUEUE_ALIGN - 1);

		if (vq->avail != avail_expected || vq->used != used_expected)
			return -1;

		pfn = vq->desc / VIRTIO_PCI_QUEUE_PAGE_SIZE;
		if (pfn > UINT32_MAX)
			return -1;

		return pdev->mdev.dev.emu->init_vq(&pdev->mdev.dev, queue_sel,
						   VIRTIO_PCI_QUEUE_PAGE_SIZE,
						   VIRTIO_PCI_QUEUE_ALIGN, pfn);
	}

	return -1;
}

static void virtio_pci_build_common_cfg(struct virtio_pci_dev *pdev,
					struct virtio_pci_common_cfg *cfg)
{
	struct virtio_pci_queue_state *vq = NULL;

	memset(cfg, 0, sizeof(*cfg));
	cfg->device_feature_select = pdev->device_feature_select;
	cfg->device_feature = virtio_pci_host_features(pdev,
						       pdev->device_feature_select);
	cfg->guest_feature_select = pdev->guest_feature_select;
	cfg->guest_feature = pdev->guest_feature_select ?
			     (uint32_t)(pdev->guest_features >> 32) :
			     (uint32_t)pdev->guest_features;
	cfg->msix_config = pdev->msix_config;
	cfg->num_queues = pdev->num_queues;
	cfg->device_status = pdev->device_status;
	cfg->config_generation = pdev->config_generation;
	cfg->queue_select = pdev->queue_select;

	if (pdev->queue_select < VIRTIO_PCI_QUEUE_MAX)
		vq = &pdev->queues[pdev->queue_select];

	if (!vq)
		return;

	cfg->queue_size = vq->size;
	cfg->queue_msix_vector = vq->msix_vector;
	cfg->queue_enable = vq->enabled;
	cfg->queue_notify_off = pdev->queue_select;
	cfg->queue_desc_lo = (uint32_t)vq->desc;
	cfg->queue_desc_hi = (uint32_t)(vq->desc >> 32);
	cfg->queue_avail_lo = (uint32_t)vq->avail;
	cfg->queue_avail_hi = (uint32_t)(vq->avail >> 32);
	cfg->queue_used_lo = (uint32_t)vq->used;
	cfg->queue_used_hi = (uint32_t)(vq->used >> 32);
}

static void virtio_pci_dev_init_header(struct virtio_pci_dev *pdev)
{
	memset(&pdev->header, 0, sizeof(pdev->header));

	pdev->header.vendor_id = PCI_SUBVENDOR_ID_REDHAT;
	pdev->header.device_id = virtio_pci_modern_device_id(pdev);
	pdev->header.revision_id = 1;
	pdev->header.prog_if = pdev->info.prog_if;
	pdev->header.subclass = pdev->info.subclass;
	pdev->header.class_code = pdev->info.class_code;
	pdev->header.status = PCI_STATUS_CAP_LIST;
	pdev->header.header_type = PCI_HEADER_TYPE_NORMAL;
	pdev->header.subsystem_vendor_id = PCI_SUBVENDOR_ID_REDHAT;
	pdev->header.subsystem_id = pdev->mdev.dev.emu->id_table[0].type;
	pdev->header.capabilities_ptr = VIRTIO_PCI_COMMON_CAP_OFFSET;
	pdev->header.interrupt_pin = 1;
}

static int virtio_pci_dev_read_emu_config(struct virtio_pci_dev *pdev,
					  uint32_t offset, void *dst, int len)
{
	if (!pdev->mdev.dev.emu || !pdev->mdev.dev.emu->read_config)
		return 0;

	if (pdev->mdev.dev.emu->read_config(&pdev->mdev.dev, offset, dst, len))
		return -1;

	return 0;
}

static int virtio_pci_dev_write_emu_config(struct virtio_pci_dev *pdev,
					   uint32_t offset, void *src, int len)
{
	if (!pdev->mdev.dev.emu || !pdev->mdev.dev.emu->write_config)
		return 0;

	return pdev->mdev.dev.emu->write_config(&pdev->mdev.dev, offset, src, len);
}

static uint8_t virtio_pci_dev_read_header_byte(struct virtio_pci_dev *pdev,
					       uint32_t offset)
{
	uint32_t bar_base = offsetof(struct pci_config_header, bar[0]);
	uint32_t bar_end = offsetof(struct pci_config_header, bar[6]);
	uint32_t bar;
	uint32_t bar_val;

	if (offset >= sizeof(pdev->header))
		return 0;

	if (offset >= bar_base && offset < bar_end) {
		bar = (offset - bar_base) / sizeof(uint32_t);
		if (pdev->bars[bar].probe)
			bar_val = virtio_pci_bar_probe_value(pdev, bar);
		else
			bar_val = pdev->header.bar[bar];

		return ((uint8_t *)&bar_val)[(offset - bar_base) % sizeof(uint32_t)];
	}

	return ((uint8_t *)&pdev->header)[offset];
}

static void virtio_pci_dev_write_command(struct virtio_pci_dev *pdev,
					 uint32_t offset, void *src, int len)
{
	uint16_t cmd = pdev->header.command;

	memcpy(((uint8_t *)&cmd) +
	       (offset - offsetof(struct pci_config_header, command)),
	       src, len);
	pdev->header.command = cmd;
}

static void virtio_pci_dev_write_bar(struct virtio_pci_dev *pdev, uint32_t bar,
				     uint32_t offset, void *src, int len)
{
	uint32_t val = pdev->header.bar[bar];
	uint32_t off = offset - (offsetof(struct pci_config_header, bar[0]) +
				 bar * sizeof(uint32_t));

	memcpy(((uint8_t *)&val) + off, src, len);

	if (val == UINT_MAX) {
		pdev->bars[bar].probe = true;
		return;
	}

	pdev->bars[bar].probe = false;

	if (!pdev->bars[bar].size) {
		pdev->header.bar[bar] = 0;
		return;
	}

	pdev->header.bar[bar] = val & virtio_pci_bar_size_mask(pdev->bars[bar].size);
}

static int virtio_pci_dev_common_cfg_read(struct virtio_pci_dev *pdev,
					  uint32_t offset, void *dst, int len)
{
	struct virtio_pci_common_cfg cfg;

	if (offset + len > sizeof(cfg))
		return -1;

	virtio_pci_build_common_cfg(pdev, &cfg);
	memcpy(dst, ((uint8_t *)&cfg) + offset, len);

	return 0;
}

static int virtio_pci_dev_common_cfg_write(struct virtio_pci_dev *pdev,
					   uint32_t offset, void *src, int len)
{
	struct virtio_pci_queue_state *vq = NULL;
	uint32_t val32 = 0;
	uint16_t val16 = 0;
	uint8_t val8 = 0;

	if (len == 4)
		memcpy(&val32, src, sizeof(val32));
	else if (len == 2)
		memcpy(&val16, src, sizeof(val16));
	else if (len == 1)
		memcpy(&val8, src, sizeof(val8));
	else
		return -1;

	if (offset == offsetof(struct virtio_pci_common_cfg, device_feature_select) &&
	    len == 4) {
		pdev->device_feature_select = val32;
		return 0;
	}

	if (offset == offsetof(struct virtio_pci_common_cfg, guest_feature_select) &&
	    len == 4) {
		pdev->guest_feature_select = val32;
		return 0;
	}

	if (offset == offsetof(struct virtio_pci_common_cfg, guest_feature) &&
	    len == 4) {
		pdev->guest_features &=
			~((uint64_t)UINT_MAX << (pdev->guest_feature_select * 32));
		pdev->guest_features |=
			((uint64_t)val32 << (pdev->guest_feature_select * 32));
		virtio_device_set_guest_features(&pdev->mdev.dev,
						 pdev->guest_feature_select,
						 val32);
		if (pdev->mdev.dev.emu && pdev->mdev.dev.emu->set_guest_features)
			pdev->mdev.dev.emu->set_guest_features(&pdev->mdev.dev,
						       pdev->guest_feature_select,
						       val32);
		return 0;
	}

	if (offset == offsetof(struct virtio_pci_common_cfg, msix_config) && len == 2) {
		pdev->msix_config = val16;
		return 0;
	}

	if (offset == offsetof(struct virtio_pci_common_cfg, device_status) && len == 1) {
		if (!val8) {
			virtio_pci_reset_transport(pdev);
			if (pdev->mdev.dev.emu && pdev->mdev.dev.emu->reset)
				pdev->mdev.dev.emu->reset(&pdev->mdev.dev);
			return 0;
		}

		if (val8 != pdev->device_status &&
		    pdev->mdev.dev.emu && pdev->mdev.dev.emu->status_changed)
			pdev->mdev.dev.emu->status_changed(&pdev->mdev.dev, val8);

		pdev->device_status = val8;
		return 0;
	}

	if (offset == offsetof(struct virtio_pci_common_cfg, queue_select) && len == 2) {
		pdev->queue_select = val16;
		return 0;
	}

	if (pdev->queue_select < VIRTIO_PCI_QUEUE_MAX)
		vq = &pdev->queues[pdev->queue_select];

	if (!vq)
		return 0;

	if (offset == offsetof(struct virtio_pci_common_cfg, queue_size) && len == 2) {
		vq->size = val16;
		if (pdev->mdev.dev.emu && pdev->mdev.dev.emu->set_size_vq)
			pdev->mdev.dev.emu->set_size_vq(&pdev->mdev.dev,
							pdev->queue_select, val16);
		return 0;
	}

	if (offset == offsetof(struct virtio_pci_common_cfg, queue_msix_vector) &&
	    len == 2) {
		vq->msix_vector = val16;
		return 0;
	}

	if (offset == offsetof(struct virtio_pci_common_cfg, queue_enable) && len == 2) {
		vq->enabled = val16;
		return virtio_pci_init_queue(pdev, vq, pdev->queue_select);
	}

	if (offset == offsetof(struct virtio_pci_common_cfg, queue_desc_lo) && len == 4) {
		vq->desc = (vq->desc & 0xffffffff00000000ULL) | val32;
		return 0;
	}

	if (offset == offsetof(struct virtio_pci_common_cfg, queue_desc_hi) && len == 4) {
		vq->desc = (vq->desc & 0xffffffffULL) | ((uint64_t)val32 << 32);
		return 0;
	}

	if (offset == offsetof(struct virtio_pci_common_cfg, queue_avail_lo) && len == 4) {
		vq->avail = (vq->avail & 0xffffffff00000000ULL) | val32;
		return 0;
	}

	if (offset == offsetof(struct virtio_pci_common_cfg, queue_avail_hi) && len == 4) {
		vq->avail = (vq->avail & 0xffffffffULL) | ((uint64_t)val32 << 32);
		return 0;
	}

	if (offset == offsetof(struct virtio_pci_common_cfg, queue_used_lo) && len == 4) {
		vq->used = (vq->used & 0xffffffff00000000ULL) | val32;
		return 0;
	}

	if (offset == offsetof(struct virtio_pci_common_cfg, queue_used_hi) && len == 4) {
		vq->used = (vq->used & 0xffffffffULL) | ((uint64_t)val32 << 32);
		return 0;
	}

	return 0;
}

static int virtio_pci_dev_bar_read_internal(struct virtio_pci_dev *pdev, uint8_t bar,
					    uint64_t offset, void *dst, int len)
{
	uint8_t isr;

	if (!pdev || !dst || len <= 0 || len > 4 || bar >= 6)
		return -1;

	memset(dst, 0, len);

	if (bar != VIRTIO_PCI_BAR0 || !pdev->bars[bar].size)
		return -1;

	if (offset < VIRTIO_PCI_COMMON_CFG_SIZE)
		return virtio_pci_dev_common_cfg_read(pdev, offset, dst, len);

	if (offset >= VIRTIO_PCI_ISR_CFG_OFFSET &&
	    offset + len <= VIRTIO_PCI_ISR_CFG_OFFSET + VIRTIO_PCI_ISR_CFG_SIZE) {
		isr = pdev->isr_status;
		pdev->isr_status = 0;
		((uint8_t *)dst)[0] = isr;
		return 0;
	}

	if (offset >= VIRTIO_PCI_DEVICE_CFG_OFFSET &&
	    offset + len <= VIRTIO_PCI_DEVICE_CFG_OFFSET + VIRTIO_PCI_DEVICE_CFG_SIZE)
		return virtio_pci_dev_read_emu_config(pdev,
						      offset - VIRTIO_PCI_DEVICE_CFG_OFFSET,
						      dst, len);

	return 0;
}

static int virtio_pci_dev_bar_write_internal(struct virtio_pci_dev *pdev, uint8_t bar,
					     uint64_t offset, void *src, int len,
					     int *is_doorbell)
{
	uint16_t queue;

	if (!pdev || !src || len <= 0 || len > 4 || bar >= 6)
		return -1;

	if (is_doorbell)
		*is_doorbell = 0;

	if (bar != VIRTIO_PCI_BAR0 || !pdev->bars[bar].size)
		return -1;

	if (offset < VIRTIO_PCI_COMMON_CFG_SIZE)
		return virtio_pci_dev_common_cfg_write(pdev, offset, src, len);

	if (offset >= VIRTIO_PCI_DEVICE_CFG_OFFSET &&
	    offset + len <= VIRTIO_PCI_DEVICE_CFG_OFFSET + VIRTIO_PCI_DEVICE_CFG_SIZE)
		return virtio_pci_dev_write_emu_config(pdev,
						       offset - VIRTIO_PCI_DEVICE_CFG_OFFSET,
						       src, len);

	if (offset >= VIRTIO_PCI_NOTIFY_CFG_OFFSET) {
		queue = (offset - VIRTIO_PCI_NOTIFY_CFG_OFFSET) /
			VIRTIO_PCI_NOTIFY_OFF_MULTIPLIER;
		if (queue >= pdev->num_queues)
			return -1;

		if (pdev->mdev.dev.emu && pdev->mdev.dev.emu->notify_vq &&
		    !pdev->mdev.dev.emu->notify_vq(&pdev->mdev.dev, queue) &&
		    is_doorbell)
			*is_doorbell = 1;

		return 0;
	}

	return 0;
}

static int virtio_pci_dev_config_read(uint32_t offset, void *dst, int len, void *priv)
{
	struct virtio_pci_dev *pdev = priv;
	int i;

	if (!pdev || !dst || len <= 0 || len > 4)
		return -1;

	memset(dst, 0, len);

	for (i = 0; i < len; i++) {
		if (offset + i < sizeof(pdev->header)) {
			((uint8_t *)dst)[i] =
				virtio_pci_dev_read_header_byte(pdev, offset + i);
			continue;
		}

		if (offset + i < VIRTIO_PCI_CONFIG_SPACE_SIZE)
			((uint8_t *)dst)[i] = pdev->config_space[offset + i];
	}

	return 0;
}

static int virtio_pci_dev_config_write(uint32_t offset, void *src, int len, void *priv)
{
	struct virtio_pci_dev *pdev = priv;
	uint32_t bar_base = offsetof(struct pci_config_header, bar[0]);
	uint32_t bar_end = offsetof(struct pci_config_header, bar[6]);
	uint32_t bar;

	if (!pdev || !src || len <= 0 || len > 4)
		return -1;

	if (offset >= bar_base && offset + len <= bar_end) {
		bar = (offset - bar_base) / sizeof(uint32_t);
		virtio_pci_dev_write_bar(pdev, bar, offset, src, len);
		return 0;
	}

	if (offset >= offsetof(struct pci_config_header, command) &&
	    offset + len <= offsetof(struct pci_config_header, status))
		virtio_pci_dev_write_command(pdev, offset, src, len);

	if (offset == offsetof(struct pci_config_header, interrupt_line) && len == 1) {
		pdev->header.interrupt_line = *(uint8_t *)src;
		return 0;
	}

	return 0;
}

static void virtio_pci_dev_free(struct virtio_pci_dev *pdev)
{
	if (!pdev || !pdev->mdev.ops || !pdev->mdev.ops->mm_free)
		return;

	pdev->mdev.ops->mm_free((uint64_t)(uintptr_t)pdev, sizeof(*pdev));
}

static int virtio_pci_dev_notify(struct virtio_device *dev, uint32_t vq)
{
	struct virtio_pci_dev *pdev = (struct virtio_pci_dev *)dev->vn_data;

	(void)vq;
	if (pdev)
		pdev->isr_status |= VIRTIO_PCI_ISR_QUEUE;

	return my_set_irq(dev);
}

static struct virtio_notify pci_notify = {
	.name = "virtio_pci",
	.notify = virtio_pci_dev_notify,
};

static struct virtio_pci_ep_device *virtio_pci_find_bar_target(struct virtio_pci_ecam_dev *ecam,
							       uint64_t offset,
							       uint32_t *bar_num,
							       uint64_t *bar_offset)
{
	struct virtio_pci_ep_device *ep;
	struct virtio_pci_dev *pdev;
	uint64_t bar_addr;
	uint64_t start;
	uint32_t i;

	if (!ecam || offset >= ecam->bar_size)
		return NULL;

	list_for_each_entry(ep, &ecam->ep_device_list, list) {
		pdev = ep->pdev;
		if (!pdev)
			continue;

		for (i = 0; i < 6; i++) {
			if (!pdev->bars[i].size || pdev->bars[i].probe)
				continue;

			bar_addr = pdev->header.bar[i] & PCI_BASE_ADDRESS_MEM_MASK;
			if (bar_addr < ecam->bar_base)
				continue;

			start = bar_addr - ecam->bar_base;
			if (start >= ecam->bar_size)
				continue;

			if (offset >= start && offset < start + pdev->bars[i].size) {
				if (bar_num)
					*bar_num = i;
				if (bar_offset)
					*bar_offset = offset - start;
				return ep;
			}
		}
	}

	return NULL;
}

virtio_pci_ep_handle_t virtio_pci_ep_create(struct virtio_pci_ep_ops *ops)
{
	struct virtio_pci_ep *ep;

	if (!ops || !ops->config_read || !ops->config_write)
		return NULL;

	ep = (struct virtio_pci_ep *)(uintptr_t)malloc(sizeof(*ep));
	if (!ep)
		return NULL;

	ep->ops = *ops;
	return (virtio_pci_ep_handle_t)ep;
}

void virtio_pci_ep_destroy(virtio_pci_ep_handle_t ep_handle)
{
	struct virtio_pci_ep *ep = (struct virtio_pci_ep *)ep_handle;

	if (ep)
		free(ep);
}

int virtio_pci_ep_connect(virtio_handle_t ecam_handle, uint32_t bdf,
			  virtio_pci_ep_handle_t ep_handle)
{
	struct virtio_pci_ecam_dev *ecam;
	struct virtio_pci_ep_device *ep_dev;
	struct virtio_pci_ep *ep = (struct virtio_pci_ep *)ep_handle;

	if (!ep || !ep_handle)
		return -1;

	ecam = find_ecam_dev(ecam_handle);
	if (!ecam)
		return -1;

	if (is_bdf_occupied(ecam, bdf))
		return -1;

	ep_dev = (struct virtio_pci_ep_device *)(uintptr_t)ecam->ops->mm_alloc(sizeof(*ep_dev));
	if (!ep_dev)
		return -1;

	memset(ep_dev, 0, sizeof(*ep_dev));
	ep_dev->bdf = bdf;
	ep_dev->ops = ep->ops;
	list_add_tail(&ep_dev->list, &ecam->ep_device_list);

	return 0;
}

int virtio_pci_ep_disconnect(virtio_handle_t ecam_handle, uint32_t bdf)
{
	struct virtio_pci_ecam_dev *ecam;
	struct virtio_pci_ep_device *ep, *n;

	ecam = find_ecam_dev(ecam_handle);
	if (!ecam)
		return -1;

	list_for_each_entry_safe(ep, n, &ecam->ep_device_list, list) {
		if (ep->bdf == bdf) {
			list_del(&ep->list);
			ecam->ops->mm_free((uint64_t)(uintptr_t)ep, sizeof(*ep));
			return 0;
		}
	}

	return -1;
}

int virtio_pci_ecam_access_read(virtio_handle_t handle, uint64_t offset,
				void *dst, int len)
{
	struct virtio_pci_ecam_dev *ecam;
	struct virtio_pci_ep_device *ep;
	uint32_t bdf, reg;

	if (!dst || len <= 0 || len > 4)
		return -1;

	ecam = find_ecam_dev(handle);
	if (!ecam)
		return -1;

	bdf = ecam_offset_to_bdf(offset);
	reg = ecam_offset_to_reg(offset);

	ep = find_ep_device(ecam, bdf);
	if (!ep) {
		memset(dst, 0xff, len);
		return 0;
	}

	return ep->ops.config_read(reg, dst, len, ep->ops.priv);
}

int virtio_pci_ecam_access_write(virtio_handle_t handle, uint64_t offset,
				 void *src, int len)
{
	struct virtio_pci_ecam_dev *ecam;
	struct virtio_pci_ep_device *ep;
	uint32_t bdf, reg;

	if (!src || len <= 0 || len > 4)
		return -1;

	ecam = find_ecam_dev(handle);
	if (!ecam)
		return -1;

	bdf = ecam_offset_to_bdf(offset);
	reg = ecam_offset_to_reg(offset);

	ep = find_ep_device(ecam, bdf);
	if (!ep)
		return -1;

	return ep->ops.config_write(reg, src, len, ep->ops.priv);
}

virtio_handle_t virtio_pci_ecam_dev_create(const char *name, uint64_t ecam_base,
					   uint64_t ecam_size,
					   uint64_t bar_base, uint64_t bar_size,
					   struct libvirtio_ops *ops, void *priv)
{
	struct virtio_pci_ecam_dev *ecam;

	if (!ops || !name)
		return NULL;
	(void)name;

	ecam = (struct virtio_pci_ecam_dev *)(uintptr_t)ops->mm_alloc(sizeof(*ecam));
	if (!ecam)
		return NULL;

	memset(ecam, 0, sizeof(*ecam));
	ecam->ecam_base = ecam_base;
	ecam->ecam_size = ecam_size;
	ecam->bar_base = bar_base;
	ecam->bar_size = bar_size;
	ecam->ops = ops;
	ecam->priv = priv;
	INIT_LIST_HEAD(&ecam->ep_device_list);

	list_add_tail(&ecam->list, &ecam_dev_list);

	return (virtio_handle_t)(uintptr_t)ecam;
}

int virtio_pci_ecam_dev_read(virtio_handle_t handle, uint64_t offset,
			     void *dst, int len)
{
	return virtio_pci_ecam_access_read(handle, offset, dst, len);
}

int virtio_pci_ecam_dev_write(virtio_handle_t handle, uint64_t offset,
			      void *src, int len)
{
	return virtio_pci_ecam_access_write(handle, offset, src, len);
}

int virtio_pci_ecam_dev_bar_read(virtio_handle_t handle, uint64_t offset,
				 void *dst, int len)
{
	struct virtio_pci_ecam_dev *ecam;
	struct virtio_pci_ep_device *ep;
	uint32_t bar_num;
	uint64_t bar_offset;

	ecam = find_ecam_dev(handle);
	if (!ecam || !dst || len <= 0 || len > 4)
		return -1;

	ep = virtio_pci_find_bar_target(ecam, offset, &bar_num, &bar_offset);
	if (!ep || !ep->pdev)
		return -1;

	return virtio_pci_dev_bar_read_internal(ep->pdev, bar_num, bar_offset, dst, len);
}

int virtio_pci_ecam_dev_bar_write(virtio_handle_t handle, uint64_t offset,
				  void *src, int len, int *is_doorbell)
{
	struct virtio_pci_ecam_dev *ecam;
	struct virtio_pci_ep_device *ep;
	uint32_t bar_num;
	uint64_t bar_offset;

	ecam = find_ecam_dev(handle);
	if (!ecam || !src || len <= 0 || len > 4)
		return -1;

	ep = virtio_pci_find_bar_target(ecam, offset, &bar_num, &bar_offset);
	if (!ep || !ep->pdev)
		return -1;

	return virtio_pci_dev_bar_write_internal(ep->pdev, bar_num, bar_offset,
						 src, len, is_doorbell);
}

virtio_handle_t virtio_pci_dev_create(const char *name, virtio_handle_t ecam_handle,
				      uint32_t bdf, struct virtio_emulator *emu,
				      const struct virtio_pci_dev_type_info *info)
{
	struct virtio_pci_ecam_dev *ecam;
	struct virtio_pci_dev *pdev;
	struct virtio_pci_ep_ops ep_ops;
	struct virtio_pci_ep_device *ep_dev;

	ecam = find_ecam_dev(ecam_handle);
	if (!name || !ecam || !ecam->ops || !ecam->ops->mm_alloc || !emu || !info)
		return NULL;

	pdev = (struct virtio_pci_dev *)(uintptr_t)ecam->ops->mm_alloc(sizeof(*pdev));
	if (!pdev)
		return NULL;

	memset(pdev, 0, sizeof(*pdev));
	pdev->ecam_handle = ecam_handle;
	pdev->bdf = bdf;
	pdev->info = *info;
	pdev->mdev.ops = ecam->ops;
	pdev->mdev.priv = ecam->priv;
	pdev->mdev.dev.vn = &pci_notify;
	pdev->mdev.dev.vn_data = pdev;
	INIT_LIST_HEAD(&pdev->mdev.dev.addr_trans_tables);

	strcpy(pdev->mdev.dev.name, name);
	pdev->mdev.dev.emu = emu;

	if (emu->connect && emu->connect(&pdev->mdev.dev, emu)) {
		virtio_pci_dev_free(pdev);
		return NULL;
	}

	virtio_pci_init_transport(pdev);
	virtio_pci_reset_transport(pdev);
	virtio_pci_dev_init_header(pdev);

	ep_ops = (struct virtio_pci_ep_ops) {
		.config_read = virtio_pci_dev_config_read,
		.config_write = virtio_pci_dev_config_write,
		.priv = pdev,
	};

	pdev->ep_handle = virtio_pci_ep_create(&ep_ops);
	if (!pdev->ep_handle) {
		virtio_pci_dev_free(pdev);
		return NULL;
	}

	if (virtio_pci_ep_connect(ecam_handle, bdf, pdev->ep_handle)) {
		virtio_pci_ep_destroy(pdev->ep_handle);
		virtio_pci_dev_free(pdev);
		return NULL;
	}

	ep_dev = find_ep_device(ecam, bdf);
	if (!ep_dev) {
		virtio_pci_ep_disconnect(ecam_handle, bdf);
		virtio_pci_ep_destroy(pdev->ep_handle);
		virtio_pci_dev_free(pdev);
		return NULL;
	}

	ep_dev->pdev = pdev;

	return (virtio_handle_t)pdev;
}
