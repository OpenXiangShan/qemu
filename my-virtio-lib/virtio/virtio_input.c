#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "virtio_wrapper.h"
#include "virtio.h"
#include "virtio_mmio.h"
#include "virtio_input.h"
#include "virtio_ids.h"
#include "virtio_ring.h"
#include "virtio_config.h"
#include "utils.h"

#define VIRTIO_INPUT_QUEUE_SIZE 64
#define VIRTIO_INPUT_NUM_QUEUES 2
#define VIRTIO_INPUT_EVENTS_QUEUE 0
#define VIRTIO_INPUT_STATUS_QUEUE 1

#define BUS_VIRTUAL 0x06

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03
#define EV_LED 0x11
#define EV_REP 0x14

#define SYN_REPORT 0

#define KEY_ESC 1
#define KEY_KPDOT 83
#define KEY_ZENKAKUHANKAKU 85
#define KEY_F12 88
#define KEY_KPENTER 96
#define KEY_RIGHTALT 100
#define KEY_HOME 102
#define KEY_DELETE 111
#define KEY_PAUSE 119
#define KEY_LEFTMETA 125
#define KEY_RIGHTMETA 126
#define KEY_MENU 139
#define KEY_F13 183
#define KEY_F24 194

#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
#define BTN_SIDE 0x113
#define BTN_EXTRA 0x114

#define REL_X 0x00
#define REL_Y 0x01
#define REL_WHEEL 0x08

#define ABS_X 0x00
#define ABS_Y 0x01
#define INPUT_ABS_MIN 0x0000
#define INPUT_ABS_MAX 0x7fff

#define INPUT_PROP_POINTER 0x00

#define LED_NUML 0x00
#define LED_CAPSL 0x01
#define LED_SCROLLL 0x02

enum virtio_input_profile {
	VIRTIO_INPUT_PROFILE_KEYBOARD,
	VIRTIO_INPUT_PROFILE_MOUSE,
	VIRTIO_INPUT_PROFILE_TABLET,
};

struct virtio_input_queue {
	struct virtio_queue vq;
	struct virtio_iovec iov[VIRTIO_INPUT_QUEUE_SIZE];
};

struct virtio_input_dev {
	struct virtio_device *vdev;
	struct virtio_input_queue vqs[VIRTIO_INPUT_NUM_QUEUES];
	enum virtio_input_profile profile;
	uint64_t features;
	uint8_t cfg_select;
	uint8_t cfg_subsel;
	uint32_t pending_queues;
};

static uint64_t virtio_input_get_host_features(struct virtio_device *dev)
{
	return (1ULL << VMM_VIRTIO_RING_F_EVENT_IDX) |
	       (1ULL << VMM_VIRTIO_F_VERSION_1);
}

static void virtio_input_set_guest_features(struct virtio_device *dev,
					    uint32_t select, uint32_t features)
{
	struct virtio_input_dev *idev = dev->emu_data;

	if (select > 1)
		return;

	idev->features &= ~((uint64_t)UINT_MAX << (select * 32));
	idev->features |= ((uint64_t)features << (select * 32));
}

static int virtio_input_queue_size(uint32_t vq)
{
	switch (vq) {
	case VIRTIO_INPUT_EVENTS_QUEUE:
	case VIRTIO_INPUT_STATUS_QUEUE:
		return VIRTIO_INPUT_QUEUE_SIZE;
	default:
		return 0;
	}
}

static int virtio_input_init_vq(struct virtio_device *dev, uint32_t vq,
				uint32_t page_size, uint32_t align,
				uint32_t pfn)
{
	struct virtio_input_dev *idev = dev->emu_data;

	if (!virtio_input_queue_size(vq))
		return -1;

	return virtio_queue_setup(dev, &idev->vqs[vq].vq, pfn, page_size,
				  VIRTIO_INPUT_QUEUE_SIZE, align);
}

static int virtio_input_init_vq_addr(struct virtio_device *dev, uint32_t vq,
				     uint64_t desc_addr, uint64_t avail_addr,
				     uint64_t used_addr, uint32_t size)
{
	struct virtio_input_dev *idev = dev->emu_data;

	if (!virtio_input_queue_size(vq))
		return -1;

	return virtio_queue_setup_split(dev, &idev->vqs[vq].vq, desc_addr,
					avail_addr, used_addr, size);
}

