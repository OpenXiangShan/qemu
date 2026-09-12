// SPDX-License-Identifier: Apache-2.0
#include "io_axi_master_engine.hpp"

#include <algorithm>
#include <limits>

namespace io_rtl_system {

AxiMasterEngine::AxiMasterEngine(std::size_t outstanding)
    : limit_(std::max(std::size_t(1), outstanding))
{
}

void AxiMasterEngine::Reset()
{
    // The caller must reset its scheduler too before any new work is submitted.
    bursts_.clear();
    write_data_.clear();
    read_responses_.clear();
    write_responses_.clear();
    prefer_read_ = true;
    protocol_errors_ = 0;
    protocol_error_.clear();
}

bool AxiMasterEngine::Valid(const Address &a)
{
    if (a.size > 3 || a.burst != IO_AXI_BURST_INCR) {
        return false;
    }
    const uint64_t bytes = uint64_t(1) << a.size;
    const uint64_t aligned = a.addr & ~(bytes - 1);
    const uint64_t extent = bytes * Beats(a) - 1;
    return aligned <= std::numeric_limits<uint64_t>::max() - extent &&
           (a.addr >> 12) == ((aligned + extent) >> 12);
}

uint64_t AxiMasterEngine::BeatAddress(const Burst &b)
{
    const uint64_t bytes = uint64_t(1) << b.address.size;
    return b.beat == 0 ? b.address.addr :
           (b.address.addr & ~(bytes - 1)) + bytes * b.beat;
}

unsigned AxiMasterEngine::BeatBytes(const Burst &b)
{
    const unsigned bytes = 1u << b.address.size;
    return b.beat == 0 ? bytes - unsigned(b.address.addr & (bytes - 1)) :
           bytes;
}

bool AxiMasterEngine::FirstForId(const Burst &b) const
{
    for (const auto &earlier : bursts_) {
        if (&earlier == &b) {
            return true;
        }
        if (earlier.write == b.write && earlier.address.id == b.address.id) {
            return false;
        }
    }
    return false;
}

AxiMasterEngine::Outputs AxiMasterEngine::Drive(const Inputs &in) const
{
    Outputs out;
    const std::size_t free = limit_ - bursts_.size();
    out.arready = out.awready = free != 0;
    if (free == 1 && in.arvalid && in.awvalid) {
        out.arready = prefer_read_;
        out.awready = !prefer_read_;
    }
    // W can precede AW, but cannot consume unbounded storage.
    out.wready = write_data_.size() < kMaxBeats;
    if (!read_responses_.empty()) {
        const auto &r = read_responses_.front();
        out.rvalid = true;
        out.rid = r.owner->address.id;
        out.rdata = r.data;
        out.rresp = r.resp;
        out.rlast = r.last;
    }
    if (!write_responses_.empty()) {
        out.bvalid = true;
        out.bid = write_responses_.front()->address.id;
        out.bresp = write_responses_.front()->write_resp;
    }
    return out;
}

void AxiMasterEngine::Erase(Burst *b)
{
    for (auto it = bursts_.begin(); it != bursts_.end(); ++it) {
        if (&*it == b) {
            bursts_.erase(it);
            return;
        }
    }
}

void AxiMasterEngine::Violation(const char *message)
{
    ++protocol_errors_;
    protocol_error_ = message;
}

void AxiMasterEngine::CaptureWrites()
{
    for (auto &b : bursts_) {
        if (!b.write || b.captured) {
            continue;
        }
        while (!write_data_.empty() && b.data.size() < Beats(b.address)) {
            b.data.push_back(write_data_.front());
            write_data_.pop_front();
        }
        if (b.data.size() != Beats(b.address)) {
            break;
        }
        b.captured = true;
        // Validate the entire W burst before making any memory side effects.
        for (unsigned i = 0; i < b.data.size(); ++i) {
            if (b.data[i].last != (i + 1 == b.data.size())) {
                b.valid = false;
                Violation("AXI master WLAST does not match AWLEN");
                break;
            }
        }
        if (b.valid) {
            for (unsigned i = 0; i < b.data.size(); ++i) {
                b.beat = i;
                const unsigned lane = unsigned(BeatAddress(b) & 7);
                const unsigned mask = ((1u << BeatBytes(b)) - 1) << lane;
                if (b.data[i].strb & ~mask) {
                    b.valid = false;
                    Violation("AXI master WSTRB asserts a lane outside AWSIZE");
                    break;
                }
            }
            b.beat = 0;
        }
    }
}

void AxiMasterEngine::QueueErrorsAndWriteResponses()
{
    for (auto &b : bursts_) {
        if (!FirstForId(b) || b.response_pending || b.inflight) {
            continue;
        }
        if (!b.write && !b.valid) {
            read_responses_.push_back({&b, 0, IO_AXI_RESPONSE_SLVERR,
                                       b.beat + 1 == Beats(b.address)});
            b.response_pending = true;
        } else if (b.write && b.captured &&
                   (!b.valid || b.beat == Beats(b.address))) {
            if (!b.valid) {
                b.write_resp = IO_AXI_RESPONSE_SLVERR;
            }
            write_responses_.push_back(&b);
            b.response_pending = true;
        }
    }
}

void AxiMasterEngine::Commit(const Inputs &in, const Outputs &out)
{
    if (out.rvalid && in.rready) {
        auto r = read_responses_.front();
        read_responses_.pop_front();
        if (r.last) {
            Erase(r.owner);
        } else {
            r.owner->response_pending = false;
            ++r.owner->beat;
        }
    }
    if (out.bvalid && in.bready) {
        auto *b = write_responses_.front();
        write_responses_.pop_front();
        Erase(b);
    }
    if (out.arready && in.arvalid) {
        bursts_.emplace_back();
        auto &b = bursts_.back();
        b.address = in.ar;
        b.valid = Valid(in.ar);
        prefer_read_ = false;
    }
    if (out.awready && in.awvalid) {
        bursts_.emplace_back();
        auto &b = bursts_.back();
        b.address = in.aw;
        b.write = true;
        b.valid = Valid(in.aw);
        b.data.reserve(Beats(in.aw));
        prefer_read_ = true;
    }
    if (out.wready && in.wvalid) {
        write_data_.push_back(in.w);
    }
    CaptureWrites();
    QueueErrorsAndWriteResponses();
}

int AxiMasterEngine::Next(IoAxiTransaction *txn)
{
    if (!txn) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }
    QueueErrorsAndWriteResponses();
    for (auto &b : bursts_) {
        if (!b.valid || b.inflight || b.response_pending ||
            !FirstForId(b) || (b.write && !b.captured)) {
            continue;
        }
        const uint64_t addr = BeatAddress(b);
        const unsigned bytes = BeatBytes(b);
        io_axi_transaction_init(&b.memory,
            b.write ? IO_AXI_DIRECTION_IO2Q_WRITE : IO_AXI_DIRECTION_IO2Q_READ,
            addr, uint8_t(bytes));
        b.memory.transaction_id = b.address.id;
        b.memory.translated = true;
        if (b.write) {
            const auto &w = b.data[b.beat];
            const unsigned lane = unsigned(addr & 7);
            for (unsigned i = 0; i < bytes; ++i) {
                b.bytes[i] = uint8_t(w.data >> ((lane + i) * 8));
                b.strobe[i] = (w.strb & (1u << (lane + i))) ? 0xff : 0;
            }
            b.memory.data = b.bytes.data();
            b.memory.byte_strobe = b.strobe.data();
        }
        b.inflight = true;
        *txn = b.memory;
        return IO_RTL_SYSTEM_OK;
    }
    return IO_RTL_SYSTEM_ERR_NO_TRANSACTION;
}

