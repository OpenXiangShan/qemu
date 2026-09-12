// Real picker API acceptance test; no QEMU and no alternate CPU/NPU model.
#include "io_rtl_system_api.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#define API_FUNCTIONS(X) \
    X(open) X(close) X(reset) X(set_service_callback) X(mmio_read) X(mmio_write) \
    X(m_axi_next) X(m_axi_complete) X(step) X(last_error)

struct Api {
#define FIELD(name) decltype(&io_rtl_system_##name) name;
    API_FUNCTIONS(FIELD)
#undef FIELD
    void *library;
    explicit Api(const char *path) {
        library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!library) { throw std::runtime_error(dlerror()); }
#define BIND(name) name = reinterpret_cast<decltype(name)>(dlsym(library, "io_rtl_system_" #name)); \
        if (!name) { throw std::runtime_error("missing API: " #name); }
        API_FUNCTIONS(BIND)
#undef BIND
    }
    ~Api() { dlclose(library); }
};

struct Harness {
    Api api;
    io_rtl_system_handle_t *handle = nullptr;
    std::vector<uint8_t> ram = std::vector<uint8_t>(4 * 1024 * 1024);
    uint64_t reads = 0, writes = 0, cycles = 0;
    std::array<uint64_t, 4> source_transactions{};
    static constexpr uint64_t base = 0x80000000;
    static constexpr uint64_t done = 0x50000120;
    static constexpr uint64_t verdict = 0x50000140;
    static constexpr uint64_t phase = 0x50000160;

