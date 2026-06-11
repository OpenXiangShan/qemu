#ifndef __MY_VIRTIO_H__
#define __MY_VIRTIO_H__

#include "hw/irq.h"

void my_virtio_blk_create(hwaddr start, hwaddr size, qemu_irq irq);
void my_virtio_net_create(hwaddr start, hwaddr size, qemu_irq irq);
void my_virtio_console_create(hwaddr start, hwaddr size, qemu_irq irq);

#endif
