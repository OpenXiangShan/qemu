#include "iommu_sim.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>

namespace iommu {

namespace {

constexpr uint16_t kAxiSlaveIdMask = (1u << 10) - 1;
// Keep the software cap above the practical 4 KiB boundary limit so aligned
// 32-byte beats can expand to the full 128-beat AXI INCR burst when possible.
constexpr std::size_t kAxiMaxBurstBeats = 256;
constexpr std::size_t kAxiBurstBoundaryBytes = 4096;
constexpr std::size_t kAxiMaxOutstandingWriteBursts = 32;
constexpr std::size_t kAxiMaxOutstandingReadBursts = 32;
constexpr int kDefaultAtsPriTimeoutCycles = 4096;
constexpr uint64_t kAtsPriPageOffsetMask = 0xfffull;
constexpr uint8_t kAtdMsgCondis = 0x0;
constexpr uint8_t kAtdMsgTransFault = 0x1;
constexpr uint8_t kAtdMsgTransReqResp = 0x2;
constexpr uint8_t kAtdMsgPageReqAck = 0x8;
constexpr uint8_t kAtdMsgPageResp = 0x9;
constexpr uint8_t kAtdMsgPageRespAck = 0x9;
constexpr uint8_t kAtdMsgInvReqAck = 0xc;
constexpr uint8_t kAtdMsgSyncReqAck = 0xd;
constexpr uint8_t kAceLiteAtomicBitSetAwatop = 0x13;

constexpr uint32_t kIommuPermNone = 0;
constexpr uint32_t kIommuPermRead = 1;
constexpr uint32_t kIommuPermWrite = 2;
constexpr uint32_t kIommuPermExec = 4;
constexpr uint32_t kIommuPermPriv = 8;
constexpr uint32_t kIommuPermUntranslatedOnly = 32;

uint64_t LowBitsMask(unsigned bits)
{
    return bits >= 64 ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
}

uint64_t AtsPriIovaPage(uint64_t iova)
{
    return (iova >> 12) & LowBitsMask(52);
}

bool HasAceLiteAttrs(const AceLiteAttrs &attrs)
{
    return attrs.cache != 0 ||
           attrs.prot != 0 ||
           attrs.region != 0 ||
           attrs.qos != 0 ||
           attrs.snoop != 0 ||
           attrs.domain != 0 ||
           attrs.idunq != 0 ||
           attrs.loop != 0 ||
           attrs.awatop != 0;
}

std::string FormatAceLiteAttrs(const AceLiteAttrs &attrs)
{
    if (!HasAceLiteAttrs(attrs)) {
        return {};
    }

    char buffer[192];
    std::snprintf(buffer, sizeof(buffer),
                  " ace(cache=0x%x prot=0x%x region=0x%x qos=0x%x "
                  "snoop=0x%x domain=0x%x idunq=%u loop=%u awatop=0x%x)",
                  attrs.cache,
                  attrs.prot,
                  attrs.region,
                  attrs.qos,
                  attrs.snoop,
                  attrs.domain,
                  attrs.idunq,
                  attrs.loop,
                  attrs.awatop);
    return buffer;
}

uint8_t AxiSizeForSingleBeatAccess(std::size_t lane, std::size_t bytes)
{
    if (bytes == 0 || bytes > kAxiDataBytes ||
        (bytes & (bytes - 1)) != 0 ||
        (lane & (bytes - 1)) != 0) {
        return 5;
    }

    uint8_t size = 0;
    while ((std::size_t(1) << size) < bytes) {
        ++size;
    }
    return size;
}

std::vector<uint64_t> MakeAtsConnectRequest()
{
    uint64_t beat = kAtdMsgCondis;

    beat |= uint64_t(1) << 4; /* state: connect */
    beat |= uint64_t(1) << 5; /* protocol present */
    return {beat};
}

std::vector<uint64_t> MakeAtsTranslationRequest(const RequestContext &context,
                                                uint64_t iova,
                                                uint8_t request_index)
{
    uint64_t beat0 = kAtdMsgTransReqResp;
    uint64_t beat2 = 0;

    /*
     * iommu_atd_cdw.sv reconstructs cdw_req_data from a nominal
     * three-beat ATD TRANS_REQ as:
     *   {beat2[51:0], 12'd1, beat0[55:32], beat1[31:12],
     *    beat0[21], 3'd0, beat0[19:8], beat0[3:0]}
     *
     * The current iommu_atd_r2t.sv FSM only accepts the first non-last beat
     * and then the last beat, so the middle 64-bit beat is effectively zero.
     * Keep the software wire format aligned to that RTL behavior.
     */
    beat0 |= uint64_t(request_index) << 8;
    if (context.process_id_valid) {
        beat0 |= uint64_t(1) << 21;
    }
    beat0 |= uint64_t(context.device_id & 0x00ffffffu) << 32;
    beat2 |= AtsPriIovaPage(iova);

    return {beat0, beat2};
}

std::vector<uint64_t> MakePriPageRequest(const RequestContext &context,
                                         uint64_t iova,
                                         uint16_t prgi,
                                         bool lpig,
                                         bool is_read,
                                         bool is_write,
                                         bool priv_req,
                                         bool exec_req)
{
    uint64_t beat0 = kAtdMsgPageReqAck;
    uint64_t beat1 = 0;

    /*
     * iommu_atd_cdw.sv decodes PAGE_REQ as:
     *   {beat1[63:12], 1'd0, beat0[10:7], beat0[6], 6'd1,
     *    stale_ats_did, beat0[31:12], beat0[11], 6'd0,
     *    beat1[8:0], beat0[3:0]}
     *
     * The DID path intentionally follows the RTL today: it uses the most
     * recent ATS request's DID, so PRI must follow an ATS request from the
     * same device.
     */
    if (priv_req) {
        beat0 |= uint64_t(1) << 6;
    }
    if (is_read) {
        beat0 |= uint64_t(1) << 7;
    }
    if (is_write) {
        beat0 |= uint64_t(1) << 8;
    }
    if (exec_req) {
        beat0 |= uint64_t(1) << 9;
    }
    if (lpig) {
        beat0 |= uint64_t(1) << 10;
    }
    if (context.process_id_valid) {
        beat0 |= uint64_t(1) << 11;
    }
    beat0 |= uint64_t(context.process_id & 0x000fffffu) << 12;
    beat1 |= uint64_t(prgi & 0x1ffu);
    beat1 |= AtsPriIovaPage(iova) << 12;

    return {beat0, beat1};
}

std::vector<uint64_t> MakeAtsPriAck(uint8_t msg_type)
{
    return {uint64_t(msg_type & 0xf)};
}

uint32_t MapRtlPriResponse(uint32_t rtl_code)
{
    switch (rtl_code & 0x3u) {
    case 2:
        return 0; /* success */
    case 1:
        return 1; /* invalid request */
    default:
        return 2; /* failure */
    }
}

bool ShouldLogSlaveRequest(uint64_t beat_base, std::size_t chunk, bool force_log)
{
    if (force_log) {
        return true;
    }

    /*
     * Keep detailed trace for narrow control-path accesses like CQ updates,
     * doorbells, and MSI writes, but suppress 32-byte payload beats which can
     * otherwise dominate runtime and trigger guest NVMe timeouts.
     */
    return chunk < kAxiDataBytes || beat_base >= 0xffff0000ull;
}

void LogSlaveRequest(const IommuRuntime &sim,
                     const char *op,
                     uint64_t iova,
                     uint64_t beat_base,
                     std::size_t lane,
                     std::size_t chunk,
                     uint8_t axi_size,
                     uint16_t txn_id,
                     const RequestContext &context,
                     const AceLiteAttrs &attrs)
{
    if (!ShouldLogSlaveRequest(beat_base, chunk, sim.TraceTxnMatches(txn_id))) {
        return;
    }

    const std::string ace = FormatAceLiteAttrs(attrs);
    std::fprintf(stderr,
                 "[iommu-api] %s iova=0x%llx beat_base=0x%llx lane=%zu chunk=%zu "
                 "axsize=%u txn_id=0x%x rid=0x%x pasid=0x%x pasid_valid=%u "
                 "translated=%u%s\n",
                 op,
                 static_cast<unsigned long long>(iova),
                 static_cast<unsigned long long>(beat_base),
                 lane, chunk, axi_size, txn_id,
                 context.device_id,
                 context.process_id,
                 context.process_id_valid ? 1 : 0,
                 context.is_translated ? 1 : 0,
                 ace.c_str());
}

void LogSlaveWriteFailure(uint64_t iova,
                          uint64_t beat_base,
                          std::size_t lane,
                          std::size_t chunk,
                          uint16_t txn_id,
                          const RequestContext &context,
                          const AceLiteAttrs &attrs,
                          const detail::SlaveWriteBeatResult &result)
{
    const std::string ace = FormatAceLiteAttrs(attrs);
    std::fprintf(stderr,
                 "[iommu-api] slv-write-fail iova=0x%llx beat_base=0x%llx "
                 "lane=%zu chunk=%zu txn_id=0x%x rid=0x%x pasid=0x%x "
                 "pasid_valid=%u translated=%u aw_hs=%u w_hs=%u b_hs=%u "
                 "bid=0x%x bresp=0x%x%s\n",
                 static_cast<unsigned long long>(iova),
                 static_cast<unsigned long long>(beat_base),
                 lane, chunk, txn_id,
                 context.device_id,
                 context.process_id,
                 context.process_id_valid ? 1 : 0,
                 context.is_translated ? 1 : 0,
                 result.aw_handshake ? 1 : 0,
                 result.w_handshake ? 1 : 0,
                 result.b_handshake ? 1 : 0,
                 result.bid,
                 result.bresp,
                 ace.c_str());
}

void LogSlaveReadFailure(uint64_t iova,
                         uint64_t beat_base,
                         std::size_t lane,
                         std::size_t chunk,
                         uint16_t txn_id,
                         const RequestContext &context,
                         const AceLiteAttrs &attrs,
                         const detail::SlaveReadBeatResult &result)
{
    const std::string ace = FormatAceLiteAttrs(attrs);
    std::fprintf(stderr,
                 "[iommu-api] slv-read-fail iova=0x%llx beat_base=0x%llx "
                 "lane=%zu chunk=%zu txn_id=0x%x rid=0x%x pasid=0x%x "
                 "pasid_valid=%u translated=%u ar_hs=%u r_hs=%u "
                 "resp_rid=0x%x rresp=0x%x rlast=%u%s\n",
                 static_cast<unsigned long long>(iova),
                 static_cast<unsigned long long>(beat_base),
                 lane, chunk, txn_id,
                 context.device_id,
                 context.process_id,
                 context.process_id_valid ? 1 : 0,
                 context.is_translated ? 1 : 0,
                 result.ar_handshake ? 1 : 0,
                 result.r_handshake ? 1 : 0,
                 result.rid,
                 result.rresp,
                 result.rlast ? 1 : 0,
                 ace.c_str());
}

std::array<unsigned char, kAxiDataBytes> ToArray(const std::vector<unsigned char> &data)
{
    std::array<unsigned char, kAxiDataBytes> array = {};
    const std::size_t count = std::min(data.size(), array.size());
    for (std::size_t i = 0; i < count; ++i) {
        array[i] = data[i];
    }
    return array;
}

std::string ResolveCurrentExecutablePath()
{
    std::array<char, 4096> buffer = {};
    const ssize_t len = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (len > 0) {
        buffer[static_cast<std::size_t>(len)] = '\0';
        return std::string(buffer.data());
    }
    return "apb_example";
}

std::vector<std::string> BuildDefaultStartupArgs()
{
    return {ResolveCurrentExecutablePath()};
}

std::string FormatHexBytes(const std::vector<unsigned char> &data)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;

    out.reserve(data.size() * 2);
    for (unsigned char byte : data) {
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0xf]);
    }
    return out;
}

bool ShouldLogBurstBeatDefault(std::size_t beat_index, std::size_t beat_count)
{
    if (beat_count <= 32) {
        return true;
    }

    if (beat_index < 4 || beat_index + 1 == beat_count) {
        return true;
    }

    /*
     * The current split-QEMU NVMe corruption reproducer shows the first bad
     * payload window around beats 32..35 of a 128-beat write burst. Keep that
     * region visible in the slave-port handshake trace so we can compare the
     * exact bytes entering the RTL against the downstream callback payload.
     */
    return beat_index >= 32 && beat_index <= 35;
}

