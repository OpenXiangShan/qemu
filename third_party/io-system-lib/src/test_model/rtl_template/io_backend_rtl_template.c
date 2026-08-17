#include "io_cmodel_internal.h"

/*
 * Software test double for exercising the backend selector and IO2Q AXI
 * transaction plumbing.  This is intentionally not the real RTL-system path:
 * a picker/VCS-backed DUT lives under src/rtl_model/io_system and owns its
 * internal MMIO decode in io_system_q2io_read/write.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define IO_RTL_TEMPLATE_MMIO_BASE       0x0ULL
#define IO_RTL_TEMPLATE_MMIO_SIZE       0x1000ULL

#define IO_RTL_TEMPLATE_REG_ID          0x00
#define IO_RTL_TEMPLATE_REG_VERSION     0x04
#define IO_RTL_TEMPLATE_REG_CONTROL     0x08
#define IO_RTL_TEMPLATE_REG_STATUS      0x0c
#define IO_RTL_TEMPLATE_REG_OUT_ADDR    0x10
#define IO_RTL_TEMPLATE_REG_OUT_LEN     0x18
#define IO_RTL_TEMPLATE_REG_LAST_RESP   0x1c
#define IO_RTL_TEMPLATE_REG_OUT_DATA    0x40
#define IO_RTL_TEMPLATE_OUT_DATA_SIZE   IO_AXI_MAX_BEAT_BYTES

#define IO_RTL_TEMPLATE_ID              0x544c5452u /* "RTLT" */
#define IO_RTL_TEMPLATE_VERSION         1u

#define IO_RTL_TEMPLATE_CONTROL_KICK    (1u << 0)
#define IO_RTL_TEMPLATE_CONTROL_CLEAR   (1u << 1)
#define IO_RTL_TEMPLATE_CONTROL_CANCEL  (1u << 2)

#define IO_RTL_TEMPLATE_STATUS_PENDING  (1u << 0)
#define IO_RTL_TEMPLATE_STATUS_ISSUED   (1u << 1)
#define IO_RTL_TEMPLATE_STATUS_DONE     (1u << 2)
#define IO_RTL_TEMPLATE_STATUS_ERROR    (1u << 3)

typedef struct IoSystemRtlTemplateBackend {
    IoSystemBackend base;
    IoCModelMmioWindowTable windows;

    uint64_t out_addr;
    uint32_t out_len;
    uint32_t last_response;
    uint16_t next_transaction_id;
    bool pending_valid;
    bool pending_issued;
    bool done;
    bool error;

    IoAxiTransaction pending_txn;
    uint8_t out_data[IO_AXI_MAX_BEAT_BYTES];
    uint8_t pending_data[IO_AXI_MAX_BEAT_BYTES];
    uint8_t pending_strobe[IO_AXI_MAX_BEAT_BYTES];
} IoSystemRtlTemplateBackend;

static IoSystemRtlTemplateBackend *io_system_rtl_template_backend(
    IoSystemBackend *backend)
{
    return (IoSystemRtlTemplateBackend *)backend;
}

static uint64_t io_rtl_template_load_le(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint64_t value = 0;

    for (size_t i = 0; i < size && i < sizeof(value); i++) {
        value |= (uint64_t)bytes[i] << (8 * i);
    }

    return value;
}

static void io_rtl_template_store_le(void *data, uint64_t value, size_t size)
{
    uint8_t *bytes = data;

    for (size_t i = 0; i < size; i++) {
        bytes[i] = (uint8_t)(value >> (8 * i));
    }
}

static uint64_t io_rtl_template_mask(unsigned int bits)
{
    return bits >= 64 ? UINT64_MAX : ((UINT64_C(1) << bits) - 1u);
}

