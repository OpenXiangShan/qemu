#include "virtio_backend_internal.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <libslirp.h>

#define VIRTIO_BACKEND_NET_DEFAULT_NETWORK "10.0.2.0"
#define VIRTIO_BACKEND_NET_DEFAULT_NETMASK "255.255.255.0"
#define VIRTIO_BACKEND_NET_DEFAULT_HOST_IP "10.0.2.2"
#define VIRTIO_BACKEND_NET_DEFAULT_DHCP_START "10.0.2.15"
#define VIRTIO_BACKEND_NET_DEFAULT_DNS_IP "10.0.2.3"
#define VIRTIO_BACKEND_NET_POLL_MIN_TIMEOUT_MS 10
#define VIRTIO_BACKEND_NET_RX_QUEUE_DEPTH 8192U

struct virtio_backend_timer {
	struct virtio_backend_net *net;
	SlirpTimerCb cb;
	void *cb_opaque;
	int64_t expire_ms;
	int active;
	struct virtio_backend_timer *next;
};

struct virtio_backend_poll_list {
	struct pollfd *fds;
	int *slirp_events;
	int poll_count;
	int slirp_count;
	int cap;
	int event_fd;
};

struct virtio_backend_net {
	struct virtio_backend *backend;
	Slirp *slirp;
	pthread_t thread;
	pthread_mutex_t lock;
	struct virtio_backend_queue rxq;
	struct virtio_backend_queue txq;
	int event_fd;
	int lock_initialized;
	int thread_started;
	int stop_thread;
	uint8_t mac[6];
	char *hostfwd;
	struct virtio_backend_timer *timers;
};

#define VIRTIO_BACKEND_NET_HOSTFWD_ANY_IP "0.0.0.0"

struct port_range {
	int first;
	int last;
};

static const char *net_config_string(const char *value, const char *fallback)
{
	return value && *value ? value : fallback;
}

static void net_wake_thread(struct virtio_backend_net *net)
{
	uint64_t one = 1;

	if (net && net->event_fd >= 0)
		(void)write(net->event_fd, &one, sizeof(one));
}

static int64_t now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int64_t slirp_clock_get_ns_cb(void *opaque)
{
	struct timespec ts;

	(void)opaque;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static ssize_t slirp_send_packet_cb(const void *buf, size_t len, void *opaque)
{
	struct virtio_backend_net *net = opaque;
	int ret;

	if (!net)
		return -1;

	ret = virtio_backend_queue_push(&net->rxq, buf, len, 1);
	if (ret < 0) {
		virtio_backend_event(net->backend, VIRTIO_BACKEND_EVENT_ERROR);
		return ret;
	}

	virtio_backend_event(net->backend, VIRTIO_BACKEND_EVENT_READABLE);
	return (ssize_t)len;
}

static void slirp_guest_error_cb(const char *msg, void *opaque)
{
	struct virtio_backend_net *net = opaque;

	virtio_backend_log(net ? net->backend : NULL, 0,
			   "libslirp guest error: %s", msg ? msg : "");
}

static void *slirp_timer_new_cb(SlirpTimerCb cb, void *cb_opaque,
				void *opaque)
{
	struct virtio_backend_net *net = opaque;
	struct virtio_backend_timer *timer;

	timer = calloc(1, sizeof(*timer));
	if (!timer)
		return NULL;

	timer->net = net;
	timer->cb = cb;
	timer->cb_opaque = cb_opaque;
	timer->expire_ms = -1;
	timer->next = net->timers;
	net->timers = timer;
	return timer;
}

static void slirp_timer_free_cb(void *timer_opaque, void *opaque)
{
	struct virtio_backend_net *net = opaque;
	struct virtio_backend_timer *timer = timer_opaque;
	struct virtio_backend_timer **pp;

	if (!net || !timer)
		return;

	pp = &net->timers;
	while (*pp) {
		if (*pp == timer) {
			*pp = timer->next;
			free(timer);
			return;
		}
		pp = &(*pp)->next;
	}
}

static void slirp_timer_mod_cb(void *timer_opaque, int64_t expire_time,
			       void *opaque)
{
	struct virtio_backend_net *net = opaque;
	struct virtio_backend_timer *timer = timer_opaque;

	if (!timer)
		return;

	timer->expire_ms = expire_time;
	timer->active = 1;
	net_wake_thread(net);
}

static void slirp_register_poll_fd_cb(int fd, void *opaque)
{
	(void)fd;
	(void)opaque;
}

static void slirp_unregister_poll_fd_cb(int fd, void *opaque)
{
	(void)fd;
	(void)opaque;
}

static void slirp_notify_cb(void *opaque)
{
	struct virtio_backend_net *net = opaque;

	net_wake_thread(net);
}

static int poll_events_to_slirp(short revents)
{
	int events = 0;

	if (revents & POLLIN)
		events |= SLIRP_POLL_IN;
	if (revents & POLLOUT)
		events |= SLIRP_POLL_OUT;
	if (revents & POLLPRI)
		events |= SLIRP_POLL_PRI;
	if (revents & POLLERR)
		events |= SLIRP_POLL_ERR;
	if (revents & POLLHUP)
		events |= SLIRP_POLL_HUP;

	return events;
}

static short slirp_events_to_poll(int events)
{
	short poll_events = 0;

	if (events & SLIRP_POLL_IN)
		poll_events |= POLLIN;
	if (events & SLIRP_POLL_OUT)
		poll_events |= POLLOUT;
	if (events & SLIRP_POLL_PRI)
		poll_events |= POLLPRI;
	if (events & SLIRP_POLL_ERR)
		poll_events |= POLLERR;
	if (events & SLIRP_POLL_HUP)
		poll_events |= POLLHUP;

	return poll_events;
}

static int poll_list_reserve(struct virtio_backend_poll_list *plist)
{
	if (plist->poll_count == plist->cap) {
		int new_cap = plist->cap ? plist->cap * 2 : 16;
		struct pollfd *new_fds;
		int *new_events;

		new_fds = malloc((size_t)new_cap * sizeof(*new_fds));
		new_events = malloc((size_t)new_cap * sizeof(*new_events));
		if (!new_fds || !new_events) {
			free(new_fds);
			free(new_events);
			return -1;
		}
		if (plist->poll_count) {
			memcpy(new_fds, plist->fds,
			       (size_t)plist->poll_count * sizeof(*new_fds));
		}
		if (plist->slirp_count) {
			memcpy(new_events, plist->slirp_events,
			       (size_t)plist->slirp_count * sizeof(*new_events));
		}
		free(plist->fds);
		free(plist->slirp_events);
		plist->fds = new_fds;
		plist->slirp_events = new_events;
		plist->cap = new_cap;
	}

	return 0;
}

static int poll_list_add(int fd, int events, void *opaque)
{
	struct virtio_backend_poll_list *plist = opaque;
	int idx;

	if (poll_list_reserve(plist) < 0)
		return -1;

	idx = plist->slirp_count++;
	plist->fds[plist->poll_count].fd = fd;
	plist->fds[plist->poll_count].events = slirp_events_to_poll(events);
	plist->fds[plist->poll_count].revents = 0;
	plist->poll_count++;
	plist->slirp_events[idx] = 0;
	return idx;
}

static int poll_list_get_revents(int idx, void *opaque)
{
	struct virtio_backend_poll_list *plist = opaque;

	if (idx < 0 || idx >= plist->slirp_count)
		return 0;

	return plist->slirp_events[idx];
}

static void poll_list_reset(struct virtio_backend_poll_list *plist)
{
	plist->poll_count = 0;
	plist->slirp_count = 0;
	if (plist->event_fd >= 0) {
		if (poll_list_reserve(plist) < 0)
			return;
		plist->fds[plist->poll_count].fd = plist->event_fd;
		plist->fds[plist->poll_count].events = POLLIN;
		plist->fds[plist->poll_count].revents = 0;
		plist->poll_count++;
	}
}

static void run_expired_timers(struct virtio_backend_net *net)
{
	int64_t now = now_ms();
	struct virtio_backend_timer *timer = net->timers;

	while (timer) {
		if (timer->active && timer->expire_ms <= now) {
			timer->active = 0;
			timer->cb(timer->cb_opaque);
		}
		timer = timer->next;
	}
}

static uint32_t next_timeout(struct virtio_backend_net *net, uint32_t timeout)
{
	int64_t now = now_ms();
	struct virtio_backend_timer *timer = net->timers;

	while (timer) {
		if (timer->active) {
			uint32_t delta = 0;

			if (timer->expire_ms > now) {
				int64_t diff = timer->expire_ms - now;

				delta = diff > UINT32_MAX ? UINT32_MAX :
					(uint32_t)diff;
			}
			if (delta < timeout)
				timeout = delta;
		}
		timer = timer->next;
	}

	return timeout;
}

static void net_process_tx(struct virtio_backend_net *net)
{
	struct virtio_backend_packet *pkt;

	while ((pkt = virtio_backend_queue_pop(&net->txq)) != NULL) {
		slirp_input(net->slirp, pkt->data, (int)pkt->len);
		free(pkt);
	}
}

static void *net_thread_main(void *arg)
{
	struct virtio_backend_net *net = arg;
	struct virtio_backend_poll_list plist = {
		.event_fd = net->event_fd,
	};

	while (!net->stop_thread) {
		uint32_t timeout = 1000;
		int poll_timeout;
		int poll_ret;

		pthread_mutex_lock(&net->lock);
		net_process_tx(net);
		run_expired_timers(net);
		poll_list_reset(&plist);
		slirp_pollfds_fill(net->slirp, &timeout, poll_list_add,
				   &plist);
		timeout = next_timeout(net, timeout);
		if (timeout < VIRTIO_BACKEND_NET_POLL_MIN_TIMEOUT_MS)
			timeout = VIRTIO_BACKEND_NET_POLL_MIN_TIMEOUT_MS;
		pthread_mutex_unlock(&net->lock);

		poll_timeout = (timeout == UINT32_MAX) ? -1 : (int)timeout;
		poll_ret = poll(plist.fds, (nfds_t)plist.poll_count,
				poll_timeout);

		pthread_mutex_lock(&net->lock);
		for (int i = 0; i < plist.slirp_count; i++) {
			int fd_index = plist.poll_count - plist.slirp_count + i;

			plist.slirp_events[i] =
				poll_events_to_slirp(plist.fds[fd_index].revents);
		}
		if (net->event_fd >= 0 && plist.poll_count > 0 &&
		    (plist.fds[0].revents & POLLIN)) {
			uint64_t val;

			while (read(net->event_fd, &val, sizeof(val)) ==
			       sizeof(val)) {
			}
		}
		run_expired_timers(net);
		slirp_pollfds_poll(net->slirp, poll_ret < 0 ? errno : 0,
				   poll_list_get_revents, &plist);
		net_process_tx(net);
		pthread_mutex_unlock(&net->lock);
	}

	free(plist.fds);
	free(plist.slirp_events);
	return NULL;
}

static char *trim_spaces(char *s)
{
	char *end;

	while (isspace((unsigned char)*s))
		s++;

	end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		*--end = '\0';

	return s;
}

static int split_hostfwd_rule(char *rule, char *fields[5])
{
	char *p = rule;

	for (int i = 0; i < 4; i++) {
		char *sep = strchr(p, ':');

		if (!sep)
			return -1;

		fields[i] = p;
		*sep = '\0';
		p = sep + 1;
	}
	fields[4] = p;

	return strchr(fields[4], ':') ? -1 : 0;
}

static int parse_port_range(const char *text, struct port_range *range)
{
	char *end;
	long first;
	long last;

	if (!text || !*text || !range)
		return -1;

	errno = 0;
	first = strtol(text, &end, 10);
	if (errno || first <= 0 || first > 65535)
		return -1;

	if (*end == '-') {
		char *last_end;

		errno = 0;
		last = strtol(end + 1, &last_end, 10);
		if (errno || *last_end || last <= 0 || last > 65535 ||
		    last < first)
			return -1;
	} else if (*end == '\0') {
		last = first;
	} else {
		return -1;
	}

	range->first = (int)first;
	range->last = (int)last;
	return 0;
}

static unsigned int range_count(const struct port_range *range)
{
	return (unsigned int)(range->last - range->first + 1);
}

static int range_port(const struct port_range *range, unsigned int index)
{
	return range->first + (int)index;
}

static int check_host_port_available(int is_udp, struct in_addr host_addr,
				     int host_port)
{
	int fd;
	int one = 1;
	struct sockaddr_in addr;

	fd = socket(AF_INET, is_udp ? SOCK_DGRAM : SOCK_STREAM, 0);
	if (fd < 0)
		return -errno;

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr = host_addr;
	addr.sin_port = htons((uint16_t)host_port);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		int err = -errno;

		close(fd);
		return err;
	}

	close(fd);
	return 0;
}

