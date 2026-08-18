#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct iommu_handle iommu_handle_t;

typedef struct iommu_request_context {
    uint32_t device_id;
    uint32_t process_id;
    uint8_t process_id_valid;
    uint8_t is_translated;
} iommu_request_context_t;

typedef struct iommu_ace_lite_attrs {
    uint8_t cache;
    uint8_t prot;
    uint8_t region;
    uint8_t qos;
    uint8_t snoop;
    uint8_t domain;
    uint8_t idunq;
    uint8_t loop;
    uint8_t awatop;
} iommu_ace_lite_attrs_t;

typedef struct iommu_ats_request {
    iommu_request_context_t context;
    uint64_t iova;
    size_t length;
    bool no_write;
    bool priv_req;
    bool exec_req;
} iommu_ats_request_t;

typedef struct iommu_ats_response {
    uint64_t translated_addr;
    uint64_t addr_mask;
    uint32_t perm;
    uint32_t err_count;
} iommu_ats_response_t;

typedef struct iommu_pri_request {
    iommu_request_context_t context;
    uint64_t iova;
    uint16_t prgi;
    bool lpig;
    bool is_read;
    bool is_write;
    bool priv_req;
    bool exec_req;
} iommu_pri_request_t;

typedef enum iommu_pri_response_code {
    IOMMU_API_PRI_RESP_SUCCESS = 0,
    IOMMU_API_PRI_RESP_INVALID_REQUEST = 1,
    IOMMU_API_PRI_RESP_FAILURE = 2,
} iommu_pri_response_code_t;

typedef struct iommu_pri_response {
    uint32_t response_code;
    uint16_t prgi;
} iommu_pri_response_t;

typedef void (*downstream_write_callback_t)(uint64_t addr,
                                            const uint8_t *data,
                                            size_t data_len,
                                            uint32_t strobe,
                                            uint16_t id,
                                            size_t beat_index,
                                            size_t beat_count,
                                            void *user_data);

typedef int (*downstream_read_callback_t)(uint64_t addr,
                                          uint8_t *data,
                                          size_t data_len,
                                          uint16_t id,
                                          size_t beat_index,
                                          size_t beat_count,
                                          void *user_data);

typedef void (*downstream_write_callback_v2_t)(uint64_t addr,
                                               const uint8_t *data,
                                               size_t data_len,
                                               uint32_t strobe,
                                               uint16_t id,
                                               size_t beat_index,
                                               size_t beat_count,
                                               const iommu_ace_lite_attrs_t *attrs,
                                               void *user_data);

typedef int (*downstream_read_callback_v2_t)(uint64_t addr,
                                             uint8_t *data,
                                             size_t data_len,
                                             uint16_t id,
                                             size_t beat_index,
                                             size_t beat_count,
                                             const iommu_ace_lite_attrs_t *attrs,
                                             void *user_data);

typedef void (*translation_write_callback_t)(uint64_t addr,
                                             const uint8_t *data,
                                             size_t data_len,
                                             uint32_t strobe,
                                             uint16_t id,
                                             size_t beat_index,
                                             size_t beat_count,
                                             void *user_data);

typedef int (*translation_read_callback_t)(uint64_t addr,
                                           uint8_t *data,
                                           size_t data_len,
                                           uint16_t id,
                                           size_t beat_index,
                                           size_t beat_count,
                                           void *user_data);

typedef void (*translation_write_callback_v2_t)(uint64_t addr,
                                                const uint8_t *data,
                                                size_t data_len,
                                                uint32_t strobe,
                                                uint16_t id,
                                                size_t beat_index,
                                                size_t beat_count,
                                                const iommu_ace_lite_attrs_t *attrs,
                                                void *user_data);

typedef int (*translation_read_callback_v2_t)(uint64_t addr,
                                              uint8_t *data,
                                              size_t data_len,
                                              uint16_t id,
                                              size_t beat_index,
                                              size_t beat_count,
                                              const iommu_ace_lite_attrs_t *attrs,
                                              void *user_data);

// Callbacks run on the library's worker thread. Do not call the blocking C APIs
// again from inside a callback.
iommu_handle_t *iommu_open(void);
iommu_handle_t *iommu_open_with_args(int argc, char **argv);
void iommu_close(iommu_handle_t *handle);
void iommu_set_trace(iommu_handle_t *handle, int enabled);
void iommu_set_trace_txn_id(iommu_handle_t *handle, uint32_t id, int enabled);

int mmio_write(iommu_handle_t *handle, uint32_t addr, uint32_t data);
int mmio_read(iommu_handle_t *handle, uint32_t addr, uint32_t *data);
int iommu_step(iommu_handle_t *handle, int cycles);

int memory_write(iommu_handle_t *handle, uint64_t addr, const uint8_t *data, size_t len);
int memory_read(iommu_handle_t *handle, uint64_t addr, uint8_t *data, size_t len);
int memory_write_with_context(iommu_handle_t *handle,
                              uint64_t addr,
                              const uint8_t *data,
                              size_t len,
                              const iommu_request_context_t *context);
int memory_read_with_context(iommu_handle_t *handle,
                             uint64_t addr,
                             uint8_t *data,
                             size_t len,
                             const iommu_request_context_t *context);
int memory_write_with_context_ace(iommu_handle_t *handle,
                                  uint64_t addr,
                                  const uint8_t *data,
                                  size_t len,
                                  const iommu_request_context_t *context,
                                  const iommu_ace_lite_attrs_t *attrs);
int memory_read_with_context_ace(iommu_handle_t *handle,
                                 uint64_t addr,
                                 uint8_t *data,
                                 size_t len,
                                 const iommu_request_context_t *context,
                                 const iommu_ace_lite_attrs_t *attrs);

int iommu_ats_request_translation(iommu_handle_t *handle,
                                  const iommu_ats_request_t *request,
                                  iommu_ats_response_t *response);
int iommu_pri_request_page(iommu_handle_t *handle,
                           const iommu_pri_request_t *request,
                           iommu_pri_response_t *response);

void downstream_set_callbacks(iommu_handle_t *handle,
                              downstream_write_callback_t write_cb,
                              downstream_read_callback_t read_cb,
                              void *user_data);
void downstream_set_callbacks_v2(iommu_handle_t *handle,
                                 downstream_write_callback_v2_t write_cb,
                                 downstream_read_callback_v2_t read_cb,
                                 void *user_data);

int downstream_store(iommu_handle_t *handle, uint64_t addr, const uint8_t *data, size_t len);
int downstream_load(iommu_handle_t *handle, uint64_t addr, uint8_t *data, size_t len);

void translation_set_callbacks(iommu_handle_t *handle,
                               translation_write_callback_t write_cb,
                               translation_read_callback_t read_cb,
                               void *user_data);
void translation_set_callbacks_v2(iommu_handle_t *handle,
                                  translation_write_callback_v2_t write_cb,
                                  translation_read_callback_v2_t read_cb,
                                  void *user_data);

int translation_store(iommu_handle_t *handle, uint64_t addr, const uint8_t *data, size_t len);
int translation_load(iommu_handle_t *handle, uint64_t addr, uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
