/*
 * QTest testcase for qemu_to_iosystem machine
 *
 * Copyright (c) 2026 Beijing Institute of Open Source Chip (BOSC)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "libqtest.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define QTI_APLIC_M_BASE      0x31100000ULL
#define QTI_APLIC_S_BASE      0x31120000ULL
#define QTI_NET_BASE          0x31090000ULL
#define QTI_BLK_BASE          0x310a0000ULL
#define QTI_DWC_DBI_BASE      0x32000000ULL
#define QTI_IMSIC_M_BASE      0x3a800000ULL
#define QTI_IMSIC_S_BASE      0x3b000000ULL
#define QTI_PCIE_ECAM_BASE    0x67ff0000ULL
#define QTI_QUEUE_BASE        0x80010000ULL
#define QTI_REQ_OUT_BASE      0x80020000ULL
#define QTI_REQ_IN_BASE       0x80021000ULL
#define QTI_MSI_ADDR          0x80030000ULL
#define QTI_IMAGE_SIZE        4096
#define QTI_PAGE_SIZE         4096
#define QTI_QUEUE_NUM         128
#define QTI_BLK_IRQ           15
#define QTI_BLK_EIID          0x55

#define APLIC_DOMAINCFG       0x0000
#define APLIC_SOURCECFG_BASE  0x0004
#define APLIC_MMSICFGADDR     0x1bc0
#define APLIC_MMSICFGADDRH    0x1bc4
#define APLIC_SMSICFGADDR     0x1bc8
#define APLIC_SMSICFGADDRH    0x1bcc
#define APLIC_SETIP_BASE      0x1c00
#define APLIC_SETIPNUM        0x1cdc
#define APLIC_SETIENUM        0x1edc
#define APLIC_TARGET_BASE     0x3004

#define APLIC_DOMAINCFG_IE    (1u << 8)
#define APLIC_SOURCECFG_EDGE_RISE 0x4

#define VIRTIO_MMIO_MAGIC_VALUE        0x000
#define VIRTIO_MMIO_VERSION            0x004
#define VIRTIO_MMIO_DEVICE_ID          0x008
#define VIRTIO_MMIO_HOST_FEATURES      0x010
#define VIRTIO_MMIO_GUEST_FEATURES     0x020
#define VIRTIO_MMIO_GUEST_FEATURES_SEL 0x024
#define VIRTIO_MMIO_GUEST_PAGE_SIZE    0x028
#define VIRTIO_MMIO_QUEUE_SEL          0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX      0x034
#define VIRTIO_MMIO_QUEUE_NUM          0x038
#define VIRTIO_MMIO_QUEUE_ALIGN        0x03c
#define VIRTIO_MMIO_QUEUE_PFN          0x040
#define VIRTIO_MMIO_QUEUE_NOTIFY       0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS   0x060
#define VIRTIO_MMIO_INTERRUPT_ACK      0x064
#define VIRTIO_MMIO_STATUS             0x070
#define VIRTIO_MMIO_CONFIG             0x100

#define VIRTIO_BLK_T_IN                0u
#define VIRTIO_BLK_T_OUT               1u
#define VIRTIO_BLK_S_OK                0u

#define VRING_DESC_F_NEXT              1u
#define VRING_DESC_F_WRITE             2u

#define PCI_VENDOR_ID_SYNOPSYS         0x16c3
#define PCI_DEVICE_ID_DW_PCIE          0xabcd
#define PCI_VENDOR_ID_REDHAT           0x1b36
#define PCI_DEVICE_ID_REDHAT_NVME      0x0010
#define PCI_PRIMARY_BUS_REG            0x18
#define PCI_SECONDARY_BUS              0x01
#define PCI_SUBORDINATE_BUS            0xff
#define PCIE_ECAM_BUS_SHIFT            20

#define DWC_ATU_VIEWPORT               0x900
#define DWC_ATU_CR1                    0x904
#define DWC_ATU_CR2                    0x908
#define DWC_ATU_LOWER_BASE             0x90c
#define DWC_ATU_UPPER_BASE             0x910
#define DWC_ATU_LIMIT                  0x914
#define DWC_ATU_LOWER_TARGET           0x918
#define DWC_ATU_UPPER_TARGET           0x91c
#define DWC_ATU_TYPE_CFG0              0x4
#define DWC_ATU_ENABLE                 0x80000000u

typedef struct QtiBlkDesc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} QEMU_PACKED QtiBlkDesc;

typedef struct QtiBlkReq {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
} QEMU_PACKED QtiBlkReq;

typedef struct QtiMsiMem {
    uint64_t addr;
    uint32_t value;
    int writes;
} QtiMsiMem;

static uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1u) & ~(align - 1u);
}

static void qti_write32(QTestState *qts, uint64_t addr, uint32_t value)
{
    qtest_writel(qts, addr, value);
}

static uint32_t qti_read32(QTestState *qts, uint64_t addr)
{
    return qtest_readl(qts, addr);
}

static uint64_t qti_read64(QTestState *qts, uint64_t addr)
{
    return qtest_readq(qts, addr);
}

static void qti_mem_write_desc(QTestState *qts, uint64_t addr, uint64_t buf,
                               uint32_t len, uint16_t flags, uint16_t next)
{
    QtiBlkDesc desc = {
        .addr = cpu_to_le64(buf),
        .len = cpu_to_le32(len),
        .flags = cpu_to_le16(flags),
        .next = cpu_to_le16(next),
    };

    qtest_memwrite(qts, addr, &desc, sizeof(desc));
}

static void qti_mem_write_req(QTestState *qts, uint64_t addr, uint32_t type,
                              uint64_t sector)
{
    QtiBlkReq req = {
        .type = cpu_to_le32(type),
        .ioprio = 0,
        .sector = cpu_to_le64(sector),
    };

    qtest_memwrite(qts, addr, &req, sizeof(req));
}

static void qti_configure_aplic_for_blk(QTestState *qts)
{
    qtest_writel(qts, QTI_APLIC_M_BASE + APLIC_SMSICFGADDR,
                 QTI_MSI_ADDR >> 12);
    qtest_writel(qts, QTI_APLIC_M_BASE + APLIC_SMSICFGADDRH, 0);
    qtest_writel(qts, QTI_APLIC_S_BASE + APLIC_SOURCECFG_BASE +
                 (QTI_BLK_IRQ - 1) * 4, APLIC_SOURCECFG_EDGE_RISE);
    qtest_writel(qts, QTI_APLIC_S_BASE + APLIC_TARGET_BASE +
                 (QTI_BLK_IRQ - 1) * 4, QTI_BLK_EIID);
    qtest_writel(qts, QTI_APLIC_S_BASE + APLIC_DOMAINCFG, APLIC_DOMAINCFG_IE);
    qtest_writel(qts, QTI_APLIC_S_BASE + APLIC_SETIENUM, QTI_BLK_IRQ);
}

static void qti_configure_blk(QTestState *qts)
{
    uint32_t features;

    g_assert_cmphex(qti_read32(qts, QTI_BLK_BASE + VIRTIO_MMIO_MAGIC_VALUE),
                    ==, 0x74726976u);
    g_assert_cmpuint(qti_read32(qts, QTI_BLK_BASE + VIRTIO_MMIO_VERSION),
                     ==, 1);
    g_assert_cmpuint(qti_read32(qts, QTI_BLK_BASE + VIRTIO_MMIO_DEVICE_ID),
                     ==, 2);
    g_assert_cmpuint(qti_read64(qts, QTI_BLK_BASE + VIRTIO_MMIO_CONFIG),
                     ==, QTI_IMAGE_SIZE / 512);

    features = qti_read32(qts, QTI_BLK_BASE + VIRTIO_MMIO_HOST_FEATURES);
    qti_write32(qts, QTI_BLK_BASE + VIRTIO_MMIO_GUEST_FEATURES_SEL, 0);
    qti_write32(qts, QTI_BLK_BASE + VIRTIO_MMIO_GUEST_FEATURES, features);
    qti_write32(qts, QTI_BLK_BASE + VIRTIO_MMIO_GUEST_PAGE_SIZE, QTI_PAGE_SIZE);
    qti_write32(qts, QTI_BLK_BASE + VIRTIO_MMIO_QUEUE_SEL, 0);
    g_assert_cmpuint(qti_read32(qts, QTI_BLK_BASE + VIRTIO_MMIO_QUEUE_NUM_MAX),
                     ==, 128);
    qti_write32(qts, QTI_BLK_BASE + VIRTIO_MMIO_QUEUE_NUM, QTI_QUEUE_NUM);
    qti_write32(qts, QTI_BLK_BASE + VIRTIO_MMIO_QUEUE_ALIGN, QTI_PAGE_SIZE);
    qti_write32(qts, QTI_BLK_BASE + VIRTIO_MMIO_QUEUE_PFN,
                QTI_QUEUE_BASE / QTI_PAGE_SIZE);
    qti_write32(qts, QTI_BLK_BASE + VIRTIO_MMIO_STATUS, 0x0f);
}

static void qti_submit_write_request(QTestState *qts)
{
    const char payload[512] = "qti-io-system-blk-write";
    uint64_t used_pa = QTI_QUEUE_BASE + align_up(16u * QTI_QUEUE_NUM + 4u +
                                                 2u * QTI_QUEUE_NUM,
                                                 QTI_PAGE_SIZE);
    uint64_t avail_pa = QTI_QUEUE_BASE + 16u * QTI_QUEUE_NUM;
    uint8_t status = 0xff;

    qti_mem_write_req(qts, QTI_REQ_OUT_BASE, VIRTIO_BLK_T_OUT, 0);
    qtest_memwrite(qts, QTI_REQ_OUT_BASE + 16, payload, sizeof(payload));
    qtest_memwrite(qts, QTI_REQ_OUT_BASE + 16 + 512, &status, sizeof(status));

    qti_mem_write_desc(qts, QTI_QUEUE_BASE + 0 * sizeof(QtiBlkDesc),
                       QTI_REQ_OUT_BASE, 16, VRING_DESC_F_NEXT, 1);
    qti_mem_write_desc(qts, QTI_QUEUE_BASE + 1 * sizeof(QtiBlkDesc),
                       QTI_REQ_OUT_BASE + 16, 512, VRING_DESC_F_NEXT, 2);
    qti_mem_write_desc(qts, QTI_QUEUE_BASE + 2 * sizeof(QtiBlkDesc),
                       QTI_REQ_OUT_BASE + 16 + 512, 1, VRING_DESC_F_WRITE, 0);
    uint16_t ring0 = cpu_to_le16(0);
    uint16_t avail_idx = cpu_to_le16(1);
    qtest_memwrite(qts, avail_pa + 4, &ring0, sizeof(ring0));
    qtest_memwrite(qts, avail_pa + 2, &avail_idx, sizeof(avail_idx));

    qti_write32(qts, QTI_BLK_BASE + VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    g_assert_cmpuint(qtest_readb(qts, QTI_REQ_OUT_BASE + 16 + 512),
                     ==, VIRTIO_BLK_S_OK);
    g_assert_cmpuint(qtest_readw(qts, used_pa + 2), ==, 1);
    g_assert_cmpuint(qtest_readl(qts, used_pa + 4), ==, 0);
    g_assert_cmpuint(qtest_readl(qts, used_pa + 8), ==, 512);
    g_assert_cmpuint(qti_read32(qts, QTI_BLK_BASE + VIRTIO_MMIO_INTERRUPT_STATUS) &
                     1u, !=, 0);
    qti_write32(qts, QTI_BLK_BASE + VIRTIO_MMIO_INTERRUPT_ACK, 1);

    for (int i = 0; i < 32 && qtest_readl(qts, QTI_MSI_ADDR) != QTI_BLK_EIID;
         i++) {
        qtest_readl(qts, QTI_APLIC_S_BASE + APLIC_DOMAINCFG);
    }
    g_assert_cmpuint(qtest_readl(qts, QTI_MSI_ADDR), ==, QTI_BLK_EIID);
}

static void qti_submit_read_request(QTestState *qts)
{
    uint64_t used_pa = QTI_QUEUE_BASE + align_up(16u * QTI_QUEUE_NUM + 4u +
                                                 2u * QTI_QUEUE_NUM,
                                                 QTI_PAGE_SIZE);
    uint64_t avail_pa = QTI_QUEUE_BASE + 16u * QTI_QUEUE_NUM;
    uint8_t status = 0xff;
    char buffer[512] = { 0 };

    qti_mem_write_req(qts, QTI_REQ_IN_BASE, VIRTIO_BLK_T_IN, 0);
    qtest_memwrite(qts, QTI_REQ_IN_BASE + 16, buffer, sizeof(buffer));
    qtest_memwrite(qts, QTI_REQ_IN_BASE + 16 + 512, &status, sizeof(status));

    qti_mem_write_desc(qts, QTI_QUEUE_BASE + 3 * sizeof(QtiBlkDesc),
                       QTI_REQ_IN_BASE, 16, VRING_DESC_F_NEXT, 4);
    qti_mem_write_desc(qts, QTI_QUEUE_BASE + 4 * sizeof(QtiBlkDesc),
                       QTI_REQ_IN_BASE + 16, 512,
                       VRING_DESC_F_NEXT | VRING_DESC_F_WRITE, 5);
    qti_mem_write_desc(qts, QTI_QUEUE_BASE + 5 * sizeof(QtiBlkDesc),
                       QTI_REQ_IN_BASE + 16 + 512, 1, VRING_DESC_F_WRITE, 0);
    uint16_t ring1 = cpu_to_le16(3);
    uint16_t avail_idx = cpu_to_le16(2);
    qtest_memwrite(qts, avail_pa + 4 + 2, &ring1, sizeof(ring1));
    qtest_memwrite(qts, avail_pa + 2, &avail_idx, sizeof(avail_idx));

    qti_write32(qts, QTI_BLK_BASE + VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    qtest_memread(qts, QTI_REQ_IN_BASE + 16, buffer, sizeof(buffer));
    g_assert_cmpstr(buffer, ==, "qti-io-system-blk-write");
    g_assert_cmpuint(qtest_readb(qts, QTI_REQ_IN_BASE + 16 + 512),
                     ==, VIRTIO_BLK_S_OK);
    g_assert_cmpuint(qtest_readw(qts, used_pa + 2), ==, 2);
    g_assert_cmpuint(qtest_readl(qts, used_pa + 4 + 8), ==, 3);
    g_assert_cmpuint(qtest_readl(qts, used_pa + 4 + 8 + 4), ==, 512);
}

static void test_machine_virtio_blk(void)
{
    char image_path[] = "/tmp/qti-blk.XXXXXX";
    char trace_path[] = "/tmp/qti-trace.XXXXXX";
    QTestState *qts;
    uint8_t zero[0x2000] = { 0 };
    int image_fd;
    int trace_fd;
    g_autofree char *trace = NULL;
    gsize trace_len = 0;

    image_fd = mkstemp(image_path);
    g_assert_cmpint(image_fd, >=, 0);
    g_assert_cmpint(ftruncate(image_fd, QTI_IMAGE_SIZE), ==, 0);

    trace_fd = mkstemp(trace_path);
    g_assert_cmpint(trace_fd, >=, 0);
    close(trace_fd);

    qts = qtest_initf("-M qemu_to_iosystem,generated-dtb=off,"
                      "my-virtio-blk=on,my-virtio-blk-image=%s,"
                      "io-system-trace-file=%s "
                      "-m 128M -smp 1 -bios none -nodefaults -serial none",
                      image_path, trace_path);

    qtest_memwrite(qts, QTI_QUEUE_BASE, zero, sizeof(zero));
    qtest_memwrite(qts, QTI_REQ_OUT_BASE, zero, 0x1000);
    qtest_memwrite(qts, QTI_REQ_IN_BASE, zero, 0x1000);

    qti_configure_aplic_for_blk(qts);
    qti_configure_blk(qts);
    qti_submit_write_request(qts);
    qti_submit_read_request(qts);

    qtest_quit(qts);
    g_assert_true(g_file_get_contents(trace_path, &trace, &trace_len, NULL));
    g_assert_nonnull(strstr(trace, "port=io2q channel=memory-read"));
    g_assert_nonnull(strstr(trace, "port=io2q channel=memory-write"));
    g_assert_nonnull(strstr(trace, "address=0x0000000080030000"));
    close(image_fd);
    unlink(image_path);
    unlink(trace_path);
}

static void test_machine_rtl_system_virtio_blk(void)
{
    const char *rtl_api = g_getenv("IO_RTL_SYSTEM_API_SO");
    char image_path[] = "/tmp/qti-rtl-blk.XXXXXX";
    char trace_path[] = "/tmp/qti-rtl-trace.XXXXXX";
    QTestState *qts;
    uint8_t zero[0x2000] = { 0 };
    int image_fd;
    int trace_fd;
    g_autofree char *trace = NULL;
    gsize trace_len = 0;

    if (!rtl_api || !*rtl_api ||
        !g_file_test(rtl_api, G_FILE_TEST_IS_REGULAR)) {
        g_test_skip("IO_RTL_SYSTEM_API_SO does not name a built RTL API");
        return;
    }

    image_fd = mkstemp(image_path);
    g_assert_cmpint(image_fd, >=, 0);
    g_assert_cmpint(ftruncate(image_fd, QTI_IMAGE_SIZE), ==, 0);

    trace_fd = mkstemp(trace_path);
    g_assert_cmpint(trace_fd, >=, 0);
    close(trace_fd);

    qts = qtest_initf("-M qemu_to_iosystem,generated-dtb=off,"
                      "io-system-backend=rtl-system,"
                      "io-system-backend-lib=%s,"
                      "my-virtio-blk=on,my-virtio-blk-image=%s,"
                      "io-system-trace-file=%s "
                      "-m 128M -smp 1 -bios none -nodefaults -serial none",
                      rtl_api, image_path, trace_path);

    qtest_memwrite(qts, QTI_QUEUE_BASE, zero, sizeof(zero));
    qtest_memwrite(qts, QTI_REQ_OUT_BASE, zero, 0x1000);
    qtest_memwrite(qts, QTI_REQ_IN_BASE, zero, 0x1000);

    qti_configure_aplic_for_blk(qts);
    g_assert_cmphex(qtest_readl(qts, QTI_APLIC_S_BASE + APLIC_DOMAINCFG),
                    ==, 0x80000104u);
    g_assert_cmphex(qtest_readl(qts, QTI_APLIC_S_BASE +
                                APLIC_SOURCECFG_BASE +
                                (QTI_BLK_IRQ - 1) * 4),
                    ==, APLIC_SOURCECFG_EDGE_RISE);
    g_assert_cmphex(qtest_readl(qts, QTI_APLIC_S_BASE + APLIC_TARGET_BASE +
                                (QTI_BLK_IRQ - 1) * 4),
                    ==, QTI_BLK_EIID);
    g_assert_cmphex(qtest_readl(qts, QTI_APLIC_S_BASE + APLIC_SETIP_BASE +
                                4 * (QTI_BLK_IRQ / 32)),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, QTI_APLIC_S_BASE + 0x1e00 +
                                4 * (QTI_BLK_IRQ / 32)) &
                    (1u << (QTI_BLK_IRQ % 32)),
                    ==, 1u << (QTI_BLK_IRQ % 32));

    qtest_writel(qts, QTI_APLIC_S_BASE + APLIC_SETIPNUM, QTI_BLK_IRQ);
    g_assert_cmphex(qtest_readl(qts, QTI_APLIC_S_BASE + APLIC_SETIP_BASE) &
                    (1u << QTI_BLK_IRQ),
                    ==, 0);
    for (int i = 0; i < 32 && qtest_readl(qts, QTI_MSI_ADDR) != QTI_BLK_EIID;
         i++) {
        qtest_readl(qts, QTI_APLIC_S_BASE + APLIC_DOMAINCFG);
    }
    g_assert_cmphex(qtest_readl(qts, QTI_MSI_ADDR), ==, QTI_BLK_EIID);
    qtest_writel(qts, QTI_MSI_ADDR, 0);

    qtest_writel(qts, QTI_APLIC_M_BASE + APLIC_SMSICFGADDR,
                 QTI_IMSIC_S_BASE >> 12);
    qtest_writel(qts, QTI_APLIC_M_BASE + APLIC_SMSICFGADDRH, 0);
    qtest_writel(qts, QTI_APLIC_S_BASE + APLIC_SETIPNUM, QTI_BLK_IRQ);
    for (int i = 0; i < 32 &&
         (qtest_readl(qts, QTI_APLIC_S_BASE + APLIC_SETIP_BASE) &
          (1u << QTI_BLK_IRQ)); i++) {
        qtest_readl(qts, QTI_APLIC_S_BASE + APLIC_DOMAINCFG);
    }
    g_assert_cmphex(qtest_readl(qts, QTI_APLIC_S_BASE + APLIC_SETIP_BASE) &
                    (1u << QTI_BLK_IRQ),
                    ==, 0);
    qtest_writel(qts, QTI_APLIC_M_BASE + APLIC_SMSICFGADDR,
                 QTI_MSI_ADDR >> 12);
    qtest_writel(qts, QTI_APLIC_M_BASE + APLIC_SMSICFGADDRH, 0);

    qti_configure_blk(qts);
    qti_submit_write_request(qts);
    qti_submit_read_request(qts);
    g_assert_cmphex(qtest_readl(qts, QTI_NET_BASE), ==, 0xffffffffu);

    /* The VCS DPI runtime converts libqtest's SIGTERM into exit status 255. */
    qtest_set_expected_status(qts, 255);
    qtest_quit(qts);
    g_assert_true(g_file_get_contents(trace_path, &trace, &trace_len, NULL));
    g_assert_nonnull(strstr(trace, "port=io2q channel=memory-read"));
    g_assert_nonnull(strstr(trace, "port=io2q channel=memory-write"));
    g_assert_nonnull(strstr(trace, "address=0x0000000080030000"));
    g_assert_nonnull(strstr(trace,
                            "address=0x000000003b000000 beat_index=0 "
                            "beat_size=4 response=OKAY"));
    g_assert_nonnull(strstr(trace, "address=0x0000000031090000"));
    g_assert_nonnull(strstr(trace, "response=DECERR"));
    close(image_fd);
    unlink(image_path);
    unlink(trace_path);
}

static void test_machine_aplic_mmio(void)
{
    QTestState *qts;
    uint32_t value;

    qts = qtest_initf("-M qemu_to_iosystem,generated-dtb=off "
                      "-m 128M -smp 1 -bios none -nodefaults -serial none");

    value = qtest_readl(qts, QTI_APLIC_M_BASE + APLIC_DOMAINCFG);
    g_assert_cmphex(value, ==, 0x80000004);

    qtest_writel(qts, QTI_APLIC_M_BASE + APLIC_DOMAINCFG,
                 APLIC_DOMAINCFG_IE);
    value = qtest_readl(qts, QTI_APLIC_M_BASE + APLIC_DOMAINCFG);
    g_assert_cmphex(value & APLIC_DOMAINCFG_IE, ==, APLIC_DOMAINCFG_IE);

    qtest_writel(qts, QTI_APLIC_M_BASE + APLIC_MMSICFGADDR,
                 QTI_IMSIC_M_BASE >> 12);
    qtest_writel(qts, QTI_APLIC_M_BASE + APLIC_MMSICFGADDRH, 0);

    qtest_writel(qts, QTI_APLIC_M_BASE + APLIC_SOURCECFG_BASE,
                 APLIC_SOURCECFG_EDGE_RISE);
    value = qtest_readl(qts, QTI_APLIC_M_BASE + APLIC_SOURCECFG_BASE);
    g_assert_cmphex(value, ==, APLIC_SOURCECFG_EDGE_RISE);

    qtest_writel(qts, QTI_APLIC_M_BASE + APLIC_TARGET_BASE, 0x55);
    value = qtest_readl(qts, QTI_APLIC_M_BASE + APLIC_TARGET_BASE);
    g_assert_cmphex(value, ==, 0x55);

    qtest_writel(qts, QTI_APLIC_M_BASE + APLIC_SETIPNUM, 1);
    value = qtest_readl(qts, QTI_APLIC_M_BASE + APLIC_SETIP_BASE);
    g_assert_cmphex(value & (1u << 1), ==, (1u << 1));

    qtest_writel(qts, QTI_APLIC_M_BASE + APLIC_SETIENUM, 1);
    value = qtest_readl(qts, QTI_APLIC_M_BASE + APLIC_SETIP_BASE);
    g_assert_cmphex(value & (1u << 1), ==, 0);

    value = qtest_readl(qts, QTI_IMSIC_M_BASE);
    g_assert_cmphex(value, ==, 0x0);

    value = qtest_readl(qts, QTI_IMSIC_S_BASE);
    g_assert_cmphex(value, ==, 0x0);

    qtest_quit(qts);
}

