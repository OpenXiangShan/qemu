#include "virtio_backend_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

int virtio_backend_queue_init(struct virtio_backend_queue *queue,
			      unsigned int depth)
{
	memset(queue, 0, sizeof(*queue));
	if (pthread_mutex_init(&queue->lock, NULL))
		return -1;
	if (pthread_cond_init(&queue->not_full, NULL)) {
		pthread_mutex_destroy(&queue->lock);
		return -1;
	}
	if (pthread_cond_init(&queue->not_empty, NULL)) {
		pthread_cond_destroy(&queue->not_full);
		pthread_mutex_destroy(&queue->lock);
		return -1;
	}
	queue->depth = depth;
	queue->next_token = 1;
	queue->initialized = 1;
	return 0;
}

void virtio_backend_queue_stop(struct virtio_backend_queue *queue)
{
	if (!queue || !queue->initialized)
		return;

	pthread_mutex_lock(&queue->lock);
	queue->stopped = 1;
	pthread_cond_broadcast(&queue->not_full);
	pthread_cond_broadcast(&queue->not_empty);
	pthread_mutex_unlock(&queue->lock);
}

void virtio_backend_queue_destroy(struct virtio_backend_queue *queue)
{
	struct virtio_backend_packet *pkt;

	if (!queue || !queue->initialized)
		return;

	pthread_mutex_lock(&queue->lock);
	queue->stopped = 1;
	pthread_cond_broadcast(&queue->not_full);
	pthread_cond_broadcast(&queue->not_empty);
	pkt = queue->head;
	while (pkt) {
		struct virtio_backend_packet *next = pkt->next;
		free(pkt);
		pkt = next;
	}
	queue->head = NULL;
	queue->tail = NULL;
	queue->count = 0;
	pthread_mutex_unlock(&queue->lock);

	pthread_cond_destroy(&queue->not_full);
	pthread_cond_destroy(&queue->not_empty);
	pthread_mutex_destroy(&queue->lock);
	memset(queue, 0, sizeof(*queue));
}

int virtio_backend_queue_push(struct virtio_backend_queue *queue,
			      const void *buf, size_t len, int block)
{
	struct virtio_backend_packet *pkt;

	if (!queue || (!buf && len))
		return -EINVAL;

	pkt = malloc(sizeof(*pkt) + len);
	if (!pkt)
		return -ENOMEM;

	pkt->next = NULL;
	pkt->len = len;
	if (len)
		memcpy(pkt->data, buf, len);

	pthread_mutex_lock(&queue->lock);
	while (queue->depth && queue->count >= queue->depth && block &&
	       !queue->stopped)
		pthread_cond_wait(&queue->not_full, &queue->lock);

	if (queue->stopped) {
		pthread_mutex_unlock(&queue->lock);
		free(pkt);
		return -ESHUTDOWN;
	}

	if (queue->depth && queue->count >= queue->depth) {
		pthread_mutex_unlock(&queue->lock);
		free(pkt);
		return -ENOSPC;
	}

	pkt->token = queue->next_token++;
	if (!queue->next_token)
		queue->next_token = 1;

	if (queue->tail) {
		queue->tail->next = pkt;
	} else {
		queue->head = pkt;
	}
	queue->tail = pkt;
	queue->count++;
	pthread_cond_signal(&queue->not_empty);
	pthread_mutex_unlock(&queue->lock);

	return 0;
}

int virtio_backend_queue_peek(struct virtio_backend_queue *queue,
			      struct virtio_backend_io *io)
{
	struct virtio_backend_packet *pkt;
	size_t copy_len;

	if (!queue || !io || !io->buf)
		return -EINVAL;

	pthread_mutex_lock(&queue->lock);
	pkt = queue->head;
	if (!pkt) {
		pthread_mutex_unlock(&queue->lock);
		return -EAGAIN;
	}

	if (io->cap < pkt->len) {
		pthread_mutex_unlock(&queue->lock);
		return -ENOSPC;
	}

	copy_len = pkt->len;
	if (copy_len)
		memcpy(io->buf, pkt->data, copy_len);
	io->len = copy_len;
	io->token = pkt->token;
	pthread_mutex_unlock(&queue->lock);
	return 0;
}

int virtio_backend_queue_done(struct virtio_backend_queue *queue,
			      uint64_t token, int consumed)
{
	struct virtio_backend_packet *pkt;

	if (!queue)
		return -EINVAL;

	pthread_mutex_lock(&queue->lock);
	pkt = queue->head;
	if (!pkt) {
		pthread_mutex_unlock(&queue->lock);
		return -EAGAIN;
	}
	if (pkt->token != token) {
		pthread_mutex_unlock(&queue->lock);
		return -ESTALE;
	}
	if (!consumed) {
		pthread_mutex_unlock(&queue->lock);
		return 0;
	}

	queue->head = pkt->next;
	if (!queue->head)
		queue->tail = NULL;
	queue->count--;
	pthread_cond_signal(&queue->not_full);
	pthread_mutex_unlock(&queue->lock);

	free(pkt);
	return 0;
}

struct virtio_backend_packet *
virtio_backend_queue_pop(struct virtio_backend_queue *queue)
{
	struct virtio_backend_packet *pkt = NULL;

	if (virtio_backend_queue_pop_wait(queue, &pkt, 0) < 0)
		return NULL;

	return pkt;
}

int virtio_backend_queue_pop_wait(struct virtio_backend_queue *queue,
				  struct virtio_backend_packet **out,
				  int block)
{
	struct virtio_backend_packet *pkt;

	if (!queue || !out)
		return -EINVAL;

	*out = NULL;

	pthread_mutex_lock(&queue->lock);
	while (!queue->head && block && !queue->stopped)
		pthread_cond_wait(&queue->not_empty, &queue->lock);

	if (queue->stopped && !queue->head) {
		pthread_mutex_unlock(&queue->lock);
		return -ESHUTDOWN;
	}

	pkt = queue->head;
	if (pkt) {
		queue->head = pkt->next;
		if (!queue->head)
			queue->tail = NULL;
		queue->count--;
		pthread_cond_signal(&queue->not_full);
	}
	pthread_mutex_unlock(&queue->lock);

	if (!pkt)
		return -EAGAIN;

	*out = pkt;
	return 0;
}