static int virtio_input_get_pfn_vq(struct virtio_device *dev, uint32_t vq)
{
	struct virtio_input_dev *idev = dev->emu_data;

	if (!virtio_input_queue_size(vq))
		return -1;

	return virtio_queue_guest_pfn(&idev->vqs[vq].vq);
}

static int virtio_input_get_size_vq(struct virtio_device *dev, uint32_t vq)
{
	return virtio_input_queue_size(vq);
}

static int virtio_input_set_size_vq(struct virtio_device *dev,
				    uint32_t vq, int size)
{
	return size;
}

static void virtio_input_bitmap_set(uint8_t *bitmap, uint32_t bit,
				    uint8_t *size)
{
	uint32_t byte = bit / 8;

	if (byte >= 128)
		return;

	bitmap[byte] |= (uint8_t)(1U << (bit % 8));
	if (*size < byte + 1)
		*size = (uint8_t)(byte + 1);
}

static void virtio_input_bitmap_set_range(uint8_t *bitmap, uint32_t first,
					  uint32_t last, uint8_t *size)
{
	uint32_t bit;

	for (bit = first; bit <= last; bit++)
		virtio_input_bitmap_set(bitmap, bit, size);
}

static void virtio_input_config_string(struct virtio_input_config *config,
				       uint8_t select, const char *str)
{
	size_t len;

	config->select = select;
	len = strlen(str);
	if (len > sizeof(config->u.string))
		len = sizeof(config->u.string);
	memcpy(config->u.string, str, len);
	config->size = (uint8_t)len;
}

static const char *virtio_input_profile_name(enum virtio_input_profile profile)
{
	switch (profile) {
	case VIRTIO_INPUT_PROFILE_KEYBOARD:
		return "my-virtio-keyboard";
	case VIRTIO_INPUT_PROFILE_MOUSE:
		return "my-virtio-mouse";
	case VIRTIO_INPUT_PROFILE_TABLET:
		return "my-virtio-tablet";
	default:
		return "my-virtio-input";
	}
}

static const char *virtio_input_profile_serial(enum virtio_input_profile profile)
{
	switch (profile) {
	case VIRTIO_INPUT_PROFILE_KEYBOARD:
		return "my-virtio-keyboard-0";
	case VIRTIO_INPUT_PROFILE_MOUSE:
		return "my-virtio-mouse-0";
	case VIRTIO_INPUT_PROFILE_TABLET:
		return "my-virtio-tablet-0";
	default:
		return "my-virtio-input-0";
	}
}

static uint16_t virtio_input_profile_product(enum virtio_input_profile profile)
{
	switch (profile) {
	case VIRTIO_INPUT_PROFILE_KEYBOARD:
		return 0x0001;
	case VIRTIO_INPUT_PROFILE_MOUSE:
		return 0x0002;
	case VIRTIO_INPUT_PROFILE_TABLET:
		return 0x0003;
	default:
		return 0x0000;
	}
}

static void virtio_input_build_keyboard_bits(struct virtio_input_config *config)
{
	uint8_t size = 0;

	virtio_input_bitmap_set_range(config->u.bitmap, KEY_ESC, KEY_KPDOT,
				      &size);
	virtio_input_bitmap_set_range(config->u.bitmap, KEY_ZENKAKUHANKAKU,
				      KEY_F12, &size);
	virtio_input_bitmap_set_range(config->u.bitmap, KEY_KPENTER,
				      KEY_RIGHTALT, &size);
	virtio_input_bitmap_set_range(config->u.bitmap, KEY_HOME, KEY_DELETE,
				      &size);
	virtio_input_bitmap_set(config->u.bitmap, KEY_PAUSE, &size);
	virtio_input_bitmap_set(config->u.bitmap, KEY_LEFTMETA, &size);
	virtio_input_bitmap_set(config->u.bitmap, KEY_RIGHTMETA, &size);
	virtio_input_bitmap_set(config->u.bitmap, KEY_MENU, &size);
	virtio_input_bitmap_set_range(config->u.bitmap, KEY_F13, KEY_F24,
				      &size);
	config->size = size;
}

static void virtio_input_build_button_bits(struct virtio_input_config *config)
{
	virtio_input_bitmap_set(config->u.bitmap, BTN_LEFT, &config->size);
	virtio_input_bitmap_set(config->u.bitmap, BTN_RIGHT, &config->size);
	virtio_input_bitmap_set(config->u.bitmap, BTN_MIDDLE, &config->size);
	virtio_input_bitmap_set(config->u.bitmap, BTN_SIDE, &config->size);
	virtio_input_bitmap_set(config->u.bitmap, BTN_EXTRA, &config->size);
}

