/*
 * Minimal integration template for my-virtio-lib + libMyVirtio_backend.a.
 *
 * This file is intended to show the complete wiring pattern. Platform-specific
 * MMIO routing, DMA access and interrupt injection are left as TODO hooks.
 */

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "virtio_backend.h"
#include "virtio_wrapper.h"

#define TEMPLATE_MMIO_SIZE 0x1000

enum template_device_kind {
	TEMPLATE_DEV_BLK,
	TEMPLATE_DEV_NET,
	TEMPLATE_DEV_CONSOLE,
	TEMPLATE_DEV_GPU,
	TEMPLATE_DEV_KEYBOARD,
	TEMPLATE_DEV_MOUSE,
	TEMPLATE_DEV_TABLET,
};

struct template_virtio_dev {
	enum template_device_kind kind;
	const char *name;
	uint64_t mmio_base;
	uint32_t mmio_size;

	virtio_handle_t virtio;
	virtio_backend_handle_t backend;
	virtio_backend_handle_t ui;
};

struct template_context {
	struct template_virtio_dev blk;
	struct template_virtio_dev net;
	struct template_virtio_dev console;
	struct template_virtio_dev gpu;
	struct template_virtio_dev keyboard;
	struct template_virtio_dev mouse;
	struct template_virtio_dev tablet;
};

static struct template_context g_ctx;

struct template_input_event_le {
	uint16_t type;
	uint16_t code;
	uint32_t value;
};

static uint16_t template_cpu_to_le16(uint16_t val)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return (uint16_t)((val << 8) | (val >> 8));
#else
	return val;
#endif
}

static uint32_t template_cpu_to_le32(uint32_t val)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return ((val & 0x000000ffU) << 24) |
	       ((val & 0x0000ff00U) << 8) |
	       ((val & 0x00ff0000U) >> 8) |
	       ((val & 0xff000000U) >> 24);
#else
	return val;
#endif
}

static uint16_t template_le16_to_cpu(uint16_t val)
{
	return template_cpu_to_le16(val);
}

static uint32_t template_le32_to_cpu(uint32_t val)
{
	return template_cpu_to_le32(val);
}

static int template_is_input_dev(const struct template_virtio_dev *dev)
{
	return dev->kind == TEMPLATE_DEV_KEYBOARD ||
	       dev->kind == TEMPLATE_DEV_MOUSE ||
	       dev->kind == TEMPLATE_DEV_TABLET;
}

static int template_log(const char *fmt, va_list ap)
{
	return vfprintf(stderr, fmt, ap);
}

static uint64_t template_alloc(int size)
{
	return (uint64_t)(uintptr_t)calloc(1, (size_t)size);
}

static void template_free(uint64_t addr, int size)
{
	(void)size;
	free((void *)(uintptr_t)addr);
}

static int template_dma_read(uint64_t gpa, void *dst, uint32_t len, void *priv)
{
	/*
	 * TODO: Copy len bytes from guest/SoC physical address gpa into dst.
	 * In a simulator this may call the memory model. In a VMM this may walk
	 * guest RAM or an IOMMU mapping.
	 */
	(void)gpa;
	(void)dst;
	(void)len;
	(void)priv;
	return -1;
}

static int template_dma_write(uint64_t gpa, void *src, uint32_t len, void *priv)
{
	/*
	 * TODO: Copy len bytes from src into guest/SoC physical address gpa.
	 */
	(void)gpa;
	(void)src;
	(void)len;
	(void)priv;
	return -1;
}

static int template_set_irq(void *priv)
{
	struct template_virtio_dev *dev = priv;

	/*
	 * TODO: Assert/pulse the platform interrupt line wired to dev.
	 * The virtio library calls this when it needs to notify the guest.
	 */
	(void)dev;
	return 0;
}

static int template_blk_capacity(void *priv)
{
	struct template_virtio_dev *dev = priv;
	struct virtio_backend_info info;

	if (virtio_backend_get_info(dev->backend, &info) < 0 ||
	    info.type != VIRTIO_BACKEND_BLK)
		return 0;

	return info.u.blk.capacity < 0 ? 0 : info.u.blk.capacity;
}

