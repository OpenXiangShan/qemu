#include "qemu/osdep.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "qemu/event_notifier.h"
#include "qemu/module.h"
#include "sysemu/kvm.h"
#include "qom/object.h"

#define PCI_VENDOR_ID_BOSC 0x1234
#define PCI_DEVICE_ID_BOSC_DMAENGINE 0x0001

#define MY_DMAENGINE_MMIO_CH0_SRC        0x0
#define MY_DMAENGINE_MMIO_CH0_DST        0x8
#define MY_DMAENGINE_MMIO_CH0_TRAN_SIZE  0x100
#define MY_DMAENGINE_MMIO_CH0_START      0x200
#define MY_DMAENGINE_MMIO_CH0_DONE       0x10

#define MY_DMAENGINE_MMIO_CH1_SRC        0x1000
#define MY_DMAENGINE_MMIO_CH1_DST        0x1008
#define MY_DMAENGINE_MMIO_CH1_TRAN_SIZE  0x1100
#define MY_DMAENGINE_MMIO_CH1_START      0x1200
#define MY_DMAENGINE_MMIO_CH1_DONE       0x1010

#define MY_DMAENGINE_CHANNEL_NUM 2

struct my_dmaengine_channels {
    uint64_t src;
    uint64_t dst;
    uint32_t buf_size;
    uint8_t *buf;
    uint8_t done;
    uint32_t transfer_size;
};

typedef struct __PCIMyDmaEngineState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/
    MemoryRegion mmio;
    struct my_dmaengine_channels channels[MY_DMAENGINE_CHANNEL_NUM];
    qemu_irq irq;
    Object *obj;
} PCIMyDmaEngineState;

#define TO_DMAENGINE_STATE(obj) OBJECT_CHECK(PCIMyDmaEngineState, obj, "my_dmaengine")

static void my_dmaengine_uninit(PCIMyDmaEngineState *s)
{
    struct my_dmaengine_channels *chn;
    int i;

    for (i = 0; i < MY_DMAENGINE_CHANNEL_NUM; i++) {
        chn = &s->channels[i];
        if (chn->buf) {
            g_free(chn->buf);
            chn->buf_size = 0;
        }
    }
}

static void my_dmaengine_init(PCIMyDmaEngineState *s)
{
    struct my_dmaengine_channels *chn;
    int i;

    for (i = 0; i < MY_DMAENGINE_CHANNEL_NUM; i++) {
        chn = &s->channels[i];
        memset(chn, 0, sizeof(struct my_dmaengine_channels));
        chn->buf_size = 4096;
        chn->buf = g_malloc0(chn->buf_size);
    }
}

static void my_dmaengine_start(PCIMyDmaEngineState *s, int nr)
{
    struct my_dmaengine_channels *chn = &s->channels[nr];
    int cnt = chn->transfer_size / chn->buf_size;
    int left = chn->transfer_size % chn->buf_size;
    int i;
    PCIDevice *pdev = PCI_DEVICE(s->obj);

    for (i = 0; i < cnt; i++) {
        pci_dma_read(pdev, chn->src + i * chn->buf_size, chn->buf, chn->buf_size);
        pci_dma_write(pdev, chn->dst + i * chn->buf_size, chn->buf, chn->buf_size);
    }

    if (left > 0) {
        pci_dma_read(pdev, chn->src + (chn->transfer_size - left), chn->buf, left);
        pci_dma_write(pdev, chn->dst + (chn->transfer_size - left), chn->buf, left);
    }

    smp_wmb();

    chn->done = 1;
}

static void my_dmaengine_mmio_write32(PCIMyDmaEngineState *s, hwaddr offset, uint32_t val)
{
    switch (offset) {
    case MY_DMAENGINE_MMIO_CH0_TRAN_SIZE:
        s->channels[0].transfer_size = val;
        break;
    case MY_DMAENGINE_MMIO_CH1_TRAN_SIZE:
        s->channels[1].transfer_size = val;
        break;
    case MY_DMAENGINE_MMIO_CH0_START:
        my_dmaengine_start(s, 0);
        break;
    case MY_DMAENGINE_MMIO_CH1_START:
        my_dmaengine_start(s, 1);
        break;
    }
}

