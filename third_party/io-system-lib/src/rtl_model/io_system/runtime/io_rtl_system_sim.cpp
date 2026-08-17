// SPDX-License-Identifier: Apache-2.0
#include "io_rtl_system_sim.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace io_rtl_system {

namespace {

constexpr uint8_t kAxiRespOkay = 0;
constexpr uint8_t kAxiRespSlvErr = 2;
constexpr uint8_t kAxiRespDecErr = 3;
constexpr uint8_t kAxiBurstIncr = 1;
constexpr std::size_t kAxiMaxBurstBeats = 256;

std::size_t MasterBeatBytes(uint8_t size)
{
    return size <= 3 ? std::size_t(1) << size : 0;
}

std::size_t MasterBeatCount(uint8_t len)
{
    return std::size_t(len) + 1;
}

bool MasterBurstSupported(uint8_t burst)
{
    return burst == kAxiBurstIncr;
}

IoAxiBurstType MasterBurstType(uint8_t burst)
{
    return burst == kAxiBurstIncr ? IO_AXI_BURST_INCR : IO_AXI_BURST_FIXED;
}

uint64_t MasterBeatAddress(uint64_t addr, std::size_t beat_index,
                           std::size_t beat_size, uint8_t burst)
{
    if (burst == kAxiBurstIncr) {
        return addr + beat_index * beat_size;
    }
    return addr;
}

uint64_t MasterTxnBeatAddress(uint64_t addr, std::size_t beat_index,
                              std::size_t beat_size, IoAxiBurstType burst)
{
    if (burst == IO_AXI_BURST_INCR) {
        return addr + beat_index * beat_size;
    }
    return addr;
}

bool MasterBurstParamsValid(uint64_t addr, uint8_t len, uint8_t size,
                            uint8_t burst, std::size_t *beat_count,
                            std::size_t *beat_size,
                            std::size_t *total_size)
{
    const std::size_t beats = MasterBeatCount(len);
    const std::size_t bytes = MasterBeatBytes(size);
    const std::size_t total = beats * bytes;

    if (!bytes || !MasterBurstSupported(burst) ||
        total > IO_AXI_MAX_BEAT_BYTES) {
        return false;
    }

    for (std::size_t i = 0; i < beats; i++) {
        const uint64_t beat_addr = MasterBeatAddress(addr, i, bytes, burst);
        const unsigned lane = unsigned(beat_addr & (kAxiDataBytes - 1));

        if (lane + bytes > kAxiDataBytes) {
            return false;
        }
    }

    if (beat_count) {
        *beat_count = beats;
    }
    if (beat_size) {
        *beat_size = bytes;
    }
    if (total_size) {
        *total_size = total;
    }
    return true;
}

uint64_t PackReadBeat(const uint8_t *data, uint64_t beat_addr,
                      std::size_t beat_size)
{
    const unsigned lane = unsigned(beat_addr & (kAxiDataBytes - 1));
    uint64_t packed = 0;

    for (std::size_t i = 0; i < beat_size && lane + i < kAxiDataBytes; i++) {
        packed |= uint64_t(data[i]) << ((lane + i) * 8);
    }
    return packed;
}

} // namespace

Runtime::Runtime(const io_rtl_system_config_t *config,
                 const io_rtl_system_callbacks_t *callbacks)
{
    config_.mmio_base_hint = kDefaultMmioBaseHint;
    config_.mmio_size_hint = kDefaultMmioSizeHint;

    if (config) {
        config_ = *config;
        if (!config_.mmio_base_hint) {
            config_.mmio_base_hint = kDefaultMmioBaseHint;
        }
        if (!config_.mmio_size_hint) {
            config_.mmio_size_hint = kDefaultMmioSizeHint;
        }
    }
    master_outstanding_limit_ = config_.io2q_outstanding ?
                                config_.io2q_outstanding : 1;
    if (callbacks) {
        callbacks_ = *callbacks;
    }

    if (!callbacks_.guest_read || !callbacks_.guest_write) {
        throw std::runtime_error("missing guest memory callbacks");
    }

    dut_.emplace();
    InitializeDut();
    Reset();
}

