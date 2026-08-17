// SPDX-License-Identifier: Apache-2.0

#define iommu_open io_refmodel_iommu_open
#define iommu_open_with_args io_refmodel_iommu_open_with_args
#define iommu_close io_refmodel_iommu_close
#define iommu_set_trace io_refmodel_iommu_set_trace
#define iommu_set_trace_txn_id io_refmodel_iommu_set_trace_txn_id
#define mmio_write io_refmodel_mmio_write
#define mmio_read io_refmodel_mmio_read
#define iommu_step io_refmodel_iommu_step
#define memory_write io_refmodel_memory_write
#define memory_read io_refmodel_memory_read
#define memory_write_with_context io_refmodel_memory_write_with_context
#define memory_read_with_context io_refmodel_memory_read_with_context
#define memory_write_with_context_ace io_refmodel_memory_write_with_context_ace
#define memory_read_with_context_ace io_refmodel_memory_read_with_context_ace
#define iommu_ats_request_translation io_refmodel_iommu_ats_request_translation
#define iommu_pri_request_page io_refmodel_iommu_pri_request_page
#define downstream_set_callbacks io_refmodel_downstream_set_callbacks
#define downstream_set_callbacks_v2 io_refmodel_downstream_set_callbacks_v2
#define downstream_store io_refmodel_downstream_store
#define downstream_load io_refmodel_downstream_load
#define translation_set_callbacks io_refmodel_translation_set_callbacks
#define translation_set_callbacks_v2 io_refmodel_translation_set_callbacks_v2
#define translation_store io_refmodel_translation_store
#define translation_load io_refmodel_translation_load

#include <endian.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "iommu_api.h"
#include "iommu.h"

#define IOMMU_REFMODEL_MAX_CALLBACK_BYTES 31
#define IOMMU_REFMODEL_PAGE_SIZE 4096ULL

#define IOMMU_REFMODEL_PERM_READ 1u
#define IOMMU_REFMODEL_PERM_WRITE 2u
#define IOMMU_REFMODEL_PERM_EXEC 4u
#define IOMMU_REFMODEL_PERM_PRIV 8u
#define IOMMU_REFMODEL_PERM_UNTRANSLATED_ONLY 32u

struct iommu_handle {
	iommu_t iommu;
	downstream_write_callback_t downstream_write;
	downstream_read_callback_t downstream_read;
	downstream_write_callback_v2_t downstream_write_v2;
	downstream_read_callback_v2_t downstream_read_v2;
	void *downstream_user_data;
	translation_write_callback_t translation_write;
	translation_read_callback_t translation_read;
	translation_write_callback_v2_t translation_write_v2;
	translation_read_callback_v2_t translation_read_v2;
	void *translation_user_data;
	int trace_enabled;
	int trace_txn_enabled;
	uint32_t trace_txn_id;
};

static iommu_handle_t *active_handle;
static iommu_handle_t *pri_response_handle;
static bool pri_response_seen;
static uint32_t pri_response_code;

static bool is_power_of_two(size_t value)
{
	return value != 0 && (value & (value - 1)) == 0;
}

static size_t min_size(size_t a, size_t b)
{
	return a < b ? a : b;
}

static size_t callback_chunk_len(uint64_t addr, size_t remaining)
{
	size_t chunk = min_size(remaining, IOMMU_REFMODEL_MAX_CALLBACK_BYTES);

	while (chunk > 1 && is_power_of_two(chunk) &&
	       ((addr & (uint64_t)(chunk - 1)) != 0)) {
		chunk--;
	}
	return chunk;
}

static uint32_t full_strobe(size_t len)
{
	if (len >= 32) {
		return UINT32_MAX;
	}
	return (1u << len) - 1u;
}

static int swap_big_endian_buffer(char *data, size_t size)
{
	uint8_t *bytes = (uint8_t *)data;

	if (size == 4) {
		uint8_t tmp = bytes[0];
		bytes[0] = bytes[3];
		bytes[3] = tmp;
		tmp = bytes[1];
		bytes[1] = bytes[2];
		bytes[2] = tmp;
		return 0;
	}

	if ((size % 8) != 0) {
		return -EINVAL;
	}

	for (size_t i = 0; i < size; i += 8) {
		for (size_t j = 0; j < 4; j++) {
			uint8_t tmp = bytes[i + j];
			bytes[i + j] = bytes[i + 7 - j];
			bytes[i + 7 - j] = tmp;
		}
	}
	return 0;
}