int AxiMasterEngine::Complete(const IoAxiTransaction *txn)
{
    if (!txn || unsigned(txn->response) > IO_AXI_RESPONSE_DECERR) {
        return IO_RTL_SYSTEM_ERR_INVALID;
    }
    for (auto &b : bursts_) {
        if (!b.inflight || b.memory.transaction_id != txn->transaction_id ||
            b.memory.direction != txn->direction ||
            b.memory.address != txn->address ||
            b.memory.beat_count != txn->beat_count ||
            b.memory.beat_size != txn->beat_size ||
            b.memory.burst_type != txn->burst_type) {
            continue;
        }
        if (!b.write && txn->response == IO_AXI_RESPONSE_OKAY && !txn->data) {
            return IO_RTL_SYSTEM_ERR_INVALID;
        }
        b.inflight = false;
        if (b.write) {
            b.write_resp = std::max(b.write_resp, uint8_t(txn->response));
            ++b.beat;
        } else {
            uint64_t value = 0;
            if (txn->response == IO_AXI_RESPONSE_OKAY) {
                const unsigned lane = unsigned(txn->address & 7);
                for (unsigned i = 0; i < txn->beat_size; ++i) {
                    value |= uint64_t(txn->data[i]) << ((lane + i) * 8);
                }
            }
            read_responses_.push_back({&b, value, uint8_t(txn->response),
                                       b.beat + 1 == Beats(b.address)});
            b.response_pending = true;
        }
        QueueErrorsAndWriteResponses();
        return IO_RTL_SYSTEM_OK;
    }
    return IO_RTL_SYSTEM_ERR_INVALID;
}

bool AxiMasterEngine::NeedsService() const
{
    return !bursts_.empty() || !write_data_.empty();
}

std::size_t AxiMasterEngine::BufferedWriteBeats() const
{
    std::size_t count = write_data_.size();
    for (const auto &b : bursts_) {
        count += b.data.size();
    }
    return count;
}

} // namespace io_rtl_system
