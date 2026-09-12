// SPDX-License-Identifier: Apache-2.0
#include "io_axi_master_engine.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using Engine = io_rtl_system::AxiMasterEngine;

#define CHECK(x) do { if (!(x)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #x); } while (0)

static constexpr uint64_t kBase = 0x80000000;
static constexpr std::size_t kMemorySize = 128 * 1024;

struct ReadExpected {
    uint64_t data;
    uint8_t resp;
    bool last;
};

struct Pending {
    IoAxiTransaction txn;
    std::array<uint8_t, 8> data = {}, strobe = {};
};

class Harness {
public:
    Engine engine;
    std::vector<uint8_t> memory, reference;
    std::mt19937 random;
    std::vector<Pending> pending;
    std::map<uint16_t, std::deque<ReadExpected>> reads;
    std::map<uint16_t, std::deque<uint8_t>> writes;
    std::size_t cycles = 0, completed_reads = 0, completed_writes = 0;
    std::size_t memory_requests = 0, limit;
    uint64_t fault_address = UINT64_MAX;
    bool service = true;

    Harness(unsigned seed, std::size_t depth)
        : engine(depth), memory(kMemorySize), reference(kMemorySize),
          random(seed), limit(depth)
    {
        for (auto &byte : memory) byte = uint8_t(random());
        reference = memory;
    }

    bool Chance() { return (random() & 3) != 0; }

    void Complete(std::size_t index)
    {
        Pending p = pending[index];
        pending.erase(pending.begin() + index);
        auto &t = p.txn;
        CHECK(t.beat_count == 1 && t.beat_size >= 1 && t.beat_size <= 8);
        const bool valid = t.address >= kBase &&
            t.address - kBase <= memory.size() - t.beat_size &&
            !(fault_address >= t.address && fault_address < t.address + t.beat_size);
        t.response = valid ? IO_AXI_RESPONSE_OKAY : IO_AXI_RESPONSE_SLVERR;
        if (valid) {
            const auto offset = t.address - kBase;
            if (t.direction == IO_AXI_DIRECTION_IO2Q_READ) {
                std::copy_n(memory.data() + offset, t.beat_size, p.data.data());
            } else {
                for (unsigned i = 0; i < t.beat_size; ++i) {
                    if (p.strobe[i]) memory[offset + i] = p.data[i];
                }
            }
        }
        t.data = p.data.data();
        t.byte_strobe = p.strobe.data();
        CHECK(engine.Complete(&t) == IO_RTL_SYSTEM_OK);
        CHECK(engine.Complete(&t) == IO_RTL_SYSTEM_ERR_INVALID);
    }

