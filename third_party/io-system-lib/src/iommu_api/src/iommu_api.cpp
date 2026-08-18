#include "iommu_api.h"

#include "iommu_sim.hpp"

#include <array>
#include <exception>
#include <string>
#include <vector>

struct iommu_handle {
    explicit iommu_handle(std::vector<std::string> startup_args = {})
        : runtime(std::move(startup_args))
    {
    }

    iommu::IommuRuntime runtime;
};

namespace {

bool ConvertRequestContext(const iommu_request_context_t *context, iommu::RequestContext &out)
{
    if (context == nullptr) {
        out = {};
        return true;
    }

    if ((context->device_id & ~0x00ffffffu) != 0) {
        return false;
    }
    if ((context->process_id & ~0x000fffffu) != 0) {
        return false;
    }
    if (context->process_id_valid > 1 || context->is_translated > 1) {
        return false;
    }

    out.device_id = context->device_id;
    out.process_id = context->process_id;
    out.process_id_valid = context->process_id_valid != 0;
    out.is_translated = context->is_translated != 0;
    return true;
}

bool ConvertAceLiteAttrs(const iommu_ace_lite_attrs_t *attrs, iommu::AceLiteAttrs &out)
{
    if (attrs == nullptr) {
        out = {};
        return true;
    }

    if ((attrs->cache & ~0x0fu) != 0 ||
        (attrs->prot & ~0x07u) != 0 ||
        (attrs->region & ~0x0fu) != 0 ||
        (attrs->qos & ~0x0fu) != 0 ||
        (attrs->snoop & ~0x0fu) != 0 ||
        (attrs->domain & ~0x03u) != 0 ||
        (attrs->idunq & ~0x01u) != 0 ||
        (attrs->loop & ~0x01u) != 0 ||
        (attrs->awatop & ~0x3fu) != 0) {
        return false;
    }

    out.cache = attrs->cache;
    out.prot = attrs->prot;
    out.region = attrs->region;
    out.qos = attrs->qos;
    out.snoop = attrs->snoop;
    out.domain = attrs->domain;
    out.idunq = attrs->idunq;
    out.loop = attrs->loop;
    out.awatop = attrs->awatop;
    return true;
}

iommu_ace_lite_attrs_t ExportAceLiteAttrs(const iommu::AceLiteAttrs &attrs)
{
    iommu_ace_lite_attrs_t out = {};
    out.cache = attrs.cache;
    out.prot = attrs.prot;
    out.region = attrs.region;
    out.qos = attrs.qos;
    out.snoop = attrs.snoop;
    out.domain = attrs.domain;
    out.idunq = attrs.idunq;
    out.loop = attrs.loop;
    out.awatop = attrs.awatop;
    return out;
}

} // namespace