static void test_machine_service_mode_bh(void)
{
    char image_path[] = "/tmp/qti-bh-blk.XXXXXX";
    QTestState *qts;
    uint8_t zero[0x2000] = { 0 };
    int image_fd;

    image_fd = mkstemp(image_path);
    g_assert_cmpint(image_fd, >=, 0);
    g_assert_cmpint(ftruncate(image_fd, QTI_IMAGE_SIZE), ==, 0);
    close(image_fd);

    qts = qtest_initf("-M qemu_to_iosystem,generated-dtb=off,"
                      "io2q-async=on,io2q-outstanding=4,"
                      "io-system-service-mode=bh,my-virtio-blk=on,"
                      "my-virtio-blk-image=%s "
                      "-m 128M -smp 1 -bios none -nodefaults -serial none",
                      image_path);

    qtest_memwrite(qts, QTI_QUEUE_BASE, zero, sizeof(zero));
    qtest_memwrite(qts, QTI_REQ_OUT_BASE, zero, 0x1000);
    qtest_memwrite(qts, QTI_REQ_IN_BASE, zero, 0x1000);
    qti_configure_aplic_for_blk(qts);
    qti_configure_blk(qts);
    qti_submit_write_request(qts);
    qtest_quit(qts);
    unlink(image_path);
}