static IoSystemStatus io_rtl_template_read_integer(void *data,
                                                   size_t size,
                                                   uint64_t value,
                                                   uint64_t reg_base,
                                                   uint64_t access_offset,
                                                   size_t reg_size)
{
    uint64_t reg_end = reg_base + reg_size;
    uint64_t access_end = access_offset + size;
    unsigned int shift;

    if (!data || !size || access_offset < reg_base ||
        access_end > reg_end || access_end <= access_offset) {
        return IO_SYSTEM_ERR_INVALID;
    }

    shift = (unsigned int)((access_offset - reg_base) * 8u);
    io_rtl_template_store_le(data, value >> shift, size);
    return IO_SYSTEM_OK;
}

static IoSystemStatus io_rtl_template_write_integer(uint64_t *reg,
                                                    uint64_t reg_base,
                                                    uint64_t access_offset,
                                                    const void *data,
                                                    size_t size,
                                                    size_t reg_size)
{
    uint64_t reg_end = reg_base + reg_size;
    uint64_t access_end = access_offset + size;
    unsigned int shift;
    uint64_t mask;
    uint64_t value;

    if (!reg || !data || !size || access_offset < reg_base ||
        access_end > reg_end || access_end <= access_offset) {
        return IO_SYSTEM_ERR_INVALID;
    }

    shift = (unsigned int)((access_offset - reg_base) * 8u);
    mask = io_rtl_template_mask((unsigned int)(size * 8u)) << shift;
    value = io_rtl_template_load_le(data, size) << shift;
    *reg = (*reg & ~mask) | (value & mask);
    return IO_SYSTEM_OK;
}

static uint32_t io_rtl_template_status(const IoSystemRtlTemplateBackend *rtl)
{
    uint32_t status = 0;

    if (rtl->pending_valid) {
        status |= IO_RTL_TEMPLATE_STATUS_PENDING;
    }
    if (rtl->pending_issued) {
        status |= IO_RTL_TEMPLATE_STATUS_ISSUED;
    }
    if (rtl->done) {
        status |= IO_RTL_TEMPLATE_STATUS_DONE;
    }
    if (rtl->error) {
        status |= IO_RTL_TEMPLATE_STATUS_ERROR;
    }

    return status;
}

static void io_rtl_template_reset_state(IoSystemRtlTemplateBackend *rtl)
{
    rtl->out_addr = 0;
    rtl->out_len = 4;
    rtl->last_response = IO_AXI_RESPONSE_OKAY;
    rtl->next_transaction_id = 1;
    rtl->pending_valid = false;
    rtl->pending_issued = false;
    rtl->done = false;
    rtl->error = false;
    memset(&rtl->pending_txn, 0, sizeof(rtl->pending_txn));
    memset(rtl->out_data, 0, sizeof(rtl->out_data));
    memset(rtl->pending_data, 0, sizeof(rtl->pending_data));
    memset(rtl->pending_strobe, 0, sizeof(rtl->pending_strobe));
}

static IoSystemStatus io_rtl_template_queue_master_write(
    IoSystemRtlTemplateBackend *rtl)
{
    if (rtl->pending_valid || !rtl->out_len ||
        rtl->out_len > IO_AXI_MAX_BEAT_BYTES) {
        rtl->error = true;
        return IO_SYSTEM_ERR_IO;
    }

    memset(&rtl->pending_txn, 0, sizeof(rtl->pending_txn));
    memset(rtl->pending_data, 0, sizeof(rtl->pending_data));
    memset(rtl->pending_strobe, 0, sizeof(rtl->pending_strobe));

    memcpy(rtl->pending_data, rtl->out_data, rtl->out_len);
    memset(rtl->pending_strobe, 0xff, rtl->out_len);

    rtl->pending_txn.direction = IO_AXI_DIRECTION_IO2Q_WRITE;
    rtl->pending_txn.transaction_id = rtl->next_transaction_id++;
    rtl->pending_txn.address = rtl->out_addr;
    rtl->pending_txn.beat_count = 1;
    rtl->pending_txn.beat_size = (uint8_t)rtl->out_len;
    rtl->pending_txn.burst_type = IO_AXI_BURST_INCR;
    rtl->pending_txn.data = rtl->pending_data;
    rtl->pending_txn.byte_strobe = rtl->pending_strobe;
    rtl->pending_txn.last = true;
    rtl->pending_txn.response = IO_AXI_RESPONSE_OKAY;
    rtl->pending_txn.translated = true;

    rtl->pending_valid = true;
    rtl->pending_issued = false;
    rtl->done = false;
    rtl->error = false;
    rtl->last_response = IO_AXI_RESPONSE_OKAY;
    return IO_SYSTEM_OK;
}