Runtime::~Runtime()
{
    if (dut_) {
        dut_.reset();
    }
}

void Runtime::SetError(const char *msg)
{
    std::snprintf(last_error_.data(), last_error_.size(), "%s",
                  msg ? msg : "");
    Log(0, "%s", last_error_.data());
}

void Runtime::Log(int level, const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    if (!callbacks_.log) {
        return;
    }

    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    callbacks_.log(callbacks_.opaque, level, buf);
}

uint64_t Runtime::LoadLe(const void *data, std::size_t size)
{
    const auto *bytes = static_cast<const uint8_t *>(data);
    uint64_t value = 0;

    for (std::size_t i = 0; i < size && i < sizeof(value); i++) {
        value |= uint64_t(bytes[i]) << (i * 8);
    }
    return value;
}

void Runtime::StoreLe(void *data, uint64_t value, std::size_t size)
{
    auto *bytes = static_cast<uint8_t *>(data);

    for (std::size_t i = 0; i < size; i++) {
        bytes[i] = uint8_t(value >> (i * 8));
    }
}

uint64_t Runtime::LowMask(unsigned bits)
{
    return bits >= 64 ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
}

unsigned Runtime::AxiSizeForBytes(std::size_t size)
{
    unsigned shift = 0;

    while ((std::size_t(1) << shift) < size) {
        shift++;
    }
    return shift;
}

void Runtime::InitializeDut()
{
    auto &dut = *dut_;

    dut.InitClock("clk");
    for (auto *pin : dut.xport.port_vec) {
        if (pin && pin->IsInIO() && pin != &dut.clk) {
            pin->AsImmWrite();
        }
    }
    DriveIdleInputs();
    dut.RefreshComb();
}

void Runtime::DriveIdleInputs()
{
    auto &dut = *dut_;

    for (auto *pin : dut.xport.port_vec) {
        if (pin && pin->IsInIO()) {
            pin->Set(uint64_t(0));
        }
    }

    dut.clk = 0;
    dut.rstn = 1;

    dut.s_axi_awid = 0;
    dut.s_axi_awaddr = 0;
    dut.s_axi_awlen = 0;
    dut.s_axi_awsize = 0;
    dut.s_axi_awburst = kAxiBurstIncr;
    dut.s_axi_awvalid = 0;
    dut.s_axi_wdata = 0;
    dut.s_axi_wstrb = 0;
    dut.s_axi_wlast = 0;
    dut.s_axi_wvalid = 0;
    dut.s_axi_bready = 0;
    dut.s_axi_arid = 0;
    dut.s_axi_araddr = 0;
    dut.s_axi_arlen = 0;
    dut.s_axi_arsize = 0;
    dut.s_axi_arburst = kAxiBurstIncr;
    dut.s_axi_arvalid = 0;
    dut.s_axi_rready = 0;

    dut.m_axi_awready = 0;
    dut.m_axi_wready = 0;
    dut.m_axi_bvalid = 0;
    dut.m_axi_bid = 0;
    dut.m_axi_bresp = kAxiRespOkay;
    dut.m_axi_arready = 0;
    dut.m_axi_rvalid = 0;
    dut.m_axi_rid = 0;
    dut.m_axi_rdata = 0;
    dut.m_axi_rresp = kAxiRespOkay;
    dut.m_axi_rlast = 1;

    dut.gbus_axi_awid = 0;
    dut.gbus_axi_awaddr = 0;
    dut.gbus_axi_awlen = 0;
    dut.gbus_axi_awsize = 0;
    dut.gbus_axi_awburst = kAxiBurstIncr;
    dut.gbus_axi_awvalid = 0;
    dut.gbus_axi_wdata = 0;
    dut.gbus_axi_wstrb = 0;
    dut.gbus_axi_wlast = 0;
    dut.gbus_axi_wvalid = 0;
    dut.gbus_axi_bready = 0;
    dut.gbus_axi_arid = 0;
    dut.gbus_axi_araddr = 0;
    dut.gbus_axi_arlen = 0;
    dut.gbus_axi_arsize = 0;
    dut.gbus_axi_arburst = kAxiBurstIncr;
    dut.gbus_axi_arvalid = 0;
    dut.gbus_axi_rready = 0;
}