void LogSlaveBurstBeat(const IommuRuntime &sim,
                       const char *op,
                       uint64_t addr,
                       std::size_t beat_index,
                       std::size_t beat_count,
                       const std::vector<unsigned char> &data,
                       bool last,
                       uint16_t txn_id,
                       const RequestContext &context,
                       const AceLiteAttrs &attrs)
{
    if (!sim.ShouldLogSelectedSlaveTxnBeat(txn_id, beat_index, beat_count)) {
        return;
    }

    const std::string ace = FormatAceLiteAttrs(attrs);
    std::fprintf(stderr,
                 "[iommu-api] %s addr=0x%llx beat=%zu/%zu last=%u "
                 "txn_id=0x%x rid=0x%x pasid=0x%x pasid_valid=%u translated=%u "
                 "data=%s%s\n",
                 op,
                 static_cast<unsigned long long>(addr),
                 beat_index,
                 beat_count,
                 last ? 1 : 0,
                 txn_id,
                 context.device_id,
                 context.process_id,
                 context.process_id_valid ? 1 : 0,
                 context.is_translated ? 1 : 0,
                 FormatHexBytes(data).c_str(),
                 ace.c_str());
}

std::size_t MaxFullBeatBurstBeats(uint64_t addr, std::size_t remaining_bytes)
{
    if ((addr & (kAxiDataBytes - 1)) != 0 || remaining_bytes < kAxiDataBytes) {
        return 0;
    }

    std::size_t beats = remaining_bytes / kAxiDataBytes;
    const std::size_t boundary_bytes =
        kAxiBurstBoundaryBytes - (addr & (kAxiBurstBoundaryBytes - 1));

    beats = std::min(beats, boundary_bytes / kAxiDataBytes);
    beats = std::min(beats, kAxiMaxBurstBeats);
    return beats;
}

void LogSlaveBurstRequest(const IommuRuntime &sim,
                          const char *op,
                          uint64_t addr,
                          std::size_t beat_count,
                          uint16_t txn_id,
                          const RequestContext &context,
                          const AceLiteAttrs &attrs)
{
    if (beat_count <= 1 || !sim.ShouldLogSelectedSlaveTxn(txn_id)) {
        return;
    }

    const std::string ace = FormatAceLiteAttrs(attrs);
    std::fprintf(stderr,
                 "[iommu-api] %s addr=0x%llx beats=%zu axlen=%zu "
                 "beat_bytes=%zu total_bytes=%zu txn_id=0x%x rid=0x%x "
                 "pasid=0x%x pasid_valid=%u translated=%u%s\n",
                 op,
                 static_cast<unsigned long long>(addr),
                 beat_count,
                 beat_count - 1,
                 kAxiDataBytes,
                 beat_count * kAxiDataBytes,
                 txn_id,
                 context.device_id,
                 context.process_id,
                 context.process_id_valid ? 1 : 0,
                 context.is_translated ? 1 : 0,
                 ace.c_str());
}

} // namespace

ApbMmioPort::ApbMmioPort(IommuRuntime &sim) : sim_(sim) {}

bool ApbMmioPort::Write(uint32_t addr, uint32_t data, int timeout_cycles)
{
    return sim_.RunOnWorker([this, addr, data, timeout_cycles]() {
        return sim_.MmioWriteImpl(addr, data, timeout_cycles);
    });
}

bool ApbMmioPort::Read(uint32_t addr, uint32_t &data, int timeout_cycles)
{
    auto result = sim_.RunOnWorker([this, addr, timeout_cycles]() {
        uint32_t value = 0;
        const bool ok = sim_.MmioReadImpl(addr, value, timeout_cycles);
        return std::make_pair(ok, value);
    });
    data = result.second;
    return result.first;
}

AxiSlaveMemoryPort::AxiSlaveMemoryPort(IommuRuntime &sim) : sim_(sim) {}

bool AxiSlaveMemoryPort::Write(uint64_t addr, const std::vector<unsigned char> &data, int timeout_cycles)
{
    return WriteWithContext(addr, data, RequestContext {}, timeout_cycles);
}

bool AxiSlaveMemoryPort::Read(uint64_t addr,
                              std::vector<unsigned char> &data,
                              std::size_t len,
                              int timeout_cycles)
{
    return ReadWithContext(addr, data, len, RequestContext {}, timeout_cycles);
}

bool AxiSlaveMemoryPort::WriteWithContext(uint64_t addr,
                                          const std::vector<unsigned char> &data,
                                          const RequestContext &context,
                                          int timeout_cycles)
{
    return WriteWithContextAce(addr, data, context, AceLiteAttrs {}, timeout_cycles);
}

bool AxiSlaveMemoryPort::WriteWithContextAce(uint64_t addr,
                                             const std::vector<unsigned char> &data,
                                             const RequestContext &context,
                                             const AceLiteAttrs &attrs,
                                             int timeout_cycles)
{
    return sim_.RunOnWorker([this, addr, data, context, attrs, timeout_cycles]() {
        struct PendingWriteBurst {
            uint16_t txn_id = 0;
            uint64_t addr = 0;
            std::size_t beat_count = 0;
        };

        UTiommu_wrap &dut = *sim_.dut_;
        std::deque<PendingWriteBurst> pending_write_b;

        auto retire_write_b = [&pending_write_b](const detail::StepTrace &trace) {
            if (!trace.slv_b_handshake) {
                return true;
            }

            auto it = std::find_if(
                pending_write_b.begin(), pending_write_b.end(),
                [&trace](const PendingWriteBurst &pending) {
                    return pending.txn_id == trace.slv.bid;
                });

            if (it == pending_write_b.end()) {
                std::fprintf(stderr,
                             "[iommu-api] slv-write-burst-b-fail unknown bid=0x%x "
                             "bresp=0x%x\n",
                             trace.slv.bid, trace.slv.bresp);
                return false;
            }

            if (trace.slv.bresp != detail::kAxiRespOkay) {
                std::fprintf(stderr,
                             "[iommu-api] slv-write-burst-b-fail addr=0x%llx "
                             "beats=%zu txn_id=0x%x bid=0x%x bresp=0x%x\n",
                             static_cast<unsigned long long>(it->addr),
                             it->beat_count,
                             it->txn_id,
                             trace.slv.bid,
                             trace.slv.bresp);
                return false;
            }

            pending_write_b.erase(it);
            return true;
        };

        auto wait_for_write_b_slot = [&]() {
            int idle_cycles = 0;
            dut.iommu_slv_bready = 1;

            while (pending_write_b.size() >= kAxiMaxOutstandingWriteBursts) {
                const detail::StepTrace trace = sim_.StepOneImpl();
                const bool progress = trace.slv_b_handshake;

                if (!retire_write_b(trace)) {
                    return false;
                }

                if (progress) {
                    idle_cycles = 0;
                    continue;
                }

                if (++idle_cycles >= timeout_cycles) {
                    std::fprintf(stderr,
                                 "[iommu-api] slv-write-burst-b-timeout "
                                 "pending=%zu limit=%zu\n",
                                 pending_write_b.size(),
                                 kAxiMaxOutstandingWriteBursts);
                    return false;
                }
            }

            return true;
        };

        auto drain_write_b = [&]() {
            int idle_cycles = 0;
            dut.iommu_slv_bready = 1;

            while (!pending_write_b.empty()) {
                const detail::StepTrace trace = sim_.StepOneImpl();
                const bool progress = trace.slv_b_handshake;

                if (!retire_write_b(trace)) {
                    return false;
                }

                if (progress) {
                    idle_cycles = 0;
                    continue;
                }

                if (++idle_cycles >= timeout_cycles) {
                    std::fprintf(stderr,
                                 "[iommu-api] slv-write-burst-b-timeout "
                                 "pending=%zu\n",
                                 pending_write_b.size());
                    return false;
                }
            }

            dut.iommu_slv_bready = 0;
            return true;
        };

        auto write_full_beats = [this, &data, &context, &attrs, &pending_write_b,
                                 &retire_write_b, timeout_cycles](
                                    uint64_t burst_addr,
                                    std::size_t burst_offset,
            std::size_t burst_beats) {
            UTiommu_wrap &dut = *sim_.dut_;
            const uint16_t txn_id = next_id_++ & kAxiSlaveIdMask;
            const int burst_timeout_cycles =
                std::max(timeout_cycles, static_cast<int>(burst_beats) * timeout_cycles);
            bool aw_pending = true;
            std::size_t beat_index = 0;
            bool w_valid = false;
            int idle_cycles = 0;

            LogSlaveBurstRequest(sim_, "slv-write-burst", burst_addr,
                                 burst_beats, txn_id, context, attrs);

            sim_.DriveSlaveWriteAddressImpl(true, txn_id, burst_addr,
                                            static_cast<uint8_t>(burst_beats - 1),
                                            /*size=*/5, detail::kAxiBurstIncr,
                                            context, attrs);
            dut.iommu_slv_bready = 1;

            while (true) {
                if (idle_cycles >= burst_timeout_cycles) {
                    std::fprintf(stderr,
                                 "[iommu-api] slv-write-burst-timeout addr=0x%llx "
                                 "beats=%zu txn_id=0x%x aw_pending=%u "
                                 "beat_index=%zu w_valid=%u pending_b=%zu\n",
                                 static_cast<unsigned long long>(burst_addr),
                                 burst_beats,
                                 txn_id,
                                 aw_pending ? 1 : 0,
                                 beat_index,
                                 w_valid ? 1 : 0,
                                 pending_write_b.size());
                    break;
                }

                /*
                 * Keep W beats behind the AW handshake. The RTL slave path can
                 * apply backpressure independently on the address and data
                 * channels; issuing later data beats before the burst address
                 * is accepted risks shifting the write stream.
                 */
                if (!aw_pending && !w_valid && beat_index < burst_beats) {
                    std::vector<unsigned char> beat(kAxiDataBytes, 0);
                    const auto begin =
                        data.begin() + burst_offset + beat_index * kAxiDataBytes;

                    std::copy_n(begin, kAxiDataBytes, beat.begin());
                    LogSlaveBurstBeat(sim_, "slv-write-burst-drive",
                                      burst_addr + beat_index * kAxiDataBytes,
                                      beat_index, burst_beats,
                                      beat, beat_index + 1 == burst_beats,
                                      txn_id, context, attrs);
                    sim_.DriveSlaveWriteDataImpl(true, beat, 0xffffffffu,
                                                 beat_index + 1 == burst_beats);
                    w_valid = true;
                }

                const detail::StepTrace trace = sim_.StepOneImpl();
                bool progress = false;

                if (aw_pending && trace.slv_aw_handshake) {
                    aw_pending = false;
                    progress = true;
                    sim_.ReleaseSlaveWriteAddressImpl();
                }

                if (w_valid && trace.slv_w_handshake) {
                    LogSlaveBurstBeat(sim_, "slv-write-burst-hs",
                                      burst_addr + beat_index * kAxiDataBytes,
                                      beat_index, burst_beats,
                                      trace.slv.wdata,
                                      trace.slv.wlast,
                                      txn_id, context, attrs);
                    w_valid = false;
                    ++beat_index;
                    progress = true;
                    sim_.ReleaseSlaveWriteDataImpl();

                    if (beat_index == burst_beats) {
                        pending_write_b.push_back({txn_id, burst_addr, burst_beats});
                    }
                }

                if (!retire_write_b(trace)) {
                    break;
                }

                progress = progress || trace.slv_b_handshake;
                if (!aw_pending && beat_index == burst_beats) {
                    sim_.ReleaseSlaveWriteAddressImpl();
                    sim_.ReleaseSlaveWriteDataImpl();
                    return true;
                }

                idle_cycles = progress ? 0 : idle_cycles + 1;
            }

            sim_.ReleaseSlaveWriteAddressImpl();
            sim_.ReleaseSlaveWriteDataImpl();
            return false;
        };

        uint64_t cursor = addr;
        std::size_t offset = 0;

        while (offset < data.size()) {
            const std::size_t burst_beats =
                MaxFullBeatBurstBeats(cursor, data.size() - offset);

            /*
             * Large DMA payloads dominate runtime if every 32-byte beat is sent
             * as its own AW/W/B transaction. Collapse aligned full-beat regions
             * into INCR bursts and fall back to the old single-beat path for
             * unaligned head/tail fragments such as CQEs and doorbells.
             */
            if (burst_beats > 1) {
                if (!wait_for_write_b_slot()) {
                    sim_.ReleaseSlaveWriteAddressImpl();
                    sim_.ReleaseSlaveWriteDataImpl();
                    dut.iommu_slv_bready = 0;
                    return false;
                }

                if (!write_full_beats(cursor, offset, burst_beats)) {
                    sim_.ReleaseSlaveWriteAddressImpl();
                    sim_.ReleaseSlaveWriteDataImpl();
                    dut.iommu_slv_bready = 0;
                    return false;
                }
                cursor += burst_beats * kAxiDataBytes;
                offset += burst_beats * kAxiDataBytes;
                continue;
            }

            if (!drain_write_b()) {
                sim_.ReleaseSlaveWriteAddressImpl();
                sim_.ReleaseSlaveWriteDataImpl();
                dut.iommu_slv_bready = 0;
                return false;
            }

            const uint64_t beat_base = cursor & ~(uint64_t(kAxiDataBytes - 1));
            const std::size_t lane = static_cast<std::size_t>(cursor - beat_base);
            const std::size_t chunk = std::min(data.size() - offset, kAxiDataBytes - lane);

            std::vector<unsigned char> beat(kAxiDataBytes, 0);
            uint32_t strobe = 0;
            for (std::size_t i = 0; i < chunk; ++i) {
                const std::size_t dst_lane = lane + i;
                beat[dst_lane] = data[offset + i];
                strobe |= (uint32_t(1) << dst_lane);
            }

            const uint16_t txn_id = next_id_++ & kAxiSlaveIdMask;
            const uint8_t axi_size = AxiSizeForSingleBeatAccess(lane, chunk);
            if (sim_.ShouldLogSelectedSlaveTxn(txn_id)) {
                LogSlaveRequest(sim_, "slv-write", cursor, beat_base, lane, chunk,
                                axi_size, txn_id, context, attrs);
                LogSlaveBurstBeat(sim_, "slv-write-beat-drive", beat_base,
                                  0, 1, beat, true, txn_id, context, attrs);
            }
            const detail::SlaveWriteBeatResult result =
                sim_.WriteSlaveSingleBeatImpl(cursor, beat, strobe, axi_size,
                                              txn_id, context, attrs, timeout_cycles);
            if (result.w_handshake) {
                LogSlaveBurstBeat(sim_, "slv-write-beat-hs", beat_base,
                                  0, 1, result.wdata, result.wlast,
                                  txn_id, context, attrs);
            }
            if (!result.aw_handshake || !result.w_handshake || !result.b_handshake) {
                if (sim_.ShouldLogSelectedSlaveTxn(txn_id)) {
                    LogSlaveWriteFailure(cursor, beat_base, lane, chunk,
                                         txn_id, context, attrs, result);
                }
                return false;
            }
            if (result.bid != txn_id || result.bresp != detail::kAxiRespOkay) {
                if (sim_.ShouldLogSelectedSlaveTxn(txn_id)) {
                    LogSlaveWriteFailure(cursor, beat_base, lane, chunk,
                                         txn_id, context, attrs, result);
                }
                return false;
            }

            cursor += chunk;
            offset += chunk;
        }

        if (!drain_write_b()) {
            sim_.ReleaseSlaveWriteAddressImpl();
            sim_.ReleaseSlaveWriteDataImpl();
            dut.iommu_slv_bready = 0;
            return false;
        }

        return true;
    });
}