static void test_machine_unimplemented_bridge(void)
{
    char trace_path[] = "/tmp/qti-unmapped-trace.XXXXXX";
    QTestState *qts;
    int trace_fd;
    g_autofree char *trace = NULL;
    gsize trace_len = 0;

    trace_fd = mkstemp(trace_path);
    g_assert_cmpint(trace_fd, >=, 0);
    close(trace_fd);

    qts = qtest_initf("-M qemu_to_iosystem,generated-dtb=off,"
                      "io-system-trace-file=%s "
                      "-m 128M -smp 1 -bios none -nodefaults -serial none",
                      trace_path);

    g_assert_cmphex(qtest_readl(qts, QTI_NET_BASE), ==, 0xffffffffu);

    qtest_quit(qts);
    g_assert_true(g_file_get_contents(trace_path, &trace, &trace_len, NULL));
    g_assert_nonnull(strstr(trace, "address=0x0000000031090000"));
    g_assert_nonnull(strstr(trace, "response=DECERR"));
    unlink(trace_path);
}

static void test_machine_dwc_pcie_root(void)
{
    QTestState *qts;
    uint32_t id;

    qts = qtest_initf("-M qemu_to_iosystem,generated-dtb=off,dw-pcie=on "
                      "-m 128M -smp 1 -bios none -nodefaults -serial none");

    id = qtest_readl(qts, QTI_DWC_DBI_BASE);
    g_assert_cmphex(id & 0xffff, ==, PCI_VENDOR_ID_SYNOPSYS);
    g_assert_cmphex(id >> 16, ==, PCI_DEVICE_ID_DW_PCIE);

    id = qtest_readl(qts, QTI_PCIE_ECAM_BASE);
    g_assert_cmphex(id & 0xffff, ==, PCI_VENDOR_ID_SYNOPSYS);
    g_assert_cmphex(id >> 16, ==, PCI_DEVICE_ID_DW_PCIE);

    qtest_quit(qts);
}

