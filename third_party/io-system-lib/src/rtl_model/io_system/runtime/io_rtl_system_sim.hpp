// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "io_rtl_system_api.h"
#include "UT_io_system_rtl_wrapper.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace io_rtl_system {

constexpr uint64_t kDefaultMmioBaseHint = 0x30040000ull;
constexpr uint64_t kDefaultMmioSizeHint = 0x1000ull;
constexpr std::size_t kAxiDataBytes = 8;
constexpr int kDefaultTimeoutCycles = 4096;

class Runtime {
public:
    Runtime(const io_rtl_system_config_t *config,
            const io_rtl_system_callbacks_t *callbacks);
    ~Runtime();

    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;

    int Reset();
    int MmioRead(uint64_t addr, void *data, std::size_t size);
    int MmioWrite(uint64_t addr, const void *data, std::size_t size);
    int GbusRead(uint32_t addr, uint32_t *value);
    int GbusWrite(uint32_t addr, uint32_t value);
    int NextMasterTransaction(IoAxiTransaction *txn);
    int CompleteMasterTransaction(const IoAxiTransaction *txn);
    int Step(uint64_t cycles);
    int Poll();
    bool NeedsService() const;
    const char *LastError() const { return last_error_.data(); }

private:
    struct MasterReadResponse {
        bool valid = false;
        uint16_t id = 0;
        uint8_t resp = 0;
        bool last = false;
        uint64_t data = 0;
    };

    struct MasterWriteResponse {
        bool valid = false;
        uint16_t id = 0;
        uint8_t resp = 0;
    };

    struct PendingMasterWrite {
        uint16_t id = 0;
        uint64_t addr = 0;
        uint8_t len = 0;
        uint8_t size = 0;
        uint8_t burst = 0;
    };

    struct PendingMasterWriteData {
        uint64_t data = 0;
        uint8_t strb = 0;
        bool last = false;
    };

    struct MasterTransaction {
        IoAxiTransaction txn = {};
        std::array<uint8_t, IO_AXI_MAX_BEAT_BYTES> data = {};
        std::array<uint8_t, IO_AXI_MAX_BEAT_BYTES> strobe = {};
    };

    struct StepResult {
        bool s_aw_hs = false;
        bool s_w_hs = false;
        bool s_b_hs = false;
        bool s_ar_hs = false;
        bool s_r_hs = false;
        uint8_t s_bresp = 0;
        uint8_t s_rresp = 0;
        uint64_t s_rdata = 0;
        bool gbus_aw_hs = false;
        bool gbus_w_hs = false;
        bool gbus_b_hs = false;
        bool gbus_ar_hs = false;
        bool gbus_r_hs = false;
        uint8_t gbus_bresp = 0;
        uint8_t gbus_rresp = 0;
        uint32_t gbus_rdata = 0;
    };

    void SetError(const char *msg);
    void Log(int level, const char *fmt, ...);
    void InitializeDut();
    void DriveIdleInputs();
    StepResult StepOne();
    void PlanAndApplyMasterInputs();
    void CommitMasterHandshakes();
    void QueueMasterWriteTransactions();
    void ClearSlaveRequestInputs();

    static uint64_t LoadLe(const void *data, std::size_t size);
    static void StoreLe(void *data, uint64_t value, std::size_t size);
    static unsigned AxiSizeForBytes(std::size_t size);
    static uint64_t LowMask(unsigned bits);

    std::size_t MasterOutstandingCount() const;
    bool CanAcceptMasterAddress() const;
    bool CanAcceptMasterWriteData() const;
    void QueueMasterRead(uint16_t id, uint64_t addr, uint8_t len,
                         uint8_t size, uint8_t burst);
    void QueueMasterWrite(const PendingMasterWrite &addr);
    void QueueMasterReadError(uint16_t id, uint8_t resp);
    void QueueMasterWriteError(uint16_t id, uint8_t resp);

    bool WaitSlaveAw();
    bool WaitSlaveW();
    int WaitSlaveB();
    bool WaitSlaveAr();
    int WaitSlaveR(uint64_t &value);

    io_rtl_system_config_t config_ = {};
    io_rtl_system_callbacks_t callbacks_ = {};
    std::optional<UTio_system_rtl_wrapper> dut_;
    std::array<char, 512> last_error_ = {};
    uint16_t next_slave_id_ = 1;
    std::size_t master_outstanding_limit_ = 1;

    std::deque<MasterReadResponse> read_responses_;
    std::deque<MasterWriteResponse> write_responses_;
    std::deque<PendingMasterWrite> pending_write_addrs_;
    std::deque<PendingMasterWriteData> pending_write_data_;
    std::deque<MasterTransaction> master_txn_queue_;
    std::deque<MasterTransaction> inflight_txns_;
};

} // namespace io_rtl_system