static int template_blk_submit(uint64_t sector, void *buf, int len,
			       uint8_t flags, void *priv)
{
	struct template_virtio_dev *dev = priv;
	struct virtio_backend_io io = {
		.type = VIRTIO_BACKEND_IO_BLK,
		.buf = buf,
		.len = (size_t)len,
		.u.blk.sector = sector,
	};

	if (flags == MY_BLK_REQ_READ) {
		io.u.blk.op = VIRTIO_BACKEND_BLK_READ;
		return virtio_backend_read(dev->backend, &io);
	}
	if (flags == MY_BLK_REQ_WRITE) {
		io.u.blk.op = VIRTIO_BACKEND_BLK_WRITE;
		return virtio_backend_write(dev->backend, &io);
	}

	io.u.blk.op = VIRTIO_BACKEND_BLK_FLUSH;
	return virtio_backend_write(dev->backend, &io);
}

static void template_net_set_mac(uint8_t *mac, void *priv)
{
	static const uint8_t default_mac[6] = {
		0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
	};

	(void)priv;
	memcpy(mac, default_mac, sizeof(default_mac));
}

static int template_net_ctrl_mq(int vq_pairs, void *priv)
{
	(void)priv;
	return vq_pairs == 1 ? 0 : -1;
}

static int template_net_tx(uint64_t offset, void *buf, int len, void *priv)
{
	struct template_virtio_dev *dev = priv;
	struct virtio_backend_io io = {
		.type = VIRTIO_BACKEND_IO_PACKET,
		.buf = buf,
		.len = (size_t)len,
	};

	(void)offset;
	return virtio_backend_write(dev->backend, &io);
}

static int template_console_send(void *buf, int len, void *priv)
{
	struct template_virtio_dev *dev = priv;
	struct virtio_backend_io io = {
		.type = VIRTIO_BACKEND_IO_STREAM,
		.buf = buf,
		.len = (size_t)len,
	};

	return virtio_backend_write(dev->backend, &io);
}

static int template_gpu_submit(void *cmd, int cmd_len, void *resp,
			       int resp_cap, int *resp_len, void *priv)
{
	struct template_virtio_dev *dev = priv;
	size_t out_len = 0;
	struct virtio_backend_io io = {
		.type = VIRTIO_BACKEND_IO_GPU_CMD,
		.buf = cmd,
		.len = (size_t)cmd_len,
		.cap = (size_t)resp_cap,
		.u.gpu = {
			.resp = resp,
			.resp_len = &out_len,
		},
	};
	int ret;

	ret = virtio_backend_write(dev->backend, &io);
	if (resp_len)
		*resp_len = out_len <= INT_MAX ? (int)out_len : 0;

	return ret;
}

static int template_input_status(void *event, int len, void *priv)
{
	struct template_virtio_dev *dev = priv;
	const struct template_input_event_le *le_event = event;
	struct virtio_backend_input_event backend_event;
	struct virtio_backend_io io = {
		.type = VIRTIO_BACKEND_IO_INPUT_EVENT,
		.buf = &backend_event,
		.len = sizeof(backend_event),
	};

	if (!event || len != (int)sizeof(*le_event))
		return -1;

	backend_event.type = template_le16_to_cpu(le_event->type);
	backend_event.code = template_le16_to_cpu(le_event->code);
	backend_event.value = (int32_t)template_le32_to_cpu(le_event->value);

	return virtio_backend_write(dev->backend, &io);
}

static struct libvirtio_ops template_ops = {
	.vprint = template_log,
	.mm_alloc = template_alloc,
	.mm_free = template_free,
	.guest_mem_read = template_dma_read,
	.guest_mem_write = template_dma_write,
	.set_irq = template_set_irq,
	.blk_ops = {
		.submit_blk_io = template_blk_submit,
		.get_blk_capacity = template_blk_capacity,
	},
	.net_ops = {
		.set_mac = template_net_set_mac,
		.ctrl_mq = template_net_ctrl_mq,
		.write_tap = template_net_tx,
	},
	.console_ops = {
		.send = template_console_send,
	},
	.gpu_ops = {
		.submit_ctrl = template_gpu_submit,
		.submit_cursor = template_gpu_submit,
	},
	.input_ops = {
		.status = template_input_status,
	},
};

