/*
 * Minimal host-side template for using my-virtio-lib through an FPGA gbus
 * backdoor.
 *
 * Intended architecture:
 *
 *   Linux guest CPU
 *       -> RTL virtio_gbus_mmio_{blk,net}_top.sv virtio-mmio registers
 *       -> RTL gbus CSR shadow: queue PFN/NUM/ALIGN/notify_seq/status/features
 *       -> this host/server process polls gbus CSR with virtio_gbus_poll()
 *       -> my-virtio-lib/virtio_{blk,net}.c walks vrings through guest_mem_*
 *       -> guest_mem_* calls FPGA gbus DMA read/write
 *       -> used ring complete writes HOST_IRQ_SET CSR through gbus
 *
 * Fill the fpga_* hooks with your real PCIe/JTAG/socket/devproxy gbus driver.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "virtio_backend.h"
#include "virtio_wrapper.h"

#define GBUS_TEMPLATE_MMIO_SIZE 0x1000U

/* Example FPGA gbus CSR windows. Replace with your address map. */
#define GBUS_TEMPLATE_BLK_CSR_BASE 0x00010000U
#define GBUS_TEMPLATE_NET_CSR_BASE 0x00020000U

enum gbus_template_kind {
	GBUS_TEMPLATE_BLK,
	GBUS_TEMPLATE_NET,
};

struct gbus_template_dev {
	enum gbus_template_kind kind;
	const char *virtio_name;
	uint64_t mmio_base;
	uint32_t mmio_size;
	uint32_t gbus_csr_base;

	virtio_handle_t virtio;
	virtio_backend_handle_t backend;
};

struct gbus_template_ctx {
	struct gbus_template_dev blk;
	struct gbus_template_dev net;
	uint8_t net_mac[6];
	volatile bool backend_readable;
};

static struct gbus_template_ctx g_ctx;

/*
 * Hardware hooks.
 *
 * addr is the gbus CSR address exposed by virtio_gbus_mmio_*_top.sv, not the
 * guest CPU MMIO address. gpa is the guest physical address used by the
 * virtqueue descriptor/avail/used rings.
 */
static int fpga_gbus_read32(uint32_t addr, uint32_t *val)
{
	(void)addr;
	(void)val;
	fprintf(stderr, "%s: TODO: hook your FPGA gbus CSR read\n", __func__);
	return -ENOSYS;
}

static int fpga_gbus_write32(uint32_t addr, uint32_t val)
{
	(void)addr;
	(void)val;
	fprintf(stderr, "%s: TODO: hook your FPGA gbus CSR write\n", __func__);
	return -ENOSYS;
}

static int fpga_gbus_dma_read(uint64_t gpa, void *dst, uint32_t len)
{
	(void)gpa;
	(void)dst;
	(void)len;
	fprintf(stderr, "%s: TODO: hook your FPGA guest-memory DMA read\n",
		__func__);
	return -ENOSYS;
}

static int fpga_gbus_dma_write(uint64_t gpa, const void *src, uint32_t len)
{
	(void)gpa;
	(void)src;
	(void)len;
	fprintf(stderr, "%s: TODO: hook your FPGA guest-memory DMA write\n",
		__func__);
	return -ENOSYS;
}

/*
 * Optional wait hook. A real implementation can block on:
 *
 *   - RTL sideband/doorbell telling the host that QUEUE_NOTIFY changed
 *   - backend fd readable, for example tap/slirp/pty/input
 *   - a short timeout
 *
 * A timeout-only loop is correct but wastes CPU.
 */