    Harness(const char *library, unsigned depth) : api(library) {
        io_rtl_system_config_t cfg{};
        cfg.mmio_base_hint = 0x30000000;
        cfg.mmio_size_hint = 0x47ff0000;
        cfg.io2q_max_beat_bytes = 8;
        cfg.io2q_outstanding = depth;
        io_rtl_system_callbacks_t cb{};
        cb.opaque = this;
        cb.guest_read = [](void *p, uint64_t a, void *d, uint32_t n) {
            return static_cast<Harness *>(p)->read(a, d, n);
        };
        cb.guest_write = [](void *p, uint64_t a, const void *d, uint32_t n) {
            return static_cast<Harness *>(p)->write(a, d, n);
        };
        cb.log = [](void *, int, const char *msg) { std::fprintf(stderr, "%s\n", msg); };
        handle = api.open(&cfg, &cb);
        if (!handle) { throw std::runtime_error("API open failed"); }
        check(api.set_service_callback(handle, [](void *p) {
            try { static_cast<Harness *>(p)->service(); return 0; }
            catch (const std::exception &e) {
                std::fprintf(stderr, "service callback: %s\n", e.what());
                return -5;
            }
        }, this), "set service callback");
    }
    ~Harness() { if (handle) { api.close(handle); } }
    void check(int status, const char *what) {
        if (status) {
            throw std::runtime_error(std::string(what) + " status=" +
                std::to_string(status) + " " + api.last_error(handle));
        }
    }
    bool valid(uint64_t a, uint32_t n) const {
        return a >= base && n <= ram.size() && a - base <= ram.size() - n;
    }
    int read(uint64_t a, void *d, uint32_t n) {
        if (!valid(a, n)) { return -1; }
        std::memcpy(d, ram.data() + a - base, n);
        reads++;
        return n;
    }
    int write(uint64_t a, const void *d, uint32_t n) {
        if (!valid(a, n)) { return -1; }
        std::memcpy(ram.data() + a - base, d, n);
        writes++;
        return n;
    }
    void service() {
        for (unsigned i = 0; i < 16; i++) {
            IoAxiTransaction txn{};
            int rc = api.m_axi_next(handle, &txn);
            if (rc == IO_RTL_SYSTEM_ERR_NO_TRANSACTION) { return; }
            check(rc, "next master");
            if (txn.transaction_id >= 1024) {
                throw std::runtime_error("unexpected master source ID");
            }
            source_transactions[txn.transaction_id >> 8]++;
            uint32_t size = txn.beat_count * txn.beat_size;
            std::array<uint8_t, IO_AXI_MAX_BEAT_BYTES> data{};
            if (!size || size > data.size() || !valid(txn.address, size)) {
                txn.response = IO_AXI_RESPONSE_DECERR;
                std::fprintf(stderr, "memory error addr=0x%llx size=%u\n",
                             (unsigned long long)txn.address, size);
            } else if (txn.direction == IO_AXI_DIRECTION_IO2Q_READ) {
                read(txn.address, data.data(), size);
                txn.data = data.data();
                txn.response = IO_AXI_RESPONSE_OKAY;
            } else if (txn.direction == IO_AXI_DIRECTION_IO2Q_WRITE) {
                if (!txn.data) { throw std::runtime_error("write missing data"); }
                for (unsigned j = 0; j < size; j++) {
                    if (!txn.byte_strobe || txn.byte_strobe[j]) {
                        ram[txn.address - base + j] = txn.data[j];
                    }
                }
                writes++;
                txn.response = IO_AXI_RESPONSE_OKAY;
            } else { throw std::runtime_error("unexpected master direction"); }
            check(api.m_axi_complete(handle, &txn), "complete master");
        }
    }
    void step(uint64_t count) {
        for (uint64_t i = 0; i < count; i++) {
            check(api.step(handle, 1), "step");
            cycles++;
            service();
        }
    }
    uint32_t mmio(uint64_t address) {
        uint32_t data = 0;
        check(api.mmio_read(handle, address, &data, sizeof(data)), "MMIO read");
        return data;
    }
    void mmio(uint64_t address, uint32_t value) {
        check(api.mmio_write(handle, address, &value, sizeof(value)), "MMIO write");
    }
    void load(const char *path) {
        std::fill(ram.begin(), ram.end(), 0);
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() <= 0 || file.tellg() > (std::streamoff)ram.size()) {
            throw std::runtime_error("invalid RAM image");
        }
        auto size = file.tellg();
        file.seekg(0);
        if (!file.read(reinterpret_cast<char *>(ram.data()), size)) {
            throw std::runtime_error("RAM image read failed");
        }
    }
    void start() {
        mmio(done, 0); mmio(verdict, 0); mmio(phase, 0);
        mmio(0x50000100, 0x80010000);
        mmio(0x50000060, 0);
        mmio(0x50000050, 1);
        mmio(0x50000060, 1);
    }
    void start_other_masters() {
        for (unsigned i = 0; i < 256; i++) { ram[0x32000 + i] = i ^ 0x96; }
        // Issue DMAC and both APLIC domains while E6/AME runs. Their source
        // ID prefixes must route responses independently through the mux.
        mmio(0x30040010, 0x80032000); mmio(0x30040014, 0);
        mmio(0x30040018, 0x80033000); mmio(0x3004001c, 0);
        mmio(0x30040020, 256); mmio(0x30040008, 1);
        mmio(0x31101bc0, 0x80030); mmio(0x31101bc4, 0);
        mmio(0x31101bc8, 0x80031); mmio(0x31101bcc, 0);
        for (uint64_t domain : {0x31100000ULL, 0x31120000ULL}) {
            mmio(domain, 0x104);
            mmio(domain + 4, 4); // source 1, edge rise
            mmio(domain + 0x3004, domain == 0x31100000 ? 0x65 : 0x66);
            mmio(domain + 0x1edc, 1);
            mmio(domain + 0x1cdc, 1);
        }
    }
    void check_other_masters() {
        step(2048);
        if (mmio(0x3004000c) != 2 ||
            std::memcmp(ram.data() + 0x32000, ram.data() + 0x33000, 256)) {
            throw std::runtime_error("concurrent DMAC failed");
        }
        uint32_t msi_m, msi_s;
        std::memcpy(&msi_m, ram.data() + 0x30000, 4);
        std::memcpy(&msi_s, ram.data() + 0x31000, 4);
        if (msi_m != 0x65 || msi_s != 0x66) {
            throw std::runtime_error("concurrent APLIC MSI failed");
        }
        for (auto count : source_transactions) {
            if (!count) { throw std::runtime_error("master source was not exercised"); }
        }
    }
    void run(const char *image, unsigned which, bool quiet) {
        check(api.reset(handle), "global reset");
        uint64_t idle_reads = reads, idle_writes = writes;
        if (mmio(0x50000060) & 1) { throw std::runtime_error("E6 not held in reset"); }
        step(256);
        if (reads != idle_reads || writes != idle_writes) {
            throw std::runtime_error("DDR activity before E6 release");
        }
        load(image);
        start();
        start_other_masters();
        if (quiet) {
            // No configuration traffic during autonomous CPU/NPU execution.
            step(100000);
            if (!mmio(done)) { throw std::runtime_error("no completion without MMIO"); }
        }
        uint32_t state = 0;
        for (unsigned batch = 0; batch < 20000; batch++) {
            state = mmio(done);
            if (state) { break; }
            step(256);
            if (batch % 1000 == 0) {
                std::printf("ARENA STANDALONE progress cycles=%llu phase=%x reads=%llu writes=%llu\n",
                    (unsigned long long)cycles, mmio(phase),
                    (unsigned long long)reads, (unsigned long long)writes);
                std::fflush(stdout);
            }
        }
        if (state != 1) {
            throw std::runtime_error("E6 failed/timeout done=" + std::to_string(state) +
                                     " phase=" + std::to_string(mmio(phase)));
        }
        static const int32_t expected[2][6] = {{16,-18,26,-2,35,-17}, {-2,1,-10,7,9,6}};
        for (unsigned i = 0; i < 6; i++) {
            int32_t actual;
            std::memcpy(&actual, ram.data() + 0x20300 + i * 4, 4);
            if (actual != expected[which][i]) {
                throw std::runtime_error("result mismatch index=" + std::to_string(i) +
                                         " actual=" + std::to_string(actual));
            }
        }
        for (unsigned i = 0; i < 64; i++) {
            if (ram[0x202c0 + i] != 0xa5 || ram[0x20318 + i] != 0xa5) {
                throw std::runtime_error("output guard overwritten");
            }
        }
        mmio(verdict, 0xff);
        for (unsigned i = 0; i < 10000 && mmio(phase) != 0xff; i++) { step(32); }
        if (mmio(phase) != 0xff) { throw std::runtime_error("verdict not acknowledged"); }
        check_other_masters();
        std::printf("ARENA STANDALONE CASE PASS case=%u quiet=%u cycles=%llu reads=%llu writes=%llu\n",
                    which, quiet, (unsigned long long)cycles,
                    (unsigned long long)reads, (unsigned long long)writes);
        std::fflush(stdout);
    }
};

int main(int argc, char **argv) {
    if (argc < 4 || argc > 5) {
        std::fprintf(stderr, "usage: standalone API_SO CASE0_BIN CASE1_BIN [DEPTH]\n");
        return 2;
    }
    try {
        Harness harness(argv[1], argc == 5 ? std::stoul(argv[4]) : 16);
        harness.run(argv[2], 0, false);
        harness.run(argv[3], 1, true);
        harness.run(argv[2], 0, false);
        std::puts("ARENA STANDALONE PASS resets=3 cases=3");
    } catch (const std::exception &e) {
        std::fprintf(stderr, "ARENA STANDALONE FAIL %s\n", e.what());
        return 1;
    }
    std::puts("ARENA STANDALONE CLOSED");
    return 0;
}
