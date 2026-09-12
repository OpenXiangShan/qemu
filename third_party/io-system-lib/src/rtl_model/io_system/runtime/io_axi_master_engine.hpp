// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "io_rtl_system_api.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <list>
#include <string>
#include <vector>

namespace io_rtl_system {

// A bounded AXI4 slave for the RTL's 64-bit memory master. Each scheduler
// transaction describes only the valid bytes of one bus beat (at most eight).
// No picker dependency: the same engine is exercised by the standalone test.
class AxiMasterEngine {
public:
    struct Address {
        uint16_t id = 0;
        uint64_t addr = 0;
        uint8_t len = 0;
        uint8_t size = 0;
        uint8_t burst = 1;
    };
    struct WriteData {
        uint64_t data = 0;
        uint8_t strb = 0;
        bool last = false;
    };
    struct Inputs {
        Address aw, ar;
        WriteData w;
        bool awvalid = false, arvalid = false, wvalid = false;
        bool bready = false, rready = false;
    };
    struct Outputs {
        bool awready = false, arready = false, wready = false;
        bool bvalid = false, rvalid = false, rlast = false;
        uint16_t bid = 0, rid = 0;
        uint8_t bresp = 0, rresp = 0;
        uint64_t rdata = 0;
    };

    explicit AxiMasterEngine(std::size_t outstanding = 1);
    void Reset();
    Outputs Drive(const Inputs &in) const;
    void Commit(const Inputs &in, const Outputs &out);
    int Next(IoAxiTransaction *txn);
    int Complete(const IoAxiTransaction *txn);
    bool NeedsService() const;
    std::size_t Outstanding() const { return bursts_.size(); }
    std::size_t BufferedWriteBeats() const;
    std::size_t ProtocolErrors() const { return protocol_errors_; }
    const std::string &LastProtocolError() const { return protocol_error_; }

private:
    static constexpr std::size_t kMaxBeats = 256;
    struct Burst {
        Address address;
        bool write = false;
        bool valid = false;
        bool captured = false;
        bool inflight = false;
        bool response_pending = false;
        unsigned beat = 0;
        uint8_t write_resp = IO_AXI_RESPONSE_OKAY;
        std::vector<WriteData> data;
        IoAxiTransaction memory = {};
        std::array<uint8_t, 8> bytes = {}, strobe = {};
    };
    struct ReadResponse {
        Burst *owner;
        uint64_t data;
        uint8_t resp;
        bool last;
    };
    static bool Valid(const Address &a);
    static unsigned Beats(const Address &a) { return unsigned(a.len) + 1; }
    static uint64_t BeatAddress(const Burst &b);
    static unsigned BeatBytes(const Burst &b);
    bool FirstForId(const Burst &b) const;
    void CaptureWrites();
    void QueueErrorsAndWriteResponses();
    void Erase(Burst *b);
    void Violation(const char *message);

    std::size_t limit_;
    bool prefer_read_ = true;
    std::list<Burst> bursts_;
    std::deque<WriteData> write_data_;
    std::deque<ReadResponse> read_responses_;
    std::deque<Burst *> write_responses_;
    std::size_t protocol_errors_ = 0;
    std::string protocol_error_;
};

} // namespace io_rtl_system
