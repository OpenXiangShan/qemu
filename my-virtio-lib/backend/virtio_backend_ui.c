#include "virtio_backend_internal.h"

#include <errno.h>

static struct virtio_backend_ui *
virtio_backend_get_ui(virtio_backend_handle_t handle)
{
	struct virtio_backend *backend = handle;

	if (!backend || backend->type != VIRTIO_BACKEND_UI)
		return NULL;

	return backend->dev;
}

int virtio_backend_ui_update(virtio_backend_handle_t handle,
			     const void *pixels, uint32_t width,
			     uint32_t height, uint32_t stride, uint32_t x,
			     uint32_t y, uint32_t w, uint32_t h)
{
	struct virtio_backend_ui *ui = virtio_backend_get_ui(handle);

	if (!ui || !ui->ops || !ui->ops->update)
		return -ENOTSUP;
	return ui->ops->update(ui, pixels, width, height, stride, x, y, w, h);
}

void virtio_backend_ui_disable(virtio_backend_handle_t handle)
{
	struct virtio_backend_ui *ui = virtio_backend_get_ui(handle);

	if (!ui || !ui->ops || !ui->ops->disable)
		return;
	ui->ops->disable(ui);
}

int virtio_backend_ui_read_input(virtio_backend_handle_t handle,
				 enum virtio_backend_input_profile profile,
				 struct virtio_backend_input_event *event,
				 int block)
{
	struct virtio_backend_ui *ui = virtio_backend_get_ui(handle);

	if (!ui || !ui->ops || !ui->ops->read_input)
		return -ENOTSUP;
	return ui->ops->read_input(ui, profile, event, block);
}
