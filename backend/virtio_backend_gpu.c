#include "virtio_backend_internal.h"
#include "../virtio/virtio_gpu.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GPU_DEFAULT_WIDTH	1280U
#define GPU_DEFAULT_HEIGHT	800U
#define GPU_MAX_BACKING		16384U

struct virtio_backend_gpu_backing {
	uint64_t addr;
	uint32_t len;
};

struct virtio_backend_gpu_resource {
	struct virtio_backend_gpu_resource *next;
	uint32_t resource_id;
	uint32_t format;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t scanout_mask;
	uint8_t *pixels;
	struct virtio_backend_gpu_backing *backing;
	uint32_t nr_backing;
};

struct virtio_backend_gpu_scanout {
	uint32_t resource_id;
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
};

struct virtio_backend_gpu {
	uint32_t width;
	uint32_t height;
	uint32_t max_outputs;
	virtio_backend_handle_t ui;
	struct virtio_backend_gpu_resource *resources;
	struct virtio_backend_gpu_scanout scanouts[VIRTIO_GPU_MAX_SCANOUTS];
	int (*guest_read)(void *opaque, uint64_t gpa, void *dst, uint32_t len);
	void (*scanout_update)(void *opaque, uint32_t scanout_id,
			       const void *pixels, uint32_t width,
			       uint32_t height, uint32_t stride,
			       uint32_t x, uint32_t y, uint32_t w, uint32_t h);
	void (*scanout_disable)(void *opaque, uint32_t scanout_id);
	void *opaque;
};

static int gpu_format_supported(uint32_t format)
{
	switch (format) {
	case VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM:
	case VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM:
	case VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM:
	case VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM:
		return 1;
	default:
		return 0;
	}
}

static struct virtio_backend_gpu_resource *
gpu_find_resource(struct virtio_backend_gpu *gpu, uint32_t resource_id)
{
	struct virtio_backend_gpu_resource *res;

	for (res = gpu->resources; res; res = res->next) {
		if (res->resource_id == resource_id)
			return res;
	}

	return NULL;
}

static void gpu_fill_ctrl_hdr(struct virtio_gpu_ctrl_hdr *resp,
			      const struct virtio_gpu_ctrl_hdr *cmd,
			      uint32_t type)
{
	memset(resp, 0, sizeof(*resp));
	resp->type = type;
	if (cmd && (cmd->flags & VIRTIO_GPU_FLAG_FENCE)) {
		resp->flags = VIRTIO_GPU_FLAG_FENCE;
		resp->fence_id = cmd->fence_id;
		resp->ctx_id = cmd->ctx_id;
	}
}

static int gpu_set_response(const void *src, size_t src_len, void *resp,
			    size_t resp_cap, size_t *resp_len)
{
	if (resp_cap < src_len)
		return -ENOSPC;
	memcpy(resp, src, src_len);
	*resp_len = src_len;
	return 0;
}

static int gpu_response_nodata(const struct virtio_gpu_ctrl_hdr *cmd,
			       uint32_t type, void *resp, size_t resp_cap,
			       size_t *resp_len)
{
	struct virtio_gpu_ctrl_hdr hdr;

	gpu_fill_ctrl_hdr(&hdr, cmd, type);
	return gpu_set_response(&hdr, sizeof(hdr), resp, resp_cap, resp_len);
}

static int gpu_rect_valid(uint32_t width, uint32_t height,
			  const struct virtio_gpu_rect *r)
{
	if (!r->width || !r->height)
		return 0;
	if (r->x > width || r->y > height)
		return 0;
	if (r->width > width || r->height > height)
		return 0;
	if (r->x + r->width > width || r->y + r->height > height)
		return 0;
	return 1;
}

static int gpu_copy_from_backing(struct virtio_backend_gpu *gpu,
				 struct virtio_backend_gpu_resource *res,
				 uint64_t offset, uint8_t *dst, size_t len)
{
	uint32_t i;

	if (!len)
		return 0;
	if (!gpu->guest_read)
		return -ENOTSUP;

	for (i = 0; i < res->nr_backing && len; i++) {
		struct virtio_backend_gpu_backing *b = &res->backing[i];

		if (offset >= b->len) {
			offset -= b->len;
			continue;
		}

		while (i < res->nr_backing && len) {
			size_t chunk;

			b = &res->backing[i];
			chunk = b->len - (uint32_t)offset;
			if (chunk > len)
				chunk = len;
			if (gpu->guest_read(gpu->opaque, b->addr + offset,
					    dst, (uint32_t)chunk) !=
			    (int)chunk)
				return -EIO;
			dst += chunk;
			len -= chunk;
			offset = 0;
			i++;
		}
	}

	return len ? -EINVAL : 0;
}