int Runtime::Reset()
{
    DriveIdleInputs();
    read_responses_.clear();
    write_responses_.clear();
    pending_write_addrs_.clear();
    pending_write_data_.clear();
    master_txn_queue_.clear();
    inflight_txns_.clear();
    next_slave_id_ = 1;
    last_error_[0] = '\0';

    dut_->rstn = 0;
    Step(8);
    dut_->rstn = 1;
    Step(8);
    return IO_RTL_SYSTEM_OK;
}

void Runtime::PlanAndApplyMasterInputs()
{
    auto &dut = *dut_;
    const bool addr_ready = CanAcceptMasterAddress();

    dut.m_axi_awready = addr_ready ? 1 : 0;
    dut.m_axi_wready = CanAcceptMasterWriteData() ? 1 : 0;
    dut.m_axi_arready = addr_ready ? 1 : 0;

    dut.m_axi_bvalid = write_responses_.empty() ? 0 : 1;
    dut.m_axi_bid = write_responses_.empty() ? 0 : write_responses_.front().id;
    dut.m_axi_bresp = write_responses_.empty() ? kAxiRespOkay :
                      write_responses_.front().resp;

    dut.m_axi_rvalid = read_responses_.empty() ? 0 : 1;
    dut.m_axi_rid = read_responses_.empty() ? 0 : read_responses_.front().id;
    dut.m_axi_rdata = read_responses_.empty() ? 0 :
                      read_responses_.front().data;
    dut.m_axi_rresp = read_responses_.empty() ? kAxiRespOkay :
                      read_responses_.front().resp;
    dut.m_axi_rlast = read_responses_.empty() ? 1 :
                      read_responses_.front().last;
}

std::size_t Runtime::MasterOutstandingCount() const
{
    return pending_write_addrs_.size() + master_txn_queue_.size() +
           inflight_txns_.size() + read_responses_.size() +
           write_responses_.size();
}

bool Runtime::CanAcceptMasterAddress() const
{
    return MasterOutstandingCount() < master_outstanding_limit_;
}

bool Runtime::CanAcceptMasterWriteData() const
{
    if (!pending_write_addrs_.empty()) {
        const PendingMasterWrite &addr = pending_write_addrs_.front();
        std::size_t beat_count;
        std::size_t beat_size;
        std::size_t total_size;

        if (!MasterBurstParamsValid(addr.addr, addr.len, addr.size,
                                    addr.burst, &beat_count, &beat_size,
                                    &total_size)) {
            return pending_write_data_.size() < MasterBeatCount(addr.len);
        }
        return pending_write_data_.size() < beat_count;
    }
    return pending_write_data_.size() < kAxiMaxBurstBeats;
}

void Runtime::QueueMasterReadError(uint16_t id, uint8_t resp)
{
    MasterReadResponse entry;

    entry.valid = true;
    entry.id = id;
    entry.resp = resp;
    entry.last = true;
    entry.data = 0;
    read_responses_.push_back(entry);
}

void Runtime::QueueMasterWriteError(uint16_t id, uint8_t resp)
{
    MasterWriteResponse entry;

    entry.valid = true;
    entry.id = id;
    entry.resp = resp;
    write_responses_.push_back(entry);
}

void Runtime::QueueMasterRead(uint16_t id, uint64_t addr, uint8_t len,
                              uint8_t size, uint8_t burst)
{
    MasterTransaction txn = {};
    std::size_t beat_count;
    std::size_t beat_size;
    std::size_t total_size;

    if (!MasterBurstParamsValid(addr, len, size, burst, &beat_count,
                                &beat_size, &total_size)) {
        QueueMasterReadError(id, kAxiRespSlvErr);
        return;
    }

    txn.txn.direction = IO_AXI_DIRECTION_IO2Q_READ;
    txn.txn.transaction_id = id;
    txn.txn.address = addr;
    txn.txn.beat_count = uint16_t(beat_count);
    txn.txn.beat_size = uint8_t(beat_size);
    txn.txn.burst_type = MasterBurstType(burst);
    txn.txn.last = true;
    txn.txn.response = IO_AXI_RESPONSE_OKAY;
    txn.txn.translated = true;
    master_txn_queue_.push_back(txn);
}

