#include "virtio_backend_internal.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

char *virtio_backend_strdup(const char *s)
{
	size_t len;
	char *copy;

	if (!s)
		s = "";

	len = strlen(s) + 1;
	copy = malloc(len);
	if (copy)
		memcpy(copy, s, len);

	return copy;
}

void virtio_backend_log(struct virtio_backend *backend, int level,
			const char *fmt, ...)
{
	va_list ap;

	if (!backend || !backend->callbacks.log)
		return;

	va_start(ap, fmt);
	backend->callbacks.log(backend->callback_opaque, level, fmt, ap);
	va_end(ap);
}

void virtio_backend_event(struct virtio_backend *backend, unsigned int events)
{
	if (backend && backend->callbacks.event)
		backend->callbacks.event(backend->callback_opaque, backend,
					 events);
}

virtio_backend_handle_t
virtio_backend_create(const struct virtio_backend_config *config)
{
	struct virtio_backend *backend;
	int ret;

	if (!config)
		return NULL;

	backend = calloc(1, sizeof(*backend));
	if (!backend)
		return NULL;

	backend->type = config->type;
	if (config->callbacks)
		backend->callbacks = *config->callbacks;
	backend->callback_opaque = config->callback_opaque;

	switch (config->type) {
	case VIRTIO_BACKEND_BLK:
		ret = virtio_backend_blk_create(backend, config);
		break;
	case VIRTIO_BACKEND_NET:
		ret = virtio_backend_net_create(backend, config);
		break;
	case VIRTIO_BACKEND_CONSOLE:
		ret = virtio_backend_console_create(backend, config);
		break;
	case VIRTIO_BACKEND_GPU:
		ret = virtio_backend_gpu_create(backend, config);
		break;
	case VIRTIO_BACKEND_INPUT:
		ret = virtio_backend_input_create(backend, config);
		break;
	case VIRTIO_BACKEND_UI:
		ret = virtio_backend_ui_create(backend, config);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret < 0) {
		virtio_backend_log(backend, 0,
				   "create backend type=%d failed ret=%d",
				   config->type, ret);
		virtio_backend_destroy(backend);
		return NULL;
	}

	return backend;
}

void virtio_backend_destroy(virtio_backend_handle_t handle)
{
	struct virtio_backend *backend = handle;

	if (!backend)
		return;

	if (backend->ops && backend->ops->destroy)
		backend->ops->destroy(backend);

	free(backend);
}

int virtio_backend_write(virtio_backend_handle_t handle,
			 const struct virtio_backend_io *io)
{
	struct virtio_backend *backend = handle;

	if (!backend || !io)
		return -EINVAL;
	if (!backend->ops || !backend->ops->write)
		return -ENOTSUP;

	return backend->ops->write(backend, io);
}

int virtio_backend_read(virtio_backend_handle_t handle,
			struct virtio_backend_io *io)
{
	struct virtio_backend *backend = handle;

	if (!backend || !io)
		return -EINVAL;
	if (!backend->ops || !backend->ops->read)
		return -ENOTSUP;

	return backend->ops->read(backend, io);
}

int virtio_backend_read_done(virtio_backend_handle_t handle,
			     uint64_t token, int consumed)
{
	struct virtio_backend *backend = handle;

	if (!backend)
		return -EINVAL;
	if (!backend->ops || !backend->ops->read_done)
		return -ENOTSUP;

	return backend->ops->read_done(backend, token, consumed);
}

int virtio_backend_push_readable(virtio_backend_handle_t handle,
				 const void *buf, size_t len)
{
	struct virtio_backend *backend = handle;

	if (!backend)
		return -EINVAL;
	if (!backend->ops || !backend->ops->push_readable)
		return -ENOTSUP;

	return backend->ops->push_readable(backend, buf, len);
}

int virtio_backend_get_info(virtio_backend_handle_t handle,
			    struct virtio_backend_info *info)
{
	struct virtio_backend *backend = handle;

	if (!backend || !info)
		return -EINVAL;

	memset(info, 0, sizeof(*info));
	info->type = backend->type;

	if (!backend->ops || !backend->ops->get_info)
		return 0;

	return backend->ops->get_info(backend, info);
}