static void gpu_disable_scanout(struct virtio_backend_gpu *gpu,
				uint32_t scanout_id)
{
	struct virtio_backend_gpu_resource *res;

	for (res = gpu->resources; res; res = res->next)
		res->scanout_mask &= ~(1U << scanout_id);

	memset(&gpu->scanouts[scanout_id], 0, sizeof(gpu->scanouts[0]));
	if (gpu->scanout_disable)
		gpu->scanout_disable(gpu->opaque, scanout_id);
	if (gpu->ui)
		virtio_backend_ui_disable(gpu->ui);
}

static int gpu_get_display_info(struct virtio_backend_gpu *gpu,
				const struct virtio_gpu_ctrl_hdr *cmd,
				void *resp, size_t resp_cap,
				size_t *resp_len)
{
	struct virtio_gpu_resp_display_info info;
	uint32_t i;

	memset(&info, 0, sizeof(info));
	gpu_fill_ctrl_hdr(&info.hdr, cmd, VIRTIO_GPU_RESP_OK_DISPLAY_INFO);
	for (i = 0; i < gpu->max_outputs; i++) {
		info.pmodes[i].r.width = gpu->width;
		info.pmodes[i].r.height = gpu->height;
		info.pmodes[i].enabled = 1;
	}

	return gpu_set_response(&info, sizeof(info), resp, resp_cap, resp_len);
}

static int gpu_resource_create_2d(struct virtio_backend_gpu *gpu,
				  const void *cmd, size_t cmd_len,
				  void *resp, size_t resp_cap,
				  size_t *resp_len)
{
	const struct virtio_gpu_resource_create_2d *c = cmd;
	struct virtio_backend_gpu_resource *res;
	uint64_t bytes;

	if (cmd_len < sizeof(*c) || !c->resource_id ||
	    !c->width || !c->height || !gpu_format_supported(c->format) ||
	    gpu_find_resource(gpu, c->resource_id))
		return gpu_response_nodata(&c->hdr,
					   VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
					   resp, resp_cap, resp_len);

	bytes = (uint64_t)c->width * c->height * 4U;
	if (bytes > SIZE_MAX)
		return gpu_response_nodata(&c->hdr,
					   VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
					   resp, resp_cap, resp_len);

	res = calloc(1, sizeof(*res));
	if (!res)
		return gpu_response_nodata(&c->hdr,
					   VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
					   resp, resp_cap, resp_len);

	res->pixels = calloc(1, (size_t)bytes);
	if (!res->pixels) {
		free(res);
		return gpu_response_nodata(&c->hdr,
					   VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
					   resp, resp_cap, resp_len);
	}
	res->resource_id = c->resource_id;
	res->format = c->format;
	res->width = c->width;
	res->height = c->height;
	res->stride = c->width * 4U;
	res->next = gpu->resources;
	gpu->resources = res;

	return gpu_response_nodata(&c->hdr, VIRTIO_GPU_RESP_OK_NODATA,
				   resp, resp_cap, resp_len);
}

static int gpu_resource_unref(struct virtio_backend_gpu *gpu, const void *cmd,
			      size_t cmd_len, void *resp, size_t resp_cap,
			      size_t *resp_len)
{
	const struct virtio_gpu_resource_unref *c = cmd;
	struct virtio_backend_gpu_resource **pp, *res;
	uint32_t i;

	if (cmd_len < sizeof(*c))
		return -EINVAL;

	pp = &gpu->resources;
	while (*pp && (*pp)->resource_id != c->resource_id)
		pp = &(*pp)->next;
	res = *pp;
	if (!res)
		return gpu_response_nodata(&c->hdr,
					   VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
					   resp, resp_cap, resp_len);

	for (i = 0; i < gpu->max_outputs; i++) {
		if (res->scanout_mask & (1U << i))
			gpu_disable_scanout(gpu, i);
	}
	*pp = res->next;
	free(res->backing);
	free(res->pixels);
	free(res);

	return gpu_response_nodata(&c->hdr, VIRTIO_GPU_RESP_OK_NODATA,
				   resp, resp_cap, resp_len);
}

static int gpu_attach_backing(struct virtio_backend_gpu *gpu, const void *cmd,
			      size_t cmd_len, void *resp, size_t resp_cap,
			      size_t *resp_len)
{
	const struct virtio_gpu_resource_attach_backing *c = cmd;
	const struct virtio_gpu_mem_entry *entries;
	struct virtio_backend_gpu_resource *res;
	uint32_t i;

	if (cmd_len < sizeof(*c) || c->nr_entries > GPU_MAX_BACKING)
		return -EINVAL;

	if (cmd_len < sizeof(*c) + c->nr_entries * sizeof(*entries))
		return gpu_response_nodata(&c->hdr,
					   VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
					   resp, resp_cap, resp_len);

	res = gpu_find_resource(gpu, c->resource_id);
	if (!res)
		return gpu_response_nodata(&c->hdr,
					   VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
					   resp, resp_cap, resp_len);
	if (res->backing)
		return gpu_response_nodata(&c->hdr, VIRTIO_GPU_RESP_ERR_UNSPEC,
					   resp, resp_cap, resp_len);

	res->backing = calloc(c->nr_entries, sizeof(*res->backing));
	if (!res->backing)
		return gpu_response_nodata(&c->hdr,
					   VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
					   resp, resp_cap, resp_len);
	res->nr_backing = c->nr_entries;

	entries = (const void *)((const uint8_t *)cmd + sizeof(*c));
	for (i = 0; i < c->nr_entries; i++) {
		res->backing[i].addr = entries[i].addr;
		res->backing[i].len = entries[i].length;
	}

	return gpu_response_nodata(&c->hdr, VIRTIO_GPU_RESP_OK_NODATA,
				   resp, resp_cap, resp_len);
}

static int gpu_detach_backing(struct virtio_backend_gpu *gpu, const void *cmd,
			      size_t cmd_len, void *resp, size_t resp_cap,
			      size_t *resp_len)
{
	const struct virtio_gpu_resource_detach_backing *c = cmd;
	struct virtio_backend_gpu_resource *res;

	if (cmd_len < sizeof(*c))
		return -EINVAL;

	res = gpu_find_resource(gpu, c->resource_id);
	if (!res)
		return gpu_response_nodata(&c->hdr,
					   VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
					   resp, resp_cap, resp_len);

	free(res->backing);
	res->backing = NULL;
	res->nr_backing = 0;
	return gpu_response_nodata(&c->hdr, VIRTIO_GPU_RESP_OK_NODATA,
				   resp, resp_cap, resp_len);
}

static int gpu_transfer_to_host_2d(struct virtio_backend_gpu *gpu,
				   const void *cmd, size_t cmd_len,
				   void *resp, size_t resp_cap,
				   size_t *resp_len)
{
	const struct virtio_gpu_transfer_to_host_2d *c = cmd;
	struct virtio_backend_gpu_resource *res;
	uint32_t y;
	int ret;

	if (cmd_len < sizeof(*c))
		return -EINVAL;

	res = gpu_find_resource(gpu, c->resource_id);
	if (!res)
		return gpu_response_nodata(&c->hdr,
					   VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
					   resp, resp_cap, resp_len);
	if (!res->backing || !gpu_rect_valid(res->width, res->height, &c->r))
		return gpu_response_nodata(&c->hdr,
					   VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
					   resp, resp_cap, resp_len);

	for (y = 0; y < c->r.height; y++) {
		uint8_t *dst = res->pixels +
			(uint64_t)(c->r.y + y) * res->stride + c->r.x * 4U;
		ret = gpu_copy_from_backing(gpu, res,
					    c->offset + (uint64_t)y * res->stride,
					    dst, c->r.width * 4U);
		if (ret < 0)
			return gpu_response_nodata(&c->hdr,
						   VIRTIO_GPU_RESP_ERR_UNSPEC,
						   resp, resp_cap, resp_len);
	}

	return gpu_response_nodata(&c->hdr, VIRTIO_GPU_RESP_OK_NODATA,
				   resp, resp_cap, resp_len);
}