    Engine::Outputs Tick(Engine::Inputs in = {})
    {
        if (++cycles > 4000000) throw std::runtime_error("cycle timeout");
        auto out = engine.Drive(in);
        if (stalled_r_) {
            CHECK(out.rvalid && out.rid == previous_.rid &&
                  out.rdata == previous_.rdata && out.rresp == previous_.rresp &&
                  out.rlast == previous_.rlast);
        }
        if (stalled_b_) {
            CHECK(out.bvalid && out.bid == previous_.bid &&
                  out.bresp == previous_.bresp);
        }
        if (out.rvalid && in.rready) {
            CHECK(!reads[out.rid].empty());
            auto expected = reads[out.rid].front();
            reads[out.rid].pop_front();
            CHECK(out.rresp == expected.resp && out.rlast == expected.last);
            CHECK(out.rdata == expected.data);
            completed_reads += out.rlast;
        }
        if (out.bvalid && in.bready) {
            CHECK(!writes[out.bid].empty());
            CHECK(out.bresp == writes[out.bid].front());
            writes[out.bid].pop_front();
            // No write memory operation may remain when the burst's B arrives.
            for (const auto &p : pending) {
                CHECK(p.txn.direction != IO_AXI_DIRECTION_IO2Q_WRITE ||
                      p.txn.transaction_id != out.bid);
            }
            ++completed_writes;
        }
        stalled_r_ = out.rvalid && !in.rready;
        stalled_b_ = out.bvalid && !in.bready;
        previous_ = out;
        engine.Commit(in, out);
        CHECK(engine.Outstanding() <= limit);
        CHECK(engine.BufferedWriteBeats() <= (limit + 1) * 256);
        if (service) {
            while (pending.size() < limit) {
                Pending p = {};
                int rc = engine.Next(&p.txn);
                if (rc == IO_RTL_SYSTEM_ERR_NO_TRANSACTION) break;
                CHECK(rc == IO_RTL_SYSTEM_OK);
                CHECK(p.txn.beat_count == 1 && p.txn.beat_size <= 8);
                CHECK(p.txn.translated && p.txn.burst_type == IO_AXI_BURST_INCR);
                for (const auto &older : pending) {
                    CHECK(older.txn.direction != p.txn.direction ||
                          older.txn.transaction_id != p.txn.transaction_id);
                }
                if (p.txn.direction == IO_AXI_DIRECTION_IO2Q_WRITE) {
                    std::copy_n(p.txn.data, p.txn.beat_size, p.data.data());
                    std::copy_n(p.txn.byte_strobe, p.txn.beat_size, p.strobe.data());
                }
                pending.push_back(p);
                ++memory_requests;
            }
            if (!pending.empty() && Chance()) Complete(random() % pending.size());
        }
        return out;
    }

    void Idle(unsigned count)
    {
        while (count--) {
            Engine::Inputs in;
            in.rready = Chance(); in.bready = Chance();
            Tick(in);
        }
    }

    void Drain()
    {
        while (engine.NeedsService() || !pending.empty()) Idle(1);
        for (const auto &r : reads) CHECK(r.second.empty());
        for (const auto &w : writes) CHECK(w.second.empty());
        CHECK(memory == reference);
    }

    static bool Valid(const Engine::Address &a)
    {
        if (a.size > 3 || a.burst != 1) return false;
        const uint64_t size = uint64_t(1) << a.size;
        const uint64_t first = a.addr - a.addr % size;
        const uint64_t end = first + size * (unsigned(a.len) + 1) - 1;
        return end >= first && (a.addr / 4096 == end / 4096);
    }

    static std::pair<uint64_t, unsigned> Span(const Engine::Address &a, unsigned beat)
    {
        const unsigned bytes = 1u << a.size;
        const uint64_t aligned = a.addr - a.addr % bytes;
        const uint64_t start = beat ? aligned + beat * bytes : a.addr;
        return {start, unsigned(aligned + (beat + 1) * bytes - start)};
    }

    void ExpectRead(const Engine::Address &a)
    {
        for (unsigned b = 0; b <= a.len; ++b) {
            ReadExpected e = {0, IO_AXI_RESPONSE_SLVERR, b == a.len};
            if (Valid(a)) {
                const auto [addr, size] = Span(a, b);
                if (addr >= kBase && addr - kBase <= reference.size() - size &&
                    !(fault_address >= addr && fault_address < addr + size)) {
                    e.resp = IO_AXI_RESPONSE_OKAY;
                    for (unsigned i = 0; i < size; ++i) {
                        e.data |= uint64_t(reference[addr - kBase + i]) <<
                                  ((addr % 8 + i) * 8);
                    }
                }
            }
            reads[a.id].push_back(e);
        }
    }

