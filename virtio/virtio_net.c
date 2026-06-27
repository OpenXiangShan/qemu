#include <stddef.h>
#include <string.h>
#include "virtio_wrapper.h"
#include "virtio.h"
#include "virtio_mmio.h"
#include "virtio_net.h"
#include "virtio_ids.h"
#include "virtio_ring.h"
#include "virtio_config.h"
#include "utils.h"

#define VIRTIO_NET_QUEUE_SIZE		256

#define VIRTIO_NET_UNK_QUEUE		0
#define VIRTIO_NET_RX_QUEUE		1
#define VIRTIO_NET_TX_QUEUE		2
#define VIRTIO_NET_CTRL_QUEUE		3

struct virtio_net_dev;
static void virtio_net_tx_poke(struct virtio_net_dev *ndev, uint32_t qnum);

struct virtio_net_queue {
	int num;
	int valid;
	int type;
	struct virtio_queue vq;
	struct virtio_iovec iov[VIRTIO_NET_QUEUE_SIZE];
	struct virtio_net_dev *ndev;
};

struct virtio_net_dev {
	struct virtio_device *vdev;

	struct virtio_net_queue *vqs;
	uint32_t cq;		/* Configuration queue number */
	uint32_t max_queues;
	uint32_t pending_queues;
	uint32_t can_receive;
	struct virtio_net_config config;
	uint64_t features;

	int mode;
	char name[64];
};

static void __virtio_net_tx_poke(struct virtio_net_dev *ndev, int budget, uint32_t qnum)
{
	struct virtio_net_queue *q = &ndev->vqs[qnum];
	struct virtio_queue *vq = &q->vq;
	struct virtio_device *dev = ndev->vdev;
	struct virtio_iovec *iov = q->iov;
	int rc;
	uint16_t head = 0;
	uint32_t iov_cnt = 0, pkt_len = 0, total_len = 0;
	void *data;

	while ((budget > 0) && virtio_queue_available(vq)) {
		rc = virtio_queue_get_iovec(vq, iov,
					    &iov_cnt, &total_len, &head);
		if (rc) {
			my_print(dev, "%s: failed to get iovec (error %d)\n",
				 __FUNCTION__, rc);
			continue;
		}

		/* iov[0] is offload info */
		pkt_len = total_len - iov[0].len;

		data = (void *)my_alloc(dev, pkt_len);
		virtio_iovec_to_buf_read(dev, &iov[1], iov_cnt - 1,
					 data, pkt_len);
		if (pkt_len != my_net_write_tap(dev, 0, data, pkt_len)) {
			my_print(dev, "%s write tap failed\n", __FUNCTION__);
			my_free(dev, (uint64_t)data, pkt_len);
			return;
		}
		my_free(dev, (uint64_t)data, pkt_len);

		virtio_queue_set_used_elem(vq, head, total_len);

		budget--;
	}

	if (virtio_queue_should_signal(vq)) {
		if (dev->vn && dev->vn->notify)
			dev->vn->notify(dev, q->num);
	}

	virtio_net_tx_poke(ndev, q->num);
}

static void virtio_net_tx_poke(struct virtio_net_dev *ndev, uint32_t qnum)
{
	struct virtio_net_queue *q = &ndev->vqs[qnum];

	if (virtio_queue_available(&q->vq)) {
		__virtio_net_tx_poke(ndev, VIRTIO_NET_QUEUE_SIZE / 4, qnum);
	}
}

