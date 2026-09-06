#ifndef IO_IOMMU_API_COMPAT_H
#define IO_IOMMU_API_COMPAT_H

#include <stddef.h>
#include <stdint.h>

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

typedef void (*io_iommu_write_callback_v2)(uint64_t addr,
                                           const uint8_t *data,
                                           size_t data_len,
                                           uint32_t strobe,
                                           uint16_t id,
                                           size_t beat_index,
                                           size_t beat_count,
                                           const iommu_ace_lite_attrs_t *attrs,
                                           void *user_data);

typedef int (*io_iommu_read_callback_v2)(uint64_t addr,
                                         uint8_t *data,
                                         size_t data_len,
                                         uint16_t id,
                                         size_t beat_index,
                                         size_t beat_count,
                                         const iommu_ace_lite_attrs_t *attrs,
                                         void *user_data);

#endif