static void my_dmaengine_mmio_write64(PCIMyDmaEngineState *s, hwaddr offset, uint64_t val)
{
    switch(offset) {
    case MY_DMAENGINE_MMIO_CH0_SRC:
        s->channels[0].src = val;
        break;
    case MY_DMAENGINE_MMIO_CH0_DST:
        s->channels[0].dst = val;
        break;
    case MY_DMAENGINE_MMIO_CH1_SRC:
        s->channels[1].src = val;
        break;
    case MY_DMAENGINE_MMIO_CH1_DST:
        s->channels[1].dst = val;
        break;
    }
}

static void
pci_my_dmaengine_mmio_write(void *opaque, hwaddr offset, uint64_t val,
                            unsigned size)
{
    PCIMyDmaEngineState *s = opaque;

    if (size == 4) {
        my_dmaengine_mmio_write32(s, offset, val);
    } else if (size == 8)
        my_dmaengine_mmio_write64(s, offset, val);
}

static uint64_t
pci_my_dmaengine_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    PCIMyDmaEngineState *s = opaque;
    uint64_t ret = 0;

    switch(offset) {
    case MY_DMAENGINE_MMIO_CH0_SRC:
        ret = s->channels[0].src;
        break;
    case MY_DMAENGINE_MMIO_CH0_DST:
        ret = s->channels[0].dst;
        break;
    case MY_DMAENGINE_MMIO_CH1_SRC:
        ret = s->channels[1].src;
        break;
    case MY_DMAENGINE_MMIO_CH1_DST:
        ret = s->channels[1].dst;
        break;
    case MY_DMAENGINE_MMIO_CH0_DONE:
        ret = s->channels[0].done;
        s->channels[0].done = 0;
        break;
    case MY_DMAENGINE_MMIO_CH1_DONE:
        ret = s->channels[1].done;
        s->channels[1].done = 0;
        break;
    }

    return ret;
}

static const MemoryRegionOps pci_my_dmaengine_mmio_ops = {
    .read = pci_my_dmaengine_mmio_read,
    .write = pci_my_dmaengine_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};


static void pci_my_dmaengine_realize(PCIDevice *pci_dev, Error **errp)
{
    PCIMyDmaEngineState *s = TO_DMAENGINE_STATE(pci_dev);
    uint8_t *pci_conf = pci_dev->config;
#if 0
    Error *err = NULL;

    pci_conf[PCI_INTERRUPT_PIN] = 1;
    if (msi_init(pdev, 0, 1, true, false, &err))
	error_free(err);
#else
    pci_conf[PCI_INTERRUPT_PIN] = 0;
#endif
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
    pcie_endpoint_cap_init(pci_dev, 0xe0);

    my_dmaengine_init(s);
}

static void pci_my_dmaengine_init(Object *obj)
{
    PCIMyDmaEngineState *s = TO_DMAENGINE_STATE(obj);

    memory_region_init_io(&s->mmio, OBJECT(obj), &pci_my_dmaengine_mmio_ops, s,
                          "pci-mydmaengine-mmio", 4096 * 4);

    s->obj = obj;
}

static void
pci_my_dmaengine_uninit(PCIDevice *dev)
{
    PCIMyDmaEngineState *s = TO_DMAENGINE_STATE(dev);

    my_dmaengine_uninit(s);
}

static void qdev_pci_my_dmaengine_reset(DeviceState *dev)
{

}

static void pci_my_dmaengine_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_my_dmaengine_realize;
    k->exit = pci_my_dmaengine_uninit;
    k->vendor_id = PCI_VENDOR_ID_BOSC;
    k->device_id = PCI_DEVICE_ID_BOSC_DMAENGINE;
    k->revision = 0x10;
    k->class_id = PCI_CLASS_OTHERS;
    dc->desc = "PCI MyDmaengine Device";
    dc->reset = qdev_pci_my_dmaengine_reset;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo pci_my_dmaengine_info = {
    .name          = "my_dmaengine",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIMyDmaEngineState),
    .instance_init = pci_my_dmaengine_init,
    .class_init    = pci_my_dmaengine_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { },
    },
};

static void pci_my_dmaengine_register_types(void)
{
    type_register_static(&pci_my_dmaengine_info);
}

type_init(pci_my_dmaengine_register_types)
