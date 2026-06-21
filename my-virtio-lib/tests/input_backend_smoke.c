#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../backend/virtio_backend.h"

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_LED 0x11
#define SYN_REPORT 0
#define KEY_W 17
#define REL_X 0
#define LED_CAPSL 1

static int readable_events;
static int error_events;

static void backend_event(void *opaque, virtio_backend_handle_t handle,
			  unsigned int events)
{
	(void)opaque;
	(void)handle;

	if (events & VIRTIO_BACKEND_EVENT_READABLE)
		readable_events++;
	if (events & VIRTIO_BACKEND_EVENT_ERROR)
		error_events++;
}

static int expect_read(virtio_backend_handle_t backend,
		       const struct virtio_backend_input_event *expect)
{
	struct virtio_backend_input_event got;
	struct virtio_backend_io io = {
		.type = VIRTIO_BACKEND_IO_INPUT_EVENT,
		.buf = &got,
		.cap = sizeof(got),
	};
	int ret;

	memset(&got, 0, sizeof(got));
	ret = virtio_backend_read(backend, &io);
	if (ret != sizeof(got)) {
		fprintf(stderr, "read ret=%d\n", ret);
		return 1;
	}
	if (memcmp(&got, expect, sizeof(got))) {
		fprintf(stderr,
			"event mismatch got type=%u code=%u value=%d expect type=%u code=%u value=%d\n",
			got.type, got.code, got.value, expect->type,
			expect->code, expect->value);
		return 1;
	}
	if (virtio_backend_read_done(backend, io.token, 1)) {
		fprintf(stderr, "read_done failed\n");
		return 1;
	}

	return 0;
}

int main(void)
{
	struct virtio_backend_callbacks callbacks = {
		.event = backend_event,
	};
	struct virtio_backend_config config = {
		.type = VIRTIO_BACKEND_INPUT,
		.callbacks = &callbacks,
		.u.input.profile = VIRTIO_BACKEND_INPUT_KEYBOARD,
	};
	struct virtio_backend_input_event events[] = {
		{ EV_KEY, KEY_W, 1 },
		{ EV_SYN, SYN_REPORT, 0 },
	};
	struct virtio_backend_input_event rel = { EV_REL, REL_X, 9 };
	struct virtio_backend_input_event led = { EV_LED, LED_CAPSL, 1 };
	struct virtio_backend_info info;
	struct virtio_backend_io io;
	virtio_backend_handle_t backend;
	int ret;

	backend = virtio_backend_create(&config);
	if (!backend) {
		fprintf(stderr, "failed to create input backend\n");
		return 1;
	}

	ret = virtio_backend_push_readable(backend, events, sizeof(events));
	if (ret != sizeof(events) || readable_events != 1 || error_events) {
		fprintf(stderr, "push events ret=%d readable=%d error=%d\n",
			ret, readable_events, error_events);
		return 1;
	}

	if (expect_read(backend, &events[0]) ||
	    expect_read(backend, &events[1])) {
		return 1;
	}

	memset(&io, 0, sizeof(io));
	io.type = VIRTIO_BACKEND_IO_INPUT_EVENT;
	io.buf = &rel;
	io.len = sizeof(rel) - 1;
	if (virtio_backend_push_readable(backend, &rel, sizeof(rel) - 1) !=
	    -EINVAL) {
		fprintf(stderr, "bad short push result\n");
		return 1;
	}
	if (virtio_backend_write(backend, &io) != -EINVAL) {
		fprintf(stderr, "bad short status result\n");
		return 1;
	}

	io.len = sizeof(led);
	io.buf = &led;
	if (virtio_backend_write(backend, &io) != sizeof(led)) {
		fprintf(stderr, "status write failed\n");
		return 1;
	}

	memset(&info, 0, sizeof(info));
	if (virtio_backend_get_info(backend, &info) ||
	    info.type != VIRTIO_BACKEND_INPUT ||
	    info.u.input.profile != VIRTIO_BACKEND_INPUT_KEYBOARD ||
	    info.u.input.source != VIRTIO_BACKEND_INPUT_SOURCE_EXTERNAL ||
	    info.u.input.led_state != (1U << 1)) {
		fprintf(stderr,
			"bad info type=%d profile=%d source=%d led=0x%x\n",
			info.type, info.u.input.profile,
			info.u.input.source, info.u.input.led_state);
		return 1;
	}

	memset(&io, 0, sizeof(io));
	io.type = VIRTIO_BACKEND_IO_INPUT_EVENT;
	io.buf = &rel;
	io.cap = sizeof(rel);
	if (virtio_backend_read(backend, &io) != -EAGAIN) {
		fprintf(stderr, "empty read did not return EAGAIN\n");
		return 1;
	}

	virtio_backend_destroy(backend);

	memset(&config, 0, sizeof(config));
	config.type = VIRTIO_BACKEND_INPUT;
	config.u.input.profile = VIRTIO_BACKEND_INPUT_KEYBOARD;
	config.u.input.source = VIRTIO_BACKEND_INPUT_SOURCE_EVDEV;
	config.u.input.evdev_path = "/no/such/input-event";
	backend = virtio_backend_create(&config);
	if (backend) {
		fprintf(stderr, "bad evdev path unexpectedly created backend\n");
		virtio_backend_destroy(backend);
		return 1;
	}

	return 0;
}
