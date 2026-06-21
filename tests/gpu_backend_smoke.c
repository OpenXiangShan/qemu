#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../backend/virtio_backend.h"
#include "../virtio/virtio_gpu.h"

struct test_state {
	uint8_t guest_mem[4096];
	uint32_t updates;
	uint32_t last_width;
	uint32_t last_height;
	uint32_t last_stride;
	uint32_t last_x;
	uint32_t last_y;
	uint32_t last_w;
	uint32_t last_h;
	const void *last_pixels;
};

static int guest_read(void *opaque, uint64_t gpa, void *dst, uint32_t len)
{
	struct test_state *s = opaque;

	if (gpa + len > sizeof(s->guest_mem))
		return -1;

	memcpy(dst, s->guest_mem + gpa, len);
	return (int)len;
}

static void scanout_update(void *opaque, uint32_t scanout_id,
			   const void *pixels, uint32_t width,
			   uint32_t height, uint32_t stride,
			   uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	struct test_state *s = opaque;

	(void)scanout_id;
	s->updates++;
	s->last_pixels = pixels;
	s->last_width = width;
	s->last_height = height;
	s->last_stride = stride;
	s->last_x = x;
	s->last_y = y;
	s->last_w = w;
	s->last_h = h;
}

static int submit(virtio_backend_handle_t backend, const void *cmd,
		  size_t cmd_len, uint32_t expect_type)
{
	struct virtio_gpu_ctrl_hdr resp;
	size_t resp_len = 0;
	struct virtio_backend_io io = {
		.type = VIRTIO_BACKEND_IO_GPU_CMD,
		.buf = (void *)cmd,
		.len = cmd_len,
		.cap = sizeof(resp),
		.u.gpu = {
			.resp = &resp,
			.resp_len = &resp_len,
		},
	};
	int ret;

	memset(&resp, 0, sizeof(resp));
	ret = virtio_backend_write(backend, &io);
	if (ret < 0) {
		fprintf(stderr, "submit failed ret=%d expect=0x%x\n",
			ret, expect_type);
		return 1;
	}
	if (resp_len < sizeof(resp) || resp.type != expect_type) {
		fprintf(stderr, "bad response len=%zu type=0x%x expect=0x%x\n",
			resp_len, resp.type, expect_type);
		return 1;
	}

	return 0;
}

int main(void)
{
	struct test_state state;
	struct virtio_backend_config config;
	virtio_backend_handle_t backend;
	uint32_t *pixels;
	struct {
		struct virtio_gpu_resource_attach_backing cmd;
		struct virtio_gpu_mem_entry entry;
	} attach;
	struct virtio_gpu_resource_create_2d create;
	struct virtio_gpu_transfer_to_host_2d transfer;
	struct virtio_gpu_set_scanout scanout;
	struct virtio_gpu_resource_flush flush;
	uint32_t expect[4] = {
		0xff0000ff,
		0xff00ff00,
		0xffff0000,
		0xffffffff,
	};

	memset(&state, 0, sizeof(state));
	memcpy(state.guest_mem + 0x100, expect, sizeof(expect));

	memset(&config, 0, sizeof(config));
	config.type = VIRTIO_BACKEND_GPU;
	config.u.gpu.width = 2;
	config.u.gpu.height = 2;
	config.u.gpu.max_outputs = 1;
	config.u.gpu.guest_read = guest_read;
	config.u.gpu.scanout_update = scanout_update;
	config.u.gpu.opaque = &state;

	backend = virtio_backend_create(&config);
	if (!backend) {
		fprintf(stderr, "failed to create gpu backend\n");
		return 1;
	}

	memset(&create, 0, sizeof(create));
	create.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
	create.resource_id = 1;
	create.format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
	create.width = 2;
	create.height = 2;
	if (submit(backend, &create, sizeof(create),
		   VIRTIO_GPU_RESP_OK_NODATA))
		return 1;

	memset(&attach, 0, sizeof(attach));
	attach.cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
	attach.cmd.resource_id = 1;
	attach.cmd.nr_entries = 1;
	attach.entry.addr = 0x100;
	attach.entry.length = sizeof(expect);
	if (submit(backend, &attach, sizeof(attach),
		   VIRTIO_GPU_RESP_OK_NODATA))
		return 1;

	memset(&transfer, 0, sizeof(transfer));
	transfer.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
	transfer.r.width = 2;
	transfer.r.height = 2;
	transfer.resource_id = 1;
	if (submit(backend, &transfer, sizeof(transfer),
		   VIRTIO_GPU_RESP_OK_NODATA))
		return 1;

	memset(&scanout, 0, sizeof(scanout));
	scanout.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
	scanout.r.width = 2;
	scanout.r.height = 2;
	scanout.resource_id = 1;
	if (submit(backend, &scanout, sizeof(scanout),
		   VIRTIO_GPU_RESP_OK_NODATA))
		return 1;

	memset(&flush, 0, sizeof(flush));
	flush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
	flush.r.width = 2;
	flush.r.height = 2;
	flush.resource_id = 1;
	if (submit(backend, &flush, sizeof(flush),
		   VIRTIO_GPU_RESP_OK_NODATA))
		return 1;

	if (state.updates != 2 || !state.last_pixels ||
	    state.last_width != 2 || state.last_height != 2 ||
	    state.last_stride != 8 || state.last_w != 2 ||
	    state.last_h != 2) {
		fprintf(stderr, "bad scanout update state\n");
		return 1;
	}

	pixels = (uint32_t *)state.last_pixels;
	if (memcmp(pixels, expect, sizeof(expect))) {
		fprintf(stderr, "framebuffer contents mismatch\n");
		return 1;
	}

	virtio_backend_destroy(backend);
	return 0;
}