static virtio_handle_t template_create_mmio(struct template_virtio_dev *dev,
					    bool packed)
{
	struct virtio_mmio_options options = {
		.packed = packed,
	};

	return virtio_mmio_create_ex(dev->name, dev->mmio_base,
				     dev->mmio_size, &template_ops, dev,
				     &options);
}

static void template_net_drain_rx(struct template_virtio_dev *dev)
{
	for (;;) {
		uint8_t buf[65536];
		struct virtio_backend_io io = {
			.type = VIRTIO_BACKEND_IO_PACKET,
			.buf = buf,
			.cap = sizeof(buf),
		};
		int ret;

		ret = virtio_backend_read(dev->backend, &io);
		if (ret < 0)
			break;

		ret = virtio_receive(dev->virtio, io.buf, (int)io.len);
		virtio_backend_read_done(dev->backend, io.token, ret ? 0 : 1);
		if (ret)
			break;
	}
}

static void template_console_drain_rx(struct template_virtio_dev *dev)
{
	for (;;) {
		uint8_t buf[4096];
		struct virtio_backend_io io = {
			.type = VIRTIO_BACKEND_IO_STREAM,
			.buf = buf,
			.cap = sizeof(buf),
		};
		int ret;

		ret = virtio_backend_read(dev->backend, &io);
		if (ret < 0)
			break;

		ret = virtio_receive(dev->virtio, io.buf, (int)io.len);
		virtio_backend_read_done(dev->backend, io.token, ret <= 0 ? 0 : 1);
		if (ret <= 0)
			break;
	}
}

static void template_input_drain_rx(struct template_virtio_dev *dev)
{
	for (;;) {
		struct virtio_backend_input_event backend_event;
		struct template_input_event_le event;
		struct virtio_backend_io io = {
			.type = VIRTIO_BACKEND_IO_INPUT_EVENT,
			.buf = &backend_event,
			.cap = sizeof(backend_event),
		};
		int ret;

		ret = virtio_backend_read(dev->backend, &io);
		if (ret < 0)
			break;
		if (ret != (int)sizeof(backend_event)) {
			virtio_backend_read_done(dev->backend, io.token, 0);
			break;
		}

		event.type = template_cpu_to_le16(backend_event.type);
		event.code = template_cpu_to_le16(backend_event.code);
		event.value = template_cpu_to_le32((uint32_t)backend_event.value);

		ret = virtio_receive(dev->virtio, &event, sizeof(event));
		if (ret <= 0) {
			virtio_backend_read_done(dev->backend, io.token, 0);
			break;
		}

		virtio_backend_read_done(dev->backend, io.token, 1);
	}
}

static void template_backend_event(void *opaque, virtio_backend_handle_t handle,
				   unsigned int events)
{
	struct template_virtio_dev *dev = opaque;

	(void)handle;

	if (events & VIRTIO_BACKEND_EVENT_ERROR) {
		fprintf(stderr, "%s backend reported error\n", dev->name);
		return;
	}

	if (!(events & VIRTIO_BACKEND_EVENT_READABLE))
		return;

	if (dev->kind == TEMPLATE_DEV_NET)
		template_net_drain_rx(dev);
	else if (dev->kind == TEMPLATE_DEV_CONSOLE)
		template_console_drain_rx(dev);
	else if (template_is_input_dev(dev))
		template_input_drain_rx(dev);
}

static int template_create_blk(struct template_virtio_dev *dev,
			       uint64_t base, const char *image_path,
			       bool packed)
{
	struct virtio_backend_config backend_config = {
		.type = VIRTIO_BACKEND_BLK,
		.u.blk.image_path = image_path,
	};

	dev->kind = TEMPLATE_DEV_BLK;
	dev->name = VIRTIO_EMU_NAME_BLK;
	dev->mmio_base = base;
	dev->mmio_size = TEMPLATE_MMIO_SIZE;

	dev->backend = virtio_backend_create(&backend_config);
	if (!dev->backend)
		return -1;

	dev->virtio = template_create_mmio(dev, packed);
	return dev->virtio ? 0 : -1;
}

