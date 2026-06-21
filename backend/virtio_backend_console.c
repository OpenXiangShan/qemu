#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "virtio_backend_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <termios.h>
#include <unistd.h>

struct virtio_backend_console {
	struct virtio_backend_queue rxq;
	enum virtio_backend_console_backend backend_type;
	int (*host_write)(void *opaque, const uint8_t *buf, size_t len);
	void *host_opaque;
	int input_fd;
	int output_fd;
	int close_input_fd;
	int close_output_fd;
	int wake_fd;
	int input_thread_started;
	int stop_input_thread;
	pthread_t input_thread;
	int pty_slave_fd;
	char *pty_path;
};

static void wake_console_input_thread(struct virtio_backend_console *console)
{
	if (console->wake_fd >= 0) {
		uint64_t one = 1;

		(void)write(console->wake_fd, &one, sizeof(one));
	}
}

static int write_all_fd(int fd, const uint8_t *buf, size_t len)
{
	size_t done = 0;

	if (fd < 0 || (!buf && len))
		return -EINVAL;

	while (done < len) {
		ssize_t n = write(fd, buf + done, len - done);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		if (!n)
			return -EIO;
		done += (size_t)n;
	}

	return (int)len;
}

static int set_fd_nonblock(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags < 0)
		return -errno;
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		return -errno;

	return 0;
}

static int open_console_pty(struct virtio_backend_console *console)
{
	char path[128];
	struct termios tio;
	int fd;
	int ret;

	fd = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	if (grantpt(fd) < 0) {
		ret = -errno;
		close(fd);
		return ret;
	}
	if (unlockpt(fd) < 0) {
		ret = -errno;
		close(fd);
		return ret;
	}
	if (ptsname_r(fd, path, sizeof(path)) < 0) {
		ret = -errno;
		close(fd);
		return ret;
	}

	console->pty_slave_fd = open(path, O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (console->pty_slave_fd < 0) {
		ret = -errno;
		close(fd);
		return ret;
	}
	if (tcgetattr(console->pty_slave_fd, &tio) < 0) {
		ret = -errno;
		close(console->pty_slave_fd);
		console->pty_slave_fd = -1;
		close(fd);
		return ret;
	}
	cfmakeraw(&tio);
	if (tcsetattr(console->pty_slave_fd, TCSANOW, &tio) < 0) {
		ret = -errno;
		close(console->pty_slave_fd);
		console->pty_slave_fd = -1;
		close(fd);
		return ret;
	}

	console->pty_path = virtio_backend_strdup(path);
	if (!console->pty_path) {
		close(console->pty_slave_fd);
		console->pty_slave_fd = -1;
		close(fd);
		return -ENOMEM;
	}

	console->input_fd = fd;
	console->output_fd = fd;
	console->close_input_fd = 1;
	console->close_output_fd = 0;
	return 0;
}

static void *console_input_thread_main(void *arg)
{
	struct virtio_backend *backend = arg;
	struct virtio_backend_console *console = backend->dev;
	struct pollfd fds[2];

	fds[0].fd = console->input_fd;
	fds[0].events = POLLIN | POLLERR | POLLHUP;
	fds[1].fd = console->wake_fd;
	fds[1].events = POLLIN;

	while (!console->stop_input_thread) {
		uint8_t buf[4096];
		int nfds = fds[1].fd >= 0 ? 2 : 1;
		int ret;

		fds[0].revents = 0;
		fds[1].revents = 0;
		ret = poll(fds, (nfds_t)nfds, -1);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			virtio_backend_event(backend, VIRTIO_BACKEND_EVENT_ERROR);
			break;
		}

		if (nfds > 1 && (fds[1].revents & POLLIN)) {
			uint64_t val;

			while (read(fds[1].fd, &val, sizeof(val)) ==
			       sizeof(val)) {
			}
		}
		if (console->stop_input_thread)
			break;

		if (fds[0].revents & POLLIN) {
			ssize_t n;

			do {
				n = read(fds[0].fd, buf, sizeof(buf));
			} while (n < 0 && errno == EINTR);

			if (n > 0) {
				ret = virtio_backend_queue_push(&console->rxq,
								buf, (size_t)n, 1);
				if (!ret) {
					virtio_backend_event(
						backend,
						VIRTIO_BACKEND_EVENT_READABLE);
				} else if (ret != -ESHUTDOWN) {
					virtio_backend_event(
						backend,
						VIRTIO_BACKEND_EVENT_ERROR);
				}
			} else if (!n || (errno != EAGAIN && errno != EWOULDBLOCK)) {
				virtio_backend_event(backend,
						     VIRTIO_BACKEND_EVENT_ERROR);
				break;
			}
		}
		if (fds[0].revents & (POLLERR | POLLHUP)) {
			virtio_backend_event(backend, VIRTIO_BACKEND_EVENT_ERROR);
			break;
		}
	}

	return NULL;
}

static int create_console_input_thread(struct virtio_backend *backend)
{
	struct virtio_backend_console *console = backend->dev;
	int ret;

	if (console->input_fd < 0)
		return 0;

	ret = set_fd_nonblock(console->input_fd);
	if (ret < 0)
		return ret;

	console->wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (console->wake_fd < 0)
		return -errno;

	ret = pthread_create(&console->input_thread, NULL,
			     console_input_thread_main, backend);
	if (ret)
		return -ret;

	console->input_thread_started = 1;
	return 0;
}