bool AxiSlaveMemoryPort::ReadWithContext(uint64_t addr,
                                         std::vector<unsigned char> &data,
                                         std::size_t len,
                                         const RequestContext &context,
                                         int timeout_cycles)
{
    return ReadWithContextAce(addr, data, len, context, AceLiteAttrs {}, timeout_cycles);
}

bool AxiSlaveMemoryPort::ReadWithContextAce(uint64_t addr,
                                            std::vector<unsigned char> &data,
                                            std::size_t len,
                                            const RequestContext &context,
                                            const AceLiteAttrs &attrs,
                                            int timeout_cycles)
{
    auto result = sim_.RunOnWorker([this, addr, len, context, attrs, timeout_cycles]() {
        struct PendingReadBurst {
            uint16_t txn_id = 0;
            uint64_t addr = 0;
            std::size_t out_offset = 0;
            std::size_t beat_count = 0;
            std::size_t beat_index = 0;
        };

        std::vector<unsigned char> out(len, 0);
        UTiommu_wrap &dut = *sim_.dut_;
        std::deque<PendingReadBurst> pending_read_r;

        auto retire_read_r = [&pending_read_r, &out, this,
                              &context, &attrs](const detail::StepTrace &trace) {
            if (!trace.slv_r_handshake) {
                return true;
            }

            auto it = std::find_if(
                pending_read_r.begin(), pending_read_r.end(),
                [&trace](const PendingReadBurst &pending) {
                    return pending.txn_id == trace.slv.rid;
                });

            if (it == pending_read_r.end()) {
                std::fprintf(stderr,
                             "[iommu-api] slv-read-burst-r-fail unknown rid=0x%x "
                             "rresp=0x%x rlast=%u\n",
                             trace.slv.rid,
                             trace.slv.rresp,
                             trace.slv.rlast ? 1 : 0);
                return false;
            }

            const bool expected_last = it->beat_index + 1 == it->beat_count;
            LogSlaveBurstBeat(sim_, "slv-read-burst-hs",
                              it->addr + it->beat_index * kAxiDataBytes,
                              it->beat_index, it->beat_count,
                              trace.slv.rdata, trace.slv.rlast,
                              it->txn_id, context, attrs);

            if (trace.slv.rresp != detail::kAxiRespOkay ||
                trace.slv.rlast != expected_last) {
                std::fprintf(stderr,
                             "[iommu-api] slv-read-burst-r-fail addr=0x%llx "
                             "beat=%zu/%zu txn_id=0x%x rid=0x%x rresp=0x%x "
                             "rlast=%u expected_last=%u\n",
                             static_cast<unsigned long long>(it->addr),
                             it->beat_index,
                             it->beat_count,
                             it->txn_id,
                             trace.slv.rid,
                             trace.slv.rresp,
                             trace.slv.rlast ? 1 : 0,
                             expected_last ? 1 : 0);
                return false;
            }

            std::copy(trace.slv.rdata.begin(), trace.slv.rdata.end(),
                      out.begin() + it->out_offset + it->beat_index * kAxiDataBytes);
            ++it->beat_index;
            if (trace.slv.rlast) {
                pending_read_r.erase(it);
            }

            return true;
        };

        auto wait_for_read_r_slot = [&]() {
            int idle_cycles = 0;
            sim_.ReleaseSlaveReadAddressImpl();
            dut.iommu_slv_rready = 1;

            while (pending_read_r.size() >= kAxiMaxOutstandingReadBursts) {
                const detail::StepTrace trace = sim_.StepOneImpl();
                const bool progress = trace.slv_r_handshake;

                if (!retire_read_r(trace)) {
                    return false;
                }

                if (progress) {
                    idle_cycles = 0;
                    continue;
                }

                if (++idle_cycles >= timeout_cycles) {
                    std::fprintf(stderr,
                                 "[iommu-api] slv-read-burst-r-timeout "
                                 "pending=%zu limit=%zu\n",
                                 pending_read_r.size(),
                                 kAxiMaxOutstandingReadBursts);
                    return false;
                }
            }

            return true;
        };

        auto drain_read_r = [&]() {
            int idle_cycles = 0;
            sim_.ReleaseSlaveReadAddressImpl();
            dut.iommu_slv_rready = 1;

            while (!pending_read_r.empty()) {
                const detail::StepTrace trace = sim_.StepOneImpl();
                const bool progress = trace.slv_r_handshake;

                if (!retire_read_r(trace)) {
                    return false;
                }

                if (progress) {
                    idle_cycles = 0;
                    continue;
                }

                if (++idle_cycles >= timeout_cycles) {
                    std::fprintf(stderr,
                                 "[iommu-api] slv-read-burst-r-timeout "
                                 "pending=%zu\n",
                                 pending_read_r.size());
                    return false;
                }
            }

            dut.iommu_slv_rready = 0;
            return true;
        };

        auto issue_read_ar = [this, &context, &attrs, &pending_read_r, &retire_read_r,
                              timeout_cycles](
                                  uint64_t burst_addr,
                                  std::size_t burst_offset,
                                  std::size_t burst_beats) {
            UTiommu_wrap &dut = *sim_.dut_;
            const uint16_t txn_id = next_id_++ & kAxiSlaveIdMask;
            const int burst_timeout_cycles =
                std::max(timeout_cycles, static_cast<int>(burst_beats) * timeout_cycles);
            int idle_cycles = 0;

            LogSlaveBurstRequest(sim_, "slv-read-burst", burst_addr,
                                 burst_beats, txn_id, context, attrs);

            sim_.DriveSlaveReadAddressImpl(true, txn_id, burst_addr,
                                           static_cast<uint8_t>(burst_beats - 1),
                                           /*size=*/5, detail::kAxiBurstIncr,
                                           context, attrs);
            dut.iommu_slv_rready = 1;

            while (true) {
                if (idle_cycles >= burst_timeout_cycles) {
                    std::fprintf(stderr,
                                 "[iommu-api] slv-read-burst-timeout addr=0x%llx "
                                 "beats=%zu txn_id=0x%x pending_r=%zu\n",
                                 static_cast<unsigned long long>(burst_addr),
                                 burst_beats,
                                 txn_id,
                                 pending_read_r.size());
                    break;
                }

                const detail::StepTrace trace = sim_.StepOneImpl();
                bool progress = false;

                if (trace.slv_ar_handshake) {
                    pending_read_r.push_back(
                        {txn_id, burst_addr, burst_offset, burst_beats, 0});
                    progress = true;
                    sim_.ReleaseSlaveReadAddressImpl();
                }

                if (!retire_read_r(trace)) {
                    break;
                }

                progress = progress || trace.slv_r_handshake;
                if (trace.slv_ar_handshake) {
                    sim_.ReleaseSlaveReadAddressImpl();
                    return true;
                }

                idle_cycles = progress ? 0 : idle_cycles + 1;
            }

            sim_.ReleaseSlaveReadAddressImpl();
            return false;
        };

        uint64_t cursor = addr;
        std::size_t offset = 0;

        while (offset < len) {
            const std::size_t burst_beats =
                MaxFullBeatBurstBeats(cursor, len - offset);

            if (burst_beats > 1) {
                if (!wait_for_read_r_slot()) {
                    sim_.ReleaseSlaveReadAddressImpl();
                    dut.iommu_slv_rready = 0;
                    return std::make_pair(false, std::vector<unsigned char>());
                }

                if (!issue_read_ar(cursor, offset, burst_beats)) {
                    sim_.ReleaseSlaveReadAddressImpl();
                    dut.iommu_slv_rready = 0;
                    return std::make_pair(false, std::vector<unsigned char>());
                }
                cursor += burst_beats * kAxiDataBytes;
                offset += burst_beats * kAxiDataBytes;
                continue;
            }

            if (!drain_read_r()) {
                sim_.ReleaseSlaveReadAddressImpl();
                dut.iommu_slv_rready = 0;
                return std::make_pair(false, std::vector<unsigned char>());
            }

            const uint64_t beat_base = cursor & ~(uint64_t(kAxiDataBytes - 1));
            const std::size_t lane = static_cast<std::size_t>(cursor - beat_base);
            const std::size_t chunk = std::min(len - offset, kAxiDataBytes - lane);
            const uint16_t txn_id = next_id_++ & kAxiSlaveIdMask;
            const uint8_t axi_size = AxiSizeForSingleBeatAccess(lane, chunk);
            if (sim_.ShouldLogSelectedSlaveTxn(txn_id)) {
                LogSlaveRequest(sim_, "slv-read", cursor, beat_base, lane, chunk,
                                axi_size, txn_id, context, attrs);
            }
            const detail::SlaveReadBeatResult beat =
                sim_.ReadSlaveSingleBeatImpl(cursor, axi_size, txn_id, context, attrs, timeout_cycles);
            if (beat.r_handshake) {
                LogSlaveBurstBeat(sim_, "slv-read-beat-hs", beat_base,
                                  0, 1, beat.rdata, beat.rlast,
                                  txn_id, context, attrs);
            }
            if (!beat.ar_handshake || !beat.r_handshake) {
                if (sim_.ShouldLogSelectedSlaveTxn(txn_id)) {
                    LogSlaveReadFailure(cursor, beat_base, lane, chunk,
                                        txn_id, context, attrs, beat);
                }
                return std::make_pair(false, std::vector<unsigned char>());
            }
            if (beat.rid != txn_id || beat.rresp != detail::kAxiRespOkay || !beat.rlast) {
                if (sim_.ShouldLogSelectedSlaveTxn(txn_id)) {
                    LogSlaveReadFailure(cursor, beat_base, lane, chunk,
                                        txn_id, context, attrs, beat);
                }
                return std::make_pair(false, std::vector<unsigned char>());
            }

            for (std::size_t i = 0; i < chunk; ++i) {
                out[offset + i] = beat.rdata[lane + i];
            }

            cursor += chunk;
            offset += chunk;
        }

        if (!drain_read_r()) {
            sim_.ReleaseSlaveReadAddressImpl();
            dut.iommu_slv_rready = 0;
            return std::make_pair(false, std::vector<unsigned char>());
        }

        return std::make_pair(true, out);
    });

    if (result.first) {
        data = std::move(result.second);
    } else {
        data.clear();
    }
    return result.first;
}

DownstreamPort::DownstreamPort(IommuRuntime &sim) : sim_(sim) {}

void DownstreamPort::SetCallbacks(DownstreamWriteCallback write_cb, DownstreamReadCallback read_cb)
{
    sim_.RunOnWorker([this, write_cb = std::move(write_cb), read_cb = std::move(read_cb)]() mutable {
        write_cb_ = std::move(write_cb);
        read_cb_ = std::move(read_cb);
    });
}

void DownstreamPort::Store(uint64_t addr, const std::vector<unsigned char> &data)
{
    sim_.RunOnWorker([this, addr, data]() { StoreImpl(addr, data); });
}

