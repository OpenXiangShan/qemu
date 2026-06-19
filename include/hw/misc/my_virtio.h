#ifndef __MY_VIRTIO_H__
#define __MY_VIRTIO_H__

#include "hw/irq.h"

void my_virtio_blk_create(hwaddr start, hwaddr size, qemu_irq irq,
                          const char *image_path);
void my_virtio_net_create(hwaddr start, hwaddr size, qemu_irq irq,
                          const char *hostfwd, const char *network,
                          const char *netmask, const char *host_ip,
                          const char *dhcp_start, const char *dns_ip);
void my_virtio_console_create(hwaddr start, hwaddr size, qemu_irq irq,
                              const char *backend, const char *input_path,
                              const char *output_path);
void *my_virtio_ui_create_vnc(const char *listen, uint32_t width,
                              uint32_t height);
void my_virtio_ui_destroy(void *ui);
void my_virtio_gpu_create(hwaddr start, hwaddr size, qemu_irq irq, void *ui);

#endif
