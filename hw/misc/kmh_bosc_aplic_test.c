/*
 * BOSC Kunminghu APLIC interrupt-source test device.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/intc/riscv_aplic.h"
#include "hw/misc/kmh_bosc_aplic_test.h"
#include "hw/qdev-core.h"
#include "hw/resettable.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_KMH_BOSC_APLIC_TEST "kmh-bosc-aplic-test"
OBJECT_DECLARE_SIMPLE_TYPE(KmhBoscAplicTestState, KMH_BOSC_APLIC_TEST)

struct KmhBoscAplicTestState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irqs[KMH_BOSC_APLIC_TEST_NUM_IRQS];
    DeviceState *aplic;
};

static bool kmh_bosc_aplic_test_source_excluded(uint32_t source)
{
    return (source >= 7 && source <= 31) ||
           (source >= 43 && source <= 63) ||
           (source >= 89 && source <= 95) ||
           (source >= 108 && source <= 127) ||
           (source >= 130 && source <= 159) ||
           (source >= 160 && source <= 192);
}

static void kmh_bosc_aplic_test_raise_all(KmhBoscAplicTestState *s)
{
    for (uint32_t i = 0; i < KMH_BOSC_APLIC_TEST_NUM_IRQS; i++) {
        uint32_t source = i + 1;

        if (kmh_bosc_aplic_test_source_excluded(source)) {
            continue;
        }
        qemu_irq_raise(s->irqs[i]);
    }
}

static uint64_t kmh_bosc_aplic_test_read(void *opaque, hwaddr offset,
                                         unsigned size)
{
    (void)opaque;
    (void)size;

    if (offset != KMH_BOSC_APLIC_TEST_CLEAR_OFFSET) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid read at offset 0x%" HWADDR_PRIx "\n",
                      TYPE_KMH_BOSC_APLIC_TEST, offset);
    }
    return 0;
}

static void kmh_bosc_aplic_test_write(void *opaque, hwaddr offset,
                                      uint64_t value, unsigned size)
{
    KmhBoscAplicTestState *s = opaque;

    (void)size;
    if (offset != KMH_BOSC_APLIC_TEST_CLEAR_OFFSET) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid write at offset 0x%" HWADDR_PRIx "\n",
                      TYPE_KMH_BOSC_APLIC_TEST, offset);
        return;
    }

    if (value == KMH_BOSC_APLIC_TEST_RAISE_ALL) {
        kmh_bosc_aplic_test_raise_all(s);
        return;
    }
    if (value < 1 || value > KMH_BOSC_APLIC_TEST_NUM_IRQS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid interrupt source %" PRIu64 "\n",
                      TYPE_KMH_BOSC_APLIC_TEST, value);
        return;
    }

    qemu_irq_lower(s->irqs[value - 1]);
}

static const MemoryRegionOps kmh_bosc_aplic_test_ops = {
    .read = kmh_bosc_aplic_test_read,
    .write = kmh_bosc_aplic_test_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void kmh_bosc_aplic_test_reset(DeviceState *dev)
{
    KmhBoscAplicTestState *s = KMH_BOSC_APLIC_TEST(dev);

    for (uint32_t i = 0; i < KMH_BOSC_APLIC_TEST_NUM_IRQS; i++) {
        qemu_irq_lower(s->irqs[i]);
    }
}

static void kmh_bosc_aplic_test_init(Object *obj)
{
    KmhBoscAplicTestState *s = KMH_BOSC_APLIC_TEST(obj);

    qdev_init_gpio_out(DEVICE(obj), s->irqs,
                       KMH_BOSC_APLIC_TEST_NUM_IRQS);
}

static void kmh_bosc_aplic_test_realize(DeviceState *dev, Error **errp)
{
    KmhBoscAplicTestState *s = KMH_BOSC_APLIC_TEST(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &kmh_bosc_aplic_test_ops, s,
                          TYPE_KMH_BOSC_APLIC_TEST,
                          KMH_BOSC_APLIC_TEST_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void kmh_bosc_aplic_test_class_init(ObjectClass *klass,
                                           const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = kmh_bosc_aplic_test_realize;
    device_class_set_legacy_reset(dc, kmh_bosc_aplic_test_reset);
    dc->desc = "BOSC Kunminghu APLIC interrupt-source test device";
    dc->user_creatable = false;
}

static const TypeInfo kmh_bosc_aplic_test_info = {
    .name = TYPE_KMH_BOSC_APLIC_TEST,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(KmhBoscAplicTestState),
    .instance_init = kmh_bosc_aplic_test_init,
    .class_init = kmh_bosc_aplic_test_class_init,
};

static void kmh_bosc_aplic_test_register_types(void)
{
    type_register_static(&kmh_bosc_aplic_test_info);
}

type_init(kmh_bosc_aplic_test_register_types)

void kmh_bosc_aplic_test_create(DeviceState *aplic, hwaddr addr)
{
    DeviceState *dev = qdev_new(TYPE_KMH_BOSC_APLIC_TEST);
    KmhBoscAplicTestState *s = KMH_BOSC_APLIC_TEST(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    s->aplic = aplic;
    sysbus_realize(sbd, &error_fatal);
    sysbus_mmio_map_overlap(sbd, 0, addr, 1);

    for (uint32_t i = 0; i < KMH_BOSC_APLIC_TEST_NUM_IRQS; i++) {
        qdev_connect_gpio_out(dev, i, qdev_get_gpio_in(aplic, i + 1));
    }
    device_cold_reset(dev);
    object_unref(OBJECT(dev));
}
