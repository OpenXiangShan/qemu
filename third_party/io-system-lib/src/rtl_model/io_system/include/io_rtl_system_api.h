// SPDX-License-Identifier: Apache-2.0
#ifndef IO_RTL_SYSTEM_API_H
#define IO_RTL_SYSTEM_API_H

#include <stddef.h>
#include <stdint.h>

#include "io_axi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct io_rtl_system_handle io_rtl_system_handle_t;

typedef int (*io_rtl_system_guest_read_cb)(void *opaque, uint64_t gpa,
                                         void *dst, uint32_t len);
typedef int (*io_rtl_system_guest_write_cb)(void *opaque, uint64_t gpa,
                                          const void *src, uint32_t len);
typedef void (*io_rtl_system_log_cb)(void *opaque, int level,
                                   const char *msg);

typedef struct io_rtl_system_callbacks {
    void *opaque;
    io_rtl_system_guest_read_cb guest_read;
    io_rtl_system_guest_write_cb guest_write;
    io_rtl_system_log_cb log;
} io_rtl_system_callbacks_t;

typedef struct io_rtl_system_config {
    /*
     * Informational host-side hint for the RTL io-system MMIO aperture.
     * Address decode itself is owned by the RTL wrapper.
     */
    uint64_t mmio_base_hint;
    uint64_t mmio_size_hint;
    uint32_t io2q_max_beat_bytes;
    uint32_t io2q_outstanding;
    const char *wave_file;
    int trace;
} io_rtl_system_config_t;

enum {
    IO_RTL_SYSTEM_OK = 0,
    IO_RTL_SYSTEM_ERR_INVALID = -1,
    IO_RTL_SYSTEM_ERR_NOMEM = -2,
    IO_RTL_SYSTEM_ERR_UNMAPPED = -3,
    IO_RTL_SYSTEM_ERR_UNSUPPORTED = -4,
    IO_RTL_SYSTEM_ERR_IO = -5,
    IO_RTL_SYSTEM_ERR_TIMEOUT = -6,
    IO_RTL_SYSTEM_ERR_NO_TRANSACTION = -7,
};

io_rtl_system_handle_t *io_rtl_system_open(
    const io_rtl_system_config_t *config,
    const io_rtl_system_callbacks_t *callbacks);
void io_rtl_system_close(io_rtl_system_handle_t *handle);
int io_rtl_system_reset(io_rtl_system_handle_t *handle);
int io_rtl_system_mmio_read(io_rtl_system_handle_t *handle, uint64_t addr,
                          void *data, size_t size);
int io_rtl_system_mmio_write(io_rtl_system_handle_t *handle, uint64_t addr,
                           const void *data, size_t size);
int io_rtl_system_gbus_read(io_rtl_system_handle_t *handle, uint32_t addr,
                          uint32_t *value);
int io_rtl_system_gbus_write(io_rtl_system_handle_t *handle, uint32_t addr,
                           uint32_t value);
int io_rtl_system_m_axi_next(io_rtl_system_handle_t *handle,
                           IoAxiTransaction *txn);
int io_rtl_system_m_axi_complete(io_rtl_system_handle_t *handle,
                               const IoAxiTransaction *txn);
int io_rtl_system_step(io_rtl_system_handle_t *handle, uint64_t cycles);
int io_rtl_system_poll(io_rtl_system_handle_t *handle);
int io_rtl_system_needs_service(io_rtl_system_handle_t *handle);
const char *io_rtl_system_last_error(io_rtl_system_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif
