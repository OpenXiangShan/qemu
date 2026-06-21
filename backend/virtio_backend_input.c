#include "virtio_backend_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <unistd.h>

#define VIRTIO_BACKEND_INPUT_QUEUE_DEPTH 256
#define VIRTIO_BACKEND_INPUT_MAX_EVDEV_FDS 32
#define VIRTIO_BACKEND_INPUT_UI_POLL_MS 10

struct virtio_backend_input_fd {
	int fd;
	char *path;
};

struct virtio_backend_input {
	struct virtio_backend_queue eventq;
	enum virtio_backend_input_profile profile;
	enum virtio_backend_input_source source;
	virtio_backend_handle_t ui;
	uint8_t led_state;
	struct virtio_backend_input_fd evdev[VIRTIO_BACKEND_INPUT_MAX_EVDEV_FDS];
	unsigned int evdev_count;
	int wake_fd;
	int input_thread_started;
	int stop_input_thread;
	pthread_t input_thread;
};

static int test_bit(const unsigned long *bits, unsigned int bit)
{
	return !!(bits[bit / (8 * sizeof(*bits))] &
		  (1UL << (bit % (8 * sizeof(*bits)))));
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

static int input_queue_event(struct virtio_backend *backend,
			     const struct virtio_backend_input_event *event,
			     int block)
{
	struct virtio_backend_input *input = backend->dev;
	int ret;

	ret = virtio_backend_queue_push(&input->eventq, event, sizeof(*event),
					block);
	if (!ret)
		virtio_backend_event(backend, VIRTIO_BACKEND_EVENT_READABLE);
	return ret;
}

static int keyboard_key_code_allowed(uint16_t code)
{
	return (code >= KEY_ESC && code <= KEY_KPDOT) ||
	       (code >= KEY_ZENKAKUHANKAKU && code <= KEY_F12) ||
	       (code >= KEY_KPENTER && code <= KEY_RIGHTALT) ||
	       (code >= KEY_HOME && code <= KEY_DELETE) ||
	       code == KEY_PAUSE ||
	       code == KEY_LEFTMETA ||
	       code == KEY_RIGHTMETA ||
	       code == KEY_MENU ||
	       (code >= KEY_F13 && code <= KEY_F24);
}

static int mouse_key_code_allowed(uint16_t code)
{
	return code == BTN_LEFT ||
	       code == BTN_RIGHT ||
	       code == BTN_MIDDLE ||
	       code == BTN_SIDE ||
	       code == BTN_EXTRA;
}

static int mouse_rel_code_allowed(uint16_t code)
{
	return code == REL_X ||
	       code == REL_Y ||
	       code == REL_WHEEL;
}

static int tablet_abs_code_allowed(uint16_t code)
{
	return code == ABS_X ||
	       code == ABS_Y;
}

static int tablet_rel_code_allowed(uint16_t code)
{
	return code == REL_WHEEL;
}

static int input_event_allowed(struct virtio_backend_input *input,
			       const struct input_event *event)
{
	if (event->type == EV_SYN)
		return 1;

	switch (input->profile) {
	case VIRTIO_BACKEND_INPUT_KEYBOARD:
		return event->type == EV_KEY &&
		       keyboard_key_code_allowed(event->code);
	case VIRTIO_BACKEND_INPUT_MOUSE:
		return (event->type == EV_KEY &&
			mouse_key_code_allowed(event->code)) ||
		       (event->type == EV_REL &&
			mouse_rel_code_allowed(event->code));
	case VIRTIO_BACKEND_INPUT_TABLET:
		return (event->type == EV_KEY &&
			mouse_key_code_allowed(event->code)) ||
		       (event->type == EV_REL &&
			tablet_rel_code_allowed(event->code)) ||
		       (event->type == EV_ABS &&
			tablet_abs_code_allowed(event->code));
	default:
		return 0;
	}
}

static int queue_evdev_event(struct virtio_backend *backend,
			     const struct input_event *event)
{
	struct virtio_backend_input *input = backend->dev;
	struct virtio_backend_input_event backend_event;

	if (!input_event_allowed(input, event))
		return 0;

	backend_event.type = event->type;
	backend_event.code = event->code;
	backend_event.value = event->value;
	return input_queue_event(backend, &backend_event, 1);
}

static int evdev_has_cap(int fd, enum virtio_backend_input_profile profile)
{
	unsigned long ev_bits[(EV_MAX + 8 * sizeof(unsigned long)) /
			      (8 * sizeof(unsigned long))];
	unsigned long key_bits[(KEY_MAX + 8 * sizeof(unsigned long)) /
			       (8 * sizeof(unsigned long))];
	unsigned long rel_bits[(REL_MAX + 8 * sizeof(unsigned long)) /
			       (8 * sizeof(unsigned long))];
	unsigned long abs_bits[(ABS_MAX + 8 * sizeof(unsigned long)) /
			       (8 * sizeof(unsigned long))];

	memset(ev_bits, 0, sizeof(ev_bits));
	memset(key_bits, 0, sizeof(key_bits));
	memset(rel_bits, 0, sizeof(rel_bits));
	memset(abs_bits, 0, sizeof(abs_bits));

	if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0)
		return 0;

	if (profile == VIRTIO_BACKEND_INPUT_KEYBOARD) {
		if (!test_bit(ev_bits, EV_KEY))
			return 0;
		if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)),
			  key_bits) < 0)
			return 0;
		return test_bit(key_bits, KEY_A) ||
		       test_bit(key_bits, KEY_ENTER) ||
		       test_bit(key_bits, KEY_ESC);
	}

	if (profile == VIRTIO_BACKEND_INPUT_TABLET) {
		return test_bit(ev_bits, EV_ABS) &&
		       ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)),
			     abs_bits) >= 0 &&
		       test_bit(abs_bits, ABS_X) &&
		       test_bit(abs_bits, ABS_Y);
	}

	if (profile != VIRTIO_BACKEND_INPUT_MOUSE)
		return 0;

	if (!test_bit(ev_bits, EV_REL) && !test_bit(ev_bits, EV_KEY))
		return 0;
	if (test_bit(ev_bits, EV_REL) &&
	    ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel_bits)), rel_bits) >= 0 &&
	    (test_bit(rel_bits, REL_X) || test_bit(rel_bits, REL_Y) ||
	     test_bit(rel_bits, REL_WHEEL))) {
		return 1;
	}
	if (test_bit(ev_bits, EV_KEY) &&
	    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) >= 0 &&
	    (test_bit(key_bits, BTN_LEFT) || test_bit(key_bits, BTN_RIGHT) ||
	     test_bit(key_bits, BTN_MIDDLE))) {
		return 1;
	}

	return 0;
}