static int callback_read(translation_read_callback_t cb,
			 translation_read_callback_v2_t cb_v2,
			 void *user_data,
			 uint64_t addr, uint8_t *data, size_t len)
{
	const iommu_ace_lite_attrs_t attrs = { 0 };
	size_t offset = 0;
	size_t beat = 0;

	if (len == 0) {
		return 1;
	}
	if (cb == NULL && cb_v2 == NULL) {
		return 0;
	}

	while (offset < len) {
		size_t chunk = callback_chunk_len(addr + offset, len - offset);

		if (cb_v2 != NULL) {
			if (!cb_v2(addr + offset, data + offset, chunk, 0,
				   beat, 1, &attrs, user_data)) {
				return 0;
			}
		} else if (!cb(addr + offset, data + offset, chunk, 0,
			      beat, 1, user_data)) {
			return 0;
		}
		offset += chunk;
		beat++;
	}
	return 1;
}

static int downstream_callback_read(iommu_handle_t *handle, uint64_t addr,
				    uint8_t *data, size_t len)
{
	size_t offset = 0;
	size_t beat = 0;

	if (len == 0) {
		return 1;
	}
	if (handle->downstream_read == NULL) {
		if (handle->downstream_read_v2 == NULL) {
			return 0;
		}
	}

	if (handle->downstream_read_v2 != NULL) {
		const iommu_ace_lite_attrs_t attrs = { 0 };

		while (offset < len) {
			size_t chunk = callback_chunk_len(addr + offset, len - offset);

			if (!handle->downstream_read_v2(addr + offset,
							data + offset, chunk,
							0, beat, 1, &attrs,
							handle->downstream_user_data)) {
				return 0;
			}
			offset += chunk;
			beat++;
		}
		return 1;
	}

	if (handle->downstream_read == NULL) {
		return 0;
	}

	while (offset < len) {
		size_t chunk = callback_chunk_len(addr + offset, len - offset);

		if (!handle->downstream_read(addr + offset, data + offset, chunk,
					     0, beat, 1,
					     handle->downstream_user_data)) {
			return 0;
		}
		offset += chunk;
		beat++;
	}
	return 1;
}

static int callback_write(translation_write_callback_t cb,
			  translation_write_callback_v2_t cb_v2,
			  void *user_data,
			  uint64_t addr, const uint8_t *data, size_t len)
{
	const iommu_ace_lite_attrs_t attrs = { 0 };
	size_t offset = 0;
	size_t beat = 0;

	if (len == 0) {
		return 1;
	}
	if (cb == NULL && cb_v2 == NULL) {
		return 0;
	}

	while (offset < len) {
		size_t chunk = callback_chunk_len(addr + offset, len - offset);

		if (cb_v2 != NULL) {
			cb_v2(addr + offset, data + offset, chunk,
			      full_strobe(chunk), 0, beat, 1, &attrs, user_data);
		} else {
			cb(addr + offset, data + offset, chunk, full_strobe(chunk),
			   0, beat, 1, user_data);
		}
		offset += chunk;
		beat++;
	}
	return 1;
}

static int downstream_callback_write(iommu_handle_t *handle, uint64_t addr,
				     const uint8_t *data, size_t len)
{
	size_t offset = 0;
	size_t beat = 0;

	if (len == 0) {
		return 1;
	}
	if (handle->downstream_write == NULL) {
		if (handle->downstream_write_v2 == NULL) {
			return 0;
		}
	}

	if (handle->downstream_write_v2 != NULL) {
		const iommu_ace_lite_attrs_t attrs = { 0 };

		while (offset < len) {
			size_t chunk = callback_chunk_len(addr + offset, len - offset);

			handle->downstream_write_v2(addr + offset,
						    data + offset, chunk,
						    full_strobe(chunk), 0,
						    beat, 1, &attrs,
						    handle->downstream_user_data);
			offset += chunk;
			beat++;
		}
		return 1;
	}

	if (handle->downstream_write == NULL) {
		return 0;
	}

	while (offset < len) {
		size_t chunk = callback_chunk_len(addr + offset, len - offset);

		handle->downstream_write(addr + offset, data + offset, chunk,
					 full_strobe(chunk), 0, beat, 1,
					 handle->downstream_user_data);
		offset += chunk;
		beat++;
	}
	return 1;
}

