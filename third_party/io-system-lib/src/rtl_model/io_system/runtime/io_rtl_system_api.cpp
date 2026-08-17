// SPDX-License-Identifier: Apache-2.0
#include "io_rtl_system_api.h"

#include "io_rtl_system_sim.hpp"

#include <new>

struct io_rtl_system_handle {
    explicit io_rtl_system_handle(const io_rtl_system_config_t *config,
                                const io_rtl_system_callbacks_t *callbacks)
        : runtime(config, callbacks)
    {
    }

    io_rtl_system::Runtime runtime;
};

extern "C" {

io_rtl_system_handle_t *io_rtl_system_open(
    const io_rtl_system_config_t *config,
    const io_rtl_system_callbacks_t *callbacks)
{
    try {
        return new io_rtl_system_handle(config, callbacks);
    } catch (...) {
        return nullptr;
    }
}

void io_rtl_system_close(io_rtl_system_handle_t *handle)
{
    delete handle;
}

int io_rtl_system_reset(io_rtl_system_handle_t *handle)
{
    if (!handle) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }
    try {
        return handle->runtime.Reset();
    } catch (...) {
        return IO_RTL_SYSTEM_ERR_IO;
    }
}

int io_rtl_system_mmio_read(io_rtl_system_handle_t *handle, uint64_t addr,
                          void *data, size_t size)
{
    if (!handle) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }
    try {
        return handle->runtime.MmioRead(addr, data, size);
    } catch (...) {
        return IO_RTL_SYSTEM_ERR_IO;
    }
}

int io_rtl_system_mmio_write(io_rtl_system_handle_t *handle, uint64_t addr,
                           const void *data, size_t size)
{
    if (!handle) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }
    try {
        return handle->runtime.MmioWrite(addr, data, size);
    } catch (...) {
        return IO_RTL_SYSTEM_ERR_IO;
    }
}

int io_rtl_system_gbus_read(io_rtl_system_handle_t *handle, uint32_t addr,
                          uint32_t *value)
{
    if (!handle || !value) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }
    try {
        return handle->runtime.GbusRead(addr, value);
    } catch (...) {
        return IO_RTL_SYSTEM_ERR_IO;
    }
}

int io_rtl_system_gbus_write(io_rtl_system_handle_t *handle, uint32_t addr,
                           uint32_t value)
{
    if (!handle) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }
    try {
        return handle->runtime.GbusWrite(addr, value);
    } catch (...) {
        return IO_RTL_SYSTEM_ERR_IO;
    }
}

int io_rtl_system_m_axi_next(io_rtl_system_handle_t *handle,
                           IoAxiTransaction *txn)
{
    if (!handle) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }
    try {
        return handle->runtime.NextMasterTransaction(txn);
    } catch (...) {
        return IO_RTL_SYSTEM_ERR_IO;
    }
}

int io_rtl_system_m_axi_complete(io_rtl_system_handle_t *handle,
                               const IoAxiTransaction *txn)
{
    if (!handle) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }
    try {
        return handle->runtime.CompleteMasterTransaction(txn);
    } catch (...) {
        return IO_RTL_SYSTEM_ERR_IO;
    }
}

int io_rtl_system_step(io_rtl_system_handle_t *handle, uint64_t cycles)
{
    if (!handle) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }
    try {
        return handle->runtime.Step(cycles);
    } catch (...) {
        return IO_RTL_SYSTEM_ERR_IO;
    }
}

int io_rtl_system_poll(io_rtl_system_handle_t *handle)
{
    if (!handle) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }
    try {
        return handle->runtime.Poll();
    } catch (...) {
        return IO_RTL_SYSTEM_ERR_IO;
    }
}

int io_rtl_system_needs_service(io_rtl_system_handle_t *handle)
{
    if (!handle) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }
    return handle->runtime.NeedsService() ? 1 : 0;
}

const char *io_rtl_system_last_error(io_rtl_system_handle_t *handle)
{
    if (!handle) {
        return "invalid io-rtl-system handle";
    }
    return handle->runtime.LastError();
}

}