static int add_hostfwd_rule(struct virtio_backend_net *net, char *rule,
			    const char *default_guest_ip)
{
	char *fields[5];
	struct in_addr host_addr;
	struct in_addr guest_addr;
	struct port_range host_ports;
	struct port_range guest_ports;
	const char *host_ip;
	const char *guest_ip;
	int is_udp;

	if (split_hostfwd_rule(rule, fields) < 0)
		return -EINVAL;

	if (!strcmp(fields[0], "tcp")) {
		is_udp = 0;
	} else if (!strcmp(fields[0], "udp")) {
		is_udp = 1;
	} else {
		return -EINVAL;
	}

	host_ip = *fields[1] ? fields[1] : VIRTIO_BACKEND_NET_HOSTFWD_ANY_IP;
	guest_ip = *fields[3] ? fields[3] : default_guest_ip;

	if (inet_pton(AF_INET, host_ip, &host_addr) != 1 ||
	    inet_pton(AF_INET, guest_ip, &guest_addr) != 1 ||
	    parse_port_range(fields[2], &host_ports) < 0 ||
	    parse_port_range(fields[4], &guest_ports) < 0 ||
	    range_count(&host_ports) != range_count(&guest_ports)) {
		return -EINVAL;
	}

	for (unsigned int i = 0; i < range_count(&host_ports); i++) {
		int ret = check_host_port_available(is_udp, host_addr,
						    range_port(&host_ports, i));

		if (ret < 0)
			return ret;
	}

	for (unsigned int i = 0; i < range_count(&host_ports); i++) {
		int ret;
		int host_port = range_port(&host_ports, i);
		int guest_port = range_port(&guest_ports, i);

		ret = slirp_add_hostfwd(net->slirp, is_udp, host_addr,
					host_port, guest_addr, guest_port);
		if (ret < 0)
			return -EINVAL;
	}

	return 0;
}

