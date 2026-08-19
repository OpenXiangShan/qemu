#include "virtio_backend_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#include <sys/ioctl.h>
#endif

#define VIRTIO_BACKEND_BLK_SECTOR_SIZE 512ULL

struct virtio_backend_blk {
	int fd;
	char *image_path;
	int64_t capacity;
};

static int blk_read_write(struct virtio_backend_blk *blk,
			  const struct virtio_backend_io *io, int write_io)
{
	const uint8_t *wbuf = io->buf;
	uint8_t *rbuf = io->buf;
	size_t done = 0;
	off_t off;

	if (!io->buf && io->len)
		return -EINVAL;

	off = (off_t)(io->u.blk.sector * VIRTIO_BACKEND_BLK_SECTOR_SIZE);
	while (done < io->len) {
		ssize_t n;

		if (write_io)
			n = pwrite(blk->fd, wbuf + done,
				   io->len - done, off + done);
		else
			n = pread(blk->fd, rbuf + done,
				  io->len - done, off + done);
		if (n < 0)
			return -errno;
		if (!n)
			return -EIO;
		done += (size_t)n;
	}

	return 0;
}

static int virtio_backend_blk_write(struct virtio_backend *backend,
				    const struct virtio_backend_io *io)
{
	struct virtio_backend_blk *blk = backend->dev;

	if (io->type != VIRTIO_BACKEND_IO_BLK)
		return -EINVAL;

	if (io->u.blk.op == VIRTIO_BACKEND_BLK_WRITE)
		return blk_read_write(blk, io, 1);
	if (io->u.blk.op == VIRTIO_BACKEND_BLK_FLUSH)
		return fsync(blk->fd) < 0 ? -errno : 0;

	return -EINVAL;
}

static int virtio_backend_blk_read(struct virtio_backend *backend,
				   struct virtio_backend_io *io)
{
	struct virtio_backend_blk *blk = backend->dev;

	if (io->type != VIRTIO_BACKEND_IO_BLK ||
	    io->u.blk.op != VIRTIO_BACKEND_BLK_READ)
		return -EINVAL;

	return blk_read_write(blk, io, 0);
}

static int virtio_backend_blk_get_info(struct virtio_backend *backend,
				       struct virtio_backend_info *info)
{
	struct virtio_backend_blk *blk = backend->dev;

	info->u.blk.capacity = blk->capacity;
	return 0;
}

static void virtio_backend_blk_destroy(struct virtio_backend *backend)
{
	struct virtio_backend_blk *blk = backend->dev;

	if (!blk)
		return;

	if (blk->fd >= 0)
		close(blk->fd);
	free(blk->image_path);
	free(blk);
	backend->dev = NULL;
}

static const struct virtio_backend_ops virtio_backend_blk_ops = {
	.write = virtio_backend_blk_write,
	.read = virtio_backend_blk_read,
	.get_info = virtio_backend_blk_get_info,
	.destroy = virtio_backend_blk_destroy,
};

int virtio_backend_blk_create(struct virtio_backend *backend,
			      const struct virtio_backend_config *config)
{
	const char *path = config->u.blk.image_path;
	struct virtio_backend_blk *blk;
	struct stat st;
	uint64_t size;
	int ret;

	if (!path || !*path)
		return -EINVAL;

	blk = calloc(1, sizeof(*blk));
	if (!blk)
		return -ENOMEM;
	blk->fd = -1;

	blk->fd = open(path, O_RDWR);
	if (blk->fd < 0) {
		ret = -errno;
		goto fail;
	}

	if (fstat(blk->fd, &st) < 0) {
		ret = -errno;
		goto fail;
	}

	size = st.st_size;
#ifdef __linux__
	if (S_ISBLK(st.st_mode) && ioctl(blk->fd, BLKGETSIZE64, &size) < 0) {
		ret = -errno;
		goto fail;
	}
#endif

	blk->image_path = virtio_backend_strdup(path);
	if (!blk->image_path) {
		ret = -ENOMEM;
		goto fail;
	}
	blk->capacity = size / VIRTIO_BACKEND_BLK_SECTOR_SIZE;

	backend->dev = blk;
	backend->ops = &virtio_backend_blk_ops;
	return 0;

fail:
	if (blk->fd >= 0)
		close(blk->fd);
	free(blk->image_path);
	free(blk);
	return ret;
}