static int gpu_set_scanout(struct virtio_backend_gpu *gpu, const void *cmd,
			   size_t cmd_len, void *resp, size_t resp_cap,
			   size_t *resp_len)
{
	const struct virtio_gpu_set_scanout *c = cmd;
	struct virtio_backend_gpu_resource *res;

	if (cmd_len < sizeof(*c))
		return -EINVAL;
	if (c->scanout_id >= gpu->max_outputs)
		return gpu_response_nodata(&c->hdr,
					   VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID,
					   resp, resp_cap, resp_len);

	if (!c->resource_id) {
		gpu_disable_scanout(gpu, c->scanout_id);
		return gpu_response_nodata(&c->hdr, VIRTIO_GPU_RESP_OK_NODATA,
					   resp, resp_cap, resp_len);
	}

	res = gpu_find_resource(gpu, c->resource_id);
	if (!res)
		return gpu_response_nodata(&c->hdr,
					   VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
					   resp, resp_cap, resp_len);
	if (!gpu_rect_valid(res->width, res->height, &c->r))
		return gpu_response_nodata(&c->hdr,
					   VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
					   resp, resp_cap, resp_len);

	gpu_disable_scanout(gpu, c->scanout_id);
	res->scanout_mask |= 1U << c->scanout_id;
	gpu->scanouts[c->scanout_id].resource_id = c->resource_id;
	gpu->scanouts[c->scanout_id].x = c->r.x;
	gpu->scanouts[c->scanout_id].y = c->r.y;
	gpu->scanouts[c->scanout_id].width = c->r.width;
	gpu->scanouts[c->scanout_id].height = c->r.height;

	if (gpu->scanout_update)
		gpu->scanout_update(gpu->opaque, c->scanout_id, res->pixels,
				     res->width, res->height, res->stride,
				     c->r.x, c->r.y, c->r.width, c->r.height);
	if (gpu->ui)
		(void)virtio_backend_ui_update(gpu->ui, res->pixels,
					       res->width, res->height,
					       res->stride, c->r.x, c->r.y,
					       c->r.width, c->r.height);

	return gpu_response_nodata(&c->hdr, VIRTIO_GPU_RESP_OK_NODATA,
				   resp, resp_cap, resp_len);
}

static int gpu_resource_flush(struct virtio_backend_gpu *gpu, const void *cmd,
			      size_t cmd_len, void *resp, size_t resp_cap,
			      size_t *resp_len)
{
	const struct virtio_gpu_resource_flush *c = cmd;
	struct virtio_backend_gpu_resource *res;
	uint32_t i;

	if (cmd_len < sizeof(*c))
		return -EINVAL;

	res = gpu_find_resource(gpu, c->resource_id);
	if (!res)
		return gpu_response_nodata(&c->hdr,
					   VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
					   resp, resp_cap, resp_len);
	if (!gpu_rect_valid(res->width, res->height, &c->r))
		return gpu_response_nodata(&c->hdr,
					   VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
					   resp, resp_cap, resp_len);

	if (gpu->scanout_update) {
		for (i = 0; i < gpu->max_outputs; i++) {
			if (res->scanout_mask & (1U << i)) {
				gpu->scanout_update(gpu->opaque, i, res->pixels,
						     res->width, res->height,
						     res->stride, c->r.x,
						     c->r.y, c->r.width,
						     c->r.height);
			}
		}
	}
	if (gpu->ui && res->scanout_mask)
		(void)virtio_backend_ui_update(gpu->ui, res->pixels,
					       res->width, res->height,
					       res->stride, c->r.x, c->r.y,
					       c->r.width, c->r.height);

	return gpu_response_nodata(&c->hdr, VIRTIO_GPU_RESP_OK_NODATA,
				   resp, resp_cap, resp_len);
}