static int setup_hostfwd(struct virtio_backend_net *net, const char *hostfwd,
			 const char *default_guest_ip)
{
	char *copy;
	char *saveptr = NULL;
	char *rule;

	if (!hostfwd || !*hostfwd)
		return 0;

	copy = virtio_backend_strdup(hostfwd);
	if (!copy)
		return -ENOMEM;

	for (rule = strtok_r(copy, ",", &saveptr); rule;
	     rule = strtok_r(NULL, ",", &saveptr)) {
		char *trimmed = trim_spaces(rule);
		int ret;

		if (!*trimmed)
			continue;

		ret = add_hostfwd_rule(net, trimmed, default_guest_ip);
		if (ret < 0) {
			free(copy);
			return ret;
		}
	}

	free(copy);
	return 0;
}

static int virtio_backend_net_write(struct virtio_backend *backend,
				    const struct virtio_backend_io *io)
{
	struct virtio_backend_net *net = backend->dev;
	int ret;

	if (io->type != VIRTIO_BACKEND_IO_PACKET)
		return -EINVAL;

	ret = virtio_backend_queue_push(&net->txq, io->buf, io->len, 0);
	if (ret < 0)
		return ret;
	net_wake_thread(net);
	return (int)io->len;
}

static int virtio_backend_net_read(struct virtio_backend *backend,
				   struct virtio_backend_io *io)
{
	struct virtio_backend_net *net = backend->dev;

