#pragma once

#include "UT_iommu_wrap.hpp"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace iommu {

constexpr std::size_t kAxiDataBytes = 32;
constexpr int kDefaultAxiSlaveTimeoutCycles = 4096;

struct RequestContext {
    uint32_t device_id = 0;
    uint32_t process_id = 0;
    bool process_id_valid = false;
    bool is_translated = false;
};

struct AceLiteAttrs {
    uint8_t cache = 0;
    uint8_t prot = 0;
    uint8_t region = 0;
    uint8_t qos = 0;
    uint8_t snoop = 0;
    uint8_t domain = 0;
    uint8_t idunq = 0;
    uint8_t loop = 0;
    uint8_t awatop = 0;
};

struct DownstreamWriteEvent {
    uint64_t addr = 0;
    uint16_t id = 0;
    uint8_t size = 0;
    uint8_t burst = 0;
    std::size_t beat_index = 0;
    std::size_t beat_count = 0;
    uint32_t strobe = 0;
    bool last = false;
    std::array<unsigned char, kAxiDataBytes> data = {};
    AceLiteAttrs attrs = {};
};

struct DownstreamReadEvent {
    uint64_t addr = 0;
    uint16_t id = 0;
    uint8_t size = 0;
    uint8_t burst = 0;
    std::size_t beat_index = 0;
    std::size_t beat_count = 0;
    AceLiteAttrs attrs = {};
};

using DownstreamWriteCallback = std::function<void(const DownstreamWriteEvent &)>;
using DownstreamReadCallback =
    std::function<bool(const DownstreamReadEvent &, std::array<unsigned char, kAxiDataBytes> &)>;
using TranslationWriteEvent = DownstreamWriteEvent;
using TranslationReadEvent = DownstreamReadEvent;
using TranslationWriteCallback = DownstreamWriteCallback;
using TranslationReadCallback = DownstreamReadCallback;

namespace detail {

constexpr uint8_t kAxiBurstFixed = 0;
constexpr uint8_t kAxiBurstIncr = 1;
constexpr uint8_t kAxiRespOkay = 0;
constexpr uint8_t kAxiRespDecErr = 3;

struct AxiMasterSnapshot {
    bool awvalid = false;
    bool wvalid = false;
    bool arvalid = false;
    bool rready = false;
    bool bready = false;

    uint16_t awid = 0;
    uint16_t arid = 0;
    uint64_t awaddr = 0;
    uint64_t araddr = 0;
    uint8_t awlen = 0;
    uint8_t arlen = 0;
    uint8_t awsize = 0;
    uint8_t arsize = 0;
    uint8_t awburst = 0;
    uint8_t arburst = 0;
    uint32_t wstrb = 0;
    bool wlast = false;
    AceLiteAttrs aw_attrs = {};
    AceLiteAttrs ar_attrs = {};
    std::vector<unsigned char> wdata = std::vector<unsigned char>(kAxiDataBytes, 0);
};

struct AxiSlaveDrive {
    bool awready = false;
    bool wready = false;
    bool arready = false;

    bool bvalid = false;
    uint16_t bid = 0;
    uint8_t bresp = 0;

    bool rvalid = false;
    uint16_t rid = 0;
    uint8_t rresp = 0;
    bool rlast = false;
    std::vector<unsigned char> rdata = std::vector<unsigned char>(kAxiDataBytes, 0);
};

struct AxiSlaveSnapshot {
    bool awvalid = false;
    bool awready = false;
    bool wvalid = false;
    bool wready = false;
    bool wlast = false;
    bool arvalid = false;
    bool arready = false;
    bool bvalid = false;
    bool bready = false;
    bool rvalid = false;
    bool rready = false;
    bool rlast = false;