static int template_create_net(struct template_virtio_dev *dev, uint64_t base,
			       bool packed)
{
	static const uint8_t mac[6] = {
		0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
	};
	struct virtio_backend_callbacks callbacks = {
		.event = template_backend_event,
	};
	struct virtio_backend_config backend_config = {
		.type = VIRTIO_BACKEND_NET,
		.callbacks = &callbacks,
		.callback_opaque = dev,
		.u.net.hostfwd = "",
		.u.net.mac = mac,
		/*
		 * These topology fields are optional. Leave them NULL/empty to
		 * use the backend defaults:
		 * 10.0.2.0/24, host 10.0.2.2, DHCP 10.0.2.15, DNS 10.0.2.3.
		 */
		.u.net.network = "10.0.2.0",
		.u.net.netmask = "255.255.255.0",
		.u.net.host_ip = "10.0.2.2",
		.u.net.dhcp_start = "10.0.2.15",
		.u.net.dns_ip = "10.0.2.3",
	};

	dev->kind = TEMPLATE_DEV_NET;
	dev->name = VIRTIO_EMU_NAME_NET;
	dev->mmio_base = base;
	dev->mmio_size = TEMPLATE_MMIO_SIZE;

	dev->backend = virtio_backend_create(&backend_config);
	if (!dev->backend)
		return -1;

	dev->virtio = template_create_mmio(dev, packed);
	return dev->virtio ? 0 : -1;
}

static int template_create_console(struct template_virtio_dev *dev,
				   uint64_t base, bool packed)
{
	struct virtio_backend_callbacks callbacks = {
		.event = template_backend_event,
	};
	struct virtio_backend_config backend_config = {
		.type = VIRTIO_BACKEND_CONSOLE,
		.callbacks = &callbacks,
		.callback_opaque = dev,
		.u.console.backend = VIRTIO_BACKEND_CONSOLE_STDIO,
	};

	dev->kind = TEMPLATE_DEV_CONSOLE;
	dev->name = VIRTIO_EMU_NAME_CONSOLE;
	dev->mmio_base = base;
	dev->mmio_size = TEMPLATE_MMIO_SIZE;

	dev->backend = virtio_backend_create(&backend_config);
	if (!dev->backend)
		return -1;

	dev->virtio = template_create_mmio(dev, packed);
	return dev->virtio ? 0 : -1;
}

static int template_gpu_guest_read(void *opaque, uint64_t gpa,
				   void *dst, uint32_t len)
{
	(void)opaque;
	return template_dma_read(gpa, dst, len, NULL);
}