	if (io->type != VIRTIO_BACKEND_IO_PACKET)
		return -EINVAL;

	return virtio_backend_queue_peek(&net->rxq, io);
}

static int virtio_backend_net_read_done(struct virtio_backend *backend,
					uint64_t token, int consumed)
{
	struct virtio_backend_net *net = backend->dev;

	return virtio_backend_queue_done(&net->rxq, token, consumed);
}

static int virtio_backend_net_push_readable(struct virtio_backend *backend,
					    const void *buf, size_t len)
{
	struct virtio_backend_net *net = backend->dev;
	int ret;

	ret = virtio_backend_queue_push(&net->rxq, buf, len, 0);
	if (!ret)
		virtio_backend_event(backend, VIRTIO_BACKEND_EVENT_READABLE);
	return ret;
}

static int virtio_backend_net_get_info(struct virtio_backend *backend,
				       struct virtio_backend_info *info)
{
	(void)backend;
	(void)info;
	return 0;
}

static void virtio_backend_net_destroy(struct virtio_backend *backend)
{
	struct virtio_backend_net *net = backend->dev;
	struct virtio_backend_timer *timer;

	if (!net)
		return;

	net->stop_thread = 1;
	virtio_backend_queue_stop(&net->rxq);
	virtio_backend_queue_stop(&net->txq);
	net_wake_thread(net);
	if (net->thread_started)
		pthread_join(net->thread, NULL);
	if (net->slirp)
		slirp_cleanup(net->slirp);
	if (net->event_fd >= 0)
		close(net->event_fd);
	timer = net->timers;
	while (timer) {
		struct virtio_backend_timer *next = timer->next;

		free(timer);
		timer = next;
	}
	virtio_backend_queue_destroy(&net->rxq);
	virtio_backend_queue_destroy(&net->txq);
	if (net->lock_initialized)
		pthread_mutex_destroy(&net->lock);
	free(net->hostfwd);
	free(net);
	backend->dev = NULL;
}

static const SlirpCb net_slirp_callbacks = {
	.send_packet = slirp_send_packet_cb,
	.guest_error = slirp_guest_error_cb,
	.clock_get_ns = slirp_clock_get_ns_cb,
	.timer_new = slirp_timer_new_cb,
	.timer_free = slirp_timer_free_cb,
	.timer_mod = slirp_timer_mod_cb,
	.register_poll_fd = slirp_register_poll_fd_cb,
	.unregister_poll_fd = slirp_unregister_poll_fd_cb,
	.notify = slirp_notify_cb,
};

static const struct virtio_backend_ops virtio_backend_net_ops = {
	.write = virtio_backend_net_write,
	.read = virtio_backend_net_read,
	.read_done = virtio_backend_net_read_done,
	.push_readable = virtio_backend_net_push_readable,
	.get_info = virtio_backend_net_get_info,
	.destroy = virtio_backend_net_destroy,
};