extern "C" {

iommu_handle_t *iommu_open(void)
{
    try {
        return new iommu_handle();
    } catch (...) {
        return nullptr;
    }
}

iommu_handle_t *iommu_open_with_args(int argc, char **argv)
{
    if (argc < 0 || (argc != 0 && argv == nullptr)) {
        return nullptr;
    }

    try {
        std::vector<std::string> startup_args;
        startup_args.reserve(static_cast<std::size_t>(argc));
        for (int i = 0; i < argc; ++i) {
            startup_args.emplace_back(argv[i] == nullptr ? "" : argv[i]);
        }
        return new iommu_handle(std::move(startup_args));
    } catch (...) {
        return nullptr;
    }
}

void iommu_close(iommu_handle_t *handle)
{
    delete handle;
}

void iommu_set_trace(iommu_handle_t *handle, int enabled)
{
    if (handle == nullptr) {
        return;
    }

    try {
        handle->runtime.SetTrace(enabled != 0);
    } catch (...) {
    }
}

void iommu_set_trace_txn_id(iommu_handle_t *handle, uint32_t id, int enabled)
{
    if (handle == nullptr) {
        return;
    }

    if (enabled != 0 && id > UINT16_MAX) {
        return;
    }

    try {
        handle->runtime.SetTraceTxnId(enabled != 0,
                                      static_cast<uint16_t>(id));
    } catch (...) {
    }
}

int mmio_write(iommu_handle_t *handle, uint32_t addr, uint32_t data)
{
    if (handle == nullptr) {
        return 0;
    }
    try {
        return handle->runtime.mmio().Write(addr, data) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int mmio_read(iommu_handle_t *handle, uint32_t addr, uint32_t *data)
{
    if (handle == nullptr || data == nullptr) {
        return 0;
    }
    try {
        uint32_t value = 0;
        if (!handle->runtime.mmio().Read(addr, value)) {
            return 0;
        }
        *data = value;
        return 1;
    } catch (...) {
        return 0;
    }
}

int iommu_step(iommu_handle_t *handle, int cycles)
{
    if (handle == nullptr) {
        return 0;
    }
    try {
        return handle->runtime.Step(cycles) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int memory_write(iommu_handle_t *handle, uint64_t addr, const uint8_t *data, size_t len)
{
    return memory_write_with_context(handle, addr, data, len, nullptr);
}

int memory_read(iommu_handle_t *handle, uint64_t addr, uint8_t *data, size_t len)
{
    return memory_read_with_context(handle, addr, data, len, nullptr);
}

int memory_write_with_context(iommu_handle_t *handle,
                              uint64_t addr,
                              const uint8_t *data,
                              size_t len,
                              const iommu_request_context_t *context)
{
    return memory_write_with_context_ace(handle, addr, data, len, context, nullptr);
}

int memory_write_with_context_ace(iommu_handle_t *handle,
                                  uint64_t addr,
                                  const uint8_t *data,
                                  size_t len,
                                  const iommu_request_context_t *context,
                                  const iommu_ace_lite_attrs_t *attrs)
{
    if (handle == nullptr || (data == nullptr && len != 0)) {
        return 0;
    }
    try {
        iommu::RequestContext request_context;
        if (!ConvertRequestContext(context, request_context)) {
            return 0;
        }
        iommu::AceLiteAttrs ace_attrs;
        if (!ConvertAceLiteAttrs(attrs, ace_attrs)) {
            return 0;
        }

        const std::vector<unsigned char> buffer =
            (len == 0) ? std::vector<unsigned char>() : std::vector<unsigned char>(data, data + len);
        return handle->runtime.memory().WriteWithContextAce(addr, buffer, request_context, ace_attrs) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int memory_read_with_context(iommu_handle_t *handle,
                             uint64_t addr,
                             uint8_t *data,
                             size_t len,
                             const iommu_request_context_t *context)
{
    return memory_read_with_context_ace(handle, addr, data, len, context, nullptr);
}

int memory_read_with_context_ace(iommu_handle_t *handle,
                                 uint64_t addr,
                                 uint8_t *data,
                                 size_t len,
                                 const iommu_request_context_t *context,
                                 const iommu_ace_lite_attrs_t *attrs)
{
    if (handle == nullptr || (data == nullptr && len != 0)) {
        return 0;
    }
    try {
        iommu::RequestContext request_context;
        if (!ConvertRequestContext(context, request_context)) {
            return 0;
        }
        iommu::AceLiteAttrs ace_attrs;
        if (!ConvertAceLiteAttrs(attrs, ace_attrs)) {
            return 0;
        }

        std::vector<unsigned char> buffer;
        if (!handle->runtime.memory().ReadWithContextAce(addr, buffer, len, request_context, ace_attrs)) {
            return 0;
        }
        for (size_t i = 0; i < len; ++i) {
            data[i] = buffer[i];
        }
        return 1;
    } catch (...) {
        return 0;
    }
}

int iommu_ats_request_translation(iommu_handle_t *handle,
                                  const iommu_ats_request_t *request,
                                  iommu_ats_response_t *response)
{
    if (handle == nullptr || request == nullptr || response == nullptr ||
        request->length == 0) {
        return 0;
    }

    try {
        iommu::RequestContext request_context;
        if (!ConvertRequestContext(&request->context, request_context)) {
            return 0;
        }

        uint64_t translated_addr = 0;
        uint64_t addr_mask = 0;
        uint32_t perm = 0;
        uint32_t err_count = 0;
        if (!handle->runtime.AtsRequestTranslation(request_context,
                                                   request->iova,
                                                   request->length,
                                                   request->no_write,
                                                   request->priv_req,
                                                   request->exec_req,
                                                   translated_addr,
                                                   addr_mask,
                                                   perm,
                                                   err_count)) {
            return 0;
        }

        response->translated_addr = translated_addr;
        response->addr_mask = addr_mask;
        response->perm = perm;
        response->err_count = err_count;
        return 1;
    } catch (...) {
        return 0;
    }
}

int iommu_pri_request_page(iommu_handle_t *handle,
                           const iommu_pri_request_t *request,
                           iommu_pri_response_t *response)
{
    if (handle == nullptr || request == nullptr || response == nullptr) {
        return 0;
    }

    try {
        iommu::RequestContext request_context;
        if (!ConvertRequestContext(&request->context, request_context)) {
            return 0;
        }

        uint32_t response_code = 0;
        if (!handle->runtime.PriRequestPage(request_context,
                                            request->iova,
                                            request->prgi,
                                            request->lpig,
                                            request->is_read,
                                            request->is_write,
                                            request->priv_req,
                                            request->exec_req,
                                            response_code)) {
            return 0;
        }

        response->response_code = response_code;
        response->prgi = request->prgi;
        return 1;
    } catch (...) {
        return 0;
    }
}

void downstream_set_callbacks(iommu_handle_t *handle,
                              downstream_write_callback_t write_cb,
                              downstream_read_callback_t read_cb,
                              void *user_data)
{
    if (handle == nullptr) {
        return;
    }

    try {
        iommu::DownstreamWriteCallback cpp_write_cb;
        iommu::DownstreamReadCallback cpp_read_cb;

        if (write_cb != nullptr) {
            cpp_write_cb = [write_cb, user_data](const iommu::DownstreamWriteEvent &event) {
                write_cb(event.addr,
                         event.data.data(),
                         event.data.size(),
                         event.strobe,
                         event.id,
                         event.beat_index,
                         event.beat_count,
                         user_data);
            };
        }

        if (read_cb != nullptr) {
            cpp_read_cb = [read_cb, user_data](const iommu::DownstreamReadEvent &event,
                                               std::array<unsigned char, iommu::kAxiDataBytes> &data) {
                return read_cb(event.addr,
                               data.data(),
                               data.size(),
                               event.id,
                               event.beat_index,
                               event.beat_count,
                               user_data) != 0;
            };
        }

        handle->runtime.downstream().SetCallbacks(std::move(cpp_write_cb), std::move(cpp_read_cb));
    } catch (...) {
    }
}

void downstream_set_callbacks_v2(iommu_handle_t *handle,
                                 downstream_write_callback_v2_t write_cb,
                                 downstream_read_callback_v2_t read_cb,
                                 void *user_data)
{
    if (handle == nullptr) {
        return;
    }

    try {
        iommu::DownstreamWriteCallback cpp_write_cb;
        iommu::DownstreamReadCallback cpp_read_cb;

        if (write_cb != nullptr) {
            cpp_write_cb = [write_cb, user_data](const iommu::DownstreamWriteEvent &event) {
                const iommu_ace_lite_attrs_t attrs = ExportAceLiteAttrs(event.attrs);
                write_cb(event.addr,
                         event.data.data(),
                         event.data.size(),
                         event.strobe,
                         event.id,
                         event.beat_index,
                         event.beat_count,
                         &attrs,
                         user_data);
            };
        }

        if (read_cb != nullptr) {
            cpp_read_cb = [read_cb, user_data](const iommu::DownstreamReadEvent &event,
                                               std::array<unsigned char, iommu::kAxiDataBytes> &data) {
                const iommu_ace_lite_attrs_t attrs = ExportAceLiteAttrs(event.attrs);
                return read_cb(event.addr,
                               data.data(),
                               data.size(),
                               event.id,
                               event.beat_index,
                               event.beat_count,
                               &attrs,
                               user_data) != 0;
            };
        }

        handle->runtime.downstream().SetCallbacks(std::move(cpp_write_cb), std::move(cpp_read_cb));
    } catch (...) {
    }
}

int downstream_store(iommu_handle_t *handle, uint64_t addr, const uint8_t *data, size_t len)
{
    if (handle == nullptr || (data == nullptr && len != 0)) {
        return 0;
    }
    try {
        const std::vector<unsigned char> buffer =
            (len == 0) ? std::vector<unsigned char>() : std::vector<unsigned char>(data, data + len);
        handle->runtime.downstream().Store(addr, buffer);
        return 1;
    } catch (...) {
        return 0;
    }
}

int downstream_load(iommu_handle_t *handle, uint64_t addr, uint8_t *data, size_t len)
{
    if (handle == nullptr || (data == nullptr && len != 0)) {
        return 0;
    }
    try {
        const std::vector<unsigned char> buffer = handle->runtime.downstream().Load(addr, len);
        for (size_t i = 0; i < len; ++i) {
            data[i] = buffer[i];
        }
        return 1;
    } catch (...) {
        return 0;
    }
}

void translation_set_callbacks(iommu_handle_t *handle,
                               translation_write_callback_t write_cb,
                               translation_read_callback_t read_cb,
                               void *user_data)
{
    if (handle == nullptr) {
        return;
    }

    try {
        iommu::TranslationWriteCallback cpp_write_cb;
        iommu::TranslationReadCallback cpp_read_cb;

        if (write_cb != nullptr) {
            cpp_write_cb = [write_cb, user_data](const iommu::TranslationWriteEvent &event) {
                write_cb(event.addr,
                         event.data.data(),
                         event.data.size(),
                         event.strobe,
                         event.id,
                         event.beat_index,
                         event.beat_count,
                         user_data);
            };
        }

        if (read_cb != nullptr) {
            cpp_read_cb = [read_cb, user_data](const iommu::TranslationReadEvent &event,
                                               std::array<unsigned char, iommu::kAxiDataBytes> &data) {
                return read_cb(event.addr,
                               data.data(),
                               data.size(),
                               event.id,
                               event.beat_index,
                               event.beat_count,
                               user_data) != 0;
            };
        }

        handle->runtime.translation().SetCallbacks(std::move(cpp_write_cb), std::move(cpp_read_cb));
    } catch (...) {
    }
}

void translation_set_callbacks_v2(iommu_handle_t *handle,
                                  translation_write_callback_v2_t write_cb,
                                  translation_read_callback_v2_t read_cb,
                                  void *user_data)
{
    if (handle == nullptr) {
        return;
    }

    try {
        iommu::TranslationWriteCallback cpp_write_cb;
        iommu::TranslationReadCallback cpp_read_cb;

        if (write_cb != nullptr) {
            cpp_write_cb = [write_cb, user_data](const iommu::TranslationWriteEvent &event) {
                const iommu_ace_lite_attrs_t attrs = ExportAceLiteAttrs(event.attrs);
                write_cb(event.addr,
                         event.data.data(),
                         event.data.size(),
                         event.strobe,
                         event.id,
                         event.beat_index,
                         event.beat_count,
                         &attrs,
                         user_data);
            };
        }

        if (read_cb != nullptr) {
            cpp_read_cb = [read_cb, user_data](const iommu::TranslationReadEvent &event,
                                               std::array<unsigned char, iommu::kAxiDataBytes> &data) {
                const iommu_ace_lite_attrs_t attrs = ExportAceLiteAttrs(event.attrs);
                return read_cb(event.addr,
                               data.data(),
                               data.size(),
                               event.id,
                               event.beat_index,
                               event.beat_count,
                               &attrs,
                               user_data) != 0;
            };
        }

        handle->runtime.translation().SetCallbacks(std::move(cpp_write_cb), std::move(cpp_read_cb));
    } catch (...) {
    }
}

int translation_store(iommu_handle_t *handle, uint64_t addr, const uint8_t *data, size_t len)
{
    if (handle == nullptr || (data == nullptr && len != 0)) {
        return 0;
    }
    try {
        const std::vector<unsigned char> buffer =
            (len == 0) ? std::vector<unsigned char>() : std::vector<unsigned char>(data, data + len);
        handle->runtime.translation().Store(addr, buffer);
        return 1;
    } catch (...) {
        return 0;
    }
}

int translation_load(iommu_handle_t *handle, uint64_t addr, uint8_t *data, size_t len)
{
    if (handle == nullptr || (data == nullptr && len != 0)) {
        return 0;
    }
    try {
        const std::vector<unsigned char> buffer = handle->runtime.translation().Load(addr, len);
        for (size_t i = 0; i < len; ++i) {
            data[i] = buffer[i];
        }
        return 1;
    } catch (...) {
        return 0;
    }
}

} // extern "C"