static IoSystemStatus io_rtl_template_read(void *opaque,
                                           uint64_t addr,
                                           void *data,
                                           size_t size)
{
    IoSystemRtlTemplateBackend *rtl = opaque;
    uint64_t offset = addr - IO_RTL_TEMPLATE_MMIO_BASE;
    uint64_t value;

    if (offset >= IO_RTL_TEMPLATE_REG_OUT_DATA &&
        offset + size <= IO_RTL_TEMPLATE_REG_OUT_DATA +
                         IO_RTL_TEMPLATE_OUT_DATA_SIZE) {
        memcpy(data, rtl->out_data + (offset - IO_RTL_TEMPLATE_REG_OUT_DATA),
               size);
        return IO_SYSTEM_OK;
    }

    switch (offset & ~UINT64_C(0x7)) {
    case IO_RTL_TEMPLATE_REG_ID:
        if (offset < IO_RTL_TEMPLATE_REG_VERSION) {
            return io_rtl_template_read_integer(data, size,
                                                IO_RTL_TEMPLATE_ID,
                                                IO_RTL_TEMPLATE_REG_ID,
                                                offset, sizeof(uint32_t));
        }
        return io_rtl_template_read_integer(data, size,
                                            IO_RTL_TEMPLATE_VERSION,
                                            IO_RTL_TEMPLATE_REG_VERSION,
                                            offset, sizeof(uint32_t));
    case IO_RTL_TEMPLATE_REG_CONTROL:
        if (offset < IO_RTL_TEMPLATE_REG_STATUS) {
            return io_rtl_template_read_integer(data, size, 0,
                                                IO_RTL_TEMPLATE_REG_CONTROL,
                                                offset, sizeof(uint32_t));
        }
        value = io_rtl_template_status(rtl);
        return io_rtl_template_read_integer(data, size, value,
                                            IO_RTL_TEMPLATE_REG_STATUS,
                                            offset, sizeof(uint32_t));
    case IO_RTL_TEMPLATE_REG_OUT_ADDR:
        return io_rtl_template_read_integer(data, size, rtl->out_addr,
                                            IO_RTL_TEMPLATE_REG_OUT_ADDR,
                                            offset, sizeof(uint64_t));
    case IO_RTL_TEMPLATE_REG_OUT_LEN:
        if (offset < IO_RTL_TEMPLATE_REG_LAST_RESP) {
            return io_rtl_template_read_integer(data, size, rtl->out_len,
                                                IO_RTL_TEMPLATE_REG_OUT_LEN,
                                                offset, sizeof(uint32_t));
        }
        return io_rtl_template_read_integer(data, size, rtl->last_response,
                                            IO_RTL_TEMPLATE_REG_LAST_RESP,
                                            offset, sizeof(uint32_t));
    default:
        memset(data, 0xff, size);
        return IO_SYSTEM_ERR_UNMAPPED;
    }
}