void Runtime::QueueMasterWrite(const PendingMasterWrite &addr)
{
    MasterTransaction txn = {};
    std::size_t beat_count;
    std::size_t beat_size;
    std::size_t total_size;

    if (!MasterBurstParamsValid(addr.addr, addr.len, addr.size, addr.burst,
                                &beat_count, &beat_size, &total_size)) {
        QueueMasterWriteError(addr.id, kAxiRespSlvErr);
        return;
    }

    if (pending_write_data_.size() < beat_count) {
        return;
    }

    for (std::size_t beat = 0; beat < beat_count; beat++) {
        const PendingMasterWriteData &data = pending_write_data_[beat];
        const bool expected_last = beat + 1 == beat_count;
        const uint64_t beat_addr = MasterBeatAddress(addr.addr, beat,
                                                     beat_size, addr.burst);
        const unsigned lane = unsigned(beat_addr & (kAxiDataBytes - 1));
        const std::size_t out_base = beat * beat_size;

        if (data.last != expected_last) {
            QueueMasterWriteError(addr.id, kAxiRespSlvErr);
            pending_write_data_.erase(pending_write_data_.begin(),
                                      pending_write_data_.begin() +
                                      beat_count);
            return;
        }

        for (std::size_t i = 0; i < beat_size; i++) {
            const unsigned bus_lane = lane + unsigned(i);

            txn.data[out_base + i] =
                uint8_t(data.data >> (bus_lane * 8));
            txn.strobe[out_base + i] =
                (data.strb & (1u << bus_lane)) ? 0xff : 0;
        }
    }

    txn.txn.direction = IO_AXI_DIRECTION_IO2Q_WRITE;
    txn.txn.transaction_id = addr.id;
    txn.txn.address = addr.addr;
    txn.txn.beat_count = uint16_t(beat_count);
    txn.txn.beat_size = uint8_t(beat_size);
    txn.txn.burst_type = MasterBurstType(addr.burst);
    txn.txn.data = txn.data.data();
    txn.txn.byte_strobe = txn.strobe.data();
    txn.txn.last = true;
    txn.txn.response = IO_AXI_RESPONSE_OKAY;
    txn.txn.translated = true;
    master_txn_queue_.push_back(txn);
    pending_write_data_.erase(pending_write_data_.begin(),
                              pending_write_data_.begin() + beat_count);
}

void Runtime::QueueMasterWriteTransactions()
{
    while (!pending_write_addrs_.empty()) {
        PendingMasterWrite addr = pending_write_addrs_.front();
        std::size_t beat_count;
        std::size_t beat_size;
        std::size_t total_size;

        if (!MasterBurstParamsValid(addr.addr, addr.len, addr.size,
                                    addr.burst, &beat_count, &beat_size,
                                    &total_size)) {
            beat_count = MasterBeatCount(addr.len);
            if (pending_write_data_.size() < beat_count) {
                return;
            }
            pending_write_data_.erase(pending_write_data_.begin(),
                                      pending_write_data_.begin() +
                                      beat_count);
            QueueMasterWriteError(addr.id, kAxiRespSlvErr);
            pending_write_addrs_.pop_front();
            continue;
        }
        if (pending_write_data_.size() < beat_count) {
            return;
        }
        pending_write_addrs_.pop_front();
        QueueMasterWrite(addr);
    }
}