    std::vector<Engine::WriteData> MakeWrite(const Engine::Address &a,
                                           bool malformed = false)
    {
        const bool valid = Valid(a) && !malformed;
        uint8_t response = valid ? IO_AXI_RESPONSE_OKAY : IO_AXI_RESPONSE_SLVERR;
        std::vector<Engine::WriteData> data;
        for (unsigned b = 0; b <= a.len; ++b) {
            const uint64_t value = (uint64_t(random()) << 32) | random();
            uint8_t mask = uint8_t(random());
            if (a.size <= 3) {
                const auto [addr, size] = Span(a, b);
                mask &= uint8_t(((1u << size) - 1) << (addr % 8));
                if (valid) {
                    if (addr >= kBase && addr - kBase <= reference.size() - size &&
                        !(fault_address >= addr && fault_address < addr + size)) {
                        for (unsigned i = 0; i < size; ++i) {
                            if (mask & (1u << (addr % 8 + i))) {
                                reference[addr - kBase + i] = uint8_t(value >>
                                    ((addr % 8 + i) * 8));
                            }
                        }
                    } else {
                        response = IO_AXI_RESPONSE_SLVERR;
                    }
                }
            }
            data.push_back({value, mask, b == a.len});
        }
        if (malformed) data.back().last = false;
        writes[a.id].push_back(response);
        return data;
    }

    void SendAddress(const Engine::Address &a, bool write)
    {
        bool done = false;
        bool valid = false;
        while (!done) {
            Engine::Inputs in;
            in.rready = Chance(); in.bready = Chance();
            valid = valid || Chance();
            if (write) { in.aw = a; in.awvalid = valid; }
            else { in.ar = a; in.arvalid = valid; }
            const auto out = Tick(in);
            done = write ? in.awvalid && out.awready : in.arvalid && out.arready;
        }
    }

    void SendData(const std::vector<Engine::WriteData> &data)
    {
        unsigned beat = 0;
        bool valid = false;
        while (beat < data.size()) {
            Engine::Inputs in;
            valid = valid || Chance();
            in.wvalid = valid; in.w = data[beat];
            in.rready = Chance(); in.bready = Chance();
            auto out = Tick(in);
            if (in.wvalid && out.wready) { ++beat; valid = false; }
        }
    }

    void WriteTogether(const Engine::Address &a)
    {
        auto data = MakeWrite(a);
        unsigned beat = 0;
        bool aw_done = false, aw_valid = false, w_valid = false;
        while (!aw_done || beat < data.size()) {
            Engine::Inputs in;
            aw_valid = !aw_done && (aw_valid || Chance());
            w_valid = beat < data.size() && (w_valid || Chance());
            in.aw = a; in.awvalid = aw_valid;
            if (beat < data.size()) in.w = data[beat];
            in.wvalid = w_valid;
            in.rready = Chance(); in.bready = Chance();
            const auto out = Tick(in);
            if (in.awvalid && out.awready) aw_done = true;
            if (in.wvalid && out.wready) { ++beat; w_valid = false; }
        }
    }

    void Write(const Engine::Address &a, bool data_first = false, bool malformed = false)
    {
        auto data = MakeWrite(a, malformed);
        if (data_first) SendData(data);
        SendAddress(a, true);
        if (!data_first) SendData(data);
    }

    void Read(const Engine::Address &a)
    {
        ExpectRead(a);
        SendAddress(a, false);
    }

    void Reset()
    {
        pending.clear(); // scheduler reset must accompany model reset
        engine.Reset();
        reads.clear(); writes.clear();
        reference = memory;
        stalled_r_ = stalled_b_ = false;
        CHECK(!engine.NeedsService() && engine.Outstanding() == 0);
    }

private:
    Engine::Outputs previous_;
    bool stalled_r_ = false, stalled_b_ = false;
};