static void virtio_input_build_config(struct virtio_input_dev *idev,
				      uint8_t select, uint8_t subsel,
				      struct virtio_input_config *config)
{
	memset(config, 0, sizeof(*config));

	switch (select) {
	case VIRTIO_INPUT_CFG_ID_NAME:
		virtio_input_config_string(config, select,
					   virtio_input_profile_name(idev->profile));
		break;
	case VIRTIO_INPUT_CFG_ID_SERIAL:
		virtio_input_config_string(config, select,
					   virtio_input_profile_serial(idev->profile));
		break;
	case VIRTIO_INPUT_CFG_ID_DEVIDS:
		config->select = select;
		config->size = sizeof(config->u.ids);
		config->u.ids.bustype = BUS_VIRTUAL;
		config->u.ids.vendor = 0x5253;
		config->u.ids.product =
			virtio_input_profile_product(idev->profile);
		config->u.ids.version = 0x0001;
		break;
	case VIRTIO_INPUT_CFG_PROP_BITS:
		config->select = select;
		config->subsel = subsel;
		if (idev->profile == VIRTIO_INPUT_PROFILE_TABLET)
			virtio_input_bitmap_set(config->u.bitmap,
						INPUT_PROP_POINTER,
						&config->size);
		if (!config->size)
			memset(config, 0, sizeof(*config));
		break;
	case VIRTIO_INPUT_CFG_EV_BITS:
		config->select = select;
		config->subsel = subsel;
		if (idev->profile == VIRTIO_INPUT_PROFILE_KEYBOARD) {
			if (subsel == EV_KEY) {
				virtio_input_build_keyboard_bits(config);
			} else if (subsel == EV_LED) {
				virtio_input_bitmap_set(config->u.bitmap,
							LED_NUML, &config->size);
				virtio_input_bitmap_set(config->u.bitmap,
							LED_CAPSL, &config->size);
				virtio_input_bitmap_set(config->u.bitmap,
							LED_SCROLLL, &config->size);
			} else if (subsel == EV_REP) {
				config->size = 1;
			}
		} else {
			if (subsel == EV_KEY) {
				virtio_input_build_button_bits(config);
			} else if (idev->profile == VIRTIO_INPUT_PROFILE_MOUSE &&
				   subsel == EV_REL) {
				virtio_input_bitmap_set(config->u.bitmap,
							REL_X, &config->size);
				virtio_input_bitmap_set(config->u.bitmap,
							REL_Y, &config->size);
				virtio_input_bitmap_set(config->u.bitmap,
							REL_WHEEL, &config->size);
			} else if (idev->profile == VIRTIO_INPUT_PROFILE_TABLET &&
				   subsel == EV_REL) {
				virtio_input_bitmap_set(config->u.bitmap,
							REL_WHEEL, &config->size);
			} else if (idev->profile == VIRTIO_INPUT_PROFILE_TABLET &&
				   subsel == EV_ABS) {
				virtio_input_bitmap_set(config->u.bitmap,
							ABS_X, &config->size);
				virtio_input_bitmap_set(config->u.bitmap,
							ABS_Y, &config->size);
			}
		}
		if (!config->size)
			memset(config, 0, sizeof(*config));
		break;
	case VIRTIO_INPUT_CFG_ABS_INFO:
		if (idev->profile == VIRTIO_INPUT_PROFILE_TABLET &&
		    (subsel == ABS_X || subsel == ABS_Y)) {
			config->select = select;
			config->subsel = subsel;
			config->size = sizeof(config->u.abs);
			config->u.abs.min = INPUT_ABS_MIN;
			config->u.abs.max = INPUT_ABS_MAX;
		}
		break;
	default:
		break;
	}
}

static int virtio_input_send_event(struct virtio_input_dev *idev,
				   const struct virtio_input_event *event)
{
	struct virtio_input_queue *q = &idev->vqs[VIRTIO_INPUT_EVENTS_QUEUE];
	struct virtio_queue *vq = &q->vq;
	uint16_t head = 0;
	uint32_t iov_cnt = 0, total_len = 0;
	uint32_t copied;
	int ret;

	if (!virtio_queue_available(vq))
		return 0;

	ret = virtio_queue_get_iovec(vq, q->iov, &iov_cnt, &total_len, &head);
	if (ret)
		return ret;

	copied = virtio_buf_to_iovec_write(idev->vdev, q->iov, iov_cnt,
					   (void *)event, sizeof(*event));
	if (copied != sizeof(*event)) {
		virtio_queue_set_used_elem(vq, head, 0);
		return 0;
	}

	virtio_queue_set_used_elem(vq, head, sizeof(*event));
	if (virtio_queue_should_signal(vq) &&
	    idev->vdev->vn && idev->vdev->vn->notify) {
		idev->vdev->vn->notify(idev->vdev, VIRTIO_INPUT_EVENTS_QUEUE);
	}

	return sizeof(*event);
}