static int virtio_net_receive(void *buf, int len, void *priv)
{
	int rc;
	uint16_t head = 0;
	uint64_t iov0_addr;
	uint32_t iov_cnt = 0, iov0_len, total_len = 0, pkt_len = 0;
	struct virtio_net_dev *ndev = priv;
	struct virtio_net_queue *q;
	struct virtio_queue *vq;
	struct virtio_iovec *iov;
	struct virtio_device *dev;
	struct virtio_net_hdr hdr;

	if (!ndev || len <= 0)
		return VIRTIO_EINVALID;

	q = &ndev->vqs[0];
	vq = &q->vq;
	iov = q->iov;
	dev = ndev->vdev;

	if (!ndev->can_receive || !q->valid)
		return VIRTIO_EAGAIN;

	pkt_len = len;

	if (!virtio_queue_available(vq))
		return VIRTIO_EAGAIN;

	rc = virtio_queue_get_iovec(vq, iov,
				    &iov_cnt, &total_len, &head);
	if (rc) {
		my_print(dev, "%s: failed to get iovec (error %d)\n",
			 __FUNCTION__, rc);
		return rc;
	}

	if (!iov_cnt || total_len < sizeof(hdr) + pkt_len ||
	    iov[0].len < sizeof(hdr)) {
		my_print(dev, "%s: RX buffer too small, total=%u pkt=%u\n",
			 __FUNCTION__, total_len, pkt_len);
		virtio_queue_set_used_elem(vq, head, 0);
		if (virtio_queue_should_signal(vq)) {
			if (dev->vn && dev->vn->notify)
				dev->vn->notify(dev, q->num);
		}
		return VIRTIO_ENOSPC;
	}

	memset(&hdr, 0, sizeof(hdr));
	if (iov_cnt == 1) {
		virtio_buf_to_iovec_write(dev, &iov[0], 1,
					  &hdr, sizeof(hdr));
		iov0_addr = iov[0].addr;
		iov0_len = iov[0].len;
		iov[0].addr += sizeof(hdr);
		iov[0].len -= sizeof(hdr);
		virtio_buf_to_iovec_write(dev, &iov[0], 1,
					  buf, pkt_len);
		virtio_queue_set_used_elem(vq, head,
					   sizeof(hdr) + pkt_len);
		iov[0].addr = iov0_addr;
		iov[0].len = iov0_len;
	} else if (iov_cnt > 1) {
		virtio_buf_to_iovec_write(dev, &iov[0], 1,
					  &hdr, sizeof(hdr));
		virtio_buf_to_iovec_write(dev, &iov[1], iov_cnt - 1,
					  buf, pkt_len);
		virtio_queue_set_used_elem(vq, head, iov[0].len + pkt_len);
	}

	if (virtio_queue_should_signal(vq)) {
		if (dev->vn && dev->vn->notify)
			dev->vn->notify(dev, q->num);
	}

	return 0;
}

static void virtio_net_handle_ctrl(struct virtio_net_dev *ndev, uint32_t qnum)
{
	int rc;
	uint16_t head = 0;
	uint32_t iov_cnt = 0, total_len = 0;
	struct virtio_net_queue *q = &ndev->vqs[qnum];
	struct virtio_queue *vq = &q->vq;
	struct virtio_iovec *iov = q->iov;
	struct virtio_device *dev = ndev->vdev;
	struct virtio_net_ctrl_hdr ctrl_hdr;
	struct virtio_net_ctrl_mq ctrl_mq;
	virtio_net_ctrl_ack_t status;

	while (virtio_queue_available(vq)) {
		rc = virtio_queue_get_iovec(vq, iov,
					    &iov_cnt, &total_len, &head);
		if (rc) {
			my_print(dev, "%s: failed to get iovec (error %d)\n",
				 __FUNCTION__, rc);
			continue;
		}

		status = VMM_VIRTIO_NET_ERR;

		if ((iov_cnt < 2) ||
		    (iov[0].len < sizeof(ctrl_hdr)) ||
		    (iov[iov_cnt - 1].len < sizeof(status))) {
			my_print(dev, "%s: invalid ctrl IOV\n", __FUNCTION__);
			goto skip;
		}

		virtio_iovec_to_buf_read(dev, &iov[0], 1,
					     &ctrl_hdr, sizeof(ctrl_hdr));

		switch (ctrl_hdr.class) {
		case VMM_VIRTIO_NET_CTRL_MQ:
			if ((iov_cnt < 3) ||
			    (iov[1].len < sizeof(ctrl_mq))) {
				my_print(dev, "%s: invalid ctrl mq IOV\n",
					 __FUNCTION__);
				goto skip;
			}
			virtio_iovec_to_buf_read(dev, &iov[1], 1,
						     &ctrl_mq, sizeof(ctrl_mq));

			if (ctrl_mq.virtqueue_pairs < ndev->max_queues) {
				status = VMM_VIRTIO_NET_OK;
			}

			my_net_ctrl_mq(dev, ctrl_mq.virtqueue_pairs);

			break;
		default:
			my_print(dev, "%s: IOV Class %d is not handled\n",
				 __FUNCTION__, ctrl_hdr.class);
			break;
		};

skip:
		virtio_buf_to_iovec_write(dev, &iov[iov_cnt - 1], 1,
					      &status, 1);

		virtio_queue_set_used_elem(vq, head, total_len);
	}

	if (virtio_queue_should_signal(vq)) {
		if (dev->vn && dev->vn->notify)
			dev->vn->notify(dev, q->num);
	}
}