    uint16_t awid = 0;
    uint16_t arid = 0;
    uint16_t bid = 0;
    uint16_t rid = 0;
    uint64_t awaddr = 0;
    uint64_t araddr = 0;
    uint8_t awlen = 0;
    uint8_t arlen = 0;
    uint8_t awsize = 0;
    uint8_t arsize = 0;
    uint8_t awburst = 0;
    uint8_t arburst = 0;
    uint32_t wstrb = 0;
    uint8_t bresp = 0;
    uint8_t rresp = 0;
    AceLiteAttrs aw_attrs = {};
    AceLiteAttrs ar_attrs = {};

    std::vector<unsigned char> wdata = std::vector<unsigned char>(kAxiDataBytes, 0);
    std::vector<unsigned char> rdata = std::vector<unsigned char>(kAxiDataBytes, 0);
};

struct StepTrace {
    AxiMasterSnapshot mst;
    AxiSlaveSnapshot slv;
    AxiSlaveDrive mst_drive;

    bool mst_aw_handshake = false;
    bool mst_w_handshake = false;
    bool mst_ar_handshake = false;
    bool mst_b_handshake = false;
    bool mst_r_handshake = false;

    bool slv_aw_handshake = false;
    bool slv_w_handshake = false;
    bool slv_ar_handshake = false;
    bool slv_b_handshake = false;
    bool slv_r_handshake = false;

    bool tr_r_handshake = false;
    bool tr_t_handshake = false;
    uint64_t tr_tdata = 0;
    bool tr_tlast = false;
    uint8_t tr_tid = 0;
};

struct SlaveWriteBeatResult {
    bool aw_handshake = false;
    bool w_handshake = false;
    bool b_handshake = false;

    AxiMasterSnapshot aw_snapshot;
    AxiMasterSnapshot w_snapshot;
    std::vector<unsigned char> wdata = std::vector<unsigned char>(kAxiDataBytes, 0);
    uint32_t wstrb = 0;
    bool wlast = false;

    uint16_t bid = 0;
    uint8_t bresp = 0;
};

struct SlaveReadBeatResult {
    bool ar_handshake = false;
    bool r_handshake = false;

    AxiMasterSnapshot ar_snapshot;
    uint16_t rid = 0;
    uint8_t rresp = 0;
    bool rlast = false;
    std::vector<unsigned char> rdata = std::vector<unsigned char>(kAxiDataBytes, 0);
};

} // namespace detail

class IommuRuntime;

class ApbMmioPort
{
public:
    explicit ApbMmioPort(IommuRuntime &sim);

    bool Write(uint32_t addr, uint32_t data, int timeout_cycles = 32);
    bool Read(uint32_t addr, uint32_t &data, int timeout_cycles = 32);

private:
    IommuRuntime &sim_;
};

class AxiSlaveMemoryPort
{
public:
    explicit AxiSlaveMemoryPort(IommuRuntime &sim);

    bool Write(uint64_t addr,
               const std::vector<unsigned char> &data,
               int timeout_cycles = kDefaultAxiSlaveTimeoutCycles);
    bool Read(uint64_t addr,
              std::vector<unsigned char> &data,
              std::size_t len,
              int timeout_cycles = kDefaultAxiSlaveTimeoutCycles);
    bool WriteWithContext(uint64_t addr,
                          const std::vector<unsigned char> &data,
                          const RequestContext &context,
                          int timeout_cycles = kDefaultAxiSlaveTimeoutCycles);
    bool ReadWithContext(uint64_t addr,
                         std::vector<unsigned char> &data,
                         std::size_t len,
                         const RequestContext &context,
                         int timeout_cycles = kDefaultAxiSlaveTimeoutCycles);
    bool WriteWithContextAce(uint64_t addr,
                             const std::vector<unsigned char> &data,
                             const RequestContext &context,
                             const AceLiteAttrs &attrs,
                             int timeout_cycles = kDefaultAxiSlaveTimeoutCycles);
    bool ReadWithContextAce(uint64_t addr,
                            std::vector<unsigned char> &data,
                            std::size_t len,
                            const RequestContext &context,
                            const AceLiteAttrs &attrs,
                            int timeout_cycles = kDefaultAxiSlaveTimeoutCycles);

private:
    IommuRuntime &sim_;
    uint16_t next_id_ = 1;
};

