// SPDX-License-Identifier: Apache-2.0
#include "io_rtl_system_sim.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace io_rtl_system {

namespace {

constexpr uint8_t kAxiRespOkay = 0;
constexpr uint8_t kAxiRespDecErr = 3;
constexpr uint8_t kAxiBurstIncr = 1;
} // namespace

Runtime::Runtime(const io_rtl_system_config_t *config,
                 const io_rtl_system_callbacks_t *callbacks)
    : master_(config ? config->io2q_outstanding : 1)
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
    if (callbacks) {
        callbacks_ = *callbacks;
    }

    if (!callbacks_.guest_read || !callbacks_.guest_write) {
        throw std::runtime_error("missing guest memory callbacks");
    }

    // This is an embedded model, so VCS must not re-exec the host process to
    // disable ASLR for its optional save/restore facility.
    dut_.emplace(std::initializer_list<const char *>{"-no_save"});
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
    master_.Reset();
    next_slave_id_ = 1;
    last_error_[0] = '\0';

    dut_->rstn = 0;
    Step(8);
    dut_->rstn = 1;
    Step(8);
    return IO_RTL_SYSTEM_OK;
}

AxiMasterEngine::Inputs Runtime::MasterInputs()
{
    auto &dut = *dut_;
    AxiMasterEngine::Inputs in;
    in.aw = {uint16_t(dut.m_axi_awid.U()), dut.m_axi_awaddr.U(),
             uint8_t(dut.m_axi_awlen.U()), uint8_t(dut.m_axi_awsize.U()),
             uint8_t(dut.m_axi_awburst.U())};
    in.ar = {uint16_t(dut.m_axi_arid.U()), dut.m_axi_araddr.U(),
             uint8_t(dut.m_axi_arlen.U()), uint8_t(dut.m_axi_arsize.U()),
             uint8_t(dut.m_axi_arburst.U())};
    in.w = {dut.m_axi_wdata.U(), uint8_t(dut.m_axi_wstrb.U()),
            dut.m_axi_wlast.U() != 0};
    in.awvalid = dut.m_axi_awvalid.U() != 0;
    in.arvalid = dut.m_axi_arvalid.U() != 0;
    in.wvalid = dut.m_axi_wvalid.U() != 0;
    in.bready = dut.m_axi_bready.U() != 0;
    in.rready = dut.m_axi_rready.U() != 0;
    return in;
}

void Runtime::PlanAndApplyMasterInputs()
{
    auto &dut = *dut_;
    const auto out = master_.Drive(MasterInputs());
    dut.m_axi_awready = out.awready;
    dut.m_axi_arready = out.arready;
    dut.m_axi_wready = out.wready;
    dut.m_axi_bvalid = out.bvalid;
    dut.m_axi_bid = out.bid;
    dut.m_axi_bresp = out.bresp;
    dut.m_axi_rvalid = out.rvalid;
    dut.m_axi_rid = out.rid;
    dut.m_axi_rdata = out.rdata;
    dut.m_axi_rresp = out.rresp;
    dut.m_axi_rlast = out.rlast;
}

void Runtime::CommitMasterHandshakes()
{
    const auto in = MasterInputs();
    // Use the actually driven pins: a combinational RTL master may have
    // changed VALID since Drive(), but handshakes use the current READY.
    AxiMasterEngine::Outputs out;
    auto &dut = *dut_;
    out.awready = dut.m_axi_awready.U() != 0;
    out.arready = dut.m_axi_arready.U() != 0;
    out.wready = dut.m_axi_wready.U() != 0;
    out.rvalid = dut.m_axi_rvalid.U() != 0;
    out.bvalid = dut.m_axi_bvalid.U() != 0;
    const auto errors = master_.ProtocolErrors();
    master_.Commit(in, out);
    if (master_.ProtocolErrors() != errors) {
        SetError(master_.LastProtocolError().c_str());
        throw std::runtime_error(master_.LastProtocolError());
    }
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

Runtime::StepResult Runtime::StepMmio()
{
    // The scheduler callback only moves memory transactions, never clocks RTL.
    // This lets E6/AME complete DDR traffic while software waits on SYSCTRL.
    if (service_callback_ && service_callback_(service_opaque_) != 0) {
        SetError("memory service failed during blocking MMIO");
        throw std::runtime_error(LastError());
    }
    return StepOne();
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
    return master_.Next(txn);
}

int Runtime::CompleteMasterTransaction(const IoAxiTransaction *txn)
{
    return master_.Complete(txn);
}

int Runtime::Poll()
{
    return Step(1);
}

bool Runtime::NeedsService() const
{
    return master_.NeedsService();
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

    const uint16_t id = next_slave_id_++;
    bool ar_done = false;
    for (int i = 0; i < kDefaultTimeoutCycles; i++) {
        dut.gbus_axi_arid = id;
        dut.gbus_axi_araddr = addr;
        dut.gbus_axi_arlen = 0;
        dut.gbus_axi_arsize = 2;
        dut.gbus_axi_arburst = kAxiBurstIncr;
        dut.gbus_axi_arvalid = ar_done ? 0 : 1;
        dut.gbus_axi_rready = 1;

        StepResult result = StepMmio();
        if (result.gbus_ar_hs) {
            ar_done = true;
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

    const uint16_t id = next_slave_id_++;
    for (int i = 0; i < kDefaultTimeoutCycles; i++) {
        dut.gbus_axi_awid = id;
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

        StepResult result = StepMmio();
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
        if (StepMmio().s_aw_hs) {
            return true;
        }
    }
    return false;
}

bool Runtime::WaitSlaveW()
{
    for (int i = 0; i < kDefaultTimeoutCycles; i++) {
        if (StepMmio().s_w_hs) {
            return true;
        }
    }
    return false;
}

int Runtime::WaitSlaveB()
{
    for (int i = 0; i < kDefaultTimeoutCycles; i++) {
        StepResult result = StepMmio();
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
        if (StepMmio().s_ar_hs) {
            return true;
        }
    }
    return false;
}

int Runtime::WaitSlaveR(uint64_t &value)
{
    for (int i = 0; i < kDefaultTimeoutCycles; i++) {
        StepResult result = StepMmio();
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