static int virtio_input_receive(void *buf, int len, void *priv)
{
	struct virtio_input_dev *idev = priv;
	struct virtio_input_event *event = buf;

	if (!idev || !buf || len != sizeof(*event))
		return -1;

	return virtio_input_send_event(idev, event);
}

static void virtio_input_process_status(struct virtio_input_dev *idev)
{
	struct virtio_input_queue *q = &idev->vqs[VIRTIO_INPUT_STATUS_QUEUE];
	struct virtio_queue *vq = &q->vq;

	while (virtio_queue_available(vq)) {
		struct virtio_input_event event;
		uint16_t head = 0;
		uint32_t iov_cnt = 0, total_len = 0;
		uint32_t copied;
		int ret;

		ret = virtio_queue_get_iovec(vq, q->iov, &iov_cnt,
					     &total_len, &head);
		if (ret)
			continue;

		memset(&event, 0, sizeof(event));
		copied = virtio_iovec_to_buf_read(idev->vdev, q->iov,
						  iov_cnt, &event,
						  sizeof(event));
		if (copied == sizeof(event))
			my_input_status(idev->vdev, &event, sizeof(event));
		virtio_queue_set_used_elem(vq, head, copied);
	}

	if (virtio_queue_should_signal(vq) &&
	    idev->vdev->vn && idev->vdev->vn->notify) {
		idev->vdev->vn->notify(idev->vdev, VIRTIO_INPUT_STATUS_QUEUE);
	}
}

static int virtio_input_notify_vq(struct virtio_device *dev, uint32_t vq)
{
	struct virtio_input_dev *idev = dev->emu_data;

	switch (vq) {
	case VIRTIO_INPUT_EVENTS_QUEUE:
	case VIRTIO_INPUT_STATUS_QUEUE:
		idev->pending_queues |= 1U << vq;
		return 0;
	default:
		return -1;
	}
}

static void virtio_input_req_process(void *data)
{
	struct virtio_input_dev *idev = data;
	uint32_t pending;

	if (!idev)
		return;

	pending = idev->pending_queues;
	idev->pending_queues = 0;

	if (pending & (1U << VIRTIO_INPUT_STATUS_QUEUE))
		virtio_input_process_status(idev);
}

static void virtio_input_status_changed(struct virtio_device *dev,
					uint32_t new_status)
{
}

static int virtio_input_read_config(struct virtio_device *dev,
				    uint32_t offset, void *dst,
				    uint32_t dst_len)
{
	struct virtio_input_dev *idev = dev->emu_data;
	struct virtio_input_config config;
	uint8_t *src = (uint8_t *)&config;
	uint32_t i;

	virtio_input_build_config(idev, idev->cfg_select, idev->cfg_subsel,
				  &config);
	for (i = 0; (i < dst_len) && ((offset + i) < sizeof(config)); i++)
		((uint8_t *)dst)[i] = src[offset + i];

	return 0;
}

static int virtio_input_write_config(struct virtio_device *dev,
				     uint32_t offset, void *src,
				     uint32_t src_len)
{
	struct virtio_input_dev *idev = dev->emu_data;
	uint8_t *data = src;
	uint32_t i;

	for (i = 0; i < src_len; i++) {
		switch (offset + i) {
		case offsetof(struct virtio_input_config, select):
			idev->cfg_select = data[i];
			break;
		case offsetof(struct virtio_input_config, subsel):
			idev->cfg_subsel = data[i];
			break;
		default:
			break;
		}
	}

	return 0;
}

static int virtio_input_reset(struct virtio_device *dev)
{
	return 0;
}

static int virtio_input_connect_profile(struct virtio_device *dev,
					enum virtio_input_profile profile)
{
	struct virtio_input_dev *idev;
	struct virtio_mmio_dev *mdev = container_of(dev, struct virtio_mmio_dev,
						   dev);

