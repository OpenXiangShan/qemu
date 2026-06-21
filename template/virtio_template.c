/*
 * Minimal integration template for my-virtio-lib + libMyVirtio_backend.a.
 *
 * This file is intended to show the complete wiring pattern. Platform-specific
 * MMIO routing, DMA access and interrupt injection are left as TODO hooks.
 */

#include <stdarg.h>
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
};

struct template_virtio_dev {
	enum template_device_kind kind;
	const char *name;
	uint64_t mmio_base;
	uint32_t mmio_size;

	virtio_handle_t virtio;
	virtio_backend_handle_t backend;
};

struct template_context {
	struct template_virtio_dev blk;
	struct template_virtio_dev net;
	struct template_virtio_dev console;
};

static struct template_context g_ctx;

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

static int template_dma_read(uint64_t gpa, void *dst, uint32_t len)
{
	/*
	 * TODO: Copy len bytes from guest/SoC physical address gpa into dst.
	 * In a simulator this may call the memory model. In a VMM this may walk
	 * guest RAM or an IOMMU mapping.
	 */
	(void)gpa;
	(void)dst;
	(void)len;
	return -1;
}

static int template_dma_write(uint64_t gpa, void *src, uint32_t len)
{
	/*
	 * TODO: Copy len bytes from src into guest/SoC physical address gpa.
	 */
	(void)gpa;
	(void)src;
	(void)len;
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
};

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
}

static int template_create_blk(struct template_virtio_dev *dev,
			       uint64_t base, const char *image_path)
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

	dev->virtio = virtio_mmio_create(dev->name, dev->mmio_base,
					 dev->mmio_size, &template_ops, dev);
	return dev->virtio ? 0 : -1;
}

static int template_create_net(struct template_virtio_dev *dev, uint64_t base)
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

	dev->virtio = virtio_mmio_create(dev->name, dev->mmio_base,
					 dev->mmio_size, &template_ops, dev);
	return dev->virtio ? 0 : -1;
}

static int template_create_console(struct template_virtio_dev *dev,
				   uint64_t base)
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

	dev->virtio = virtio_mmio_create(dev->name, dev->mmio_base,
					 dev->mmio_size, &template_ops, dev);
	return dev->virtio ? 0 : -1;
}

int template_virtio_init(const char *disk_image)
{
	if (template_create_blk(&g_ctx.blk, 0x10001000, disk_image) < 0)
		return -1;
	if (template_create_net(&g_ctx.net, 0x10002000) < 0)
		return -1;
	if (template_create_console(&g_ctx.console, 0x10003000) < 0)
		return -1;

	return 0;
}

static struct template_virtio_dev *template_find_mmio_dev(uint64_t addr)
{
	struct template_virtio_dev *devs[] = {
		&g_ctx.blk,
		&g_ctx.net,
		&g_ctx.console,
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
		if (dev->kind == TEMPLATE_DEV_BLK)
			virtio_process_req(dev->virtio);
		else if (dev->kind == TEMPLATE_DEV_NET)
			template_net_drain_rx(dev);
		else if (dev->kind == TEMPLATE_DEV_CONSOLE)
			template_console_drain_rx(dev);
	}

	return 0;
}

int template_console_input(const void *buf, size_t len)
{
	return virtio_backend_push_readable(g_ctx.console.backend, buf, len);
}

int main(int argc, char **argv)
{
	const char *disk_image = argc > 1 ? argv[1] : "disk.img";

	if (template_virtio_init(disk_image) < 0) {
		fprintf(stderr, "failed to initialize virtio template devices\n");
		return 1;
	}

	/*
	 * TODO: Enter your platform event loop:
	 * - decode guest MMIO load/store and call template_mmio_read/write()
	 * - service DMA through template_dma_read/template_dma_write()
	 * - inject interrupts from template_set_irq()
	 * - forward host console input through template_console_input()
	 */
	return 0;
}