class DownstreamPort
{
public:
    explicit DownstreamPort(IommuRuntime &sim);

    void SetCallbacks(DownstreamWriteCallback write_cb, DownstreamReadCallback read_cb);
    void Store(uint64_t addr, const std::vector<unsigned char> &data);
    std::vector<unsigned char> Load(uint64_t addr, std::size_t len);

private:
    friend class IommuRuntime;

    struct PendingBResp {
        uint16_t id = 0;
        uint8_t resp = 0;
    };

    struct PendingRBeat {
        uint16_t id = 0;
        uint8_t resp = 0;
        bool last = false;
        std::vector<unsigned char> data = std::vector<unsigned char>(kAxiDataBytes, 0);
    };

    struct WriteState {
        bool active = false;
        uint16_t id = 0;
        uint64_t next_addr = 0;
        uint16_t beats_left = 0;
        uint16_t beats_total = 0;
        uint16_t beat_index = 0;
        uint8_t size = 0;
        uint8_t burst = 0;
        uint8_t resp = detail::kAxiRespOkay;
        AceLiteAttrs attrs = {};
    };

    void ResetStateImpl();
    void StoreImpl(uint64_t addr, const std::vector<unsigned char> &data);
    std::vector<unsigned char> LoadImpl(uint64_t addr, std::size_t len) const;

    void DriveIdleImpl(UTiommu_wrap &dut) const;
    detail::AxiMasterSnapshot CaptureImpl(UTiommu_wrap &dut) const;
    detail::AxiSlaveDrive PlanImpl(const detail::AxiMasterSnapshot &snapshot) const;
    void ApplyImpl(UTiommu_wrap &dut, const detail::AxiSlaveDrive &drive) const;
    void CommitImpl(const detail::AxiMasterSnapshot &snapshot, const detail::AxiSlaveDrive &drive);

    static uint64_t BeatBytes(uint8_t size);
    static uint64_t AlignDown(uint64_t addr, uint64_t align);
    static bool SupportedBurst(uint8_t burst);
    static bool SupportedSize(uint8_t size);
    static uint8_t MakeResp(uint8_t size, uint8_t burst);

    uint8_t LoadByteImpl(uint64_t addr) const;
    void StoreByteImpl(uint64_t addr, unsigned char value);
    uint64_t AdvanceAddrImpl(uint64_t addr, uint8_t size, uint8_t burst) const;
    std::vector<unsigned char> LoadAlignedBeatImpl(uint64_t addr) const;
    void ApplyWriteBeatImpl(uint64_t addr,
                            const std::vector<unsigned char> &data,
                            uint32_t strobe,
                            const AceLiteAttrs &attrs);
    void QueueReadBurstImpl(const detail::AxiMasterSnapshot &snapshot);

    IommuRuntime &sim_;
    std::unordered_map<uint64_t, unsigned char> memory_;
    std::deque<PendingBResp> b_queue_;
    std::deque<PendingRBeat> r_queue_;
    WriteState write_;
    DownstreamWriteCallback write_cb_;
    DownstreamReadCallback read_cb_;
};

class TranslationPort
{
public:
    explicit TranslationPort(IommuRuntime &sim);

    void SetCallbacks(TranslationWriteCallback write_cb, TranslationReadCallback read_cb);
    void Store(uint64_t addr, const std::vector<unsigned char> &data);
    std::vector<unsigned char> Load(uint64_t addr, std::size_t len);

private:
    friend class IommuRuntime;

    struct PendingBResp {
        uint16_t id = 0;
        uint8_t resp = 0;
    };

    struct PendingRBeat {
        uint16_t id = 0;
        uint8_t resp = 0;
        bool last = false;
        std::vector<unsigned char> data = std::vector<unsigned char>(kAxiDataBytes, 0);
    };

    struct WriteState {
        bool active = false;
        uint16_t id = 0;
        uint64_t next_addr = 0;
        uint16_t beats_left = 0;
        uint16_t beats_total = 0;
        uint16_t beat_index = 0;
        uint8_t size = 0;
        uint8_t burst = 0;
        uint8_t resp = detail::kAxiRespOkay;
        AceLiteAttrs attrs = {};
    };