	idev = (struct virtio_input_dev *)my_zalloc(dev, sizeof(*idev));
	if (!idev)
		return -1;

	idev->vdev = dev;
	idev->profile = profile;
	dev->emu_data = idev;

	mdev->cb.receive = virtio_input_receive;
	mdev->cb.process_req = virtio_input_req_process;
	mdev->cb.data = idev;
	return 0;
}

static int virtio_keyboard_connect(struct virtio_device *dev,
				   struct virtio_emulator *emu)
{
	return virtio_input_connect_profile(dev, VIRTIO_INPUT_PROFILE_KEYBOARD);
}

static int virtio_mouse_connect(struct virtio_device *dev,
				struct virtio_emulator *emu)
{
	return virtio_input_connect_profile(dev, VIRTIO_INPUT_PROFILE_MOUSE);
}

static int virtio_tablet_connect(struct virtio_device *dev,
				 struct virtio_emulator *emu)
{
	return virtio_input_connect_profile(dev, VIRTIO_INPUT_PROFILE_TABLET);
}

static void virtio_input_disconnect(struct virtio_device *dev)
{
	struct virtio_input_dev *idev = dev->emu_data;

	if (!idev)
		return;

	my_free(dev, (uint64_t)(uintptr_t)idev, sizeof(*idev));
	dev->emu_data = NULL;
}

static struct virtio_device_id virtio_input_emu_id[] = {
	{ .type = VMM_VIRTIO_ID_INPUT },
	{ },
};

static struct virtio_emulator virtio_keyboard = {
	.name = VIRTIO_EMU_NAME_KEYBOARD,
	.id_table = virtio_input_emu_id,

	.get_host_features = virtio_input_get_host_features,
	.set_guest_features = virtio_input_set_guest_features,
	.init_vq = virtio_input_init_vq,
	.init_vq_addr = virtio_input_init_vq_addr,
	.get_pfn_vq = virtio_input_get_pfn_vq,
	.get_size_vq = virtio_input_get_size_vq,
	.set_size_vq = virtio_input_set_size_vq,
	.notify_vq = virtio_input_notify_vq,
	.status_changed = virtio_input_status_changed,

	.read_config = virtio_input_read_config,
	.write_config = virtio_input_write_config,
	.reset = virtio_input_reset,
	.connect = virtio_keyboard_connect,
	.disconnect = virtio_input_disconnect,
};

static struct virtio_emulator virtio_mouse = {
	.name = VIRTIO_EMU_NAME_MOUSE,
	.id_table = virtio_input_emu_id,

	.get_host_features = virtio_input_get_host_features,
	.set_guest_features = virtio_input_set_guest_features,
	.init_vq = virtio_input_init_vq,
	.init_vq_addr = virtio_input_init_vq_addr,
	.get_pfn_vq = virtio_input_get_pfn_vq,
	.get_size_vq = virtio_input_get_size_vq,
	.set_size_vq = virtio_input_set_size_vq,
	.notify_vq = virtio_input_notify_vq,
	.status_changed = virtio_input_status_changed,

	.read_config = virtio_input_read_config,
	.write_config = virtio_input_write_config,
	.reset = virtio_input_reset,
	.connect = virtio_mouse_connect,
	.disconnect = virtio_input_disconnect,
};

static struct virtio_emulator virtio_tablet = {
	.name = VIRTIO_EMU_NAME_TABLET,
	.id_table = virtio_input_emu_id,

	.get_host_features = virtio_input_get_host_features,
	.set_guest_features = virtio_input_set_guest_features,
	.init_vq = virtio_input_init_vq,
	.init_vq_addr = virtio_input_init_vq_addr,
	.get_pfn_vq = virtio_input_get_pfn_vq,
	.get_size_vq = virtio_input_get_size_vq,
	.set_size_vq = virtio_input_set_size_vq,
	.notify_vq = virtio_input_notify_vq,
	.status_changed = virtio_input_status_changed,

	.read_config = virtio_input_read_config,
	.write_config = virtio_input_write_config,
	.reset = virtio_input_reset,
	.connect = virtio_tablet_connect,
	.disconnect = virtio_input_disconnect,
};

struct virtio_emulator *virtio_keyboard_emulator_create(void)
{
	return &virtio_keyboard;
}

struct virtio_emulator *virtio_mouse_emulator_create(void)
{
	return &virtio_mouse;
}

struct virtio_emulator *virtio_tablet_emulator_create(void)
{
	return &virtio_tablet;
}