static int open_evdev_one(struct virtio_backend_input *input, const char *path)
{
	int fd;
	int ret;

	if (input->evdev_count >= VIRTIO_BACKEND_INPUT_MAX_EVDEV_FDS)
		return -ENOSPC;

	fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return -errno;

	if (!evdev_has_cap(fd, input->profile)) {
		close(fd);
		return -ENODEV;
	}

	ret = set_fd_nonblock(fd);
	if (ret < 0) {
		close(fd);
		return ret;
	}

	input->evdev[input->evdev_count].path = virtio_backend_strdup(path);
	if (!input->evdev[input->evdev_count].path) {
		close(fd);
		return -ENOMEM;
	}
	input->evdev[input->evdev_count].fd = fd;
	input->evdev_count++;
	return 0;
}

static int scan_evdev(struct virtio_backend_input *input)
{
	DIR *dir;
	struct dirent *de;

	dir = opendir("/dev/input");
	if (!dir)
		return -errno;

	while ((de = readdir(dir)) != NULL &&
	       input->evdev_count < VIRTIO_BACKEND_INPUT_MAX_EVDEV_FDS) {
		char path[PATH_MAX];

		if (strncmp(de->d_name, "event", 5))
			continue;
		if (snprintf(path, sizeof(path), "/dev/input/%s",
			     de->d_name) >= (int)sizeof(path))
			continue;
		(void)open_evdev_one(input, path);
	}

	closedir(dir);
	return input->evdev_count ? 0 : -ENODEV;
}

static void close_evdev_fds(struct virtio_backend_input *input)
{
	unsigned int i;

	for (i = 0; i < input->evdev_count; i++) {
		if (input->evdev[i].fd >= 0)
			close(input->evdev[i].fd);
		free(input->evdev[i].path);
		input->evdev[i].fd = -1;
		input->evdev[i].path = NULL;
	}
	input->evdev_count = 0;
}

static void wake_input_thread(struct virtio_backend_input *input)
{
	if (input->wake_fd >= 0) {
		uint64_t one = 1;

		(void)write(input->wake_fd, &one, sizeof(one));
	}
}

