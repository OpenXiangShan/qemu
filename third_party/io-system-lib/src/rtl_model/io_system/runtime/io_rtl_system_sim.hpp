// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "io_rtl_system_api.h"
#include "io_axi_master_engine.hpp"
#include "UT_io_system_rtl_wrapper.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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
    void SetServiceCallback(io_rtl_system_service_cb callback, void *opaque)
    { service_callback_ = callback; service_opaque_ = opaque; }
    bool NeedsService() const;
    const char *LastError() const { return last_error_.data(); }

private:
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
    StepResult StepMmio();
    void PlanAndApplyMasterInputs();
    void CommitMasterHandshakes();
    void ClearSlaveRequestInputs();

    static uint64_t LoadLe(const void *data, std::size_t size);
    static void StoreLe(void *data, uint64_t value, std::size_t size);
    static unsigned AxiSizeForBytes(std::size_t size);
    static uint64_t LowMask(unsigned bits);

    AxiMasterEngine::Inputs MasterInputs();

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
    AxiMasterEngine master_;
    io_rtl_system_service_cb service_callback_ = nullptr;
    void *service_opaque_ = nullptr;

};

} // namespace io_rtl_system