std::vector<unsigned char> DownstreamPort::Load(uint64_t addr, std::size_t len)
{
    return sim_.RunOnWorker([this, addr, len]() { return LoadImpl(addr, len); });
}

void DownstreamPort::ResetStateImpl()
{
    write_ = {};
    b_queue_.clear();
    r_queue_.clear();
}

void DownstreamPort::StoreImpl(uint64_t addr, const std::vector<unsigned char> &data)
{
    for (std::size_t i = 0; i < data.size(); ++i) {
        memory_[addr + i] = data[i];
    }
}

std::vector<unsigned char> DownstreamPort::LoadImpl(uint64_t addr, std::size_t len) const
{
    std::vector<unsigned char> data(len, 0);
    for (std::size_t i = 0; i < len; ++i) {
        data[i] = LoadByteImpl(addr + i);
    }
    return data;
}

void DownstreamPort::DriveIdleImpl(UTiommu_wrap &dut) const
{
    dut.iommu_mst_awready = 0;
    dut.iommu_mst_arready = 0;
    dut.iommu_mst_wready = 0;

    dut.iommu_mst_bid = 0;
    dut.iommu_mst_bvalid = 0;
    dut.iommu_mst_bresp = 0;
    dut.iommu_mst_buser = 0;
    dut.iommu_mst_bidunq = 0;
    dut.iommu_mst_bloop = 0;

    dut.iommu_mst_rid = 0;
    dut.iommu_mst_rvalid = 0;
    dut.iommu_mst_rresp = 0;
    dut.iommu_mst_rlast = 0;
    dut.iommu_mst_ruser = 0;
    dut.iommu_mst_ridunq = 0;
    dut.iommu_mst_rloop = 0;

    auto zero_data = std::vector<unsigned char>(kAxiDataBytes, 0);
    dut.iommu_mst_rdata.SetBytes(zero_data);
}

detail::AxiMasterSnapshot DownstreamPort::CaptureImpl(UTiommu_wrap &dut) const
{
    detail::AxiMasterSnapshot snapshot;

    snapshot.awvalid = dut.iommu_mst_awvalid.U() != 0;
    snapshot.wvalid = dut.iommu_mst_wvalid.U() != 0;
    snapshot.arvalid = dut.iommu_mst_arvalid.U() != 0;
    snapshot.rready = dut.iommu_mst_rready.U() != 0;
    snapshot.bready = dut.iommu_mst_bready.U() != 0;

    snapshot.awid = static_cast<uint16_t>(dut.iommu_mst_awid.U());
    snapshot.arid = static_cast<uint16_t>(dut.iommu_mst_arid.U());
    snapshot.awaddr = dut.iommu_mst_awaddr.U();
    snapshot.araddr = dut.iommu_mst_araddr.U();
    snapshot.awlen = static_cast<uint8_t>(dut.iommu_mst_awlen.U());
    snapshot.arlen = static_cast<uint8_t>(dut.iommu_mst_arlen.U());
    snapshot.awsize = static_cast<uint8_t>(dut.iommu_mst_awsize.U());
    snapshot.arsize = static_cast<uint8_t>(dut.iommu_mst_arsize.U());
    snapshot.awburst = static_cast<uint8_t>(dut.iommu_mst_awburst.U());
    snapshot.arburst = static_cast<uint8_t>(dut.iommu_mst_arburst.U());
    snapshot.wstrb = static_cast<uint32_t>(dut.iommu_mst_wstrb.U());
    snapshot.wlast = dut.iommu_mst_wlast.U() != 0;
    snapshot.aw_attrs.cache = static_cast<uint8_t>(dut.iommu_mst_awcache.U());
    snapshot.aw_attrs.prot = static_cast<uint8_t>(dut.iommu_mst_awprot.U());
    snapshot.aw_attrs.region = static_cast<uint8_t>(dut.iommu_mst_awregion.U());
    snapshot.aw_attrs.qos = static_cast<uint8_t>(dut.iommu_mst_awqos.U());
    snapshot.aw_attrs.snoop = static_cast<uint8_t>(dut.iommu_mst_awsnoop.U());
    snapshot.aw_attrs.domain = static_cast<uint8_t>(dut.iommu_mst_awdomain.U());
    snapshot.aw_attrs.idunq = static_cast<uint8_t>(dut.iommu_mst_awidunq.U());
    snapshot.aw_attrs.loop = static_cast<uint8_t>(dut.iommu_mst_awloop.U());
    snapshot.aw_attrs.awatop = static_cast<uint8_t>(dut.iommu_mst_awatop.U());
    snapshot.ar_attrs.cache = static_cast<uint8_t>(dut.iommu_mst_arcache.U());
    snapshot.ar_attrs.prot = static_cast<uint8_t>(dut.iommu_mst_arprot.U());
    snapshot.ar_attrs.region = static_cast<uint8_t>(dut.iommu_mst_arregion.U());
    snapshot.ar_attrs.qos = static_cast<uint8_t>(dut.iommu_mst_arqos.U());
    snapshot.ar_attrs.snoop = static_cast<uint8_t>(dut.iommu_mst_arsnoop.U());
    snapshot.ar_attrs.domain = static_cast<uint8_t>(dut.iommu_mst_ardomain.U());
    snapshot.ar_attrs.idunq = static_cast<uint8_t>(dut.iommu_mst_aridunq.U());
    snapshot.ar_attrs.loop = static_cast<uint8_t>(dut.iommu_mst_arloop.U());
    snapshot.wdata = dut.iommu_mst_wdata.GetBytes();

    return snapshot;
}

detail::AxiSlaveDrive DownstreamPort::PlanImpl(const detail::AxiMasterSnapshot &snapshot) const
{
    detail::AxiSlaveDrive drive;

    const bool can_accept_aw = !write_.active && b_queue_.empty();
    drive.awready = can_accept_aw;
    drive.wready = write_.active || (can_accept_aw && snapshot.awvalid);
    drive.arready = r_queue_.empty();

    if (!b_queue_.empty()) {
        drive.bvalid = true;
        drive.bid = b_queue_.front().id;
        drive.bresp = b_queue_.front().resp;
    }

    if (!r_queue_.empty()) {
        drive.rvalid = true;
        drive.rid = r_queue_.front().id;
        drive.rresp = r_queue_.front().resp;
        drive.rlast = r_queue_.front().last;
        drive.rdata = r_queue_.front().data;
    }

    return drive;
}

void DownstreamPort::ApplyImpl(UTiommu_wrap &dut, const detail::AxiSlaveDrive &drive) const
{
    dut.iommu_mst_awready = drive.awready ? 1 : 0;
    dut.iommu_mst_arready = drive.arready ? 1 : 0;
    dut.iommu_mst_wready = drive.wready ? 1 : 0;

    dut.iommu_mst_bid = drive.bid;
    dut.iommu_mst_bvalid = drive.bvalid ? 1 : 0;
    dut.iommu_mst_bresp = drive.bresp;
    dut.iommu_mst_buser = 0;
    dut.iommu_mst_bidunq = 0;
    dut.iommu_mst_bloop = 0;

    dut.iommu_mst_rid = drive.rid;
    dut.iommu_mst_rvalid = drive.rvalid ? 1 : 0;
    dut.iommu_mst_rresp = drive.rresp;
    dut.iommu_mst_rlast = drive.rlast ? 1 : 0;
    dut.iommu_mst_ruser = 0;
    dut.iommu_mst_ridunq = 0;
    dut.iommu_mst_rloop = 0;

    auto beat = drive.rdata;
    dut.iommu_mst_rdata.SetBytes(beat);
}

void DownstreamPort::CommitImpl(const detail::AxiMasterSnapshot &snapshot,
                                const detail::AxiSlaveDrive &drive)
{
    const bool aw_handshake = snapshot.awvalid && drive.awready;
    const bool w_handshake = snapshot.wvalid && drive.wready;
    const bool ar_handshake = snapshot.arvalid && drive.arready;
    const bool b_handshake = drive.bvalid && snapshot.bready;
    const bool r_handshake = drive.rvalid && snapshot.rready;

    if (aw_handshake) {
        write_.active = true;
        write_.id = snapshot.awid;
        write_.next_addr = snapshot.awaddr;
        write_.beats_left = static_cast<uint16_t>(snapshot.awlen) + 1;
        write_.beats_total = static_cast<uint16_t>(snapshot.awlen) + 1;
        write_.beat_index = 0;
        write_.size = snapshot.awsize;
        write_.burst = snapshot.awburst;
        write_.resp = MakeResp(snapshot.awsize, snapshot.awburst);
        write_.attrs = snapshot.aw_attrs;
    }

    if (w_handshake) {
        if (!write_.active) {
            std::fprintf(stderr, "[DOWNSTREAM] W handshake arrived before AW state was captured.\n");
        } else {
            if (write_.resp == detail::kAxiRespOkay) {
                ApplyWriteBeatImpl(write_.next_addr, snapshot.wdata, snapshot.wstrb, write_.attrs);
                if (write_cb_) {
                    DownstreamWriteEvent event;
                    event.addr = write_.next_addr;
                    event.id = write_.id;
                    event.size = write_.size;
                    event.burst = write_.burst;
                    event.beat_index = write_.beat_index;
                    event.beat_count = write_.beats_total;
                    event.strobe = snapshot.wstrb;
                    event.last = snapshot.wlast;
                    event.data = ToArray(snapshot.wdata);
                    event.attrs = write_.attrs;
                    write_cb_(event);
                }
            }

            if (write_.beats_left > 0) {
                --write_.beats_left;
            }

            const bool burst_done = (write_.beats_left == 0) || snapshot.wlast;
            if (burst_done) {
                b_queue_.push_back({write_.id, write_.resp});
                write_ = {};
            } else {
                ++write_.beat_index;
                write_.next_addr = AdvanceAddrImpl(write_.next_addr, write_.size, write_.burst);
            }
        }
    }

    if (ar_handshake) {
        QueueReadBurstImpl(snapshot);
    }

    if (b_handshake && !b_queue_.empty()) {
        b_queue_.pop_front();
    }

    if (r_handshake && !r_queue_.empty()) {
        r_queue_.pop_front();
    }
}

uint64_t DownstreamPort::BeatBytes(uint8_t size)
{
    return size < 63 ? (uint64_t(1) << size) : 0;
}

uint64_t DownstreamPort::AlignDown(uint64_t addr, uint64_t align)
{
    return addr & ~(align - 1);
}

bool DownstreamPort::SupportedBurst(uint8_t burst)
{
    return burst == detail::kAxiBurstFixed || burst == detail::kAxiBurstIncr;
}

bool DownstreamPort::SupportedSize(uint8_t size)
{
    const uint64_t bytes = BeatBytes(size);
    return bytes != 0 && bytes <= kAxiDataBytes;
}

uint8_t DownstreamPort::MakeResp(uint8_t size, uint8_t burst)
{
    return SupportedBurst(burst) && SupportedSize(size) ? detail::kAxiRespOkay : detail::kAxiRespDecErr;
}

uint8_t DownstreamPort::LoadByteImpl(uint64_t addr) const
{
    const auto it = memory_.find(addr);
    return it == memory_.end() ? 0 : it->second;
}

void DownstreamPort::StoreByteImpl(uint64_t addr, unsigned char value)
{
    memory_[addr] = value;
}

uint64_t DownstreamPort::AdvanceAddrImpl(uint64_t addr, uint8_t size, uint8_t burst) const
{
    if (burst == detail::kAxiBurstFixed) {
        return addr;
    }
    if (burst == detail::kAxiBurstIncr) {
        return addr + BeatBytes(size);
    }
    return addr;
}

std::vector<unsigned char> DownstreamPort::LoadAlignedBeatImpl(uint64_t addr) const
{
    std::vector<unsigned char> beat(kAxiDataBytes, 0);
    const uint64_t base = AlignDown(addr, kAxiDataBytes);
    for (std::size_t lane = 0; lane < kAxiDataBytes; ++lane) {
        beat[lane] = LoadByteImpl(base + lane);
    }
    return beat;
}

void DownstreamPort::ApplyWriteBeatImpl(uint64_t addr,
                                        const std::vector<unsigned char> &data,
                                        uint32_t strobe,
                                        const AceLiteAttrs &attrs)
{
    const uint64_t base = AlignDown(addr, kAxiDataBytes);
    const std::size_t lane_count = std::min(data.size(), kAxiDataBytes);
    for (std::size_t lane = 0; lane < lane_count; ++lane) {
        if ((strobe & (uint32_t(1) << lane)) != 0) {
            const unsigned char value = attrs.awatop == kAceLiteAtomicBitSetAwatop ?
                static_cast<unsigned char>(LoadByteImpl(base + lane) | data[lane]) :
                data[lane];
            StoreByteImpl(base + lane, value);
        }
    }
}