static IoSystemStatus io_rtl_template_write(void *opaque,
                                            uint64_t addr,
                                            const void *data,
                                            size_t size)
{
    IoSystemRtlTemplateBackend *rtl = opaque;
    uint64_t offset = addr - IO_RTL_TEMPLATE_MMIO_BASE;
    uint64_t value;
    IoSystemStatus status;

    if (offset >= IO_RTL_TEMPLATE_REG_OUT_DATA &&
        offset + size <= IO_RTL_TEMPLATE_REG_OUT_DATA +
                         IO_RTL_TEMPLATE_OUT_DATA_SIZE) {
        memcpy(rtl->out_data + (offset - IO_RTL_TEMPLATE_REG_OUT_DATA),
               data, size);
        return IO_SYSTEM_OK;
    }

    switch (offset & ~UINT64_C(0x7)) {
    case IO_RTL_TEMPLATE_REG_CONTROL:
        if (offset != IO_RTL_TEMPLATE_REG_CONTROL ||
            size > sizeof(uint32_t)) {
            return IO_SYSTEM_ERR_INVALID;
        }

        value = io_rtl_template_load_le(data, size);
        if (value & IO_RTL_TEMPLATE_CONTROL_CANCEL) {
            rtl->pending_valid = false;
            rtl->pending_issued = false;
        }
        if (value & IO_RTL_TEMPLATE_CONTROL_CLEAR) {
            rtl->done = false;
            rtl->error = false;
            rtl->last_response = IO_AXI_RESPONSE_OKAY;
        }
        if (value & IO_RTL_TEMPLATE_CONTROL_KICK) {
            return io_rtl_template_queue_master_write(rtl);
        }
        return IO_SYSTEM_OK;
    case IO_RTL_TEMPLATE_REG_OUT_ADDR:
        return io_rtl_template_write_integer(&rtl->out_addr,
                                             IO_RTL_TEMPLATE_REG_OUT_ADDR,
                                             offset, data, size,
                                             sizeof(uint64_t));
    case IO_RTL_TEMPLATE_REG_OUT_LEN:
        if (offset >= IO_RTL_TEMPLATE_REG_LAST_RESP) {
            return IO_SYSTEM_ERR_INVALID;
        }
        value = rtl->out_len;
        status = io_rtl_template_write_integer(&value,
                                               IO_RTL_TEMPLATE_REG_OUT_LEN,
                                               offset, data, size,
                                               sizeof(uint32_t));
        if (status != IO_SYSTEM_OK) {
            return status;
        }
        if (!value || value > IO_AXI_MAX_BEAT_BYTES) {
            rtl->error = true;
            return IO_SYSTEM_ERR_INVALID;
        }
        rtl->out_len = (uint32_t)value;
        return IO_SYSTEM_OK;
    default:
        return IO_SYSTEM_ERR_UNMAPPED;
    }
}

static void io_rtl_template_destroy(IoSystemBackend *backend)
{
    free(io_system_rtl_template_backend(backend));
}

static IoSystemStatus io_rtl_template_reset(IoSystemBackend *backend)
{
    io_rtl_template_reset_state(io_system_rtl_template_backend(backend));
    return IO_SYSTEM_OK;
}

static IoSystemStatus io_rtl_template_q2io_read(IoSystemBackend *backend,
                                                uint64_t addr,
                                                void *data,
                                                size_t size)
{
    IoSystemRtlTemplateBackend *rtl =
        io_system_rtl_template_backend(backend);

    return io_cmodel_mmio_read_dispatch(backend->system, &rtl->windows,
                                        addr, data, size);
}

static IoSystemStatus io_rtl_template_q2io_write(IoSystemBackend *backend,
                                                 uint64_t addr,
                                                 const void *data,
                                                 size_t size)
{
    IoSystemRtlTemplateBackend *rtl =
        io_system_rtl_template_backend(backend);

    return io_cmodel_mmio_write_dispatch(backend->system, &rtl->windows,
                                         addr, data, size);
}

static IoSystemStatus io_rtl_template_m_axi_next(IoSystemBackend *backend,
                                                 IoAxiTransaction *txn)
{
    IoSystemRtlTemplateBackend *rtl =
        io_system_rtl_template_backend(backend);

    if (!rtl->pending_valid) {
        return IO_SYSTEM_ERR_NO_TRANSACTION;
    }

    *txn = rtl->pending_txn;
    rtl->pending_issued = true;
    io_system_trace(backend->system, "m_axi", "next", txn->address,
                    txn->data, txn->beat_size, IO_AXI_RESPONSE_OKAY);
    return IO_SYSTEM_OK;
}