static void *input_thread_main(void *arg)
{
	struct virtio_backend *backend = arg;
	struct virtio_backend_input *input = backend->dev;
	struct pollfd fds[VIRTIO_BACKEND_INPUT_MAX_EVDEV_FDS + 1];
	unsigned int i;

	if (input->source == VIRTIO_BACKEND_INPUT_SOURCE_UI) {
		while (!input->stop_input_thread) {
			struct virtio_backend_input_event event;
			struct pollfd wake = {
				.fd = input->wake_fd,
				.events = POLLIN,
			};
			int ret;

			ret = virtio_backend_ui_read_input(input->ui,
							  input->profile,
							  &event, 0);
			if (ret == (int)sizeof(event)) {
				ret = input_queue_event(backend, &event, 1);
				if (ret && ret != -ESHUTDOWN) {
					virtio_backend_event(
						backend,
						VIRTIO_BACKEND_EVENT_ERROR);
				}
				continue;
			}
			if (ret == -ESHUTDOWN)
				break;
			if (ret == -EINTR || ret == -EAGAIN)
				(void)poll(&wake, 1,
					   VIRTIO_BACKEND_INPUT_UI_POLL_MS);
			if (wake.revents & POLLIN) {
				uint64_t val;

				while (read(input->wake_fd, &val,
					    sizeof(val)) == sizeof(val)) {
				}
			}
			if (ret == -EINTR || ret == -EAGAIN)
				continue;
			virtio_backend_event(backend, VIRTIO_BACKEND_EVENT_ERROR);
			break;
		}

		return NULL;
	}

	for (i = 0; i < input->evdev_count; i++) {
		fds[i].fd = input->evdev[i].fd;
		fds[i].events = POLLIN | POLLERR | POLLHUP;
	}
	fds[input->evdev_count].fd = input->wake_fd;
	fds[input->evdev_count].events = POLLIN;

	while (!input->stop_input_thread) {
		int nfds = (int)input->evdev_count +
			   (input->wake_fd >= 0 ? 1 : 0);
		int ret;

		for (i = 0; i < input->evdev_count; i++)
			fds[i].revents = 0;
		fds[input->evdev_count].revents = 0;

		ret = poll(fds, (nfds_t)nfds, -1);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			virtio_backend_event(backend, VIRTIO_BACKEND_EVENT_ERROR);
			break;
		}

		if (input->wake_fd >= 0 &&
		    (fds[input->evdev_count].revents & POLLIN)) {
			uint64_t val;

			while (read(input->wake_fd, &val, sizeof(val)) ==
			       sizeof(val)) {
			}
		}
		if (input->stop_input_thread)
			break;

		for (i = 0; i < input->evdev_count; i++) {
			if (fds[i].revents & POLLIN) {
				for (;;) {
					struct input_event event;
					ssize_t n;

					n = read(fds[i].fd, &event,
						 sizeof(event));
					if (n == sizeof(event)) {
						ret = queue_evdev_event(backend,
									&event);
						if (ret && ret != -ESHUTDOWN) {
							virtio_backend_event(
								backend,
								VIRTIO_BACKEND_EVENT_ERROR);
						}
						continue;
					}
					if (n < 0 && (errno == EAGAIN ||
						      errno == EWOULDBLOCK ||
						      errno == EINTR)) {
						if (errno == EINTR)
							continue;
						break;
					}
					virtio_backend_event(
						backend,
						VIRTIO_BACKEND_EVENT_ERROR);
					break;
				}
			}
			if (fds[i].revents & (POLLERR | POLLHUP))
				virtio_backend_event(backend,
						     VIRTIO_BACKEND_EVENT_ERROR);
		}
	}

	return NULL;
}

static int create_input_thread(struct virtio_backend *backend)
{
	struct virtio_backend_input *input = backend->dev;
	int ret;

	if (input->source != VIRTIO_BACKEND_INPUT_SOURCE_EVDEV &&
	    input->source != VIRTIO_BACKEND_INPUT_SOURCE_UI)
		return 0;

	if (input->source == VIRTIO_BACKEND_INPUT_SOURCE_EVDEV &&
	    !input->evdev_count)
		return -ENODEV;
	if (input->source == VIRTIO_BACKEND_INPUT_SOURCE_UI && !input->ui)
		return -EINVAL;

	if (input->source == VIRTIO_BACKEND_INPUT_SOURCE_EVDEV ||
	    input->source == VIRTIO_BACKEND_INPUT_SOURCE_UI) {
		input->wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
		if (input->wake_fd < 0)
			return -errno;
	}

	ret = pthread_create(&input->input_thread, NULL, input_thread_main,
			     backend);
	if (ret)
		return -ret;

	input->input_thread_started = 1;
	return 0;
}

static int virtio_backend_input_write(struct virtio_backend *backend,
				      const struct virtio_backend_io *io)
{
	struct virtio_backend_input *input = backend->dev;
	struct virtio_backend_input_event *event;

	if (io->type != VIRTIO_BACKEND_IO_INPUT_EVENT ||
	    io->len != sizeof(*event) || !io->buf) {
		return -EINVAL;
	}

	event = io->buf;
	if (input->profile == VIRTIO_BACKEND_INPUT_KEYBOARD &&
	    event->type == EV_LED) {
		uint8_t mask = 0;

		switch (event->code) {
		case LED_NUML:
			mask = 1U << 0;
			break;
		case LED_CAPSL:
			mask = 1U << 1;
			break;
		case LED_SCROLLL:
			mask = 1U << 2;
			break;
		default:
			break;
		}

		if (mask) {
			if (event->value) {
				input->led_state |= mask;
			} else {
				input->led_state &= (uint8_t)~mask;
			}
		}
	}

	return (int)io->len;
}