static void template_gpu_scanout_update(void *opaque, uint32_t scanout_id,
					const void *pixels, uint32_t width,
					uint32_t height, uint32_t stride,
					uint32_t x, uint32_t y,
					uint32_t w, uint32_t h)
{
	/*
	 * TODO: Optional external display hook for integrations that do not use
	 * the built-in VNC/UI backend. When backend_config.u.gpu.ui is set, the
	 * GPU backend already sends scanout updates to that UI backend; this hook
	 * is only needed if the platform wants an additional framebuffer/display
	 * sink of its own.
	 */
	(void)opaque;
	(void)scanout_id;
	(void)pixels;
	(void)width;
	(void)height;
	(void)stride;
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

static void template_gpu_scanout_disable(void *opaque, uint32_t scanout_id)
{
	/*
	 * TODO: Optional external display hook matching scanout_update above.
	 * This is not related to VIRTIO_BACKEND_INPUT_SOURCE_EXTERNAL; that enum
	 * only controls where input events come from.
	 */
	(void)opaque;
	(void)scanout_id;
}

static int template_create_gpu(struct template_virtio_dev *dev,
			       uint64_t base, const char *vnc_listen,
			       bool packed)
{
	struct virtio_backend_config ui_config = {
		.type = VIRTIO_BACKEND_UI,
		.u.ui = {
			.listen = vnc_listen,
			.width = 1280,
			.height = 800,
		},
	};
	struct virtio_backend_config backend_config = {
		.type = VIRTIO_BACKEND_GPU,
		.u.gpu = {
			.width = 1280,
			.height = 800,
			.max_outputs = 1,
			.guest_read = template_gpu_guest_read,
			.scanout_update = template_gpu_scanout_update,
			.scanout_disable = template_gpu_scanout_disable,
			.opaque = dev,
		},
	};

	dev->kind = TEMPLATE_DEV_GPU;
	dev->name = VIRTIO_EMU_NAME_GPU;
	dev->mmio_base = base;
	dev->mmio_size = TEMPLATE_MMIO_SIZE;

	dev->ui = virtio_backend_create(&ui_config);
	if (!dev->ui)
		return -1;

	backend_config.u.gpu.ui = dev->ui;
	dev->backend = virtio_backend_create(&backend_config);
	if (!dev->backend) {
		virtio_backend_destroy(dev->ui);
		dev->ui = NULL;
		return -1;
	}

	dev->virtio = template_create_mmio(dev, packed);
	if (!dev->virtio) {
		virtio_backend_destroy(dev->backend);
		dev->backend = NULL;
		virtio_backend_destroy(dev->ui);
		dev->ui = NULL;
		return -1;
	}

	return 0;
}

static const char *
template_input_emu_name(enum virtio_backend_input_profile profile)
{
	switch (profile) {
	case VIRTIO_BACKEND_INPUT_KEYBOARD:
		return VIRTIO_EMU_NAME_KEYBOARD;
	case VIRTIO_BACKEND_INPUT_MOUSE:
		return VIRTIO_EMU_NAME_MOUSE;
	case VIRTIO_BACKEND_INPUT_TABLET:
		return VIRTIO_EMU_NAME_TABLET;
	default:
		return NULL;
	}
}

static int template_create_input(struct template_virtio_dev *dev,
				 uint64_t base,
				 enum template_device_kind kind,
				 enum virtio_backend_input_profile profile,
				 enum virtio_backend_input_source source,
				 const char *evdev_path,
				 virtio_backend_handle_t ui, bool packed)
{
	struct virtio_backend_callbacks callbacks = {
		.event = template_backend_event,
	};
	struct virtio_backend_config backend_config = {
		.type = VIRTIO_BACKEND_INPUT,
		.callbacks = &callbacks,
		.callback_opaque = dev,
		.u.input = {
			.profile = profile,
			.source = source,
			.evdev_path = evdev_path,
			.ui = ui,
		},
	};

	dev->kind = kind;
	dev->name = template_input_emu_name(profile);
	dev->mmio_base = base;
	dev->mmio_size = TEMPLATE_MMIO_SIZE;

	if (!dev->name)
		return -1;

	dev->backend = virtio_backend_create(&backend_config);
	if (!dev->backend)
		return -1;

	dev->virtio = template_create_mmio(dev, packed);
	if (!dev->virtio) {
		virtio_backend_destroy(dev->backend);
		dev->backend = NULL;
		return -1;
	}

	return 0;
}

int template_virtio_init_ex(const char *disk_image, const char *vnc_listen,
			    bool packed)
{
	if (template_create_blk(&g_ctx.blk, 0x10001000, disk_image, packed) < 0)
		return -1;
	if (template_create_net(&g_ctx.net, 0x10002000, packed) < 0)
		return -1;
	if (template_create_console(&g_ctx.console, 0x10003000, packed) < 0)
		return -1;
	if (template_create_gpu(&g_ctx.gpu, 0x10004000, vnc_listen, packed) < 0)
		return -1;
	if (template_create_input(&g_ctx.keyboard, 0x10005000,
				  TEMPLATE_DEV_KEYBOARD,
				  VIRTIO_BACKEND_INPUT_KEYBOARD,
				  VIRTIO_BACKEND_INPUT_SOURCE_UI, NULL,
				  g_ctx.gpu.ui, packed) < 0)
		return -1;
	if (template_create_input(&g_ctx.mouse, 0x10006000,
				  TEMPLATE_DEV_MOUSE,
				  VIRTIO_BACKEND_INPUT_MOUSE,
				  VIRTIO_BACKEND_INPUT_SOURCE_UI, NULL,
				  g_ctx.gpu.ui, packed) < 0)
		return -1;
	if (template_create_input(&g_ctx.tablet, 0x10007000,
				  TEMPLATE_DEV_TABLET,
				  VIRTIO_BACKEND_INPUT_TABLET,
				  VIRTIO_BACKEND_INPUT_SOURCE_UI, NULL,
				  g_ctx.gpu.ui, packed) < 0)
		return -1;

	return 0;
}

int template_virtio_init(const char *disk_image, const char *vnc_listen)
{
	return template_virtio_init_ex(disk_image, vnc_listen, false);
}

static struct template_virtio_dev *template_find_mmio_dev(uint64_t addr)
{
	struct template_virtio_dev *devs[] = {
		&g_ctx.blk,
		&g_ctx.net,
		&g_ctx.console,
		&g_ctx.gpu,
		&g_ctx.keyboard,
		&g_ctx.mouse,
		&g_ctx.tablet,
	};

	for (unsigned int i = 0; i < sizeof(devs) / sizeof(devs[0]); i++) {
		struct template_virtio_dev *dev = devs[i];

		if (addr >= dev->mmio_base &&
		    addr < dev->mmio_base + dev->mmio_size)
			return dev;
	}

	return NULL;
}

int template_mmio_read(uint64_t addr, uint32_t *val, int len)
{
	struct template_virtio_dev *dev = template_find_mmio_dev(addr);

	if (!dev || !dev->virtio)
		return -1;

	return virtio_mmio_read(dev->virtio, addr, val, len);
}

int template_mmio_write(uint64_t addr, uint32_t val, int len)
{
	struct template_virtio_dev *dev = template_find_mmio_dev(addr);
	int is_doorbell = 0;
	int ret;

	if (!dev || !dev->virtio)
		return -1;

	ret = virtio_mmio_write(dev->virtio, addr, val, len, &is_doorbell);
	if (ret < 0)
		return ret;

	if (is_doorbell) {
		virtio_process_req(dev->virtio);
		if (dev->kind == TEMPLATE_DEV_NET)
			template_net_drain_rx(dev);
		else if (dev->kind == TEMPLATE_DEV_CONSOLE)
			template_console_drain_rx(dev);
		else if (template_is_input_dev(dev))
			template_input_drain_rx(dev);
	}

	return 0;
}

int template_console_input(const void *buf, size_t len)
{
	return virtio_backend_push_readable(g_ctx.console.backend, buf, len);
}

int template_input_events(enum virtio_backend_input_profile profile,
			  const struct virtio_backend_input_event *events,
			  size_t nr_events)
{
	struct template_virtio_dev *dev;

	switch (profile) {
	case VIRTIO_BACKEND_INPUT_KEYBOARD:
		dev = &g_ctx.keyboard;
		break;
	case VIRTIO_BACKEND_INPUT_MOUSE:
		dev = &g_ctx.mouse;
		break;
	case VIRTIO_BACKEND_INPUT_TABLET:
		dev = &g_ctx.tablet;
		break;
	default:
		return -1;
	}

	return virtio_backend_push_readable(dev->backend, events,
					    nr_events * sizeof(*events));
}

int main(int argc, char **argv)
{
	const char *disk_image = argc > 1 ? argv[1] : "disk.img";
	const char *vnc_listen = argc > 2 ? argv[2] : "127.0.0.1:5915";
	bool packed = argc > 3 && strcmp(argv[3], "packed") == 0;

	if (template_virtio_init_ex(disk_image, vnc_listen, packed) < 0) {
		fprintf(stderr, "failed to initialize virtio template devices\n");
		return 1;
	}

	/*
	 * TODO: Enter your platform event loop:
	 * - decode guest MMIO load/store and call template_mmio_read/write()
	 * - service DMA through template_dma_read/template_dma_write()
	 * - inject interrupts from template_set_irq()
	 * - forward host console input through template_console_input()
	 * - optionally inject external input through template_input_events()
	 */
	return 0;
}