    void ResetStateImpl();
    void StoreImpl(uint64_t addr, const std::vector<unsigned char> &data);
    std::vector<unsigned char> LoadImpl(uint64_t addr, std::size_t len) const;

    void DriveIdleImpl(UTiommu_wrap &dut) const;
    detail::AxiMasterSnapshot CaptureImpl(UTiommu_wrap &dut) const;
    detail::AxiSlaveDrive PlanImpl(const detail::AxiMasterSnapshot &snapshot) const;
    void ApplyImpl(UTiommu_wrap &dut, const detail::AxiSlaveDrive &drive) const;
    void CommitImpl(const detail::AxiMasterSnapshot &snapshot, const detail::AxiSlaveDrive &drive);

    static uint64_t BeatBytes(uint8_t size);
    static uint64_t AlignDown(uint64_t addr, uint64_t align);
    static bool SupportedBurst(uint8_t burst);
    static bool SupportedSize(uint8_t size);
    static uint8_t MakeResp(uint8_t size, uint8_t burst);

    uint8_t LoadByteImpl(uint64_t addr) const;
    void StoreByteImpl(uint64_t addr, unsigned char value);
    uint64_t AdvanceAddrImpl(uint64_t addr, uint8_t size, uint8_t burst) const;
    std::vector<unsigned char> LoadAlignedBeatImpl(uint64_t addr) const;
    void ApplyWriteBeatImpl(uint64_t addr,
                            const std::vector<unsigned char> &data,
                            uint32_t strobe,
                            const AceLiteAttrs &attrs);
    void QueueReadBurstImpl(const detail::AxiMasterSnapshot &snapshot);

    IommuRuntime &sim_;
    std::unordered_map<uint64_t, unsigned char> memory_;
    std::deque<PendingBResp> b_queue_;
    std::deque<PendingRBeat> r_queue_;
    WriteState write_;
    TranslationWriteCallback write_cb_;
    TranslationReadCallback read_cb_;
};

class IommuRuntime
{
public:
    IommuRuntime();
    explicit IommuRuntime(std::vector<std::string> startup_args);
    ~IommuRuntime();

    ApbMmioPort &mmio() { return mmio_; }
    AxiSlaveMemoryPort &memory() { return memory_; }
    DownstreamPort &downstream() { return downstream_; }
    TranslationPort &translation() { return translation_; }
    bool AtsRequestTranslation(const RequestContext &context,
                               uint64_t iova,
                               std::size_t length,
                               bool no_write,
                               bool priv_req,
                               bool exec_req,
                               uint64_t &translated_addr,
                               uint64_t &addr_mask,
                               uint32_t &perm,
                               uint32_t &err_count);
    bool PriRequestPage(const RequestContext &context,
                        uint64_t iova,
                        uint16_t prgi,
                        bool lpig,
                        bool is_read,
                        bool is_write,
                        bool priv_req,
                        bool exec_req,
                        uint32_t &response_code);
    bool Step(int cycles = 1);
    void SetTrace(bool enabled);
    void SetTraceTxnId(bool enabled, uint16_t id);
    bool TraceEnabled() const { return trace_enabled_; }
    bool TraceTxnMatches(uint16_t id) const
    {
        return trace_txn_id_enabled_ && trace_txn_id_ == id;
    }
    bool ShouldLogSelectedSlaveTxn(uint16_t id) const
    {
        return trace_enabled_ || TraceTxnMatches(id);
    }
    bool ShouldLogSelectedSlaveTxnBeat(uint16_t id,
                                       std::size_t beat_index,
                                       std::size_t beat_count) const;

