#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "oram/core/Block.h"
#include "oram/path_oram/PathORAMClient.h"
#include "oram/path_oram/PathORAMServer.h"

using namespace oram;

// ===========================================================================
// Port helpers
// ===========================================================================
//
// Each test fixture instance picks a unique port derived from the gtest test
// index so consecutive process launches don't collide on the same port.
// We also sleep briefly after the server starts to guarantee the bind is
// complete before the client connects.
//
// Port range: 54400 – 54499 (well clear of the old 54321/54322 range)
// ---------------------------------------------------------------------------
namespace {

// A simple global counter so tests in the same binary also get unique ports.
std::atomic<int> g_port_counter{54400};

int NextPort() { return g_port_counter.fetch_add(1, std::memory_order_relaxed); }

}  // namespace

// ===========================================================================
// Fixture for "small" tests  (tree height 4, 16 blocks)
// ===========================================================================

class PathORAMTest : public ::testing::Test {
   protected:
    static constexpr size_t kTreeHeight = 4;
    static constexpr size_t kNumBlocks  = 16;

    void SetUp() override {
        port_ = NextPort();

        server_thread_ = std::thread([this]() {
            path_oram::PathORAMServer server("127.0.0.1", port_, kTreeHeight);

            // Start with an empty tree — the client controls all block positions
            // via its position_map_, so pre-loading the server with blocks would
            // cause a position-map mismatch (server leaf ≠ client's random leaf).
            server.Init({});
            server.HandleRequests();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    void TearDown() override {
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    int         port_{};
    std::thread server_thread_;
};

// ===========================================================================
// Fixture for "large" stress tests  (tree height 6, 64 blocks)
// ===========================================================================

class PathORAMStressTest : public ::testing::Test {
   protected:
    static constexpr size_t kTreeHeight  = 6;
    static constexpr size_t kNumBlocks   = 64;
    static constexpr size_t kNumAccesses = 1000;

    void SetUp() override {
        port_ = NextPort();

        server_thread_ = std::thread([this]() {
            path_oram::PathORAMServer server("127.0.0.1", port_, kTreeHeight);
            server.Init({});  // start with empty tree
            server.HandleRequests();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    void TearDown() override {
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    int         port_{};
    std::thread server_thread_;
};

// ===========================================================================
// Basic correctness tests
// ===========================================================================

TEST_F(PathORAMTest, BasicReadWrite) {
    path_oram::PathORAMClient client("127.0.0.1", port_, kTreeHeight, kNumBlocks);

    std::vector<uint8_t> write_data(path_oram::Config::kBlockSize);
    for (size_t i = 0; i < write_data.size(); ++i) {
        write_data[i] = static_cast<uint8_t>(42 + i);
    }

    client.Write(10, write_data);
    auto read_data = client.Read(10);

    EXPECT_EQ(read_data, write_data);
}

TEST_F(PathORAMTest, MultipleAccesses) {
    path_oram::PathORAMClient client("127.0.0.1", port_, kTreeHeight, kNumBlocks);

    // Write to multiple addresses
    for (size_t addr = 0; addr < 5; ++addr) {
        std::vector<uint8_t> data(path_oram::Config::kBlockSize);
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] = static_cast<uint8_t>(addr * 10 + i);
        }
        client.Write(addr, data);
    }

    // Read them back in different order
    for (size_t addr : {2, 4, 0, 3, 1}) {
        auto data = client.Read(addr);
        EXPECT_EQ(data[0], static_cast<uint8_t>(addr * 10));
    }
}

TEST_F(PathORAMTest, OverwriteData) {
    path_oram::PathORAMClient client("127.0.0.1", port_, kTreeHeight, kNumBlocks);

    uint64_t addr = 3;

    std::vector<uint8_t> data1(path_oram::Config::kBlockSize, 0xAA);
    client.Write(addr, data1);

    std::vector<uint8_t> data2(path_oram::Config::kBlockSize, 0xBB);
    client.Write(addr, data2);

    auto result = client.Read(addr);
    EXPECT_EQ(result, data2);
}

// ===========================================================================
// Eviction check: fill ALL addresses, then read every one back
// ===========================================================================

TEST_F(PathORAMTest, EvictionFillAllAddresses) {
    path_oram::PathORAMClient client("127.0.0.1", port_, kTreeHeight, kNumBlocks);

    std::vector<std::vector<uint8_t>> expected(kNumBlocks,
                                               std::vector<uint8_t>(path_oram::Config::kBlockSize));
    for (size_t addr = 0; addr < kNumBlocks; ++addr) {
        for (size_t i = 0; i < path_oram::Config::kBlockSize; ++i) {
            expected[addr][i] = static_cast<uint8_t>((addr * 13 + i * 7) & 0xFF);
        }
        client.Write(static_cast<uint64_t>(addr), expected[addr]);
    }

    for (size_t addr = 0; addr < kNumBlocks; ++addr) {
        auto result = client.Read(static_cast<uint64_t>(addr));
        EXPECT_EQ(result, expected[addr]) << "Block " << addr << " data mismatch after full fill";
    }
}

// ===========================================================================
// 1000 Random Access Stress Test
// ===========================================================================

TEST_F(PathORAMStressTest, RandomAccess1000) {
    path_oram::PathORAMClient client("127.0.0.1", port_, kTreeHeight, kNumBlocks);

    const size_t block_size = path_oram::Config::kBlockSize;

    // Oracle: single source of truth for each address's current value
    std::unordered_map<uint64_t, std::vector<uint8_t>> oracle;

    std::mt19937_64                         rng(12345);  // fixed seed
    std::uniform_int_distribution<uint64_t> addr_dist(0, kNumBlocks - 1);
    std::uniform_int_distribution<int>      op_dist(0, 1);
    std::uniform_int_distribution<uint8_t>  byte_dist(0, 255);

    size_t reads = 0, writes = 0, verified_reads = 0;

    for (size_t iter = 0; iter < kNumAccesses; ++iter) {
        uint64_t addr = addr_dist(rng);
        int      op   = op_dist(rng);

        if (op == 1) {
            // WRITE
            std::vector<uint8_t> data(block_size);
            for (auto& b : data) b = byte_dist(rng);
            client.Write(addr, data);
            oracle[addr] = data;
            ++writes;
        } else {
            // READ
            auto result = client.Read(addr);
            ++reads;

            if (oracle.count(addr)) {
                ASSERT_EQ(result, oracle.at(addr))
                    << "Iteration " << iter << ": read mismatch at addr=" << addr;
                ++verified_reads;
            } else {
                ASSERT_EQ(result.size(), block_size)
                    << "Iteration " << iter << ": wrong block size at unwritten addr=" << addr;
            }
        }
    }

    std::cout << "[RandomAccess1000] reads=" << reads << " writes=" << writes
              << " verified_reads=" << verified_reads << "\n";

    // Final pass: read every address we ever wrote and verify
    std::cout << "[RandomAccess1000] Final verification of " << oracle.size()
              << " written addresses...\n";

    size_t errors = 0;
    for (const auto& [addr, expected] : oracle) {
        auto result = client.Read(addr);
        if (result != expected) {
            ADD_FAILURE() << "Final verification: mismatch at addr=" << addr;
            ++errors;
        }
    }
    EXPECT_EQ(errors, 0u);
    if (errors == 0) {
        std::cout << "[RandomAccess1000] PASSED — all " << oracle.size()
                  << " addresses verified correctly.\n";
    }
}

// ===========================================================================
// Sequential scan: write all, then read all 10 rounds
// ===========================================================================

TEST_F(PathORAMStressTest, SequentialScanRepeat) {
    path_oram::PathORAMClient client("127.0.0.1", port_, kTreeHeight, kNumBlocks);

    const size_t block_size = path_oram::Config::kBlockSize;

    std::vector<std::vector<uint8_t>> expected(kNumBlocks, std::vector<uint8_t>(block_size));
    for (size_t addr = 0; addr < kNumBlocks; ++addr) {
        for (size_t i = 0; i < block_size; ++i) {
            expected[addr][i] = static_cast<uint8_t>((addr * 31 + i * 17) & 0xFF);
        }
        client.Write(static_cast<uint64_t>(addr), expected[addr]);
    }

    for (int round = 0; round < 10; ++round) {
        for (size_t addr = 0; addr < kNumBlocks; ++addr) {
            auto result = client.Read(static_cast<uint64_t>(addr));
            ASSERT_EQ(result, expected[addr]) << "Round " << round << " addr " << addr;
        }
    }
}