static void Directed()
{
    for (unsigned depth : {1, 4, 16}) {
        Harness h(100 + depth, depth);
        for (unsigned size = 0; size <= 3; ++size) {
            for (unsigned lane = 0; lane < 64; ++lane) {
                Engine::Address a{uint16_t(0xff00 + lane), kBase + 0x1000 + lane,
                                  uint8_t(lane % 9), uint8_t(size), 1};
                if (lane % 3 == 0) h.WriteTogether(a);
                else h.Write(a, lane % 2);
                h.Drain();
                h.Read(a); h.Drain();
            }
        }
        for (unsigned bytes : {128, 2048, 4096}) {
            for (unsigned off = 0; off < bytes; off += 2048) {
                auto n = std::min(2048u, bytes - off);
                Engine::Address a{0xffff, kBase + 0x4000 + off,
                                  uint8_t(n / 8 - 1), 3, 1};
                h.Write(a, true); h.Drain(); h.Read(a); h.Drain();
            }
        }
        // A partial first beat must not touch bytes before the start address.
        Engine::Address unaligned{0x310, kBase + 0x5003, 255, 3, 1};
        h.Write(unaligned); h.Drain(); h.Read(unaligned); h.Drain();
        // Legal transfer ending exactly at a page boundary.
        Engine::Address boundary{0x311, kBase + 0x6fc5, 7, 3, 1};
        h.Write(boundary); h.Drain(); h.Read(boundary); h.Drain();
        CHECK(h.engine.ProtocolErrors() == 0);
    }
}

static void ErrorsAndReset()
{
    Harness h(321, 4);
    for (auto a : {
        Engine::Address{7, kBase + 0xff8, 1, 3, 1}, // crosses 4 KiB
        Engine::Address{7, kBase + 0x1000, 31, 3, 0},
        Engine::Address{7, kBase + 0x1000, 255, 3, 2},
        Engine::Address{7, kBase + 0x1000, 3, 4, 1},
        Engine::Address{7, UINT64_MAX - 3, 1, 3, 1}}) {
        auto requests = h.memory_requests;
        h.Write(a); h.Drain(); h.Read(a); h.Drain();
        CHECK(h.memory_requests == requests);
    }
    Engine::Address missing{9, kBase + kMemorySize, 127, 3, 1};
    h.Write(missing); h.Drain(); h.Read(missing); h.Drain();
    h.fault_address = kBase + 0x2030;
    Engine::Address partly_bad{0x900, kBase + 0x2000, 15, 3, 1};
    h.Write(partly_bad); h.Drain(); h.Read(partly_bad); h.Drain();
    h.fault_address = UINT64_MAX;
    const auto requests = h.memory_requests;
    h.Write({9, kBase + 0x1000, 15, 3, 1}, false, true); h.Drain();
    CHECK(h.memory_requests == requests && h.engine.ProtocolErrors() == 1);
    // Invalid WSTRB is diagnosed before any write side effects.
    Engine::Address narrow{10, kBase + 0x1004, 0, 2, 1};
    h.writes[narrow.id].push_back(IO_AXI_RESPONSE_SLVERR);
    h.SendAddress(narrow, true);
    h.SendData({{0x1234, 0xff, true}}); h.Drain();
    CHECK(h.memory_requests == requests && h.engine.ProtocolErrors() == 2);
    // All-zero strobe must preserve all RAM bytes.
    Engine::Address zero{10, kBase + 0x1800, 0, 3, 1};
    h.writes[zero.id].push_back(IO_AXI_RESPONSE_OKAY);
    h.SendData({{UINT64_MAX, 0, true}}); h.SendAddress(zero, true); h.Drain();

    h.service = false;
    h.Read({11, kBase + 0x2000, 255, 3, 1});
    IoAxiTransaction old;
    CHECK(h.engine.Next(&old) == IO_RTL_SYSTEM_OK);
    h.Reset();
    CHECK(h.engine.Complete(&old) == IO_RTL_SYSTEM_ERR_INVALID);
    CHECK(h.engine.ProtocolErrors() == 0);
    h.service = true;
    h.Write({11, kBase + 0x2003, 255, 3, 1}); h.Drain();
    h.Read({11, kBase + 0x2003, 255, 3, 1}); h.Drain();

    // B is not issued when write data are captured but memory has not completed.
    h.service = false;
    h.Write({12, kBase + 0x8000, 255, 3, 1});
    for (unsigned i = 0; i < 32; ++i) CHECK(!h.Tick().bvalid);
    h.Reset(); h.service = true;
    // W can arrive before AW, and the queue stops at exactly 256 beats.
    h.SendData(std::vector<Engine::WriteData>(256));
    CHECK(!h.engine.Drive({}).wready);
    h.Reset();
    h.Read({33, kBase, 0, 3, 1});
    for (unsigned i = 0; i < 16; ++i) h.Tick();
    CHECK(h.engine.Drive({}).rvalid);
    h.Reset(); // a stalled response must be discarded too
    CHECK(!h.engine.Drive({}).rvalid);
    CHECK(h.engine.Next(nullptr) == IO_RTL_SYSTEM_ERR_INVALID);
    CHECK(h.engine.Complete(nullptr) == IO_RTL_SYSTEM_ERR_INVALID);
}

