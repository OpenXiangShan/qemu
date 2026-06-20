#ifndef __VIRTIO_BACKEND_H__
#define __VIRTIO_BACKEND_H__

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

typedef void *virtio_backend_handle_t;
typedef void *virtio_backend_ui_handle_t;

enum virtio_backend_type {
	VIRTIO_BACKEND_BLK = 1,
	VIRTIO_BACKEND_NET,
	VIRTIO_BACKEND_CONSOLE,
	VIRTIO_BACKEND_GPU,
	VIRTIO_BACKEND_INPUT,
};

enum virtio_backend_event {
	VIRTIO_BACKEND_EVENT_READABLE = 1U << 0,
	VIRTIO_BACKEND_EVENT_ERROR = 1U << 1,
};

enum virtio_backend_io_type {
	VIRTIO_BACKEND_IO_BLK = 1,
	VIRTIO_BACKEND_IO_PACKET,
	VIRTIO_BACKEND_IO_STREAM,
	VIRTIO_BACKEND_IO_GPU_CMD,
	VIRTIO_BACKEND_IO_INPUT_EVENT,
};

enum virtio_backend_blk_op {
	VIRTIO_BACKEND_BLK_READ = 0,
	VIRTIO_BACKEND_BLK_WRITE,
	VIRTIO_BACKEND_BLK_FLUSH,
};

enum virtio_backend_console_backend {
	VIRTIO_BACKEND_CONSOLE_EXTERNAL = 0,
	VIRTIO_BACKEND_CONSOLE_STDIO,
	VIRTIO_BACKEND_CONSOLE_FD,
	VIRTIO_BACKEND_CONSOLE_PTY,
};

enum virtio_backend_input_profile {
	VIRTIO_BACKEND_INPUT_KEYBOARD = 0,
	VIRTIO_BACKEND_INPUT_MOUSE,
	VIRTIO_BACKEND_INPUT_TABLET,
};

enum virtio_backend_input_source {
	VIRTIO_BACKEND_INPUT_SOURCE_EXTERNAL = 0,
	VIRTIO_BACKEND_INPUT_SOURCE_EVDEV,
	VIRTIO_BACKEND_INPUT_SOURCE_UI,
};

struct virtio_backend_input_event {
	uint16_t type;
	uint16_t code;
	int32_t value;
};

struct virtio_backend_callbacks {
	void (*event)(void *opaque, virtio_backend_handle_t handle,
		      unsigned int events);
	void (*log)(void *opaque, int level, const char *fmt, va_list ap);
};

struct virtio_backend_config {
	enum virtio_backend_type type;
	const struct virtio_backend_callbacks *callbacks;
	void *callback_opaque;

	union {
		struct {
			const char *image_path;
		} blk;

		struct {
			const char *hostfwd;
			const uint8_t *mac;
			const char *network;
			const char *netmask;
			const char *host_ip;
			const char *dhcp_start;
			const char *dns_ip;
		} net;

		struct {
			enum virtio_backend_console_backend backend;
			int (*host_write)(void *opaque, const uint8_t *buf,
					  size_t len);
			void *host_opaque;
			int input_fd;
			int output_fd;
			int close_fds;
		} console;

		struct {
			uint32_t width;
			uint32_t height;
			uint32_t max_outputs;
			virtio_backend_ui_handle_t ui;
			int (*guest_read)(void *opaque, uint64_t gpa,
					  void *dst, uint32_t len);
			void (*scanout_update)(void *opaque, uint32_t scanout_id,
					       const void *pixels,
					       uint32_t width,
					       uint32_t height,
					       uint32_t stride,
					       uint32_t x, uint32_t y,
					       uint32_t w, uint32_t h);
			void (*scanout_disable)(void *opaque,
						uint32_t scanout_id);
			void *opaque;
		} gpu;

		struct {
			enum virtio_backend_input_profile profile;
			enum virtio_backend_input_source source;
			const char *evdev_path;
			virtio_backend_ui_handle_t ui;
		} input;
	} u;
};

struct virtio_backend_io {
	enum virtio_backend_io_type type;

	void *buf;
	size_t len;
	size_t cap;

	uint64_t token;

	union {
		struct {
			enum virtio_backend_blk_op op;
			uint64_t sector;
		} blk;
		struct {
			void *resp;
			size_t *resp_len;
		} gpu;
	} u;
};

struct virtio_backend_info {
	enum virtio_backend_type type;

	union {
		struct {
			int64_t capacity;
		} blk;

		struct {
			enum virtio_backend_console_backend backend;
			const char *pty_path;
		} console;

		struct {
			enum virtio_backend_input_profile profile;
			enum virtio_backend_input_source source;
			uint8_t led_state;
		} input;
	} u;
};

virtio_backend_handle_t
virtio_backend_create(const struct virtio_backend_config *config);

virtio_backend_ui_handle_t
virtio_backend_ui_create_vnc(const char *listen, uint32_t width,
			     uint32_t height);

void virtio_backend_ui_destroy(virtio_backend_ui_handle_t handle);

void virtio_backend_destroy(virtio_backend_handle_t handle);

int virtio_backend_write(virtio_backend_handle_t handle,
			 const struct virtio_backend_io *io);

int virtio_backend_read(virtio_backend_handle_t handle,
			struct virtio_backend_io *io);

int virtio_backend_read_done(virtio_backend_handle_t handle,
			     uint64_t token, int consumed);

/*
 * Optional external RX/input injection helper.
 *
 * Console: inject bytes into the guest-visible input queue. This is mainly for
 * VIRTIO_BACKEND_CONSOLE_EXTERNAL users; stdio/fd/pty backends normally fill
 * the queue from their own input thread.
 *
 * Net: inject one host-to-guest RX packet. This is mainly for external/test
 * users; the built-in slirp backend feeds RX packets through its own path.
 *
 * Input: with VIRTIO_BACKEND_INPUT_SOURCE_EXTERNAL, inject one or more struct
 * virtio_backend_input_event records into the guest-visible event queue. With
 * VIRTIO_BACKEND_INPUT_SOURCE_EVDEV, the backend reads Linux evdev events from
 * evdev_path, or scans /dev/input/event* when evdev_path is NULL or empty.
 */
int virtio_backend_push_readable(virtio_backend_handle_t handle,
				 const void *buf, size_t len);

int virtio_backend_get_info(virtio_backend_handle_t handle,
			    struct virtio_backend_info *info);

#endif