static void qti_pcie_program_bus_numbers(QTestState *qts)
{
    uint32_t bus_numbers = (PCI_SUBORDINATE_BUS << 16) |
                           (PCI_SECONDARY_BUS << 8);

    qtest_writel(qts, QTI_DWC_DBI_BASE + PCI_PRIMARY_BUS_REG, bus_numbers);
}

static void qti_pcie_program_cfg0_iatu(QTestState *qts, uint8_t bus,
                                       uint8_t devfn)
{
    qtest_writel(qts, QTI_DWC_DBI_BASE + DWC_ATU_VIEWPORT, 0);
    qtest_writel(qts, QTI_DWC_DBI_BASE + DWC_ATU_LOWER_BASE,
                 QTI_PCIE_ECAM_BASE);
    qtest_writel(qts, QTI_DWC_DBI_BASE + DWC_ATU_UPPER_BASE, 0);
    qtest_writel(qts, QTI_DWC_DBI_BASE + DWC_ATU_LIMIT,
                 QTI_PCIE_ECAM_BASE + 0xfffff);
    qtest_writel(qts, QTI_DWC_DBI_BASE + DWC_ATU_LOWER_TARGET,
                 (bus << 24) | (devfn << 16));
    qtest_writel(qts, QTI_DWC_DBI_BASE + DWC_ATU_UPPER_TARGET, 0);
    qtest_writel(qts, QTI_DWC_DBI_BASE + DWC_ATU_CR1, DWC_ATU_TYPE_CFG0);
    qtest_writel(qts, QTI_DWC_DBI_BASE + DWC_ATU_CR2, DWC_ATU_ENABLE);
}