static uint64_t virtio_net_get_host_features(struct virtio_device *dev)
{
	return 1UL << VMM_VIRTIO_NET_F_MAC
		| 1UL << VMM_VIRTIO_RING_F_EVENT_IDX
		| 1UL << VMM_VIRTIO_NET_F_MQ
		| 1UL << VMM_VIRTIO_NET_F_CTRL_VQ;
}

static void virtio_net_set_guest_features(struct virtio_device *dev,
					  uint32_t select, uint32_t features)
{
	struct virtio_net_dev *ndev = dev->emu_data;

	if (1 < select)
		return;

	ndev->features &= ~((u64)UINT_MAX << (select * 32));
	ndev->features |= ((u64)features << (select * 32));
}

static int virtio_net_init_vq(struct virtio_device *dev, uint32_t vq,
			      uint32_t page_size, uint32_t align, uint32_t pfn)
{
	int rc;
	struct virtio_net_dev *ndev = dev->emu_data;

	rc = virtio_queue_setup(dev, &ndev->vqs[vq].vq, pfn, page_size,
				VIRTIO_NET_QUEUE_SIZE, align);
	if (!rc) {
		ndev->vqs[vq].valid = 1;
	}

	return rc;
}

static int virtio_net_get_pfn_vq(struct virtio_device *dev, uint32_t vq)
{
	int rc;
	struct virtio_net_dev *ndev = dev->emu_data;

	rc = virtio_queue_guest_pfn(&ndev->vqs[vq].vq);
	if (rc) {
		ndev->vqs[vq].num = vq;
		ndev->vqs[vq].valid = 1;
	}

	return rc;
}

static int virtio_net_get_size_vq(struct virtio_device *dev, uint32_t vq)
{
	return VIRTIO_NET_QUEUE_SIZE;
}

static int virtio_net_set_size_vq(struct virtio_device *dev,
				  uint32_t vq, int size)
{
	/* FIXME: dynamic */
	return size;
}

static int virtio_net_notify_vq(struct virtio_device *dev, uint32_t vq)
{
	int rc = 0;
	struct virtio_net_dev *ndev = dev->emu_data;

	if (vq >= ndev->max_queues)
		return -1;

	switch (ndev->vqs[vq].type) {
	case VIRTIO_NET_TX_QUEUE:
	case VIRTIO_NET_RX_QUEUE:
	case VIRTIO_NET_CTRL_QUEUE:
		ndev->pending_queues |= 1U << vq;
		break;
	default:
		rc = -1;
		break;
	}

	return rc;
}

static void virtio_net_req_process(void *data)
{
	struct virtio_net_dev *ndev = data;
	uint32_t pending;
	uint32_t qnum;

	if (!ndev)
		return;

	pending = ndev->pending_queues;
	ndev->pending_queues = 0;

	for (qnum = 0; qnum < ndev->max_queues; qnum++) {
		if (!(pending & (1U << qnum)))
			continue;

		switch (ndev->vqs[qnum].type) {
		case VIRTIO_NET_TX_QUEUE:
			virtio_net_tx_poke(ndev, qnum);
			break;
		case VIRTIO_NET_CTRL_QUEUE:
			virtio_net_handle_ctrl(ndev, qnum);
			break;
		default:
			break;
		}
	}
}

static void virtio_net_status_changed(struct virtio_device *dev,
				      uint32_t new_status)
{
	uint32_t i, have_rx_queue = 0;
	struct virtio_net_dev *ndev = dev->emu_data;

	for (i = 0; i < ndev->max_queues; i++) {
		if (ndev->vqs[i].valid &&
		    (ndev->vqs[i].type == VIRTIO_NET_RX_QUEUE)) {
			have_rx_queue++;
		}
	}

	if (have_rx_queue &&
	    (new_status & VMM_VIRTIO_CONFIG_S_DRIVER_OK)) {
		ndev->can_receive = 1;
	} else {
		ndev->can_receive = 0;
	}
}