static void decode_translation_response(const iommu_to_hb_rsp_t *rsp,
					uint64_t iova, uint64_t *translated,
					uint64_t *addr_mask)
{
	uint64_t encoded = rsp->trsp.PPN << 12;
	uint64_t mask = IOMMU_REFMODEL_PAGE_SIZE - 1;

	if (rsp->trsp.S) {
		unsigned int napot_bits = 0;

		while (((encoded >> (12 + napot_bits)) & 1u) != 0 &&
		       napot_bits < 51) {
			napot_bits++;
		}
		mask = (1ULL << (napot_bits + 13)) - 1ULL;
	}

	*addr_mask = mask;
	*translated = (encoded & ~mask) | (iova & mask);
}

static int translate_iova(iommu_handle_t *handle, uint64_t iova, size_t len,
			  bool is_write, const iommu_request_context_t *context,
			  uint64_t *translated, uint64_t *addr_mask)
{
	hb_to_iommu_req_t req;
	iommu_to_hb_rsp_t rsp;
	iommu_handle_t *old_active = active_handle;

	memset(&req, 0, sizeof(req));
	memset(&rsp, 0, sizeof(rsp));
	req.device_id = context ? context->device_id : 0;
	req.pid_valid = context ? context->process_id_valid : 0;
	req.process_id = context ? context->process_id : 0;
	req.tr.at = (context && context->is_translated) ?
		ADDR_TYPE_TRANSLATED : ADDR_TYPE_UNTRANSLATED;
	req.tr.iova = iova;
	req.tr.length = len > UINT32_MAX ? UINT32_MAX : (uint32_t)len;
	req.tr.read_writeAMO = is_write ? WRITE : READ;

	active_handle = handle;
	iommu_translate_iova(&handle->iommu, &req, &rsp);
	active_handle = old_active;
	if (rsp.status != SUCCESS) {
		return 0;
	}

	decode_translation_response(&rsp, iova, translated, addr_mask);
	return 1;
}

static int memory_access(iommu_handle_t *handle, uint64_t addr, uint8_t *data,
			 size_t len, bool is_write,
			 const iommu_request_context_t *context)
{
	size_t offset = 0;

	if (len == 0) {
		return 1;
	}
	if (handle == NULL || data == NULL) {
		return 0;
	}

	while (offset < len) {
		uint64_t translated = 0;
		uint64_t mask = 0;
		size_t chunk;

		if (!translate_iova(handle, addr + offset, len - offset, is_write,
				    context, &translated, &mask)) {
			return 0;
		}

		chunk = min_size(len - offset,
				 (size_t)((mask + 1) -
					  ((addr + offset) & mask)));
		if (chunk == 0) {
			return 0;
		}

		if (is_write) {
			if (!downstream_callback_write(handle, translated,
						       data + offset, chunk)) {
				return 0;
			}
		} else {
			if (!downstream_callback_read(handle, translated,
						      data + offset, chunk)) {
				return 0;
			}
		}
		offset += chunk;
	}
	return 1;
}

