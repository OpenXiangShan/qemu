#ifndef __VIRTIO_BACKEND_INTERNAL_H__
#define __VIRTIO_BACKEND_INTERNAL_H__

#include "virtio_backend.h"

#include <pthread.h>

struct virtio_backend_packet {
	struct virtio_backend_packet *next;
	uint64_t token;
	size_t len;
	uint8_t data[];
};

struct virtio_backend_queue {
	pthread_mutex_t lock;
	pthread_cond_t not_full;
	pthread_cond_t not_empty;
	struct virtio_backend_packet *head;
	struct virtio_backend_packet *tail;
	unsigned int count;
	unsigned int depth;
	uint64_t next_token;
	int initialized;
	int stopped;
};

struct virtio_backend_ui_ops {
	int (*update)(void *ui, const void *pixels, uint32_t width,
		      uint32_t height, uint32_t stride, uint32_t x, uint32_t y,
		      uint32_t w, uint32_t h);
	void (*disable)(void *ui);
	int (*read_input)(void *ui, enum virtio_backend_input_profile profile,
			  struct virtio_backend_input_event *event, int block);
	void (*destroy)(void *ui);
};

struct virtio_backend_ui {
	const struct virtio_backend_ui_ops *ops;
};

struct virtio_backend;

struct virtio_backend_ops {
	int (*write)(struct virtio_backend *backend,
		     const struct virtio_backend_io *io);
	int (*read)(struct virtio_backend *backend,
		    struct virtio_backend_io *io);
	int (*read_done)(struct virtio_backend *backend,
			 uint64_t token, int consumed);
	int (*push_readable)(struct virtio_backend *backend,
			     const void *buf, size_t len);
	int (*get_info)(struct virtio_backend *backend,
			struct virtio_backend_info *info);
	void (*destroy)(struct virtio_backend *backend);
};

struct virtio_backend {
	enum virtio_backend_type type;
	const struct virtio_backend_ops *ops;
	struct virtio_backend_callbacks callbacks;
	void *callback_opaque;
	void *dev;
};

char *virtio_backend_strdup(const char *s);

int virtio_backend_ui_update(virtio_backend_handle_t handle,
			     const void *pixels, uint32_t width,
			     uint32_t height, uint32_t stride, uint32_t x,
			     uint32_t y, uint32_t w, uint32_t h);

void virtio_backend_ui_disable(virtio_backend_handle_t handle);

int virtio_backend_ui_read_input(virtio_backend_handle_t handle,
				 enum virtio_backend_input_profile profile,
				 struct virtio_backend_input_event *event,
				 int block);

int virtio_backend_ui_create(struct virtio_backend *backend,
			     const struct virtio_backend_config *config);

void virtio_backend_log(struct virtio_backend *backend, int level,
			const char *fmt, ...);

void virtio_backend_event(struct virtio_backend *backend,
			  unsigned int events);

int virtio_backend_queue_init(struct virtio_backend_queue *queue,
			      unsigned int depth);

void virtio_backend_queue_stop(struct virtio_backend_queue *queue);

void virtio_backend_queue_destroy(struct virtio_backend_queue *queue);

int virtio_backend_queue_push(struct virtio_backend_queue *queue,
			      const void *buf, size_t len, int block);

int virtio_backend_queue_peek(struct virtio_backend_queue *queue,
			      struct virtio_backend_io *io);

int virtio_backend_queue_done(struct virtio_backend_queue *queue,
			      uint64_t token, int consumed);

struct virtio_backend_packet *
virtio_backend_queue_pop(struct virtio_backend_queue *queue);

int virtio_backend_queue_pop_wait(struct virtio_backend_queue *queue,
				  struct virtio_backend_packet **pkt,
				  int block);

int virtio_backend_blk_create(struct virtio_backend *backend,
			      const struct virtio_backend_config *config);

int virtio_backend_net_create(struct virtio_backend *backend,
			      const struct virtio_backend_config *config);

int virtio_backend_console_create(struct virtio_backend *backend,
				  const struct virtio_backend_config *config);

int virtio_backend_gpu_create(struct virtio_backend *backend,
			      const struct virtio_backend_config *config);

int virtio_backend_input_create(struct virtio_backend *backend,
				const struct virtio_backend_config *config);

#endif