static IoSystemStatus io_rtl_template_m_axi_complete(
    IoSystemBackend *backend,
    const IoAxiTransaction *txn)
{
    IoSystemRtlTemplateBackend *rtl =
        io_system_rtl_template_backend(backend);

    if (!rtl->pending_valid) {
        return IO_SYSTEM_ERR_NO_TRANSACTION;
    }
    if (txn->transaction_id != rtl->pending_txn.transaction_id ||
        txn->direction != rtl->pending_txn.direction ||
        txn->address != rtl->pending_txn.address ||
        txn->beat_size != rtl->pending_txn.beat_size) {
        rtl->error = true;
        return IO_SYSTEM_ERR_INVALID;
    }

    rtl->last_response = txn->response;
    rtl->done = true;
    rtl->error = txn->response != IO_AXI_RESPONSE_OKAY;
    rtl->pending_valid = false;
    rtl->pending_issued = false;

    io_system_trace(backend->system, "m_axi", "complete", txn->address,
                    txn->data, txn->beat_size, txn->response);
    return IO_SYSTEM_OK;
}

static IoSystemStatus io_rtl_template_service(IoSystemBackend *backend,
                                               uint32_t budget)
{
    IoSystemRtlTemplateBackend *rtl =
        io_system_rtl_template_backend(backend);
    IoAxiTransaction txn;
    IoSystemStatus status;

    if (!budget) {
        budget = 1;
    }
    if (!rtl->pending_valid || !budget) {
        return IO_SYSTEM_OK;
    }
    status = io_rtl_template_m_axi_next(backend, &txn);
    if (status != IO_SYSTEM_OK) {
        return status == IO_SYSTEM_ERR_NO_TRANSACTION ? IO_SYSTEM_OK : status;
    }
    return io_system_service_io2q_transaction(backend->system, &txn);
}

static bool io_rtl_template_needs_service(IoSystemBackend *backend)
{
    return io_system_rtl_template_backend(backend)->pending_valid;
}

static IoSystemStatus io_rtl_template_io2q_complete(
    IoSystemBackend *backend, const IoAxiTransaction *txn)
{
    return io_rtl_template_m_axi_complete(backend, txn);
}

static IoSystemStatus io_rtl_template_save_state(IoSystemBackend *backend,
                                                 void *buf,
                                                 size_t buf_size,
                                                 size_t *written)
{
    (void)backend;
    (void)buf;
    (void)buf_size;

    if (written) {
        *written = 0;
    }
    return IO_SYSTEM_ERR_UNSUPPORTED;
}

static IoSystemStatus io_rtl_template_load_state(IoSystemBackend *backend,
                                                 const void *buf,
                                                 size_t buf_size)
{
    (void)backend;
    (void)buf;
    (void)buf_size;

    return IO_SYSTEM_ERR_UNSUPPORTED;
}

static const IoSystemBackendOps io_rtl_template_ops = {
    .destroy = io_rtl_template_destroy,
    .reset = io_rtl_template_reset,
    .q2io_read = io_rtl_template_q2io_read,
    .q2io_write = io_rtl_template_q2io_write,
    .service = io_rtl_template_service,
    .needs_service = io_rtl_template_needs_service,
    .io2q_complete = io_rtl_template_io2q_complete,
    .save_state = io_rtl_template_save_state,
    .load_state = io_rtl_template_load_state,
};

IoSystemBackend *io_system_backend_rtl_template_create(IoSystem *system)
{
    IoSystemRtlTemplateBackend *rtl = calloc(1, sizeof(*rtl));
    IoSystemStatus status;

    if (!rtl) {
        return NULL;
    }

    rtl->base.system = system;
    rtl->base.ops = &io_rtl_template_ops;
    io_cmodel_mmio_window_table_init(&rtl->windows);
    io_rtl_template_reset_state(rtl);

    status = io_cmodel_mmio_window_table_register(&rtl->windows,
                                                  "rtl-template",
                                                  IO_RTL_TEMPLATE_MMIO_BASE,
                                                  IO_RTL_TEMPLATE_MMIO_SIZE,
                                                  io_rtl_template_read,
                                                  io_rtl_template_write,
                                                  rtl);
    if (status != IO_SYSTEM_OK) {
        free(rtl);
        return NULL;
    }

    return &rtl->base;
}