static void init_default_iommu(iommu_t *iommu)
{
	capabilities_t cap;
	fctl_t fctl;

	memset(&cap, 0, sizeof(cap));
	memset(&fctl, 0, sizeof(fctl));
	cap.version = 0x10;
	cap.Sv39 = 1;
	cap.Sv48 = 1;
	cap.Sv57 = 1;
	cap.Sv39x4 = 1;
	cap.Sv48x4 = 1;
	cap.Sv57x4 = 1;
	cap.amo_hwad = 1;
	cap.ats = 1;
	cap.t2gpa = 1;
	cap.hpm = 1;
	cap.msi_flat = 1;
	cap.msi_mrif = 1;
	cap.amo_mrif = 1;
	cap.dbg = 1;
	cap.pas = 50;
	cap.pd20 = 1;
	cap.pd17 = 1;
	cap.pd8 = 1;
	cap.Svrsw60t59b = 1;

	reset_iommu(iommu, 8, 40, 0xff, 3, Off, DDT_3LVL, 0xFFFFFF,
		    0, 0, FILL_IOATC_ATS_T2GPA | FILL_IOATC_ATS_ALWAYS,
		    cap, fctl, 0x40000000ULL, 0x40000000ULL, 0x40000000ULL,
		    0x200000ULL, 0x40000000ULL, 0x40000000ULL, 0x40000000ULL,
		    0x200000ULL);
}

iommu_handle_t *iommu_open(void)
{
	iommu_handle_t *handle = calloc(1, sizeof(*handle));

	if (handle == NULL) {
		return NULL;
	}
	init_default_iommu(&handle->iommu);
	active_handle = handle;
	return handle;
}

iommu_handle_t *iommu_open_with_args(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	return iommu_open();
}

void iommu_close(iommu_handle_t *handle)
{
	if (active_handle == handle) {
		active_handle = NULL;
	}
	if (pri_response_handle == handle) {
		pri_response_handle = NULL;
	}
	free(handle);
}

void iommu_set_trace(iommu_handle_t *handle, int enabled)
{
	if (handle != NULL) {
		handle->trace_enabled = enabled;
	}
}

void iommu_set_trace_txn_id(iommu_handle_t *handle, uint32_t id, int enabled)
{
	if (handle != NULL) {
		handle->trace_txn_id = id;
		handle->trace_txn_enabled = enabled;
	}
}

int mmio_write(iommu_handle_t *handle, uint32_t addr, uint32_t data)
{
	if (handle == NULL || addr > UINT16_MAX) {
		return 0;
	}
	write_register(&handle->iommu, (uint16_t)addr, sizeof(data), data);
	return 1;
}

int mmio_read(iommu_handle_t *handle, uint32_t addr, uint32_t *data)
{
	if (handle == NULL || data == NULL || addr > UINT16_MAX) {
		return 0;
	}
	*data = (uint32_t)read_register(&handle->iommu, (uint16_t)addr,
					sizeof(*data));
	return 1;
}

int iommu_step(iommu_handle_t *handle, int cycles)
{
	iommu_handle_t *old_active = active_handle;
	int count = cycles > 0 ? cycles : 1;

	if (handle == NULL) {
		return 0;
	}

	active_handle = handle;
	for (int i = 0; i < count; i++) {
		process_commands(&handle->iommu);
	}
	active_handle = old_active;
	return 1;
}

int memory_write(iommu_handle_t *handle, uint64_t addr, const uint8_t *data,
		 size_t len)
{
	return memory_write_with_context(handle, addr, data, len, NULL);
}

int memory_read(iommu_handle_t *handle, uint64_t addr, uint8_t *data,
		size_t len)
{
	return memory_read_with_context(handle, addr, data, len, NULL);
}

int memory_write_with_context(iommu_handle_t *handle, uint64_t addr,
			      const uint8_t *data, size_t len,
			      const iommu_request_context_t *context)
{
	return memory_access(handle, addr, (uint8_t *)data, len, true, context);
}

int memory_write_with_context_ace(iommu_handle_t *handle, uint64_t addr,
				  const uint8_t *data, size_t len,
				  const iommu_request_context_t *context,
				  const iommu_ace_lite_attrs_t *attrs)
{
	(void)attrs;
	return memory_write_with_context(handle, addr, data, len, context);
}

int memory_read_with_context(iommu_handle_t *handle, uint64_t addr,
			     uint8_t *data, size_t len,
			     const iommu_request_context_t *context)
{
	return memory_access(handle, addr, data, len, false, context);
}

int memory_read_with_context_ace(iommu_handle_t *handle, uint64_t addr,
				 uint8_t *data, size_t len,
				 const iommu_request_context_t *context,
				 const iommu_ace_lite_attrs_t *attrs)
{
	(void)attrs;
	return memory_read_with_context(handle, addr, data, len, context);
}