static int virtio_backend_gpu_process_cmd(struct virtio_backend *backend,
					  const void *cmd, size_t cmd_len,
					  void *resp, size_t resp_cap,
					  size_t *resp_len)
{
	struct virtio_backend_gpu *gpu = backend->dev;
	const struct virtio_gpu_ctrl_hdr *hdr = cmd;

	if (cmd_len < sizeof(*hdr))
		return -EINVAL;

	switch (hdr->type) {
	case VIRTIO_GPU_CMD_GET_DISPLAY_INFO:
		return gpu_get_display_info(gpu, hdr, resp, resp_cap, resp_len);
	case VIRTIO_GPU_CMD_RESOURCE_CREATE_2D:
		return gpu_resource_create_2d(gpu, cmd, cmd_len, resp,
					      resp_cap, resp_len);
	case VIRTIO_GPU_CMD_RESOURCE_UNREF:
		return gpu_resource_unref(gpu, cmd, cmd_len, resp, resp_cap,
					  resp_len);
	case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING:
		return gpu_attach_backing(gpu, cmd, cmd_len, resp, resp_cap,
					  resp_len);
	case VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING:
		return gpu_detach_backing(gpu, cmd, cmd_len, resp, resp_cap,
					  resp_len);
	case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D:
		return gpu_transfer_to_host_2d(gpu, cmd, cmd_len, resp,
					       resp_cap, resp_len);
	case VIRTIO_GPU_CMD_SET_SCANOUT:
		return gpu_set_scanout(gpu, cmd, cmd_len, resp, resp_cap,
				       resp_len);
	case VIRTIO_GPU_CMD_RESOURCE_FLUSH:
		return gpu_resource_flush(gpu, cmd, cmd_len, resp, resp_cap,
					  resp_len);
	case VIRTIO_GPU_CMD_UPDATE_CURSOR:
	case VIRTIO_GPU_CMD_MOVE_CURSOR:
		return gpu_response_nodata(hdr, VIRTIO_GPU_RESP_OK_NODATA,
					   resp, resp_cap, resp_len);
	default:
		return gpu_response_nodata(hdr, VIRTIO_GPU_RESP_ERR_UNSPEC,
					   resp, resp_cap, resp_len);
	}
}

static int virtio_backend_gpu_write(struct virtio_backend *backend,
				    const struct virtio_backend_io *io)
{
	if (io->type != VIRTIO_BACKEND_IO_GPU_CMD ||
	    !io->buf || !io->u.gpu.resp || !io->u.gpu.resp_len)
		return -EINVAL;

	return virtio_backend_gpu_process_cmd(backend, io->buf, io->len,
					      io->u.gpu.resp, io->cap,
					      io->u.gpu.resp_len);
}

static void virtio_backend_gpu_destroy(struct virtio_backend *backend)
{
	struct virtio_backend_gpu *gpu = backend->dev;
	struct virtio_backend_gpu_resource *res;

	if (!gpu)
		return;

	res = gpu->resources;
	while (res) {
		struct virtio_backend_gpu_resource *next = res->next;

		free(res->backing);
		free(res->pixels);
		free(res);
		res = next;
	}
	free(gpu);
	backend->dev = NULL;
}

static const struct virtio_backend_ops virtio_backend_gpu_ops = {
	.write = virtio_backend_gpu_write,
	.destroy = virtio_backend_gpu_destroy,
};

int virtio_backend_gpu_create(struct virtio_backend *backend,
			      const struct virtio_backend_config *config)
{
	struct virtio_backend_gpu *gpu;

	if (config->u.gpu.max_outputs > VIRTIO_GPU_MAX_SCANOUTS)
		return -EINVAL;

	gpu = calloc(1, sizeof(*gpu));
	if (!gpu)
		return -ENOMEM;

	gpu->width = config->u.gpu.width ? config->u.gpu.width :
					     GPU_DEFAULT_WIDTH;
	gpu->height = config->u.gpu.height ? config->u.gpu.height :
					      GPU_DEFAULT_HEIGHT;
	gpu->max_outputs = config->u.gpu.max_outputs ?
			   config->u.gpu.max_outputs : 1;
	gpu->ui = config->u.gpu.ui;
	gpu->guest_read = config->u.gpu.guest_read;
	gpu->scanout_update = config->u.gpu.scanout_update;
	gpu->scanout_disable = config->u.gpu.scanout_disable;
	gpu->opaque = config->u.gpu.opaque;

	backend->dev = gpu;
	backend->ops = &virtio_backend_gpu_ops;
	return 0;
}
