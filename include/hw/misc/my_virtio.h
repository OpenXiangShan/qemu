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
void *my_virtio_gpu_create(hwaddr start, hwaddr size, qemu_irq irq,
                           const char *vnc_listen);
void my_virtio_keyboard_create(hwaddr start, hwaddr size, qemu_irq irq,
                               const char *backend, const char *evdev_path,
                               void *ui);
void my_virtio_mouse_create(hwaddr start, hwaddr size, qemu_irq irq,
                            const char *backend, const char *evdev_path,
                            void *ui);
void my_virtio_tablet_create(hwaddr start, hwaddr size, qemu_irq irq,
                             const char *backend, const char *evdev_path,
                             void *ui);

#endif