int iommu_ats_request_translation(iommu_handle_t *handle,
				  const iommu_ats_request_t *request,
				  iommu_ats_response_t *response)
{
	hb_to_iommu_req_t req;
	iommu_to_hb_rsp_t rsp;
	iommu_handle_t *old_active = active_handle;
	uint64_t mask = IOMMU_REFMODEL_PAGE_SIZE - 1;

	if (handle == NULL || request == NULL || response == NULL ||
	    request->length == 0) {
		return 0;
	}

	memset(&req, 0, sizeof(req));
	memset(&rsp, 0, sizeof(rsp));
	req.device_id = request->context.device_id;
	req.pid_valid = request->context.process_id_valid;
	req.process_id = request->context.process_id;
	req.no_write = request->no_write;
	req.priv_req = request->priv_req;
	req.exec_req = request->exec_req;
	req.tr.at = ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST;
	req.tr.iova = request->iova;
	req.tr.length = request->length > UINT32_MAX ?
		UINT32_MAX : (uint32_t)request->length;
	req.tr.read_writeAMO = READ;

	active_handle = handle;
	iommu_translate_iova(&handle->iommu, &req, &rsp);
	active_handle = old_active;

	memset(response, 0, sizeof(*response));
	response->addr_mask = mask;
	response->translated_addr = request->iova & ~mask;
	if (rsp.status != SUCCESS) {
		response->err_count = 1;
		return 1;
	}

	decode_translation_response(&rsp, request->iova,
				    &response->translated_addr,
				    &response->addr_mask);
	response->perm = 0;
	if (rsp.trsp.R) {
		response->perm |= IOMMU_REFMODEL_PERM_READ;
	}
	if (rsp.trsp.W) {
		response->perm |= IOMMU_REFMODEL_PERM_WRITE;
	}
	if (rsp.trsp.Exe) {
		response->perm |= IOMMU_REFMODEL_PERM_EXEC;
	}
	if (rsp.trsp.Priv) {
		response->perm |= IOMMU_REFMODEL_PERM_PRIV;
	}
	if (rsp.trsp.U) {
		response->perm |= IOMMU_REFMODEL_PERM_UNTRANSLATED_ONLY;
	}

	if (request->no_write) {
		response->err_count =
			(response->perm & IOMMU_REFMODEL_PERM_READ) ? 0 : 1;
	} else {
		response->err_count =
			(response->perm & IOMMU_REFMODEL_PERM_WRITE) ? 0 : 1;
	}
	if (request->exec_req &&
	    !(response->perm & IOMMU_REFMODEL_PERM_EXEC)) {
		response->err_count = 1;
	}
	return 1;
}

int iommu_pri_request_page(iommu_handle_t *handle,
			   const iommu_pri_request_t *request,
			   iommu_pri_response_t *response)
{
	ats_msg_t pr;
	iommu_handle_t *old_active = active_handle;

	if (handle == NULL || request == NULL || response == NULL) {
		return 0;
	}

	memset(&pr, 0, sizeof(pr));
	pr.MSGCODE = PAGE_REQ_MSG_CODE;
	pr.RID = request->context.device_id & 0xffffu;
	pr.DSV = request->context.device_id > 0xffffu;
	pr.DSEG = (request->context.device_id >> 16) & 0xffu;
	pr.PV = request->context.process_id_valid;
	pr.PID = request->context.process_id;
	pr.PRIV = request->priv_req;
	pr.EXEC_REQ = request->exec_req;
	pr.PAYLOAD = (request->iova & ~0xfffULL) |
		     ((uint64_t)(request->prgi & 0x1ffu) << 3) |
		     (request->lpig ? (1ULL << 2) : 0) |
		     (request->is_write ? (1ULL << 1) : 0) |
		     (request->is_read ? 1ULL : 0);

	pri_response_handle = handle;
	pri_response_seen = false;
	pri_response_code = IOMMU_API_PRI_RESP_SUCCESS;
	active_handle = handle;
	handle_page_request(&handle->iommu, &pr);
	active_handle = old_active;

	response->response_code = pri_response_seen ?
		pri_response_code : IOMMU_API_PRI_RESP_SUCCESS;
	response->prgi = request->prgi;
	pri_response_handle = NULL;
	return 1;
}

