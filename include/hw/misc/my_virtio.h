#ifndef __MY_VIRTIO_H__
#define __MY_VIRTIO_H__

#include "hw/irq.h"

#define MY_VIRTIO_IOMMU_RELAY_BASE 0x311e0000ULL
#define MY_VIRTIO_IOMMU_RELAY_SIZE 0x1000ULL

typedef struct MyVirtioDmaConfig {
    void *iommu;
    uint32_t endpoint_id;
} MyVirtioDmaConfig;

void *my_virtio_iommu_create(hwaddr start, hwaddr size, qemu_irq irq,
                             int mode);

void my_virtio_blk_create(hwaddr start, hwaddr size, qemu_irq irq,
                          const char *image_path, bool packed,
                          const MyVirtioDmaConfig *dma);
void my_virtio_net_create(hwaddr start, hwaddr size, qemu_irq irq,
                          const char *hostfwd, const char *network,
                          const char *netmask, const char *host_ip,
                          const char *dhcp_start, const char *dns_ip,
                          bool packed, const MyVirtioDmaConfig *dma);
void my_virtio_console_create(hwaddr start, hwaddr size, qemu_irq irq,
                              const char *backend, const char *input_path,
                              const char *output_path, bool packed,
                              const MyVirtioDmaConfig *dma);
void *my_virtio_gpu_create(hwaddr start, hwaddr size, qemu_irq irq,
                           const char *vnc_listen, bool packed,
                           const MyVirtioDmaConfig *dma);
void my_virtio_keyboard_create(hwaddr start, hwaddr size, qemu_irq irq,
                               const char *backend, const char *evdev_path,
                               void *ui, bool packed,
                               const MyVirtioDmaConfig *dma);
void my_virtio_mouse_create(hwaddr start, hwaddr size, qemu_irq irq,
                            const char *backend, const char *evdev_path,
                            void *ui, bool packed,
                            const MyVirtioDmaConfig *dma);
void my_virtio_tablet_create(hwaddr start, hwaddr size, qemu_irq irq,
                             const char *backend, const char *evdev_path,
                             void *ui, bool packed,
                             const MyVirtioDmaConfig *dma);
void my_virtio_genirq_create(hwaddr start, hwaddr size, qemu_irq irq,
                             bool packed, const MyVirtioDmaConfig *dma);

#endif