static int virtio_net_reset(struct virtio_device *dev)
{
	//my_print(dev, "%s\n", __FUNCTION__);
	return 0;
}

static int virtio_net_read_config(struct virtio_device *dev,
				  uint32_t offset, void *dst, uint32_t dst_len)
{
	struct virtio_net_dev *ndev = dev->emu_data;
	uint8_t *src = (uint8_t *)&ndev->config;
	uint32_t i, src_len = sizeof(ndev->config);

	for (i = 0; (i < dst_len) && ((offset + i) < src_len); i++) {
		((uint8_t *)dst)[i] = src[offset + i];
	}

	return 0;
}

static int virtio_net_write_config(struct virtio_device *dev,
				   uint32_t offset, void *src, uint32_t src_len)
{
	struct virtio_net_dev *ndev = dev->emu_data;
	uint8_t *dst = (uint8_t *)&ndev->config;
	uint32_t i, dst_len = sizeof(ndev->config);

	for (i = 0; (i < src_len) && ((offset + i) < dst_len); i++) {
		dst[offset + i] = ((uint8_t *)src)[i];
	}

	return 0;
}

static int virtio_net_connect(struct virtio_device *dev,
			      struct virtio_emulator *emu)
{
	int i;
	struct virtio_net_dev *ndev;
	struct virtio_mmio_dev *mdev = container_of(dev, struct virtio_mmio_dev, dev);

	ndev = (struct virtio_net_dev *)my_zalloc(dev, sizeof(struct virtio_net_dev));
	if (!ndev) {
		my_print(dev, "Failed to allocate virtio net device....\n");
		return -1;
	}
	ndev->vdev = dev;
	strcpy(ndev->name, dev->name);

	ndev->config.max_virtqueue_pairs = 1;
	ndev->vqs = (struct virtio_net_queue *)
			my_zalloc(dev, sizeof(struct virtio_net_queue) *
				  ((ndev->config.max_virtqueue_pairs * 2) + 1));
	if (!ndev->vqs) {
		my_free(dev, (uint64_t)ndev, sizeof(*ndev));
		return -1;
	}
	ndev->config.status = VMM_VIRTIO_NET_S_LINK_UP;
	ndev->cq = ndev->config.max_virtqueue_pairs * 2;
	ndev->max_queues = ndev->config.max_virtqueue_pairs * 2 + 1;
	dev->emu_data = ndev;

	for (i = 0; i < ndev->max_queues; i++) {
		ndev->vqs[i].num = i;
		ndev->vqs[i].valid = 0;
		ndev->vqs[i].ndev = ndev;
		if (i == ndev->cq) {
			ndev->vqs[i].type = VIRTIO_NET_CTRL_QUEUE;
		} else {
			if (i % 2) {
				ndev->vqs[i].type = VIRTIO_NET_TX_QUEUE;
			} else {
				ndev->vqs[i].type = VIRTIO_NET_RX_QUEUE;
			}
		}
	}

	for (i = 0; i < 6; i++) {
		my_net_set_mac(dev, ndev->config.mac);
	}

	mdev->cb.receive = virtio_net_receive;
	mdev->cb.process_req = virtio_net_req_process;
	mdev->cb.data = ndev;

	return 0;
}

static void virtio_net_disconnect(struct virtio_device *dev)
{
}

static struct virtio_device_id virtio_net_emu_id[] = {
	{ .type = VMM_VIRTIO_ID_NET },
	{ },
};

static struct virtio_emulator virtio_net = {
	.name = "virtio_net",
	.id_table = virtio_net_emu_id,

	/* VirtIO operations */
	.get_host_features      = virtio_net_get_host_features,
	.set_guest_features     = virtio_net_set_guest_features,
	.init_vq                = virtio_net_init_vq,
	.get_pfn_vq             = virtio_net_get_pfn_vq,
	.get_size_vq            = virtio_net_get_size_vq,
	.set_size_vq            = virtio_net_set_size_vq,
	.notify_vq              = virtio_net_notify_vq,
	.status_changed         = virtio_net_status_changed,

	/* Emulator operations */
	.read_config = virtio_net_read_config,
	.write_config = virtio_net_write_config,
	.reset = virtio_net_reset,
	.connect = virtio_net_connect,
	.disconnect = virtio_net_disconnect,
};

struct virtio_emulator *virtio_net_emulator_create(void)
{
	return &virtio_net;
}