static void test_machine_dwc_pcie_nvme_config(void)
{
    char image_path[] = "/tmp/qti-nvme.XXXXXX";
    QTestState *qts;
    uint32_t id;
    int image_fd;

    image_fd = mkstemp(image_path);
    g_assert_cmpint(image_fd, >=, 0);
    g_assert_cmpint(ftruncate(image_fd, 8 * 1024 * 1024), ==, 0);
    close(image_fd);

    qts = qtest_initf("-M qemu_to_iosystem,generated-dtb=off,dw-pcie=on "
                      "-m 128M -smp 1 -bios none -nodefaults -serial none "
                      "-drive file=%s,format=raw,if=none,id=nvme0 "
                      "-device nvme,drive=nvme0,serial=qti-nvme0,bus=qti-pcie",
                      image_path);

    qti_pcie_program_bus_numbers(qts);

    id = qtest_readl(qts, QTI_PCIE_ECAM_BASE +
                     (PCI_SECONDARY_BUS << PCIE_ECAM_BUS_SHIFT));
    g_assert_cmphex(id & 0xffff, ==, PCI_VENDOR_ID_REDHAT);
    g_assert_cmphex(id >> 16, ==, PCI_DEVICE_ID_REDHAT_NVME);

    qtest_quit(qts);
    unlink(image_path);
}