static int virtio_backend_input_read(struct virtio_backend *backend,
				     struct virtio_backend_io *io)
{
	struct virtio_backend_input *input = backend->dev;
	int ret;

	if (io->type != VIRTIO_BACKEND_IO_INPUT_EVENT)
		return -EINVAL;

	ret = virtio_backend_queue_peek(&input->eventq, io);
	if (!ret && io->len > 0)
		return (int)io->len;
	return ret;
}

static int virtio_backend_input_read_done(struct virtio_backend *backend,
					  uint64_t token, int consumed)
{
	struct virtio_backend_input *input = backend->dev;

	return virtio_backend_queue_done(&input->eventq, token, consumed);
}

static int virtio_backend_input_push_readable(struct virtio_backend *backend,
					      const void *buf, size_t len)
{
	struct virtio_backend_input *input = backend->dev;
	const uint8_t *pos = buf;
	size_t event_len = sizeof(struct virtio_backend_input_event);
	size_t done;
	int ret = 0;

	if (!buf || !len || len % event_len)
		return -EINVAL;

	for (done = 0; done < len; done += event_len) {
		ret = virtio_backend_queue_push(&input->eventq, pos + done,
						event_len, 0);
		if (ret)
			break;
	}

	if (done)
		virtio_backend_event(backend, VIRTIO_BACKEND_EVENT_READABLE);

	return ret ? ret : (int)done;
}

static int virtio_backend_input_get_info(struct virtio_backend *backend,
					 struct virtio_backend_info *info)
{
	struct virtio_backend_input *input = backend->dev;

	info->u.input.profile = input->profile;
	info->u.input.source = input->source;
	info->u.input.led_state = input->led_state;
	return 0;
}

static void virtio_backend_input_destroy(struct virtio_backend *backend)
{
	struct virtio_backend_input *input = backend->dev;

	if (!input)
		return;

	input->stop_input_thread = 1;
	virtio_backend_queue_stop(&input->eventq);
	wake_input_thread(input);
	if (input->input_thread_started)
		pthread_join(input->input_thread, NULL);
	if (input->wake_fd >= 0)
		close(input->wake_fd);
	close_evdev_fds(input);
	virtio_backend_queue_destroy(&input->eventq);
	free(input);
	backend->dev = NULL;
}

static const struct virtio_backend_ops virtio_backend_input_ops = {
	.write = virtio_backend_input_write,
	.read = virtio_backend_input_read,
	.read_done = virtio_backend_input_read_done,
	.push_readable = virtio_backend_input_push_readable,
	.get_info = virtio_backend_input_get_info,
	.destroy = virtio_backend_input_destroy,
};

int virtio_backend_input_create(struct virtio_backend *backend,
				const struct virtio_backend_config *config)
{
	struct virtio_backend_input *input;
	int ret;

	switch (config->u.input.profile) {
	case VIRTIO_BACKEND_INPUT_KEYBOARD:
	case VIRTIO_BACKEND_INPUT_MOUSE:
	case VIRTIO_BACKEND_INPUT_TABLET:
		break;
	default:
		return -EINVAL;
	}

	switch (config->u.input.source) {
	case VIRTIO_BACKEND_INPUT_SOURCE_EXTERNAL:
	case VIRTIO_BACKEND_INPUT_SOURCE_EVDEV:
	case VIRTIO_BACKEND_INPUT_SOURCE_UI:
		break;
	default:
		return -EINVAL;
	}

	input = calloc(1, sizeof(*input));
	if (!input)
		return -ENOMEM;

	input->profile = config->u.input.profile;
	input->source = config->u.input.source;
	input->ui = config->u.input.ui;
	input->wake_fd = -1;
	ret = virtio_backend_queue_init(&input->eventq,
					VIRTIO_BACKEND_INPUT_QUEUE_DEPTH);
	if (ret < 0) {
		free(input);
		return -ENOMEM;
	}

	backend->dev = input;
	backend->ops = &virtio_backend_input_ops;
	if (input->source == VIRTIO_BACKEND_INPUT_SOURCE_EVDEV) {
		if (config->u.input.evdev_path &&
		    config->u.input.evdev_path[0]) {
			ret = open_evdev_one(input,
					     config->u.input.evdev_path);
		} else {
			ret = scan_evdev(input);
		}
		if (ret < 0)
			goto fail;

		ret = create_input_thread(backend);
		if (ret < 0)
			goto fail;
	} else if (input->source == VIRTIO_BACKEND_INPUT_SOURCE_UI) {
		ret = create_input_thread(backend);
		if (ret < 0)
			goto fail;
	}

	return 0;

fail:
	virtio_backend_input_destroy(backend);
	return ret;
}