void DownstreamPort::QueueReadBurstImpl(const detail::AxiMasterSnapshot &snapshot)
{
    const uint8_t resp = MakeResp(snapshot.arsize, snapshot.arburst);
    uint64_t addr = snapshot.araddr;
    const uint16_t beats = static_cast<uint16_t>(snapshot.arlen) + 1;

    for (uint16_t beat_idx = 0; beat_idx < beats; ++beat_idx) {
        PendingRBeat beat;
        beat.id = snapshot.arid;
        beat.resp = resp;
        beat.last = (beat_idx + 1) == beats;

        if (resp == detail::kAxiRespOkay) {
            bool provided = false;
            if (read_cb_) {
                DownstreamReadEvent event;
                event.addr = addr;
                event.id = snapshot.arid;
                event.size = snapshot.arsize;
                event.burst = snapshot.arburst;
                event.beat_index = beat_idx;
                event.beat_count = beats;
                event.attrs = snapshot.ar_attrs;

                std::array<unsigned char, kAxiDataBytes> callback_data = {};
                provided = read_cb_(event, callback_data);
                if (provided) {
                    beat.data.assign(callback_data.begin(), callback_data.end());
                }
            }

            if (!provided) {
                beat.data = LoadAlignedBeatImpl(addr);
            }
        }

        r_queue_.push_back(beat);
        addr = AdvanceAddrImpl(addr, snapshot.arsize, snapshot.arburst);
    }
}

TranslationPort::TranslationPort(IommuRuntime &sim) : sim_(sim) {}

void TranslationPort::SetCallbacks(TranslationWriteCallback write_cb, TranslationReadCallback read_cb)
{
    sim_.RunOnWorker([this, write_cb = std::move(write_cb), read_cb = std::move(read_cb)]() mutable {
        write_cb_ = std::move(write_cb);
        read_cb_ = std::move(read_cb);
    });
}

void TranslationPort::Store(uint64_t addr, const std::vector<unsigned char> &data)
{
    sim_.RunOnWorker([this, addr, data]() { StoreImpl(addr, data); });
}

std::vector<unsigned char> TranslationPort::Load(uint64_t addr, std::size_t len)
{
    return sim_.RunOnWorker([this, addr, len]() { return LoadImpl(addr, len); });
}

void TranslationPort::ResetStateImpl()
{
    write_ = {};
    b_queue_.clear();
    r_queue_.clear();
}

void TranslationPort::StoreImpl(uint64_t addr, const std::vector<unsigned char> &data)
{
    for (std::size_t i = 0; i < data.size(); ++i) {
        memory_[addr + i] = data[i];
    }
}

std::vector<unsigned char> TranslationPort::LoadImpl(uint64_t addr, std::size_t len) const
{
    std::vector<unsigned char> data(len, 0);
    for (std::size_t i = 0; i < len; ++i) {
        data[i] = LoadByteImpl(addr + i);
    }
    return data;
}

void TranslationPort::DriveIdleImpl(UTiommu_wrap &dut) const
{
    dut.iommu_ds_awready = 0;
    dut.iommu_ds_arready = 0;
    dut.iommu_ds_wready = 0;

    dut.iommu_ds_bid = 0;
    dut.iommu_ds_bvalid = 0;
    dut.iommu_ds_bresp = 0;

    dut.iommu_ds_rid = 0;
    dut.iommu_ds_rvalid = 0;
    dut.iommu_ds_rresp = 0;
    dut.iommu_ds_rlast = 0;

    auto zero_data = std::vector<unsigned char>(kAxiDataBytes, 0);
    dut.iommu_ds_rdata.SetBytes(zero_data);
}

detail::AxiMasterSnapshot TranslationPort::CaptureImpl(UTiommu_wrap &dut) const
{
    detail::AxiMasterSnapshot snapshot;

    snapshot.awvalid = dut.iommu_ds_awvalid.U() != 0;
    snapshot.wvalid = dut.iommu_ds_wvalid.U() != 0;
    snapshot.arvalid = dut.iommu_ds_arvalid.U() != 0;
    snapshot.rready = dut.iommu_ds_rready.U() != 0;
    snapshot.bready = dut.iommu_ds_bready.U() != 0;

    snapshot.awid = static_cast<uint16_t>(dut.iommu_ds_awid.U());
    snapshot.arid = static_cast<uint16_t>(dut.iommu_ds_arid.U());
    snapshot.awaddr = dut.iommu_ds_awaddr.U();
    snapshot.araddr = dut.iommu_ds_araddr.U();
    snapshot.awlen = static_cast<uint8_t>(dut.iommu_ds_awlen.U());
    snapshot.arlen = static_cast<uint8_t>(dut.iommu_ds_arlen.U());
    snapshot.awsize = static_cast<uint8_t>(dut.iommu_ds_awsize.U());
    snapshot.arsize = static_cast<uint8_t>(dut.iommu_ds_arsize.U());
    snapshot.awburst = static_cast<uint8_t>(dut.iommu_ds_awburst.U());
    snapshot.arburst = static_cast<uint8_t>(dut.iommu_ds_arburst.U());
    snapshot.wstrb = static_cast<uint32_t>(dut.iommu_ds_wstrb.U());
    snapshot.wlast = dut.iommu_ds_wlast.U() != 0;
    snapshot.aw_attrs.cache = static_cast<uint8_t>(dut.iommu_ds_awcache.U());
    snapshot.aw_attrs.prot = static_cast<uint8_t>(dut.iommu_ds_awprot.U());
    snapshot.aw_attrs.qos = static_cast<uint8_t>(dut.iommu_ds_awqos.U());
    snapshot.aw_attrs.domain = static_cast<uint8_t>(dut.iommu_ds_awdomain.U());
    snapshot.aw_attrs.awatop = static_cast<uint8_t>(dut.iommu_ds_awatop.U());
    snapshot.ar_attrs.cache = static_cast<uint8_t>(dut.iommu_ds_arcache.U());
    snapshot.ar_attrs.prot = static_cast<uint8_t>(dut.iommu_ds_arprot.U());
    snapshot.ar_attrs.qos = static_cast<uint8_t>(dut.iommu_ds_arqos.U());
    snapshot.ar_attrs.snoop = static_cast<uint8_t>(dut.iommu_ds_arsnoop.U());
    snapshot.ar_attrs.domain = static_cast<uint8_t>(dut.iommu_ds_ardomain.U());
    snapshot.wdata = dut.iommu_ds_wdata.GetBytes();

    return snapshot;
}

detail::AxiSlaveDrive TranslationPort::PlanImpl(const detail::AxiMasterSnapshot &snapshot) const
{
    detail::AxiSlaveDrive drive;

    const bool can_accept_aw = !write_.active && b_queue_.empty();
    drive.awready = can_accept_aw;
    drive.wready = write_.active || (can_accept_aw && snapshot.awvalid);
    drive.arready = r_queue_.empty();

    if (!b_queue_.empty()) {
        drive.bvalid = true;
        drive.bid = b_queue_.front().id;
        drive.bresp = b_queue_.front().resp;
    }

    if (!r_queue_.empty()) {
        drive.rvalid = true;
        drive.rid = r_queue_.front().id;
        drive.rresp = r_queue_.front().resp;
        drive.rlast = r_queue_.front().last;
        drive.rdata = r_queue_.front().data;
    }

    return drive;
}

void TranslationPort::ApplyImpl(UTiommu_wrap &dut, const detail::AxiSlaveDrive &drive) const
{
    dut.iommu_ds_awready = drive.awready ? 1 : 0;
    dut.iommu_ds_arready = drive.arready ? 1 : 0;
    dut.iommu_ds_wready = drive.wready ? 1 : 0;

    dut.iommu_ds_bid = drive.bid;
    dut.iommu_ds_bvalid = drive.bvalid ? 1 : 0;
    dut.iommu_ds_bresp = drive.bresp;

    dut.iommu_ds_rid = drive.rid;
    dut.iommu_ds_rvalid = drive.rvalid ? 1 : 0;
    dut.iommu_ds_rresp = drive.rresp;
    dut.iommu_ds_rlast = drive.rlast ? 1 : 0;

    auto beat = drive.rdata;
    dut.iommu_ds_rdata.SetBytes(beat);
}

void TranslationPort::CommitImpl(const detail::AxiMasterSnapshot &snapshot,
                                 const detail::AxiSlaveDrive &drive)
{
    const bool aw_handshake = snapshot.awvalid && drive.awready;
    const bool w_handshake = snapshot.wvalid && drive.wready;
    const bool ar_handshake = snapshot.arvalid && drive.arready;
    const bool b_handshake = drive.bvalid && snapshot.bready;
    const bool r_handshake = drive.rvalid && snapshot.rready;

    if (aw_handshake) {
        write_.active = true;
        write_.id = snapshot.awid;
        write_.next_addr = snapshot.awaddr;
        write_.beats_left = static_cast<uint16_t>(snapshot.awlen) + 1;
        write_.beats_total = static_cast<uint16_t>(snapshot.awlen) + 1;
        write_.beat_index = 0;
        write_.size = snapshot.awsize;
        write_.burst = snapshot.awburst;
        write_.resp = MakeResp(snapshot.awsize, snapshot.awburst);
        write_.attrs = snapshot.aw_attrs;
    }

    if (w_handshake) {
        if (!write_.active) {
            std::fprintf(stderr, "[TRANSLATION] W handshake arrived before AW state was captured.\n");
        } else {
            if (write_.resp == detail::kAxiRespOkay) {
                ApplyWriteBeatImpl(write_.next_addr, snapshot.wdata, snapshot.wstrb, write_.attrs);
                if (write_cb_) {
                    TranslationWriteEvent event;
                    event.addr = write_.next_addr;
                    event.id = write_.id;
                    event.size = write_.size;
                    event.burst = write_.burst;
                    event.beat_index = write_.beat_index;
                    event.beat_count = write_.beats_total;
                    event.strobe = snapshot.wstrb;
                    event.last = snapshot.wlast;
                    event.data = ToArray(snapshot.wdata);
                    event.attrs = write_.attrs;
                    write_cb_(event);
                }
            }

            if (write_.beats_left > 0) {
                --write_.beats_left;
            }

            const bool burst_done = (write_.beats_left == 0) || snapshot.wlast;
            if (burst_done) {
                b_queue_.push_back({write_.id, write_.resp});
                write_ = {};
            } else {
                ++write_.beat_index;
                write_.next_addr = AdvanceAddrImpl(write_.next_addr, write_.size, write_.burst);
            }
        }
    }

    if (ar_handshake) {
        QueueReadBurstImpl(snapshot);
    }

    if (b_handshake && !b_queue_.empty()) {
        b_queue_.pop_front();
    }

    if (r_handshake && !r_queue_.empty()) {
        r_queue_.pop_front();
    }
}

uint64_t TranslationPort::BeatBytes(uint8_t size)
{
    return size < 63 ? (uint64_t(1) << size) : 0;
}

uint64_t TranslationPort::AlignDown(uint64_t addr, uint64_t align)
{
    return addr & ~(align - 1);
}

bool TranslationPort::SupportedBurst(uint8_t burst)
{
    return burst == detail::kAxiBurstFixed || burst == detail::kAxiBurstIncr;
}

bool TranslationPort::SupportedSize(uint8_t size)
{
    const uint64_t bytes = BeatBytes(size);
    return bytes != 0 && bytes <= kAxiDataBytes;
}

uint8_t TranslationPort::MakeResp(uint8_t size, uint8_t burst)
{
    return SupportedBurst(burst) && SupportedSize(size) ? detail::kAxiRespOkay : detail::kAxiRespDecErr;
}

uint8_t TranslationPort::LoadByteImpl(uint64_t addr) const
{
    const auto it = memory_.find(addr);
    return it == memory_.end() ? 0 : it->second;
}

void TranslationPort::StoreByteImpl(uint64_t addr, unsigned char value)
{
    memory_[addr] = value;
}

uint64_t TranslationPort::AdvanceAddrImpl(uint64_t addr, uint8_t size, uint8_t burst) const
{
    if (burst == detail::kAxiBurstFixed) {
        return addr;
    }
    if (burst == detail::kAxiBurstIncr) {
        return addr + BeatBytes(size);
    }
    return addr;
}

std::vector<unsigned char> TranslationPort::LoadAlignedBeatImpl(uint64_t addr) const
{
    std::vector<unsigned char> beat(kAxiDataBytes, 0);
    const uint64_t base = AlignDown(addr, kAxiDataBytes);
    for (std::size_t lane = 0; lane < kAxiDataBytes; ++lane) {
        beat[lane] = LoadByteImpl(base + lane);
    }
    return beat;
}

void TranslationPort::ApplyWriteBeatImpl(uint64_t addr,
                                         const std::vector<unsigned char> &data,
                                         uint32_t strobe,
                                         const AceLiteAttrs &attrs)
{
    const uint64_t base = AlignDown(addr, kAxiDataBytes);
    const std::size_t lane_count = std::min(data.size(), kAxiDataBytes);
    for (std::size_t lane = 0; lane < lane_count; ++lane) {
        if ((strobe & (uint32_t(1) << lane)) != 0) {
            const unsigned char value = attrs.awatop == kAceLiteAtomicBitSetAwatop ?
                static_cast<unsigned char>(LoadByteImpl(base + lane) | data[lane]) :
                data[lane];
            StoreByteImpl(base + lane, value);
        }
    }
}