int virtio_backend_net_create(struct virtio_backend *backend,
			      const struct virtio_backend_config *config)
{
	struct in_addr vnetwork;
	struct in_addr vnetmask;
	struct in_addr vhost;
	struct in_addr vdhcp_start;
	struct in_addr vnameserver;
	struct in6_addr zero6;
	const uint8_t default_mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
	const uint8_t *mac = config->u.net.mac ? config->u.net.mac : default_mac;
	const char *network = net_config_string(config->u.net.network,
						VIRTIO_BACKEND_NET_DEFAULT_NETWORK);
	const char *netmask = net_config_string(config->u.net.netmask,
						VIRTIO_BACKEND_NET_DEFAULT_NETMASK);
	const char *host_ip = net_config_string(config->u.net.host_ip,
						VIRTIO_BACKEND_NET_DEFAULT_HOST_IP);
	const char *dhcp_start = net_config_string(config->u.net.dhcp_start,
						   VIRTIO_BACKEND_NET_DEFAULT_DHCP_START);
	const char *dns_ip = net_config_string(config->u.net.dns_ip,
					       VIRTIO_BACKEND_NET_DEFAULT_DNS_IP);
	struct virtio_backend_net *net;
	int ret;

	net = calloc(1, sizeof(*net));
	if (!net)
		return -ENOMEM;

	net->backend = backend;
	memcpy(net->mac, mac, sizeof(net->mac));
	net->event_fd = -1;

	if (pthread_mutex_init(&net->lock, NULL)) {
		ret = -errno;
		goto fail;
	}
	net->lock_initialized = 1;

	if (virtio_backend_queue_init(&net->rxq,
				      VIRTIO_BACKEND_NET_RX_QUEUE_DEPTH) < 0) {
		ret = -ENOMEM;
		goto fail;
	}
	if (virtio_backend_queue_init(&net->txq, 0) < 0) {
		ret = -ENOMEM;
		goto fail;
	}

	net->hostfwd = virtio_backend_strdup(config->u.net.hostfwd);
	if (!net->hostfwd) {
		ret = -ENOMEM;
		goto fail;
	}

	if (inet_pton(AF_INET, network, &vnetwork) != 1 ||
	    inet_pton(AF_INET, netmask, &vnetmask) != 1 ||
	    inet_pton(AF_INET, host_ip, &vhost) != 1 ||
	    inet_pton(AF_INET, dhcp_start, &vdhcp_start) != 1 ||
	    inet_pton(AF_INET, dns_ip, &vnameserver) != 1) {
		ret = -EINVAL;
		goto fail;
	}

	memset(&zero6, 0, sizeof(zero6));
	net->slirp = slirp_init(0, true, vnetwork, vnetmask, vhost,
				false, zero6, 0, zero6, NULL, NULL, NULL, NULL,
				vdhcp_start, vnameserver, zero6, NULL, NULL,
				&net_slirp_callbacks, net);
	if (!net->slirp) {
		ret = -EIO;
		goto fail;
	}

	ret = setup_hostfwd(net, net->hostfwd, dhcp_start);
	if (ret < 0)
		goto fail;

	net->event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (net->event_fd < 0) {
		ret = -errno;
		goto fail;
	}

	backend->dev = net;
	backend->ops = &virtio_backend_net_ops;

	ret = pthread_create(&net->thread, NULL, net_thread_main, net);
	if (ret) {
		ret = -ret;
		goto fail;
	}

	net->thread_started = 1;
	return 0;

fail:
	if (backend->dev == net && backend->ops)
		virtio_backend_net_destroy(backend);
	else {
		if (net->slirp)
			slirp_cleanup(net->slirp);
		if (net->event_fd >= 0)
			close(net->event_fd);
		virtio_backend_queue_destroy(&net->rxq);
		virtio_backend_queue_destroy(&net->txq);
		if (net->lock_initialized)
			pthread_mutex_destroy(&net->lock);
		free(net->hostfwd);
		free(net);
	}
	return ret;
}