static int virtio_backend_console_write(struct virtio_backend *backend,
					const struct virtio_backend_io *io)
{
	struct virtio_backend_console *console = backend->dev;

	if (io->type != VIRTIO_BACKEND_IO_STREAM)
		return -EINVAL;

	if (console->backend_type != VIRTIO_BACKEND_CONSOLE_EXTERNAL)
		return write_all_fd(console->output_fd, io->buf, io->len);
	if (!console->host_write)
		return (int)io->len;
	return console->host_write(console->host_opaque, io->buf, io->len);
}

static int virtio_backend_console_read(struct virtio_backend *backend,
				       struct virtio_backend_io *io)
{
	struct virtio_backend_console *console = backend->dev;
	int ret;

	if (io->type != VIRTIO_BACKEND_IO_STREAM)
		return -EINVAL;

	ret = virtio_backend_queue_peek(&console->rxq, io);
	if (!ret && io->len > 0)
		return (int)io->len;
	return ret;
}

static int virtio_backend_console_read_done(struct virtio_backend *backend,
					    uint64_t token, int consumed)
{
	struct virtio_backend_console *console = backend->dev;

	return virtio_backend_queue_done(&console->rxq, token, consumed);
}

static int virtio_backend_console_push_readable(struct virtio_backend *backend,
						const void *buf, size_t len)
{
	struct virtio_backend_console *console = backend->dev;
	int ret;

	ret = virtio_backend_queue_push(&console->rxq, buf, len, 0);
	if (!ret)
		virtio_backend_event(backend, VIRTIO_BACKEND_EVENT_READABLE);
	return ret;
}

static int virtio_backend_console_get_info(struct virtio_backend *backend,
					   struct virtio_backend_info *info)
{
	struct virtio_backend_console *console = backend->dev;

	info->u.console.backend = console->backend_type;
	if (console->backend_type == VIRTIO_BACKEND_CONSOLE_PTY)
		info->u.console.pty_path = console->pty_path;
	return 0;
}

static void virtio_backend_console_destroy(struct virtio_backend *backend)
{
	struct virtio_backend_console *console = backend->dev;

	if (!console)
		return;

	console->stop_input_thread = 1;
	virtio_backend_queue_stop(&console->rxq);
	wake_console_input_thread(console);
	if (console->input_thread_started)
		pthread_join(console->input_thread, NULL);
	if (console->wake_fd >= 0)
		close(console->wake_fd);
	if (console->close_input_fd && console->input_fd >= 0)
		close(console->input_fd);
	if (console->close_output_fd && console->output_fd >= 0)
		close(console->output_fd);
	if (console->pty_slave_fd >= 0)
		close(console->pty_slave_fd);
	free(console->pty_path);
	virtio_backend_queue_destroy(&console->rxq);
	free(console);
	backend->dev = NULL;
}

static const struct virtio_backend_ops virtio_backend_console_ops = {
	.write = virtio_backend_console_write,
	.read = virtio_backend_console_read,
	.read_done = virtio_backend_console_read_done,
	.push_readable = virtio_backend_console_push_readable,
	.get_info = virtio_backend_console_get_info,
	.destroy = virtio_backend_console_destroy,
};

int virtio_backend_console_create(struct virtio_backend *backend,
				  const struct virtio_backend_config *config)
{
	struct virtio_backend_console *console;
	int ret;

	console = calloc(1, sizeof(*console));
	if (!console)
		return -ENOMEM;

	console->backend_type = config->u.console.backend;
	console->input_fd = -1;
	console->output_fd = -1;
	console->wake_fd = -1;
	console->pty_slave_fd = -1;

	ret = virtio_backend_queue_init(&console->rxq, 0);
	if (ret < 0) {
		ret = -ENOMEM;
		goto fail;
	}

	switch (console->backend_type) {
	case VIRTIO_BACKEND_CONSOLE_EXTERNAL:
		console->host_write = config->u.console.host_write;
		console->host_opaque = config->u.console.host_opaque;
		break;
	case VIRTIO_BACKEND_CONSOLE_STDIO:
		console->input_fd = STDIN_FILENO;
		console->output_fd = STDOUT_FILENO;
		break;
	case VIRTIO_BACKEND_CONSOLE_FD:
		console->input_fd = config->u.console.input_fd;
		console->output_fd = config->u.console.output_fd;
		console->close_input_fd = config->u.console.close_fds;
		console->close_output_fd = config->u.console.close_fds &&
			config->u.console.output_fd != config->u.console.input_fd;
		break;
	case VIRTIO_BACKEND_CONSOLE_PTY:
		ret = open_console_pty(console);
		if (ret < 0)
			goto fail;
		break;
	default:
		ret = -EINVAL;
		goto fail;
	}

	if (console->backend_type != VIRTIO_BACKEND_CONSOLE_EXTERNAL &&
	    console->output_fd < 0) {
		ret = -EINVAL;
		goto fail;
	}

	backend->dev = console;
	backend->ops = &virtio_backend_console_ops;

	ret = create_console_input_thread(backend);
	if (ret < 0)
		goto fail;

	return 0;

fail:
	if (backend->dev == console && backend->ops)
		virtio_backend_console_destroy(backend);
	else {
		if (console->pty_slave_fd >= 0)
			close(console->pty_slave_fd);
		if (console->close_input_fd && console->input_fd >= 0)
			close(console->input_fd);
		if (console->close_output_fd && console->output_fd >= 0)
			close(console->output_fd);
		free(console->pty_path);
		virtio_backend_queue_destroy(&console->rxq);
		free(console);
	}
	return ret;
}