void TranslationPort::QueueReadBurstImpl(const detail::AxiMasterSnapshot &snapshot)
{
    const uint8_t resp = MakeResp(snapshot.arsize, snapshot.arburst);
    uint64_t addr = snapshot.araddr;
    const uint16_t beats = static_cast<uint16_t>(snapshot.arlen) + 1;

    for (uint16_t beat_idx = 0; beat_idx < beats; ++beat_idx) {
        PendingRBeat beat;
        beat.id = snapshot.arid;
        beat.resp = resp;
        beat.last = (beat_idx + 1) == beats;

        if (resp == detail::kAxiRespOkay) {
            bool provided = false;
            if (read_cb_) {
                TranslationReadEvent event;
                event.addr = addr;
                event.id = snapshot.arid;
                event.size = snapshot.arsize;
                event.burst = snapshot.arburst;
                event.beat_index = beat_idx;
                event.beat_count = beats;
                event.attrs = snapshot.ar_attrs;

                std::array<unsigned char, kAxiDataBytes> callback_data = {};
                provided = read_cb_(event, callback_data);
                if (provided) {
                    beat.data.assign(callback_data.begin(), callback_data.end());
                }
            }

            if (!provided) {
                beat.data = LoadAlignedBeatImpl(addr);
            }
        }

        r_queue_.push_back(beat);
        addr = AdvanceAddrImpl(addr, snapshot.arsize, snapshot.arburst);
    }
}

bool IommuRuntime::SendAtsPriMessageImpl(const std::vector<uint64_t> &beats,
                                         int timeout_cycles)
{
    UTiommu_wrap &dut = *dut_;

    if (beats.empty() || timeout_cycles <= 0) {
        return false;
    }

    dut.iommu_tr_tready_i = 1;

    for (std::size_t beat_index = 0; beat_index < beats.size(); ++beat_index) {
        int idle_cycles = 0;
        bool handshake = false;

        while (!handshake) {
            const bool last = (beat_index + 1) == beats.size();

            dut.iommu_tr_rvalid_i = 1;
            dut.iommu_tr_rdata_i = beats[beat_index];
            dut.iommu_tr_rstrb_i = 0xff;
            dut.iommu_tr_rkeep_i = 0xff;
            dut.iommu_tr_rlast_i = last ? 1 : 0;
            dut.iommu_tr_rid_i = 0;

            const detail::StepTrace trace = StepOneImpl();
            if (trace.tr_r_handshake) {
                handshake = true;
                continue;
            }

            if (++idle_cycles >= timeout_cycles) {
                std::fprintf(stderr,
                             "[iommu-api] ats-pri-send-timeout beat=%zu/%zu\n",
                             beat_index + 1, beats.size());
                dut.iommu_tr_rvalid_i = 0;
                dut.iommu_tr_rdata_i = 0;
                dut.iommu_tr_rstrb_i = 0;
                dut.iommu_tr_rkeep_i = 0;
                dut.iommu_tr_rlast_i = 0;
                dut.iommu_tr_rid_i = 0;
                return false;
            }
        }
    }

    dut.iommu_tr_rvalid_i = 0;
    dut.iommu_tr_rdata_i = 0;
    dut.iommu_tr_rstrb_i = 0;
    dut.iommu_tr_rkeep_i = 0;
    dut.iommu_tr_rlast_i = 0;
    dut.iommu_tr_rid_i = 0;
    return true;
}

bool IommuRuntime::ReceiveAtsPriMessageImpl(std::vector<uint64_t> &beats,
                                           int timeout_cycles)
{
    UTiommu_wrap &dut = *dut_;

    if (timeout_cycles <= 0) {
        return false;
    }

    beats.clear();
    dut.iommu_tr_tready_i = 1;

    int idle_cycles = 0;
    while (true) {
        const detail::StepTrace trace = StepOneImpl();

        if (trace.tr_t_handshake) {
            beats.push_back(trace.tr_tdata);
            idle_cycles = 0;
            if (trace.tr_tlast) {
                return true;
            }
            continue;
        }

        if (++idle_cycles >= timeout_cycles) {
            std::fprintf(stderr,
                         "[iommu-api] ats-pri-recv-timeout beats=%zu\n",
                         beats.size());
            return false;
        }
    }
}

bool IommuRuntime::SendAtsPriAckImpl(uint8_t msg_type, int timeout_cycles)
{
    return SendAtsPriMessageImpl(MakeAtsPriAck(msg_type), timeout_cycles);
}

bool IommuRuntime::EnsureAtsLinkImpl(int timeout_cycles)
{
    std::vector<uint64_t> response;

    if (ats_link_connected_) {
        return true;
    }

    if (!SendAtsPriMessageImpl(MakeAtsConnectRequest(), timeout_cycles)) {
        return false;
    }

    if (!ReceiveAtsPriMessageImpl(response, timeout_cycles)) {
        return false;
    }

    if (response.empty() || (response[0] & 0xf) != kAtdMsgCondis) {
        std::fprintf(stderr,
                     "[iommu-api] ats-connect-fail beats=%zu type=0x%llx\n",
                     response.size(),
                     response.empty() ? 0ull : (unsigned long long)(response[0] & 0xf));
        return false;
    }

    ats_link_connected_ = true;
    return true;
}

bool IommuRuntime::AtsRequestTranslation(const RequestContext &context,
                                         uint64_t iova,
                                         std::size_t length,
                                         bool no_write,
                                         bool priv_req,
                                         bool exec_req,
                                         uint64_t &translated_addr,
                                         uint64_t &addr_mask,
                                         uint32_t &perm,
                                         uint32_t &err_count)
{
    struct Result {
        bool ok = false;
        uint64_t translated_addr = 0;
        uint64_t addr_mask = 0;
        uint32_t perm = 0;
        uint32_t err_count = 0;
    };

    if (length == 0) {
        return false;
    }

    Result result = RunOnWorker([this, context, iova, length, no_write,
                                 priv_req, exec_req]() {
        Result result;
        std::vector<uint64_t> response;
        const uint8_t request_index =
            static_cast<uint8_t>((iova >> 12) & 0xffu);
        const int timeout_cycles = kDefaultAtsPriTimeoutCycles;

        if (!EnsureAtsLinkImpl(timeout_cycles)) {
            return result;
        }

        /*
         * The current RTL collapses ATS requests into a fixed read-style
         * transaction, so no_write/priv/exec are retained for logging and for
         * future protocol revisions but do not alter the emitted request.
         */
        const std::vector<uint64_t> request =
            MakeAtsTranslationRequest(context, iova, request_index);

        std::fprintf(stderr,
                     "[iommu-api] ats-submit rid=0x%x pasid=0x%x iova=0x%llx "
                     "len=%zu no_write=%u priv=%u exec=%u\n",
                     context.device_id,
                     context.process_id,
                     static_cast<unsigned long long>(iova),
                     length,
                     no_write ? 1 : 0,
                     priv_req ? 1 : 0,
                     exec_req ? 1 : 0);

        if (!SendAtsPriMessageImpl(request, timeout_cycles)) {
            return result;
        }

        for (int retries = 0; retries < 16; ++retries) {
            if (!ReceiveAtsPriMessageImpl(response, timeout_cycles)) {
                return result;
            }

            if (response.empty()) {
                continue;
            }

            const uint8_t msg_type = static_cast<uint8_t>(response[0] & 0xf);
            if (msg_type == kAtdMsgInvReqAck && response.size() == 2) {
                if (!SendAtsPriAckImpl(kAtdMsgInvReqAck, timeout_cycles)) {
                    return result;
                }
                continue;
            }
            if (msg_type == kAtdMsgSyncReqAck && response.size() == 1) {
                if (!SendAtsPriAckImpl(kAtdMsgSyncReqAck, timeout_cycles)) {
                    return result;
                }
                continue;
            }
            if (msg_type == kAtdMsgPageReqAck && response.size() == 1) {
                if (!SendAtsPriAckImpl(kAtdMsgPageRespAck, timeout_cycles)) {
                    return result;
                }
                continue;
            }

            if (msg_type == kAtdMsgTransFault && response.size() == 1) {
                result.perm = kIommuPermNone;
                result.addr_mask = kAtsPriPageOffsetMask;
                result.translated_addr = iova & ~kAtsPriPageOffsetMask;
                result.err_count = 1;
                result.ok = true;

                std::fprintf(stderr,
                             "[iommu-api] ats-fault rid=0x%x pasid=0x%x "
                             "iova=0x%llx fault=0x%llx\n",
                             context.device_id,
                             context.process_id,
                             static_cast<unsigned long long>(iova),
                             static_cast<unsigned long long>(response[0]));
                return result;
            }

            if (msg_type != kAtdMsgTransReqResp) {
                std::fprintf(stderr,
                             "[iommu-api] ats-unexpected-msg type=0x%x beats=%zu\n",
                             msg_type, response.size());
                continue;
            }

            result.err_count = 0;
            if (response.size() == 1) {
                result.perm = kIommuPermNone;
                result.addr_mask = kAtsPriPageOffsetMask;
                result.translated_addr = 0;
                result.err_count = 1;
                result.ok = true;
                return result;
            }

            if (response.size() < 3) {
                std::fprintf(stderr,
                             "[iommu-api] ats-short-response beats=%zu\n",
                             response.size());
                return result;
            }

            const uint64_t beat0 = response[0];
            const uint64_t beat1 = response[1];
            const uint64_t beat2 = response[2];
            const bool untranslated = (beat0 >> 12) & 0x1;
            const bool bypass = (beat0 >> 17) & 0x1;
            const uint32_t allow_r = static_cast<uint32_t>(beat1 & 0x1);
            const uint32_t allow_w = static_cast<uint32_t>((beat1 >> 1) & 0x1);
            const uint32_t allow_x = static_cast<uint32_t>((beat1 >> 2) & 0x1);
            const uint64_t translated_page =
                ((beat2 & LowBitsMask(42)) << 10) |
                ((beat1 >> 54) & LowBitsMask(10));

            result.translated_addr = translated_page << 12;
            result.addr_mask = kAtsPriPageOffsetMask;
            result.perm = 0;
            if (allow_r) {
                result.perm |= kIommuPermRead;
            }
            if (allow_w) {
                result.perm |= kIommuPermWrite;
            }
            if (allow_x) {
                result.perm |= kIommuPermExec;
            }
            if (untranslated) {
                result.perm |= kIommuPermUntranslatedOnly;
            }
            if (priv_req) {
                result.perm |= kIommuPermPriv;
            }
            bool access_ok = no_write ?
                ((result.perm & kIommuPermRead) != 0) :
                ((result.perm & kIommuPermWrite) != 0);
            if (exec_req) {
                access_ok = access_ok && ((result.perm & kIommuPermExec) != 0);
            }
            result.err_count = access_ok ? 0 : 1;
            if (bypass) {
                result.translated_addr = iova & ~kAtsPriPageOffsetMask;
            }
            result.ok = true;

            std::fprintf(stderr,
                         "[iommu-api] ats-result rid=0x%x pasid=0x%x iova=0x%llx "
                         "translated=0x%llx mask=0x%llx perm=0x%x err=%u\n",
                         context.device_id,
                         context.process_id,
                         static_cast<unsigned long long>(iova),
                         static_cast<unsigned long long>(result.translated_addr),
                         static_cast<unsigned long long>(result.addr_mask),
                         result.perm,
                         result.err_count);
            return result;
        }

        return result;
    });

    if (!result.ok) {
        return false;
    }

    translated_addr = result.translated_addr;
    addr_mask = result.addr_mask;
    perm = result.perm;
    err_count = result.err_count;
    return true;
}