static void test_machine_dwc_pcie_nvme_cfg_iatu(void)
{
    char image_path[] = "/tmp/qti-nvme.XXXXXX";
    QTestState *qts;
    uint32_t id;
    int image_fd;

    image_fd = mkstemp(image_path);
    g_assert_cmpint(image_fd, >=, 0);
    g_assert_cmpint(ftruncate(image_fd, 8 * 1024 * 1024), ==, 0);
    close(image_fd);

    qts = qtest_initf("-M qemu_to_iosystem,generated-dtb=off,dw-pcie=on "
                      "-m 128M -smp 1 -bios none -nodefaults -serial none "
                      "-drive file=%s,format=raw,if=none,id=nvme0 "
                      "-device nvme,drive=nvme0,serial=qti-nvme0,bus=qti-pcie",
                      image_path);

    qti_pcie_program_bus_numbers(qts);
    qti_pcie_program_cfg0_iatu(qts, PCI_SECONDARY_BUS, 0);

    id = qtest_readl(qts, QTI_PCIE_ECAM_BASE);
    g_assert_cmphex(id & 0xffff, ==, PCI_VENDOR_ID_REDHAT);
    g_assert_cmphex(id >> 16, ==, PCI_DEVICE_ID_REDHAT_NVME);

    qtest_quit(qts);
    unlink(image_path);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/qemu_to_iosystem/aplic-mmio",
                   test_machine_aplic_mmio);
    qtest_add_func("/qemu_to_iosystem/service-mode-bh",
                   test_machine_service_mode_bh);
    qtest_add_func("/qemu_to_iosystem/unimplemented-bridge",
                   test_machine_unimplemented_bridge);
    qtest_add_func("/qemu_to_iosystem/virtio-blk",
                   test_machine_virtio_blk);
    qtest_add_func("/qemu_to_iosystem/rtl-system-virtio-blk",
                   test_machine_rtl_system_virtio_blk);
    qtest_add_func("/qemu_to_iosystem/dwc-pcie-root",
                   test_machine_dwc_pcie_root);
    qtest_add_func("/qemu_to_iosystem/dwc-pcie-nvme-config",
                   test_machine_dwc_pcie_nvme_config);
    qtest_add_func("/qemu_to_iosystem/dwc-pcie-nvme-cfg-iatu",
                   test_machine_dwc_pcie_nvme_cfg_iatu);

    return g_test_run();
}