void Runtime::CommitMasterHandshakes()
{
    auto &dut = *dut_;
    const bool ar_hs = dut.m_axi_arvalid.U() != 0 &&
                       dut.m_axi_arready.U() != 0;
    const bool aw_hs = dut.m_axi_awvalid.U() != 0 &&
                       dut.m_axi_awready.U() != 0;
    const bool w_hs = dut.m_axi_wvalid.U() != 0 &&
                      dut.m_axi_wready.U() != 0;
    const bool r_hs = dut.m_axi_rvalid.U() != 0 &&
                      dut.m_axi_rready.U() != 0;
    const bool b_hs = dut.m_axi_bvalid.U() != 0 &&
                      dut.m_axi_bready.U() != 0;

    if (r_hs) {
        read_responses_.pop_front();
    }
    if (b_hs) {
        write_responses_.pop_front();
    }

    if (ar_hs) {
        QueueMasterRead(uint16_t(dut.m_axi_arid.U()),
                        dut.m_axi_araddr.U(),
                        uint8_t(dut.m_axi_arlen.U()),
                        uint8_t(dut.m_axi_arsize.U()),
                        uint8_t(dut.m_axi_arburst.U()));
    }

    if (aw_hs) {
        PendingMasterWrite entry;

        entry.id = uint16_t(dut.m_axi_awid.U());
        entry.addr = dut.m_axi_awaddr.U();
        entry.len = uint8_t(dut.m_axi_awlen.U());
        entry.size = uint8_t(dut.m_axi_awsize.U());
        entry.burst = uint8_t(dut.m_axi_awburst.U());
        pending_write_addrs_.push_back(entry);
    }
    if (w_hs) {
        PendingMasterWriteData entry;

        entry.data = dut.m_axi_wdata.U();
        entry.strb = uint8_t(dut.m_axi_wstrb.U());
        entry.last = dut.m_axi_wlast.U() != 0;
        pending_write_data_.push_back(entry);
    }
    QueueMasterWriteTransactions();
}

Runtime::StepResult Runtime::StepOne()
{
    auto &dut = *dut_;
    StepResult result = {};

    dut.RefreshComb();
    PlanAndApplyMasterInputs();
    dut.RefreshComb();

    result.s_aw_hs = dut.s_axi_awvalid.U() != 0 &&
                     dut.s_axi_awready.U() != 0;
    result.s_w_hs = dut.s_axi_wvalid.U() != 0 &&
                    dut.s_axi_wready.U() != 0;
    result.s_b_hs = dut.s_axi_bvalid.U() != 0 &&
                    dut.s_axi_bready.U() != 0;
    result.s_ar_hs = dut.s_axi_arvalid.U() != 0 &&
                     dut.s_axi_arready.U() != 0;
    result.s_r_hs = dut.s_axi_rvalid.U() != 0 &&
                    dut.s_axi_rready.U() != 0;
    result.s_bresp = uint8_t(dut.s_axi_bresp.U());
    result.s_rresp = uint8_t(dut.s_axi_rresp.U());
    result.s_rdata = dut.s_axi_rdata.U();
    result.gbus_aw_hs = dut.gbus_axi_awvalid.U() != 0 &&
                        dut.gbus_axi_awready.U() != 0;
    result.gbus_w_hs = dut.gbus_axi_wvalid.U() != 0 &&
                       dut.gbus_axi_wready.U() != 0;
    result.gbus_b_hs = dut.gbus_axi_bvalid.U() != 0 &&
                       dut.gbus_axi_bready.U() != 0;
    result.gbus_ar_hs = dut.gbus_axi_arvalid.U() != 0 &&
                        dut.gbus_axi_arready.U() != 0;
    result.gbus_r_hs = dut.gbus_axi_rvalid.U() != 0 &&
                       dut.gbus_axi_rready.U() != 0;
    result.gbus_bresp = uint8_t(dut.gbus_axi_bresp.U());
    result.gbus_rresp = uint8_t(dut.gbus_axi_rresp.U());
    result.gbus_rdata = uint32_t(dut.gbus_axi_rdata.U());

    CommitMasterHandshakes();
    dut.Step(1);
    return result;
}

int Runtime::Step(uint64_t cycles)
{
    for (uint64_t i = 0; i < cycles; i++) {
        StepOne();
    }
    return IO_RTL_SYSTEM_OK;
}

int Runtime::NextMasterTransaction(IoAxiTransaction *txn)
{
    if (!txn) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }
    if (master_txn_queue_.empty()) {
        return IO_RTL_SYSTEM_ERR_NO_TRANSACTION;
    }

    inflight_txns_.push_back(master_txn_queue_.front());
    master_txn_queue_.pop_front();

    auto &entry = inflight_txns_.back();
    entry.txn.data = entry.txn.direction == IO_AXI_DIRECTION_IO2Q_WRITE ?
                     entry.data.data() : nullptr;
    entry.txn.byte_strobe = entry.txn.direction == IO_AXI_DIRECTION_IO2Q_WRITE ?
                            entry.strobe.data() : nullptr;
    *txn = entry.txn;
    return IO_RTL_SYSTEM_OK;
}