void downstream_set_callbacks(iommu_handle_t *handle,
			      downstream_write_callback_t write_cb,
			      downstream_read_callback_t read_cb,
			      void *user_data)
{
	if (handle == NULL) {
		return;
	}
	handle->downstream_write = write_cb;
	handle->downstream_read = read_cb;
	handle->downstream_write_v2 = NULL;
	handle->downstream_read_v2 = NULL;
	handle->downstream_user_data = user_data;
}

void downstream_set_callbacks_v2(iommu_handle_t *handle,
				 downstream_write_callback_v2_t write_cb,
				 downstream_read_callback_v2_t read_cb,
				 void *user_data)
{
	if (handle == NULL) {
		return;
	}
	handle->downstream_write = NULL;
	handle->downstream_read = NULL;
	handle->downstream_write_v2 = write_cb;
	handle->downstream_read_v2 = read_cb;
	handle->downstream_user_data = user_data;
}

int downstream_store(iommu_handle_t *handle, uint64_t addr,
		     const uint8_t *data, size_t len)
{
	if (handle == NULL || (data == NULL && len != 0)) {
		return 0;
	}
	return downstream_callback_write(handle, addr, data, len);
}

int downstream_load(iommu_handle_t *handle, uint64_t addr, uint8_t *data,
		    size_t len)
{
	if (handle == NULL || (data == NULL && len != 0)) {
		return 0;
	}
	return downstream_callback_read(handle, addr, data, len);
}

void translation_set_callbacks(iommu_handle_t *handle,
			       translation_write_callback_t write_cb,
			       translation_read_callback_t read_cb,
			       void *user_data)
{
	if (handle == NULL) {
		return;
	}
	handle->translation_write = write_cb;
	handle->translation_read = read_cb;
	handle->translation_write_v2 = NULL;
	handle->translation_read_v2 = NULL;
	handle->translation_user_data = user_data;
}

void translation_set_callbacks_v2(iommu_handle_t *handle,
				  translation_write_callback_v2_t write_cb,
				  translation_read_callback_v2_t read_cb,
				  void *user_data)
{
	if (handle == NULL) {
		return;
	}
	handle->translation_write = NULL;
	handle->translation_read = NULL;
	handle->translation_write_v2 = write_cb;
	handle->translation_read_v2 = read_cb;
	handle->translation_user_data = user_data;
}

int translation_store(iommu_handle_t *handle, uint64_t addr,
		      const uint8_t *data, size_t len)
{
	if (handle == NULL || (data == NULL && len != 0)) {
		return 0;
	}
	return callback_write(handle->translation_write,
			      handle->translation_write_v2,
			      handle->translation_user_data, addr, data, len);
}

int translation_load(iommu_handle_t *handle, uint64_t addr, uint8_t *data,
		     size_t len)
{
	if (handle == NULL || (data == NULL && len != 0)) {
		return 0;
	}
	return callback_read(handle->translation_read,
			     handle->translation_read_v2,
			     handle->translation_user_data, addr, data, len);
}

uint8_t read_memory(uint64_t addr, uint8_t size, char *data, uint32_t rcid,
		    uint32_t mcid, uint32_t pma, int endian)
{
	(void)rcid;
	(void)mcid;
	(void)pma;

	if (active_handle == NULL || (data == NULL && size != 0)) {
		return ACCESS_FAULT;
	}
	if (!callback_read(active_handle->translation_read,
			   active_handle->translation_read_v2,
			   active_handle->translation_user_data, addr,
			   (uint8_t *)data, size)) {
		return ACCESS_FAULT;
	}
	if (endian == BIG_ENDIAN && swap_big_endian_buffer(data, size) < 0) {
		return DATA_CORRUPTION;
	}
	return 0;
}

uint8_t read_memory_for_AMO(uint64_t address, uint8_t size, char *data,
			    uint32_t rcid, uint32_t mcid, uint32_t pma,
			    int endian)
{
	return read_memory(address, size, data, rcid, mcid, pma, endian);
}