bool IommuRuntime::PriRequestPage(const RequestContext &context,
                                  uint64_t iova,
                                  uint16_t prgi,
                                  bool lpig,
                                  bool is_read,
                                  bool is_write,
                                  bool priv_req,
                                  bool exec_req,
                                  uint32_t &response_code)
{
    struct Result {
        bool ok = false;
        uint32_t response_code = 0;
    };

    Result result = RunOnWorker([this, context, iova, prgi, lpig, is_read,
                                 is_write, priv_req, exec_req]() {
        Result result;
        std::vector<uint64_t> response;
        const int timeout_cycles = kDefaultAtsPriTimeoutCycles;

        if (!EnsureAtsLinkImpl(timeout_cycles)) {
            return result;
        }

        const std::vector<uint64_t> request =
            MakePriPageRequest(context, iova, prgi, lpig, is_read, is_write,
                               priv_req, exec_req);

        std::fprintf(stderr,
                     "[iommu-api] pri-submit rid=0x%x pasid=0x%x iova=0x%llx "
                     "prgi=0x%x lpig=%u read=%u write=%u priv=%u exec=%u\n",
                     context.device_id,
                     context.process_id,
                     static_cast<unsigned long long>(iova),
                     prgi,
                     lpig ? 1 : 0,
                     is_read ? 1 : 0,
                     is_write ? 1 : 0,
                     priv_req ? 1 : 0,
                     exec_req ? 1 : 0);

        if (!SendAtsPriMessageImpl(request, timeout_cycles)) {
            return result;
        }

        for (int retries = 0; retries < 16; ++retries) {
            if (!ReceiveAtsPriMessageImpl(response, timeout_cycles)) {
                return result;
            }

            if (response.empty()) {
                continue;
            }

            const uint8_t msg_type = static_cast<uint8_t>(response[0] & 0xf);
            if (msg_type == kAtdMsgInvReqAck && response.size() == 2) {
                if (!SendAtsPriAckImpl(kAtdMsgInvReqAck, timeout_cycles)) {
                    return result;
                }
                continue;
            }
            if (msg_type == kAtdMsgSyncReqAck && response.size() == 1) {
                if (!SendAtsPriAckImpl(kAtdMsgSyncReqAck, timeout_cycles)) {
                    return result;
                }
                continue;
            }
            if (msg_type == kAtdMsgPageReqAck && response.size() == 1) {
                if (!SendAtsPriAckImpl(kAtdMsgPageRespAck, timeout_cycles)) {
                    return result;
                }
                continue;
            }

            if (msg_type != kAtdMsgPageResp) {
                std::fprintf(stderr,
                             "[iommu-api] pri-unexpected-msg type=0x%x beats=%zu\n",
                             msg_type, response.size());
                continue;
            }

            if (response.size() < 2) {
                std::fprintf(stderr,
                             "[iommu-api] pri-short-response beats=%zu\n",
                             response.size());
                return result;
            }

            const uint64_t beat1 = response[1];
            const uint32_t rtl_response_code =
                static_cast<uint32_t>((beat1 >> 12) & 0x3u);
            const uint32_t mapped_response_code =
                MapRtlPriResponse(rtl_response_code);
            const uint16_t resp_prgi = static_cast<uint16_t>(beat1 & 0x1ffu);

            result.response_code = mapped_response_code;
            result.ok = true;

            std::fprintf(stderr,
                         "[iommu-api] pri-result rid=0x%x pasid=0x%x iova=0x%llx "
                         "prgi=0x%x rtl_resp=%u resp=%u\n",
                         context.device_id,
                         context.process_id,
                         static_cast<unsigned long long>(iova),
                         resp_prgi,
                         rtl_response_code,
                         mapped_response_code);
            return result;
        }

        return result;
    });

    if (!result.ok) {
        return false;
    }

    response_code = result.response_code;
    return true;
}

IommuRuntime::IommuRuntime() : IommuRuntime(BuildDefaultStartupArgs()) {}

IommuRuntime::IommuRuntime(std::vector<std::string> startup_args)
    : downstream_(*this),
      translation_(*this),
      mmio_(*this),
      memory_(*this),
      startup_args_(startup_args.empty() ? BuildDefaultStartupArgs() : std::move(startup_args))
{
    worker_ = std::thread(&IommuRuntime::WorkerLoop, this);
    RunOnWorker([]() {});
}

IommuRuntime::~IommuRuntime()
{
    StopWorker();
}

bool IommuRuntime::Step(int cycles)
{
    if (cycles < 0) {
        return false;
    }

    RunOnWorker([this, cycles]() {
        if (cycles > 0) {
            StepImpl(cycles);
        }
    });
    return true;
}

void IommuRuntime::SetTrace(bool enabled)
{
    RunOnWorker([this, enabled]() {
        trace_enabled_ = enabled;
    });
}

void IommuRuntime::SetTraceTxnId(bool enabled, uint16_t id)
{
    RunOnWorker([this, enabled, id]() {
        trace_txn_id_enabled_ = enabled;
        trace_txn_id_ = enabled ? id : 0;
    });
}

bool IommuRuntime::ShouldLogSelectedSlaveTxnBeat(uint16_t id,
                                                 std::size_t beat_index,
                                                 std::size_t beat_count) const
{
    if (TraceTxnMatches(id)) {
        return true;
    }
    if (!trace_enabled_) {
        return false;
    }
    return ShouldLogBurstBeatDefault(beat_index, beat_count);
}

void IommuRuntime::WorkerLoop()
{
    worker_id_ = std::this_thread::get_id();

    std::vector<std::string> startup_args = startup_args_;
    if (startup_args.empty()) {
        startup_args = BuildDefaultStartupArgs();
    }

    std::vector<char *> argv;
    argv.reserve(startup_args.size() + 1);
    for (std::string &arg : startup_args) {
        argv.push_back(arg.empty() ? const_cast<char *>("") : arg.data());
    }
    argv.push_back(nullptr);

    dut_ = std::make_unique<UTiommu_wrap>(static_cast<int>(startup_args.size()), argv.data());
    InitializeDutImpl();
    ResetDutImpl();

    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stop_requested_ || !tasks_.empty(); });

            if (stop_requested_ && tasks_.empty()) {
                break;
            }

            task = std::move(tasks_.front());
            tasks_.pop_front();
        }

        task();
    }

}

void IommuRuntime::StopWorker()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = true;
    }
    cv_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }

    if (dut_) {
        // Finish the VCS/Verdi runtime from the caller thread after the
        // worker has stopped driving the model. This avoids doing the final
        // FSDB shutdown from the worker thread immediately before process exit.
        dut_->Finish();
        dut_.reset();
    }
}

void IommuRuntime::InitializeDutImpl()
{
    UTiommu_wrap &dut = *dut_;
    dut.InitClock(dut.iommu_clk);

    // The runtime captures AXI/APB handshakes after RefreshComb() and before
    // dut.Step(1). Use immediate writes for externally-driven DUT inputs so
    // the captured snapshots reflect the values visible to the DUT in the
    // current cycle instead of the next scheduled edge.
    for (auto *pin : dut.xport.port_vec) {
        if (pin != nullptr && pin->IsInIO() && pin != &dut.iommu_clk) {
            pin->AsImmWrite();
        }
    }

    InitIdleInputsImpl();
    downstream_.ResetStateImpl();
    translation_.ResetStateImpl();
    downstream_.DriveIdleImpl(dut);
    translation_.DriveIdleImpl(dut);
    dut.RefreshComb();
}

void IommuRuntime::InitIdleInputsImpl()
{
    UTiommu_wrap &dut = *dut_;

    for (auto *pin : dut.xport.port_vec) {
        if (pin != nullptr && pin->IsInIO()) {
            pin->Set(uint64_t(0));
        }
    }

    dut.iommu_clk = 0;
    dut.iommu_psel_i = 0;
    dut.iommu_penable_i = 0;
    dut.iommu_pwrite_i = 0;
    dut.iommu_paddr_i = 0;
    dut.iommu_pwdata_i = 0;

    dut.iommu_slv_rready = 0;
    dut.iommu_slv_bready = 0;

    dut.iommu_ds_crready = 1;

    dut.iommu_tr_tready_i = 1;
    downstream_.DriveIdleImpl(dut);
    translation_.DriveIdleImpl(dut);
}

void IommuRuntime::ResetDutImpl(int cycles)
{
    UTiommu_wrap &dut = *dut_;
    InitIdleInputsImpl();
    downstream_.ResetStateImpl();
    translation_.ResetStateImpl();
    ats_link_connected_ = false;

    dut.iommu_rstn = 0;
    StepImpl(cycles);

    dut.iommu_rstn = 1;
    StepImpl(cycles);
}

void IommuRuntime::StepImpl(int cycles)
{
    for (int i = 0; i < cycles; ++i) {
        StepOneImpl();
    }
}

detail::StepTrace IommuRuntime::StepOneImpl()
{
    detail::StepTrace trace;
    UTiommu_wrap &dut = *dut_;

    if (dut.iommu_rstn.U() == 0) {
        downstream_.DriveIdleImpl(dut);
        translation_.DriveIdleImpl(dut);
        dut.Step(1);
        downstream_.ResetStateImpl();
        translation_.ResetStateImpl();
        return trace;
    }

    dut.RefreshComb();
    const detail::AxiMasterSnapshot preview = downstream_.CaptureImpl(dut);
    const detail::AxiSlaveDrive drive = downstream_.PlanImpl(preview);
    const detail::AxiMasterSnapshot translation_preview = translation_.CaptureImpl(dut);
    const detail::AxiSlaveDrive translation_drive = translation_.PlanImpl(translation_preview);

    downstream_.ApplyImpl(dut, drive);
    translation_.ApplyImpl(dut, translation_drive);
    dut.RefreshComb();

    const detail::AxiMasterSnapshot snapshot = downstream_.CaptureImpl(dut);
    const detail::AxiMasterSnapshot translation_snapshot = translation_.CaptureImpl(dut);
    const detail::AxiSlaveSnapshot slv_snapshot = CaptureSlavePortImpl();

    trace.mst = snapshot;
    trace.slv = slv_snapshot;
    trace.mst_drive = drive;
    trace.mst_aw_handshake = snapshot.awvalid && drive.awready;
    trace.mst_w_handshake = snapshot.wvalid && drive.wready;
    trace.mst_ar_handshake = snapshot.arvalid && drive.arready;
    trace.mst_b_handshake = drive.bvalid && snapshot.bready;
    trace.mst_r_handshake = drive.rvalid && snapshot.rready;
    trace.slv_aw_handshake = slv_snapshot.awvalid && slv_snapshot.awready;
    trace.slv_w_handshake = slv_snapshot.wvalid && slv_snapshot.wready;
    trace.slv_ar_handshake = slv_snapshot.arvalid && slv_snapshot.arready;
    trace.slv_b_handshake = slv_snapshot.bvalid && slv_snapshot.bready;
    trace.slv_r_handshake = slv_snapshot.rvalid && slv_snapshot.rready;
    trace.tr_r_handshake = dut.iommu_tr_rvalid_i.U() != 0 &&
                           dut.iommu_tr_rready_o.U() != 0;
    trace.tr_t_handshake = dut.iommu_tr_tvalid_o.U() != 0 &&
                           dut.iommu_tr_tready_i.U() != 0;
    trace.tr_tdata = dut.iommu_tr_tdata_o.U();
    trace.tr_tlast = dut.iommu_tr_tlast_o.U() != 0;
    trace.tr_tid = static_cast<uint8_t>(dut.iommu_tr_tid_o.U());

    dut.Step(1);
    downstream_.CommitImpl(snapshot, drive);
    translation_.CommitImpl(translation_snapshot, translation_drive);
    return trace;
}

void IommuRuntime::DriveApbSetupImpl(bool is_write, uint32_t addr, uint32_t data)
{
    UTiommu_wrap &dut = *dut_;
    dut.iommu_psel_i = 1;
    dut.iommu_penable_i = 0;
    dut.iommu_pwrite_i = is_write ? 1 : 0;
    dut.iommu_paddr_i = addr;
    dut.iommu_pwdata_i = data;
}

void IommuRuntime::ReleaseApbImpl()
{
    UTiommu_wrap &dut = *dut_;
    dut.iommu_psel_i = 0;
    dut.iommu_penable_i = 0;
    dut.iommu_pwrite_i = 0;
    dut.iommu_paddr_i = 0;
    dut.iommu_pwdata_i = 0;
}

bool IommuRuntime::WaitApbReadyImpl(int timeout_cycles)
{
    UTiommu_wrap &dut = *dut_;
    for (int i = 0; i < timeout_cycles; ++i) {
        StepImpl(1);
        dut.RefreshComb();
        if (dut.iommu_pready_o.U() != 0) {
            return true;
        }
    }
    return false;
}

bool IommuRuntime::MmioWriteImpl(uint32_t addr, uint32_t data, int timeout_cycles)
{
    UTiommu_wrap &dut = *dut_;
    DriveApbSetupImpl(true, addr, data);
    StepImpl(1);

    dut.iommu_penable_i = 1;
    const bool ok = WaitApbReadyImpl(timeout_cycles);
    const bool slverr = dut.iommu_pslverr_o.U() != 0;

    ReleaseApbImpl();
    StepImpl(1);

    if (!ok) {
        std::fprintf(stderr, "APB write timeout: addr=0x%08x data=0x%08x\n", addr, data);
        return false;
    }
    if (slverr) {
        std::fprintf(stderr, "APB write PSLVERR: addr=0x%08x data=0x%08x\n", addr, data);
        return false;
    }
    return true;
}