int Runtime::CompleteMasterTransaction(const IoAxiTransaction *txn)
{
    if (!txn) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }

    for (auto it = inflight_txns_.begin(); it != inflight_txns_.end(); ++it) {
        if (it->txn.transaction_id != txn->transaction_id ||
            it->txn.direction != txn->direction ||
            it->txn.address != txn->address ||
            it->txn.beat_count != txn->beat_count ||
            it->txn.beat_size != txn->beat_size ||
            it->txn.burst_type != txn->burst_type) {
            continue;
        }

        if (txn->direction == IO_AXI_DIRECTION_IO2Q_READ) {
            const auto *bytes = txn->data;
            const std::size_t beat_count = txn->beat_count ?
                                           txn->beat_count : 1;
            const std::size_t beat_size = txn->beat_size ?
                                          txn->beat_size : kAxiDataBytes;

            for (std::size_t beat = 0; beat < beat_count; beat++) {
                MasterReadResponse resp = {};
                const uint64_t beat_addr = MasterTxnBeatAddress(
                    txn->address, beat, beat_size, txn->burst_type);

                resp.valid = true;
                resp.id = txn->transaction_id;
                resp.resp = static_cast<uint8_t>(txn->response);
                resp.last = beat + 1 == beat_count;
                if (bytes && txn->response == IO_AXI_RESPONSE_OKAY) {
                    resp.data = PackReadBeat(bytes + beat * beat_size,
                                             beat_addr, beat_size);
                }
                read_responses_.push_back(resp);
            }
        } else if (txn->direction == IO_AXI_DIRECTION_IO2Q_WRITE) {
            MasterWriteResponse resp = {};
            resp.valid = true;
            resp.id = txn->transaction_id;
            resp.resp = static_cast<uint8_t>(txn->response);
            write_responses_.push_back(resp);
        } else {
            return IO_RTL_SYSTEM_ERR_INVALID;
        }

        inflight_txns_.erase(it);
        return IO_RTL_SYSTEM_OK;
    }

    return IO_RTL_SYSTEM_ERR_INVALID;
}

int Runtime::Poll()
{
    return Step(1);
}

bool Runtime::NeedsService() const
{
    return !master_txn_queue_.empty() || !inflight_txns_.empty() ||
           !pending_write_addrs_.empty() || !pending_write_data_.empty() ||
           !read_responses_.empty() || !write_responses_.empty();
}

void Runtime::ClearSlaveRequestInputs()
{
    auto &dut = *dut_;

    dut.s_axi_awvalid = 0;
    dut.s_axi_awaddr = 0;
    dut.s_axi_awlen = 0;
    dut.s_axi_awsize = 0;
    dut.s_axi_awburst = kAxiBurstIncr;

    dut.s_axi_wvalid = 0;
    dut.s_axi_wdata = 0;
    dut.s_axi_wstrb = 0;
    dut.s_axi_wlast = 0;
    dut.s_axi_bready = 0;

    dut.s_axi_arvalid = 0;
    dut.s_axi_araddr = 0;
    dut.s_axi_arlen = 0;
    dut.s_axi_arsize = 0;
    dut.s_axi_arburst = kAxiBurstIncr;
    dut.s_axi_rready = 0;
}