    template <typename Fn>
    auto RunOnWorker(Fn &&fn) -> std::invoke_result_t<std::decay_t<Fn> &>
    {
        using FnType = std::decay_t<Fn>;
        using ReturnT = std::invoke_result_t<FnType &>;

        if (std::this_thread::get_id() == worker_id_) {
            FnType local(std::forward<Fn>(fn));
            if constexpr (std::is_void_v<ReturnT>) {
                local();
                return;
            } else {
                return local();
            }
        }

        auto task = std::make_shared<std::packaged_task<ReturnT()>>(FnType(std::forward<Fn>(fn)));
        auto future = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push_back([task]() { (*task)(); });
        }
        cv_.notify_one();

        if constexpr (std::is_void_v<ReturnT>) {
            future.get();
            return;
        } else {
            return future.get();
        }
    }

private:
    friend class ApbMmioPort;
    friend class AxiSlaveMemoryPort;
    friend class DownstreamPort;
    friend class TranslationPort;

    void WorkerLoop();
    void StopWorker();

    void InitializeDutImpl();
    void InitIdleInputsImpl();
    void ResetDutImpl(int cycles = 5);
    void StepImpl(int cycles = 1);
    detail::StepTrace StepOneImpl();

    void DriveApbSetupImpl(bool is_write, uint32_t addr, uint32_t data);
    void ReleaseApbImpl();
    bool WaitApbReadyImpl(int timeout_cycles);
    bool MmioWriteImpl(uint32_t addr, uint32_t data, int timeout_cycles);
    bool MmioReadImpl(uint32_t addr, uint32_t &data, int timeout_cycles);

    detail::AxiSlaveSnapshot CaptureSlavePortImpl();
    void DriveSlaveWriteAddressImpl(bool valid,
                                    uint16_t id,
                                    uint64_t addr,
                                    uint8_t len,
                                    uint8_t size,
                                    uint8_t burst,
                                    const RequestContext &context,
                                    const AceLiteAttrs &attrs);
    void ReleaseSlaveWriteAddressImpl();
    void DriveSlaveWriteDataImpl(bool valid,
                                 const std::vector<unsigned char> &data,
                                 uint32_t strobe,
                                 bool last);
    void ReleaseSlaveWriteDataImpl();
    void DriveSlaveReadAddressImpl(bool valid,
                                   uint16_t id,
                                   uint64_t addr,
                                   uint8_t len,
                                   uint8_t size,
                                   uint8_t burst,
                                   const RequestContext &context,
                                   const AceLiteAttrs &attrs);
    void ReleaseSlaveReadAddressImpl();

    detail::SlaveWriteBeatResult WriteSlaveSingleBeatImpl(uint64_t addr,
                                                          const std::vector<unsigned char> &data,
                                                          uint32_t strobe,
                                                          uint8_t size,
                                                          uint16_t id,
                                                          const RequestContext &context,
                                                          const AceLiteAttrs &attrs,
                                                          int timeout_cycles);
    detail::SlaveReadBeatResult ReadSlaveSingleBeatImpl(uint64_t addr,
                                                        uint8_t size,
                                                        uint16_t id,
                                                        const RequestContext &context,
                                                        const AceLiteAttrs &attrs,
                                                        int timeout_cycles);
    static uint64_t PackSlaveUserImpl(const RequestContext &context);
    bool EnsureAtsLinkImpl(int timeout_cycles);
    bool SendAtsPriMessageImpl(const std::vector<uint64_t> &beats,
                               int timeout_cycles);
    bool ReceiveAtsPriMessageImpl(std::vector<uint64_t> &beats,
                                  int timeout_cycles);
    bool SendAtsPriAckImpl(uint8_t msg_type, int timeout_cycles);

    std::unique_ptr<UTiommu_wrap> dut_;
    DownstreamPort downstream_;
    TranslationPort translation_;
    ApbMmioPort mmio_;
    AxiSlaveMemoryPort memory_;
    std::vector<std::string> startup_args_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> tasks_;
    std::thread worker_;
    std::thread::id worker_id_ = {};
    bool stop_requested_ = false;
    bool trace_enabled_ = false;
    bool trace_txn_id_enabled_ = false;
    uint16_t trace_txn_id_ = 0;
    bool ats_link_connected_ = false;
};

} // namespace iommu