static void Concurrent()
{
    Harness h(917, 16);
    h.service = false;
    std::vector<std::vector<Engine::WriteData>> writes;
    for (unsigned i = 0; i < 8; ++i) {
        Engine::Address a{uint16_t(0x8000 | (i % 4)),
                          kBase + 0x1000 + i * 4096, 31, 3, 1};
        writes.push_back(h.MakeWrite(a)); h.SendAddress(a, true);
    }
    for (unsigned i = 0; i < 8; ++i) {
        h.Read({uint16_t(0x8000 | (i % 4)),
                kBase + 0x10000 + i * 4096, 31, 3, 1});
    }
    CHECK(h.engine.Outstanding() == 16);
    auto full = h.engine.Drive({});
    CHECK(!full.awready && !full.arready);
    for (auto &w : writes) h.SendData(w);
    h.service = true; h.Drain();
    CHECK(h.completed_reads == 8 && h.completed_writes == 8);

    // Same ID and identical addresses must not alias completion contexts.
    h.service = false;
    Engine::Address duplicate{0xabab, kBase + 0x7000, 7, 3, 1};
    h.Write(duplicate); h.Write(duplicate);
    h.service = true; h.Drain();
    h.service = false;
    h.Read(duplicate); h.Read(duplicate);
    h.service = true; h.Drain();

    // Simultaneous AR/AW at depth one must never overbook the last credit.
    Harness one(9, 1);
    Engine::Address a{3, kBase, 0, 3, 1};
    one.ExpectRead(a);
    auto w = one.MakeWrite(a);
    Engine::Inputs in;
    in.aw = in.ar = a; in.awvalid = in.arvalid = true;
    auto out = one.Tick(in);
    CHECK(out.awready != out.arready);
    if (out.arready) {
        // Finish read before modifying its data.
        while (one.engine.NeedsService()) one.Idle(1);
        one.SendAddress(a, true); one.SendData(w);
    } else {
        one.SendData(w);
        while (one.engine.NeedsService()) one.Idle(1);
        one.SendAddress(a, false);
    }
    one.Drain();
}

static void Randomized(unsigned seed)
{
    Harness h(seed, 16);
    for (unsigned n = 0; n < 1000; ++n) {
        const unsigned size = h.random() % 4;
        const unsigned count = h.random() % 256 + 1;
        const unsigned page = h.random() % 30;
        const unsigned start = (h.random() % (4096 - (count << size))) & ~7u;
        const unsigned misalignment = h.random() % (1u << size);
        Engine::Address a{uint16_t(h.random()),
            kBase + page * 4096 + start + misalignment,
            uint8_t(count - 1), uint8_t(size), 1};
        h.Write(a, h.Chance()); h.Drain();
        h.Read(a); h.Drain();
    }
    CHECK(h.engine.ProtocolErrors() == 0);
    std::cout << "seed=" << seed << " requests=2000 cycles=" << h.cycles
              << " memory_beats=" << h.memory_requests << " PASS\n";
}

int main()
{
    try {
        Directed(); ErrorsAndReset(); Concurrent();
        for (unsigned seed : {0x1234, 0x5a5a, 0xc0ffee}) Randomized(seed);
        std::cout << "AXI_MASTER_ENGINE_PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "AXI_MASTER_ENGINE_FAIL " << e.what() << '\n';
        return 1;
    }
}