int Runtime::GbusRead(uint32_t addr, uint32_t *value)
{
    auto &dut = *dut_;

    if (!value || (addr & 0x3u)) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }

    for (int i = 0; i < kDefaultTimeoutCycles; i++) {
        dut.gbus_axi_arid = next_slave_id_++;
        dut.gbus_axi_araddr = addr;
        dut.gbus_axi_arlen = 0;
        dut.gbus_axi_arsize = 2;
        dut.gbus_axi_arburst = kAxiBurstIncr;
        dut.gbus_axi_arvalid = 1;
        dut.gbus_axi_rready = 1;

        StepResult result = StepOne();
        if (result.gbus_ar_hs) {
            dut.gbus_axi_arvalid = 0;
        }
        if (result.gbus_r_hs) {
            const uint8_t resp = result.gbus_rresp;
            *value = result.gbus_rdata;
            dut.gbus_axi_arvalid = 0;
            dut.gbus_axi_rready = 0;
            return resp == kAxiRespOkay ? IO_RTL_SYSTEM_OK :
                   resp == kAxiRespDecErr ? IO_RTL_SYSTEM_ERR_UNMAPPED :
                   IO_RTL_SYSTEM_ERR_IO;
        }
    }

    dut.gbus_axi_arvalid = 0;
    dut.gbus_axi_rready = 0;
    SetError("timeout waiting for gbus AXI read response");
    return IO_RTL_SYSTEM_ERR_TIMEOUT;
}

int Runtime::GbusWrite(uint32_t addr, uint32_t value)
{
    auto &dut = *dut_;
    bool aw_done = false;
    bool w_done = false;

    if (addr & 0x3u) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }

    for (int i = 0; i < kDefaultTimeoutCycles; i++) {
        dut.gbus_axi_awid = next_slave_id_++;
        dut.gbus_axi_awaddr = addr;
        dut.gbus_axi_awlen = 0;
        dut.gbus_axi_awsize = 2;
        dut.gbus_axi_awburst = kAxiBurstIncr;
        dut.gbus_axi_awvalid = aw_done ? 0 : 1;
        dut.gbus_axi_wdata = value;
        dut.gbus_axi_wstrb = 0x0f;
        dut.gbus_axi_wlast = 1;
        dut.gbus_axi_wvalid = w_done ? 0 : 1;
        dut.gbus_axi_bready = 1;

        StepResult result = StepOne();
        if (result.gbus_aw_hs) {
            aw_done = true;
            dut.gbus_axi_awvalid = 0;
        }
        if (result.gbus_w_hs) {
            w_done = true;
            dut.gbus_axi_wvalid = 0;
        }
        if (result.gbus_b_hs) {
            const uint8_t resp = result.gbus_bresp;
            dut.gbus_axi_awvalid = 0;
            dut.gbus_axi_wvalid = 0;
            dut.gbus_axi_bready = 0;
            return resp == kAxiRespOkay ? IO_RTL_SYSTEM_OK :
                   resp == kAxiRespDecErr ? IO_RTL_SYSTEM_ERR_UNMAPPED :
                   IO_RTL_SYSTEM_ERR_IO;
        }
    }

    dut.gbus_axi_awvalid = 0;
    dut.gbus_axi_wvalid = 0;
    dut.gbus_axi_bready = 0;
    SetError("timeout waiting for gbus AXI write response");
    return IO_RTL_SYSTEM_ERR_TIMEOUT;
}

bool Runtime::WaitSlaveAw()
{
    for (int i = 0; i < kDefaultTimeoutCycles; i++) {
        if (StepOne().s_aw_hs) {
            return true;
        }
    }
    return false;
}

bool Runtime::WaitSlaveW()
{
    for (int i = 0; i < kDefaultTimeoutCycles; i++) {
        if (StepOne().s_w_hs) {
            return true;
        }
    }
    return false;
}

int Runtime::WaitSlaveB()
{
    for (int i = 0; i < kDefaultTimeoutCycles; i++) {
        StepResult result = StepOne();
        if (result.s_b_hs) {
            const uint8_t resp = result.s_bresp;
            return resp == kAxiRespOkay ? IO_RTL_SYSTEM_OK :
                   resp == kAxiRespDecErr ? IO_RTL_SYSTEM_ERR_UNMAPPED :
                   IO_RTL_SYSTEM_ERR_IO;
        }
    }
    SetError("timeout waiting for AXI slave write response");
    return IO_RTL_SYSTEM_ERR_TIMEOUT;
}

bool Runtime::WaitSlaveAr()
{
    for (int i = 0; i < kDefaultTimeoutCycles; i++) {
        if (StepOne().s_ar_hs) {
            return true;
        }
    }
    return false;
}