static void fpga_wait_for_work(unsigned timeout_ms)
{
	struct timespec ts = {
		.tv_sec = timeout_ms / 1000U,
		.tv_nsec = (long)(timeout_ms % 1000U) * 1000L * 1000L,
	};

	nanosleep(&ts, NULL);
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

static int template_vprint(const char *fmt, va_list ap)
{
	return vfprintf(stderr, fmt, ap);
}

static int template_guest_mem_read(uint64_t gpa, void *dst, uint32_t len,
				   void *priv)
{
	(void)priv;

	return fpga_gbus_dma_read(gpa, dst, len) < 0 ? -1 : (int)len;
}

static int template_guest_mem_write(uint64_t gpa, void *src, uint32_t len,
				    void *priv)
{
	(void)priv;

	return fpga_gbus_dma_write(gpa, src, len) < 0 ? -1 : (int)len;
}

static int template_set_irq(void *priv)
{
	(void)priv;

	/*
	 * In gbus mode virtio_gbus.c normally writes HOST_IRQ_SET itself from
	 * its notify callback. Keep this fallback for code paths that still
	 * call ops.set_irq().
	 */
	return fpga_gbus_write32(VIRTIO_GBUS_CSR_HOST_IRQ_SET,
				 VIRTIO_GBUS_HOST_IRQ_VRING);
}

static int template_gbus_read(void *opaque, uint32_t addr, uint32_t *val)
{
	struct gbus_template_dev *dev = opaque;

	return fpga_gbus_read32(dev->gbus_csr_base + addr, val);
}

static int template_gbus_write(void *opaque, uint32_t addr, uint32_t val)
{
	struct gbus_template_dev *dev = opaque;

	return fpga_gbus_write32(dev->gbus_csr_base + addr, val);
}

static const struct virtio_gbus_ops template_gbus_ops = {
	.read = template_gbus_read,
	.write = template_gbus_write,
};

static int template_get_blk_capacity(void *priv)
{
	struct gbus_template_dev *dev = priv;
	struct virtio_backend_info info;

	if (virtio_backend_get_info(dev->backend, &info) < 0 ||
	    info.type != VIRTIO_BACKEND_BLK || info.u.blk.capacity < 0) {
		return 0;
	}

	return (int)info.u.blk.capacity;
}

static int template_submit_blk_io(uint64_t sector, void *buf, int len,
				  uint8_t flags, void *priv)
{
	struct gbus_template_dev *dev = priv;
	struct virtio_backend_io io = {
		.type = VIRTIO_BACKEND_IO_BLK,
		.buf = buf,
		.len = (size_t)len,
		.cap = (size_t)len,
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

static void template_set_mac(uint8_t *mac, void *priv)
{
	struct gbus_template_ctx *ctx = priv;

	memcpy(mac, ctx->net_mac, sizeof(ctx->net_mac));
}

static int template_net_ctrl_mq(int vq_pairs, void *priv)
{
	(void)priv;

	return vq_pairs == 1 ? 0 : -1;
}

static int template_net_write(uint64_t offset, void *buf, int len, void *priv)
{
	struct gbus_template_dev *dev = priv;
	struct virtio_backend_io io = {
		.type = VIRTIO_BACKEND_IO_PACKET,
		.buf = buf,
		.len = (size_t)len,
	};

	(void)offset;
	return virtio_backend_write(dev->backend, &io);
}

static struct libvirtio_ops template_ops = {
	.vprint = template_vprint,
	.mm_alloc = template_alloc,
	.mm_free = template_free,
	.guest_mem_read = template_guest_mem_read,
	.guest_mem_write = template_guest_mem_write,
	.set_irq = template_set_irq,
	.blk_ops = {
		.submit_blk_io = template_submit_blk_io,
		.get_blk_capacity = template_get_blk_capacity,
	},
	.net_ops = {
		.set_mac = template_set_mac,
		.ctrl_mq = template_net_ctrl_mq,
		.read_tap = NULL,
		.write_tap = template_net_write,
	},
};

static void template_backend_event(void *opaque,
				   virtio_backend_handle_t handle,
				   unsigned int events)
{
	struct gbus_template_ctx *ctx = opaque;

	(void)handle;
	if (events & VIRTIO_BACKEND_EVENT_READABLE) {
		ctx->backend_readable = true;
	}
}

static const struct virtio_backend_callbacks template_backend_callbacks = {
	.event = template_backend_event,
};

static int template_dev_gbus_read32(struct gbus_template_dev *dev,
				    uint32_t addr, uint32_t *val)
{
	return fpga_gbus_read32(dev->gbus_csr_base + addr, val);
}

static int template_dev_gbus_write32(struct gbus_template_dev *dev,
				     uint32_t addr, uint32_t val)
{
	return fpga_gbus_write32(dev->gbus_csr_base + addr, val);
}

static void template_init_blk_rtl_config(struct gbus_template_dev *dev)
{
	struct virtio_backend_info info;
	uint64_t capacity = 0;

	if (virtio_backend_get_info(dev->backend, &info) == 0 &&
	    info.type == VIRTIO_BACKEND_BLK && info.u.blk.capacity > 0) {
		capacity = (uint64_t)info.u.blk.capacity;
	}

	template_dev_gbus_write32(dev, VIRTIO_GBUS_CSR_BLK_CAPACITY_LOW,
				  (uint32_t)capacity);
	template_dev_gbus_write32(dev, VIRTIO_GBUS_CSR_BLK_CAPACITY_HIGH,
				  (uint32_t)(capacity >> 32));
	template_dev_gbus_write32(dev, VIRTIO_GBUS_CSR_BLK_SEG_MAX, 126);
	template_dev_gbus_write32(dev, VIRTIO_GBUS_CSR_BLK_SIZE, 512);
}

static void template_init_net_rtl_config(struct gbus_template_ctx *ctx)
{
	struct gbus_template_dev *dev = &ctx->net;
	uint32_t mac_low = (uint32_t)ctx->net_mac[0] |
			   ((uint32_t)ctx->net_mac[1] << 8) |
			   ((uint32_t)ctx->net_mac[2] << 16) |
			   ((uint32_t)ctx->net_mac[3] << 24);
	uint32_t mac_high = (uint32_t)ctx->net_mac[4] |
			    ((uint32_t)ctx->net_mac[5] << 8);

	template_dev_gbus_write32(dev, VIRTIO_GBUS_CSR_NET_MAC_LOW, mac_low);
	template_dev_gbus_write32(dev, VIRTIO_GBUS_CSR_NET_MAC_HIGH, mac_high);
	template_dev_gbus_write32(dev, VIRTIO_GBUS_CSR_NET_STATUS, 1);
	template_dev_gbus_write32(dev, VIRTIO_GBUS_CSR_NET_MAX_QUEUE_PAIRS, 1);
}

static int template_check_gbus_magic(struct gbus_template_dev *dev)
{
	uint32_t magic = 0;
	uint32_t version = 0;

	if (template_dev_gbus_read32(dev, VIRTIO_GBUS_CSR_MAGIC, &magic) < 0 ||
	    template_dev_gbus_read32(dev, VIRTIO_GBUS_CSR_VERSION,
				     &version) < 0) {
		return -1;
	}

	if (magic != VIRTIO_GBUS_MAGIC || version != VIRTIO_GBUS_VERSION) {
		fprintf(stderr,
			"bad %s gbus RTL at 0x%08x: magic=0x%08x version=%u\n",
			dev->virtio_name, dev->gbus_csr_base, magic, version);
		return -1;
	}

	return 0;
}

static int template_create_blk(struct gbus_template_ctx *ctx,
			       uint64_t mmio_base, const char *image_path)
{
	struct gbus_template_dev *dev = &ctx->blk;
	struct virtio_backend_config backend_config = {
		.type = VIRTIO_BACKEND_BLK,
	};

	dev->kind = GBUS_TEMPLATE_BLK;
	dev->virtio_name = VIRTIO_EMU_NAME_BLK;
	dev->mmio_base = mmio_base;
	dev->mmio_size = GBUS_TEMPLATE_MMIO_SIZE;
	dev->gbus_csr_base = GBUS_TEMPLATE_BLK_CSR_BASE;

	backend_config.u.blk.image_path = image_path;
	dev->backend = virtio_backend_create(&backend_config);
	if (!dev->backend) {
		fprintf(stderr, "failed to create blk backend image=%s\n",
			image_path);
		return -1;
	}

	if (template_check_gbus_magic(dev) < 0) {
		return -1;
	}

	template_init_blk_rtl_config(dev);
	dev->virtio = virtio_gbus_create(dev->virtio_name, dev->mmio_base,
					 dev->mmio_size, &template_ops, dev,
					 &template_gbus_ops, dev);
	if (!dev->virtio) {
		fprintf(stderr, "failed to create gbus blk transport\n");
		return -1;
	}

	return 0;
}

static int template_create_net(struct gbus_template_ctx *ctx,
			       uint64_t mmio_base)
{
	struct gbus_template_dev *dev = &ctx->net;
	struct virtio_backend_config backend_config = {
		.type = VIRTIO_BACKEND_NET,
		.callbacks = &template_backend_callbacks,
		.callback_opaque = ctx,
	};

	static const uint8_t default_mac[6] = {
		0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
	};

	dev->kind = GBUS_TEMPLATE_NET;
	dev->virtio_name = VIRTIO_EMU_NAME_NET;
	dev->mmio_base = mmio_base;
	dev->mmio_size = GBUS_TEMPLATE_MMIO_SIZE;
	dev->gbus_csr_base = GBUS_TEMPLATE_NET_CSR_BASE;
	memcpy(ctx->net_mac, default_mac, sizeof(ctx->net_mac));

	backend_config.u.net.mac = ctx->net_mac;
	backend_config.u.net.network = "10.0.2.0";
	backend_config.u.net.netmask = "255.255.255.0";
	backend_config.u.net.host_ip = "10.0.2.2";
	backend_config.u.net.dhcp_start = "10.0.2.15";
	backend_config.u.net.dns_ip = "10.0.2.3";
	backend_config.u.net.hostfwd = "";

	dev->backend = virtio_backend_create(&backend_config);
	if (!dev->backend) {
		fprintf(stderr, "failed to create net backend\n");
		return -1;
	}

	if (template_check_gbus_magic(dev) < 0) {
		return -1;
	}

	template_init_net_rtl_config(ctx);
	dev->virtio = virtio_gbus_create(dev->virtio_name, dev->mmio_base,
					 dev->mmio_size, &template_ops, dev,
					 &template_gbus_ops, dev);
	if (!dev->virtio) {
		fprintf(stderr, "failed to create gbus net transport\n");
		return -1;
	}

	return 0;
}

static void template_drain_net_rx(struct gbus_template_ctx *ctx)
{
	struct gbus_template_dev *dev = &ctx->net;

	for (;;) {
		uint8_t buf[65536];
		struct virtio_backend_io io = {
			.type = VIRTIO_BACKEND_IO_PACKET,
			.buf = buf,
			.cap = sizeof(buf),
		};
		int ret;

		if (!dev->backend || !dev->virtio) {
			return;
		}

		ret = virtio_backend_read(dev->backend, &io);
		if (ret < 0) {
			return;
		}

		ret = virtio_receive(dev->virtio, io.buf, (int)io.len);
		if (ret) {
			virtio_backend_read_done(dev->backend, io.token, 0);
			return;
		}

		virtio_backend_read_done(dev->backend, io.token, 1);
	}
}

static void template_poll_one(struct gbus_template_dev *dev)
{
	if (dev->virtio) {
		virtio_gbus_poll(dev->virtio);
	}
}

static void template_poll_all(struct gbus_template_ctx *ctx)
{
	template_poll_one(&ctx->blk);
	template_poll_one(&ctx->net);
	template_drain_net_rx(ctx);
	ctx->backend_readable = false;
}

static void template_destroy(struct gbus_template_ctx *ctx)
{
	if (ctx->net.backend) {
		virtio_backend_destroy(ctx->net.backend);
		ctx->net.backend = NULL;
	}
	if (ctx->blk.backend) {
		virtio_backend_destroy(ctx->blk.backend);
		ctx->blk.backend = NULL;
	}
}

/*
 * Example init flow. The mmio_base values should match the addresses described
 * in the guest DTB and decoded by the FPGA RTL virtio-mmio tops.
 */
static int template_init(struct gbus_template_ctx *ctx, const char *blk_image)
{
	memset(ctx, 0, sizeof(*ctx));

	if (template_create_blk(ctx, 0x10001000ULL, blk_image) < 0) {
		return -1;
	}

	if (template_create_net(ctx, 0x10002000ULL) < 0) {
		return -1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	const char *blk_image = argc > 1 ? argv[1] : "disk.img";

	if (template_init(&g_ctx, blk_image) < 0) {
		template_destroy(&g_ctx);
		return 1;
	}

	for (;;) {
		/*
		 * Correctness rule:
		 *   Always call virtio_gbus_poll() after the FPGA reports a
		 *   guest QUEUE_NOTIFY/status/config change, and periodically as
		 *   a fallback. For net RX, call virtio_receive() after polling
		 *   so C can place host packets into the guest RX vring.
		 */
		template_poll_all(&g_ctx);

		/*
		 * Replace this with your host event loop. If the backend callback
		 * set backend_readable, reduce the wait time or wake immediately.
		 */
		fpga_wait_for_work(g_ctx.backend_readable ? 0 : 1);
	}

	template_destroy(&g_ctx);
	return 0;
}