bool IommuRuntime::MmioReadImpl(uint32_t addr, uint32_t &data, int timeout_cycles)
{
    UTiommu_wrap &dut = *dut_;
    DriveApbSetupImpl(false, addr, 0);
    StepImpl(1);

    dut.iommu_penable_i = 1;
    const bool ok = WaitApbReadyImpl(timeout_cycles);
    if (ok) {
        data = static_cast<uint32_t>(dut.iommu_prdata_o.U());
    }
    const bool slverr = dut.iommu_pslverr_o.U() != 0;

    ReleaseApbImpl();
    StepImpl(1);

    if (!ok) {
        std::fprintf(stderr, "APB read timeout: addr=0x%08x\n", addr);
        return false;
    }
    if (slverr) {
        std::fprintf(stderr, "APB read PSLVERR: addr=0x%08x\n", addr);
        return false;
    }
    return true;
}

detail::AxiSlaveSnapshot IommuRuntime::CaptureSlavePortImpl()
{
    detail::AxiSlaveSnapshot snapshot;
    UTiommu_wrap &dut = *dut_;

    snapshot.awvalid = dut.iommu_slv_awvalid.U() != 0;
    snapshot.awready = dut.iommu_slv_awready.U() != 0;
    snapshot.wvalid = dut.iommu_slv_wvalid.U() != 0;
    snapshot.wready = dut.iommu_slv_wready.U() != 0;
    snapshot.wlast = dut.iommu_slv_wlast.U() != 0;
    snapshot.arvalid = dut.iommu_slv_arvalid.U() != 0;
    snapshot.arready = dut.iommu_slv_arready.U() != 0;
    snapshot.bvalid = dut.iommu_slv_bvalid.U() != 0;
    snapshot.bready = dut.iommu_slv_bready.U() != 0;
    snapshot.rvalid = dut.iommu_slv_rvalid.U() != 0;
    snapshot.rready = dut.iommu_slv_rready.U() != 0;
    snapshot.rlast = dut.iommu_slv_rlast.U() != 0;

    snapshot.awid = static_cast<uint16_t>(dut.iommu_slv_awid.U());
    snapshot.arid = static_cast<uint16_t>(dut.iommu_slv_arid.U());
    snapshot.bid = static_cast<uint16_t>(dut.iommu_slv_bid.U());
    snapshot.rid = static_cast<uint16_t>(dut.iommu_slv_rid.U());
    snapshot.awaddr = dut.iommu_slv_awaddr.U();
    snapshot.araddr = dut.iommu_slv_araddr.U();
    snapshot.awlen = static_cast<uint8_t>(dut.iommu_slv_awlen.U());
    snapshot.arlen = static_cast<uint8_t>(dut.iommu_slv_arlen.U());
    snapshot.awsize = static_cast<uint8_t>(dut.iommu_slv_awsize.U());
    snapshot.arsize = static_cast<uint8_t>(dut.iommu_slv_arsize.U());
    snapshot.awburst = static_cast<uint8_t>(dut.iommu_slv_awburst.U());
    snapshot.arburst = static_cast<uint8_t>(dut.iommu_slv_arburst.U());
    snapshot.wstrb = static_cast<uint32_t>(dut.iommu_slv_wstrb.U());
    snapshot.bresp = static_cast<uint8_t>(dut.iommu_slv_bresp.U());
    snapshot.rresp = static_cast<uint8_t>(dut.iommu_slv_rresp.U());
    snapshot.aw_attrs.cache = static_cast<uint8_t>(dut.iommu_slv_awcache.U());
    snapshot.aw_attrs.prot = static_cast<uint8_t>(dut.iommu_slv_awprot.U());
    snapshot.aw_attrs.region = static_cast<uint8_t>(dut.iommu_slv_awregion.U());
    snapshot.aw_attrs.qos = static_cast<uint8_t>(dut.iommu_slv_awqos.U());
    snapshot.aw_attrs.snoop = static_cast<uint8_t>(dut.iommu_slv_awsnoop.U());
    snapshot.aw_attrs.domain = static_cast<uint8_t>(dut.iommu_slv_awdomain.U());
    snapshot.aw_attrs.idunq = static_cast<uint8_t>(dut.iommu_slv_awidunq.U());
    snapshot.aw_attrs.loop = static_cast<uint8_t>(dut.iommu_slv_awloop.U());
    snapshot.aw_attrs.awatop = static_cast<uint8_t>(dut.iommu_slv_awatop.U());
    snapshot.ar_attrs.cache = static_cast<uint8_t>(dut.iommu_slv_arcache.U());
    snapshot.ar_attrs.prot = static_cast<uint8_t>(dut.iommu_slv_arprot.U());
    snapshot.ar_attrs.region = static_cast<uint8_t>(dut.iommu_slv_arregion.U());
    snapshot.ar_attrs.qos = static_cast<uint8_t>(dut.iommu_slv_arqos.U());
    snapshot.ar_attrs.snoop = static_cast<uint8_t>(dut.iommu_slv_arsnoop.U());
    snapshot.ar_attrs.domain = static_cast<uint8_t>(dut.iommu_slv_ardomain.U());
    snapshot.ar_attrs.idunq = static_cast<uint8_t>(dut.iommu_slv_aridunq.U());
    snapshot.ar_attrs.loop = static_cast<uint8_t>(dut.iommu_slv_arloop.U());
    snapshot.wdata = dut.iommu_slv_wdata.GetBytes();
    snapshot.rdata = dut.iommu_slv_rdata.GetBytes();

    return snapshot;
}

void IommuRuntime::DriveSlaveWriteAddressImpl(bool valid,
                                              uint16_t id,
                                              uint64_t addr,
                                              uint8_t len,
                                              uint8_t size,
                                              uint8_t burst,
                                              const RequestContext &context,
                                              const AceLiteAttrs &attrs)
{
    UTiommu_wrap &dut = *dut_;
    dut.iommu_slv_awid = id;
    dut.iommu_slv_awaddr = addr;
    dut.iommu_slv_awlen = len;
    dut.iommu_slv_awsize = size;
    dut.iommu_slv_awburst = burst;
    dut.iommu_slv_awlock = 0;
    dut.iommu_slv_awcache = attrs.cache;
    dut.iommu_slv_awprot = attrs.prot;
    dut.iommu_slv_awregion = attrs.region;
    dut.iommu_slv_awuser = PackSlaveUserImpl(context);
    dut.iommu_slv_awqos = attrs.qos;
    dut.iommu_slv_awsnoop = attrs.snoop;
    dut.iommu_slv_awdomain = attrs.domain;
    dut.iommu_slv_awidunq = attrs.idunq;
    dut.iommu_slv_awatop = attrs.awatop;
    dut.iommu_slv_awloop = attrs.loop;
    dut.iommu_slv_awvalid = valid ? 1 : 0;
}

void IommuRuntime::ReleaseSlaveWriteAddressImpl()
{
    DriveSlaveWriteAddressImpl(false, 0, 0, 0, 0, 0, RequestContext {}, AceLiteAttrs {});
}

void IommuRuntime::DriveSlaveWriteDataImpl(bool valid,
                                           const std::vector<unsigned char> &data,
                                           uint32_t strobe,
                                           bool last)
{
    UTiommu_wrap &dut = *dut_;
    auto beat = data;
    dut.iommu_slv_wdata.SetBytes(beat);
    dut.iommu_slv_wstrb = strobe;
    dut.iommu_slv_wlast = last ? 1 : 0;
    dut.iommu_slv_wuser = 0;
    dut.iommu_slv_wvalid = valid ? 1 : 0;
}

void IommuRuntime::ReleaseSlaveWriteDataImpl()
{
    UTiommu_wrap &dut = *dut_;
    auto zero_data = std::vector<unsigned char>(kAxiDataBytes, 0);
    dut.iommu_slv_wdata.SetBytes(zero_data);
    dut.iommu_slv_wstrb = 0;
    dut.iommu_slv_wlast = 0;
    dut.iommu_slv_wuser = 0;
    dut.iommu_slv_wvalid = 0;
}

void IommuRuntime::DriveSlaveReadAddressImpl(bool valid,
                                             uint16_t id,
                                             uint64_t addr,
                                             uint8_t len,
                                             uint8_t size,
                                             uint8_t burst,
                                             const RequestContext &context,
                                             const AceLiteAttrs &attrs)
{
    UTiommu_wrap &dut = *dut_;
    dut.iommu_slv_arid = id;
    dut.iommu_slv_araddr = addr;
    dut.iommu_slv_arlen = len;
    dut.iommu_slv_arsize = size;
    dut.iommu_slv_arburst = burst;
    dut.iommu_slv_arlock = 0;
    dut.iommu_slv_arcache = attrs.cache;
    dut.iommu_slv_arprot = attrs.prot;
    dut.iommu_slv_arregion = attrs.region;
    dut.iommu_slv_aruser = PackSlaveUserImpl(context);
    dut.iommu_slv_arqos = attrs.qos;
    dut.iommu_slv_arsnoop = attrs.snoop;
    dut.iommu_slv_ardomain = attrs.domain;
    dut.iommu_slv_aridunq = attrs.idunq;
    dut.iommu_slv_arloop = attrs.loop;
    dut.iommu_slv_arvalid = valid ? 1 : 0;
}

void IommuRuntime::ReleaseSlaveReadAddressImpl()
{
    DriveSlaveReadAddressImpl(false, 0, 0, 0, 0, 0, RequestContext {}, AceLiteAttrs {});
}

detail::SlaveWriteBeatResult IommuRuntime::WriteSlaveSingleBeatImpl(uint64_t addr,
                                                                    const std::vector<unsigned char> &data,
                                                                    uint32_t strobe,
                                                                    uint8_t size,
                                                                    uint16_t id,
                                                                    const RequestContext &context,
                                                                    const AceLiteAttrs &attrs,
                                                                    int timeout_cycles)
{
    UTiommu_wrap &dut = *dut_;
    detail::SlaveWriteBeatResult result;

    if (data.size() != kAxiDataBytes) {
        std::fprintf(stderr,
                     "AXI slave write requires exactly %zu bytes, got %zu.\n",
                     kAxiDataBytes, data.size());
        return result;
    }

    bool aw_pending = true;
    bool w_pending = true;

    DriveSlaveWriteAddressImpl(
        true, id, addr, /*len=*/0, size, detail::kAxiBurstIncr, context, attrs);
    DriveSlaveWriteDataImpl(true, data, strobe, /*last=*/true);
    dut.iommu_slv_bready = 1;

    for (int cycle = 0; cycle < timeout_cycles; ++cycle) {
        const detail::StepTrace trace = StepOneImpl();

        if (aw_pending && trace.slv_aw_handshake) {
            aw_pending = false;
            result.aw_handshake = true;
            result.aw_snapshot = trace.mst;
            ReleaseSlaveWriteAddressImpl();
        }

        if (w_pending && trace.slv_w_handshake) {
            w_pending = false;
            result.w_handshake = true;
            result.w_snapshot = trace.mst;
            result.wdata = trace.slv.wdata;
            result.wstrb = trace.slv.wstrb;
            result.wlast = trace.slv.wlast;
            ReleaseSlaveWriteDataImpl();
        }

        if (trace.slv_b_handshake) {
            result.b_handshake = true;
            result.bid = trace.slv.bid;
            result.bresp = trace.slv.bresp;
            break;
        }
    }

    ReleaseSlaveWriteAddressImpl();
    ReleaseSlaveWriteDataImpl();
    dut.iommu_slv_bready = 0;

    return result;
}

detail::SlaveReadBeatResult IommuRuntime::ReadSlaveSingleBeatImpl(uint64_t addr,
                                                                  uint8_t size,
                                                                  uint16_t id,
                                                                  const RequestContext &context,
                                                                  const AceLiteAttrs &attrs,
                                                                  int timeout_cycles)
{
    UTiommu_wrap &dut = *dut_;
    detail::SlaveReadBeatResult result;

    DriveSlaveReadAddressImpl(
        true, id, addr, /*len=*/0, size, detail::kAxiBurstIncr, context, attrs);
    dut.iommu_slv_rready = 1;

    for (int cycle = 0; cycle < timeout_cycles; ++cycle) {
        const detail::StepTrace trace = StepOneImpl();

        if (!result.ar_handshake && trace.slv_ar_handshake) {
            result.ar_handshake = true;
            result.ar_snapshot = trace.mst;
            ReleaseSlaveReadAddressImpl();
        }

        if (trace.slv_r_handshake) {
            result.r_handshake = true;
            result.rid = trace.slv.rid;
            result.rresp = trace.slv.rresp;
            result.rlast = trace.slv.rlast;
            result.rdata = trace.slv.rdata;
            break;
        }
    }

    ReleaseSlaveReadAddressImpl();
    dut.iommu_slv_rready = 0;

    return result;
}

uint64_t IommuRuntime::PackSlaveUserImpl(const RequestContext &context)
{
    uint64_t user = 0;
    user |= (uint64_t(context.device_id & 0x00ffffffu) << 8);
    user |= (uint64_t(context.process_id & 0x000fffffu) << 32);
    user |= (uint64_t(context.process_id_valid ? 1u : 0u) << 52);
    user |= (uint64_t(context.is_translated ? 1u : 0u) << 53);
    return user;
}

} // namespace iommu