int Runtime::WaitSlaveR(uint64_t &value)
{
    for (int i = 0; i < kDefaultTimeoutCycles; i++) {
        StepResult result = StepOne();
        if (result.s_r_hs) {
            const uint8_t resp = result.s_rresp;
            value = result.s_rdata;
            return resp == kAxiRespOkay ? IO_RTL_SYSTEM_OK :
                   resp == kAxiRespDecErr ? IO_RTL_SYSTEM_ERR_UNMAPPED :
                   IO_RTL_SYSTEM_ERR_IO;
        }
    }
    SetError("timeout waiting for AXI slave read response");
    return IO_RTL_SYSTEM_ERR_TIMEOUT;
}

int Runtime::MmioWrite(uint64_t addr, const void *data, std::size_t size)
{
    auto &dut = *dut_;

    if (!data || size == 0 || size > kAxiDataBytes ||
        (size & (size - 1)) != 0 ||
        ((addr & (size - 1)) != 0)) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }

    const uint64_t value = LoadLe(data, size);
    const unsigned lane = unsigned(addr & (kAxiDataBytes - 1));
    const uint64_t shifted = (value & LowMask(unsigned(size * 8))) <<
                             (lane * 8);
    const uint8_t strobe = uint8_t(((1u << size) - 1u) << lane);

    ClearSlaveRequestInputs();
    dut.s_axi_bready = 1;
    dut.s_axi_awid = next_slave_id_++;
    dut.s_axi_awaddr = addr;
    dut.s_axi_awlen = 0;
    dut.s_axi_awsize = AxiSizeForBytes(size);
    dut.s_axi_awburst = kAxiBurstIncr;
    dut.s_axi_awvalid = 1;

    if (!WaitSlaveAw()) {
        ClearSlaveRequestInputs();
        SetError("timeout waiting for AXI slave AWREADY");
        return IO_RTL_SYSTEM_ERR_TIMEOUT;
    }
    dut.s_axi_awvalid = 0;

    dut.s_axi_wdata = shifted;
    dut.s_axi_wstrb = strobe;
    dut.s_axi_wlast = 1;
    dut.s_axi_wvalid = 1;

    if (!WaitSlaveW()) {
        ClearSlaveRequestInputs();
        SetError("timeout waiting for AXI slave WREADY");
        return IO_RTL_SYSTEM_ERR_TIMEOUT;
    }
    dut.s_axi_wvalid = 0;
    dut.s_axi_wlast = 0;

    const int status = WaitSlaveB();
    ClearSlaveRequestInputs();
    if (status != IO_RTL_SYSTEM_OK) {
        SetError("AXI slave write returned non-OKAY response");
    }
    return status;
}

int Runtime::MmioRead(uint64_t addr, void *data, std::size_t size)
{
    auto &dut = *dut_;

    if (!data || size == 0 || size > kAxiDataBytes ||
        (size & (size - 1)) != 0 ||
        ((addr & (size - 1)) != 0)) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }

    ClearSlaveRequestInputs();
    dut.s_axi_rready = 1;
    dut.s_axi_arid = next_slave_id_++;
    dut.s_axi_araddr = addr;
    dut.s_axi_arlen = 0;
    dut.s_axi_arsize = AxiSizeForBytes(size);
    dut.s_axi_arburst = kAxiBurstIncr;
    dut.s_axi_arvalid = 1;

    if (!WaitSlaveAr()) {
        ClearSlaveRequestInputs();
        SetError("timeout waiting for AXI slave ARREADY");
        return IO_RTL_SYSTEM_ERR_TIMEOUT;
    }
    dut.s_axi_arvalid = 0;

    uint64_t raw = 0;
    const int status = WaitSlaveR(raw);
    ClearSlaveRequestInputs();
    if (status != IO_RTL_SYSTEM_OK) {
        std::memset(data, 0xff, size);
        SetError("AXI slave read returned non-OKAY response");
        return status;
    }

    const unsigned lane = unsigned(addr & (kAxiDataBytes - 1));
    StoreLe(data, raw >> (lane * 8), size);
    return IO_RTL_SYSTEM_OK;
}

} // namespace io_rtl_system