uint8_t write_memory(char *data, uint64_t address, uint32_t size,
		     uint32_t rcid, uint32_t mcid, uint32_t pma, int endian)
{
	uint8_t stack_buf[64];
	uint8_t *write_buf = (uint8_t *)data;
	uint8_t *heap_buf = NULL;
	int ok;

	(void)rcid;
	(void)mcid;
	(void)pma;

	if (active_handle == NULL || (data == NULL && size != 0)) {
		return ACCESS_FAULT;
	}
	if (endian == BIG_ENDIAN && size != 0) {
		if (size <= sizeof(stack_buf)) {
			write_buf = stack_buf;
		} else {
			heap_buf = malloc(size);
			if (heap_buf == NULL) {
				return ACCESS_FAULT;
			}
			write_buf = heap_buf;
		}
		memcpy(write_buf, data, size);
		if (swap_big_endian_buffer((char *)write_buf, size) < 0) {
			free(heap_buf);
			return DATA_CORRUPTION;
		}
	}

	ok = callback_write(active_handle->translation_write,
			    active_handle->translation_write_v2,
			    active_handle->translation_user_data, address,
			    write_buf, size);
	free(heap_buf);
	return ok ? 0 : ACCESS_FAULT;
}

uint8_t read_memory_test(uint64_t addr, uint8_t size, char *data)
{
	return read_memory(addr, size, data, 0, 0, PMA, LITTLE_ENDIAN);
}

uint8_t write_memory_test(char *data, uint64_t address, uint32_t size)
{
	return write_memory(data, address, size, 0, 0, PMA, LITTLE_ENDIAN);
}

void iommu_to_hb_do_global_observability_sync(uint8_t PR, uint8_t PW)
{
	(void)PR;
	(void)PW;
}

void send_msg_iommu_to_hb(ats_msg_t *msg)
{
	uint32_t code;

	if (pri_response_handle == NULL || msg == NULL ||
	    msg->MSGCODE != PRGR_MSG_CODE) {
		return;
	}

	code = (uint32_t)((msg->PAYLOAD >> 44) & 0xfu);
	switch (code) {
	case PRGR_SUCCESS:
		pri_response_code = IOMMU_API_PRI_RESP_SUCCESS;
		break;
	case PRGR_INVALID_REQUEST:
		pri_response_code = IOMMU_API_PRI_RESP_INVALID_REQUEST;
		break;
	default:
		pri_response_code = IOMMU_API_PRI_RESP_FAILURE;
		break;
	}
	pri_response_seen = true;
}

void get_attribs_from_req(hb_to_iommu_req_t *req, uint8_t *read,
			  uint8_t *write, uint8_t *exec, uint8_t *priv)
{
	*read = (req->tr.read_writeAMO == READ && req->exec_req &&
		 req->tr.at == ADDR_TYPE_UNTRANSLATED) ? 0 :
		(req->tr.read_writeAMO == READ) ? 1 : 0;
	*write = req->tr.read_writeAMO == WRITE ? 1 : 0;
	*write = (req->tr.at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST &&
		  req->no_write == 0) ? 1 : *write;
	*exec = (req->tr.read_writeAMO == READ &&
		 (req->exec_req &&
		  (req->tr.at == ADDR_TYPE_UNTRANSLATED || req->pid_valid))) ?
		1 : 0;
	*priv = (req->pid_valid && req->priv_req) ? S_MODE : U_MODE;
}

void handle_virtual_interrupt_file_overlap(device_context_t *DC, uint64_t gpa,
					   uint64_t *gst_page_sz)
{
	uint64_t m = DC->msi_addr_mask.mask << 12;
	uint64_t p = DC->msi_addr_pattern.pattern << 12;

	if (DC->msiptp.MODE == MSIPTP_Off || *gst_page_sz == PAGESIZE) {
		return;
	}

	for (uint64_t sz = PAGESIZE << 36; sz >= (PAGESIZE << 9); sz >>= 9) {
		uint64_t mask = m & ~(sz - 1);

		if (*gst_page_sz >= sz && ((gpa & mask) == (p & mask))) {
			*gst_page_sz = sz >> 9;
		}
	}
}
