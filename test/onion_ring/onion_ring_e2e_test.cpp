#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

#include "oram/onion_ring/OnionRingClient.h"
#include "oram/onion_ring/OnionRingServer.h"
#include "oram/onion_ring/WaksmanNetwork.h"

namespace oram::onion_ring {
namespace {

std::atomic<int> g_port_counter{55400};

int NextPort() { return g_port_counter.fetch_add(1, std::memory_order_relaxed); }

RuntimeConfig AccessOnlyConfig() {
    RuntimeConfig cfg;
    cfg.z = 4;
    cfg.s = 4;
    cfg.a = 64;
    cfg.tree_height = 3;
    cfg.num_blocks = 8;
    cfg.block_size = 32;
    return cfg;
}

RuntimeConfig EvictingConfig() {
    RuntimeConfig cfg = AccessOnlyConfig();
    cfg.a = 3;
    return cfg;
}

RuntimeConfig LongRunConfig() {
    RuntimeConfig cfg;
    cfg.z = 2;
    cfg.s = 2;
    cfg.a = 3;
    cfg.tree_height = 2;
    cfg.num_blocks = 4;
    cfg.block_size = 16;
    return cfg;
}

RuntimeConfig PracticalFallbackConfig() {
    RuntimeConfig cfg = EvictingConfig();
    cfg.use_recursive_packed_swap_bits = false;
    return cfg;
}

RuntimeConfig RecursiveCutoverSmokeConfig() {
    RuntimeConfig cfg;
    cfg.z = 1;
    cfg.s = 1;
    cfg.a = 2;
    cfg.tree_height = 1;
    cfg.num_blocks = 2;
    cfg.block_size = 8;
    return cfg;
}

RuntimeConfig PaperCryptoAmortizedConfig() {
    RuntimeConfig cfg;
    cfg.z = 254;
    cfg.s = 254;
    cfg.a = 249;
    cfg.tree_height = 4;
    cfg.num_blocks = 1024;
    cfg.block_size = 3072;
    cfg.use_recursive_packed_swap_bits = true;
    cfg.recursive_tlwe_n = 2048;
    cfg.swap_tgsw_l = 8;
    cfg.swap_tgsw_bgbit = 3;
    cfg.neg_sk_tgsw_l = 7;
    cfg.neg_sk_tgsw_bgbit = 7;
    cfg.rlwe_ks_basebit = 5;
    cfg.rlwe_ks_length = 10;
    cfg.plaintext_bits = 12;
    cfg.alpha = std::ldexp(1.0, -55);
    return cfg;
}

std::vector<uint8_t> MakeBlock(size_t block_size, uint8_t seed) {
    std::vector<uint8_t> block(block_size, 0);
    for (size_t i = 0; i < block.size(); ++i) {
        block[i] = static_cast<uint8_t>(seed + i);
    }
    return block;
}

class OnionRingE2ETest : public ::testing::Test {
   protected:
    void SetUp() override { config_ = AccessOnlyConfig(); }

    void TearDown() override {
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    void StartServer(const RuntimeConfig& config) {
        config_ = config;
        port_ = NextPort();
        server_thread_ = std::thread([this]() {
            OnionRingServer server("127.0.0.1", port_, config_);
            server.Init();
            server.HandleRequests();
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    RuntimeConfig config_;
    int port_{};
    std::thread server_thread_;
};

TEST_F(OnionRingE2ETest, WriteThenReadSingleBlock) {
    StartServer(AccessOnlyConfig());

    OnionRingClient client("127.0.0.1", port_, config_);
    std::vector<uint8_t> data(config_.block_size, 0x2B);

    client.Write(3, data);
    auto read_back = client.Read(3);

    EXPECT_EQ(read_back, data);
}

TEST_F(OnionRingE2ETest, OverwriteBlockReturnsLatestValue) {
    StartServer(AccessOnlyConfig());

    OnionRingClient client("127.0.0.1", port_, config_);
    std::vector<uint8_t> first(config_.block_size, 0x11);
    std::vector<uint8_t> second(config_.block_size, 0x22);

    client.Write(1, first);
    client.Write(1, second);
    auto read_back = client.Read(1);

    EXPECT_EQ(read_back, second);
}

TEST_F(OnionRingE2ETest, MultipleWritesAndReadsWorkBeforeEvictionThreshold) {
    StartServer(AccessOnlyConfig());

    OnionRingClient client("127.0.0.1", port_, config_);

    std::vector<uint8_t> a(config_.block_size, 0xA1);
    std::vector<uint8_t> b(config_.block_size, 0xB2);

    client.Write(0, a);
    client.Write(5, b);

    EXPECT_EQ(client.Read(0), a);
    EXPECT_EQ(client.Read(5), b);
}

TEST_F(OnionRingE2ETest, EvictionPreservesBlocksAcrossManyAccesses) {
    StartServer(EvictingConfig());

    OnionRingClient client("127.0.0.1", port_, config_);
    std::vector<std::vector<uint8_t>> oracle(config_.num_blocks);
    for (size_t addr = 0; addr < config_.num_blocks; ++addr) {
        oracle[addr] = MakeBlock(config_.block_size, static_cast<uint8_t>(0x10 + addr * 7));
        client.Write(addr, oracle[addr]);
    }

    for (size_t round = 0; round < 18; ++round) {
        const uint64_t addr = round % config_.num_blocks;
        if (round % 3 == 0) {
            oracle[addr] = MakeBlock(config_.block_size, static_cast<uint8_t>(0x80 + round));
            client.Write(addr, oracle[addr]);
        } else {
            EXPECT_EQ(client.Read(addr), oracle[addr]);
        }
    }

    for (size_t addr = 0; addr < config_.num_blocks; ++addr) {
        EXPECT_EQ(client.Read(addr), oracle[addr]);
    }
}

TEST(OnionRingScheduleTest, ReverseBitEvictionScheduleCoversLeaves) {
    constexpr size_t kTreeHeight = 3;
    std::vector<uint64_t> seen(1ULL << kTreeHeight, 0);

    for (uint64_t counter = 0; counter < seen.size(); ++counter) {
        const uint64_t leaf = OnionRingClient::ReverseBits(counter, kTreeHeight);
        ASSERT_LT(leaf, seen.size());
        seen[leaf]++;
    }

    for (uint64_t count : seen) {
        EXPECT_EQ(count, 1U);
    }
}

TEST_F(OnionRingE2ETest, LeafRefreshReencryptsAndPreservesData) {
    StartServer(EvictingConfig());

    OnionRingClient client("127.0.0.1", port_, config_);
    std::vector<std::vector<uint8_t>> oracle(config_.num_blocks);
    for (size_t addr = 0; addr < config_.num_blocks; ++addr) {
        oracle[addr] = MakeBlock(config_.block_size, static_cast<uint8_t>(0x20 + addr * 5));
        client.Write(addr, oracle[addr]);
    }

    for (size_t round = 0; round < 24; ++round) {
        const uint64_t addr = (round * 3) % config_.num_blocks;
        EXPECT_EQ(client.Read(addr), oracle[addr]);
    }

    for (size_t addr = 0; addr < config_.num_blocks; ++addr) {
        EXPECT_EQ(client.Read(addr), oracle[addr]);
    }
}

TEST_F(OnionRingE2ETest, MixedAccessesRemainCorrectAfterRefreshAndEviction) {
    StartServer(EvictingConfig());

    OnionRingClient client("127.0.0.1", port_, config_);
    std::vector<std::vector<uint8_t>> oracle(config_.num_blocks,
                                             std::vector<uint8_t>(config_.block_size, 0));

    for (size_t round = 0; round < 30; ++round) {
        const uint64_t addr = (round * 5) % config_.num_blocks;
        if (round % 2 == 0) {
            oracle[addr] = MakeBlock(config_.block_size, static_cast<uint8_t>(0x40 + round));
            client.Write(addr, oracle[addr]);
        } else {
            EXPECT_EQ(client.Read(addr), oracle[addr]);
        }
    }

    for (size_t addr = 0; addr < config_.num_blocks; ++addr) {
        EXPECT_EQ(client.Read(addr), oracle[addr]);
    }
}

TEST_F(OnionRingE2ETest, PracticalPackedSwapBitFallbackStillWorksEndToEnd) {
    StartServer(PracticalFallbackConfig());

    OnionRingClient client("127.0.0.1", port_, config_);
    std::vector<std::vector<uint8_t>> oracle(config_.num_blocks);
    for (size_t addr = 0; addr < config_.num_blocks; ++addr) {
        oracle[addr] = MakeBlock(config_.block_size, static_cast<uint8_t>(0x55 + addr * 3));
        client.Write(addr, oracle[addr]);
    }

    for (size_t round = 0; round < 12; ++round) {
        const uint64_t addr = (round * 3 + 1) % config_.num_blocks;
        if (round % 2 == 0) {
            oracle[addr] = MakeBlock(config_.block_size, static_cast<uint8_t>(0x90 + round));
            client.Write(addr, oracle[addr]);
        } else {
            EXPECT_EQ(client.Read(addr), oracle[addr]);
        }
    }

    for (size_t addr = 0; addr < config_.num_blocks; ++addr) {
        EXPECT_EQ(client.Read(addr), oracle[addr]);
    }
}

TEST_F(OnionRingE2ETest, RecursiveDefaultPackedSwapBitsSurviveMinimalEviction) {
    RuntimeConfig config = RecursiveCutoverSmokeConfig();
    ASSERT_TRUE(config.use_recursive_packed_swap_bits);

    StartServer(config);

    OnionRingClient client("127.0.0.1", port_, config_);
    std::vector<std::vector<uint8_t>> oracle(config_.num_blocks);
    oracle[0] = MakeBlock(config_.block_size, 0x61);
    oracle[1] = MakeBlock(config_.block_size, 0x72);

    client.Write(0, oracle[0]);
    client.Write(1, oracle[1]);
    EXPECT_EQ(client.Read(0), oracle[0]);

    oracle[1] = MakeBlock(config_.block_size, 0x83);
    client.Write(1, oracle[1]);

    EXPECT_EQ(client.Read(1), oracle[1]);
    EXPECT_EQ(client.Read(0), oracle[0]);
}

TEST_F(OnionRingE2ETest, DISABLED_PaperCryptoAmortizedDelayFourLevelTree) {
    RuntimeConfig config = PaperCryptoAmortizedConfig();
    const size_t total_accesses = config.a * 10;
    const size_t scheduled_evictions = total_accesses / config.a;
    const WaksmanNetwork network(config.BucketSlots());
    const size_t recursive_chunks =
        (network.NumGates() + static_cast<size_t>(config.recursive_tlwe_n) - 1) /
        static_cast<size_t>(config.recursive_tlwe_n);
    const size_t recursive_rlwe_ciphertexts_per_permutation =
        recursive_chunks * static_cast<size_t>(config.swap_tgsw_l);

    std::cout << "\n[paper-crypto-amortized-config]\n"
              << "tree_height=" << config.tree_height << "\n"
              << "bucket_slots=" << config.BucketSlots() << "\n"
              << "accesses=" << total_accesses << "\n"
              << "scheduled_evictions=" << scheduled_evictions << "\n"
              << "block_size=" << config.block_size << "\n"
              << "plaintext_bits=" << config.plaintext_bits << "\n"
              << "waksman_gates_per_permutation=" << network.NumGates() << "\n"
              << "recursive_rlwe_ciphertexts_per_permutation="
              << recursive_rlwe_ciphertexts_per_permutation << "\n";

    const auto setup_start = std::chrono::steady_clock::now();
    StartServer(config);
    OnionRingClient client("127.0.0.1", port_, config_);
    const auto setup_end = std::chrono::steady_clock::now();

    std::vector<std::vector<uint8_t>> oracle(config.num_blocks,
                                             std::vector<uint8_t>(config.block_size, 0));
    const auto access_start = std::chrono::steady_clock::now();
    for (size_t round = 0; round < total_accesses; ++round) {
        const uint64_t addr = (round * 131 + 17) % config.num_blocks;
        if (round % 2 == 0 || round % 17 == 0) {
            oracle[addr] =
                MakeBlock(config.block_size, static_cast<uint8_t>((round * 29 + addr) & 0xFF));
            client.Write(addr, oracle[addr]);
        } else {
            ASSERT_EQ(client.Read(addr), oracle[addr]) << "round=" << round << " addr=" << addr;
        }
    }
    const auto access_end = std::chrono::steady_clock::now();

    const double setup_seconds =
        std::chrono::duration<double>(setup_end - setup_start).count();
    const double total_seconds =
        std::chrono::duration<double>(access_end - access_start).count();
    const double average_seconds = total_seconds / static_cast<double>(total_accesses);
    const double amortized_eviction_window_seconds =
        total_seconds / static_cast<double>(scheduled_evictions);

    std::cout << std::fixed << std::setprecision(6)
              << "\n[paper-crypto-amortized-results]\n"
              << "setup_seconds=" << setup_seconds << "\n"
              << "total_access_seconds=" << total_seconds << "\n"
              << "average_access_seconds=" << average_seconds << "\n"
              << "average_access_ms=" << average_seconds * 1000.0 << "\n"
              << "amortized_eviction_window_seconds=" << amortized_eviction_window_seconds << "\n";
}

TEST_F(OnionRingE2ETest, TenEvictionWindowsPreserveRewrittenBlocks) {
    StartServer(LongRunConfig());

    OnionRingClient client("127.0.0.1", port_, config_);
    std::vector<std::vector<uint8_t>> oracle(config_.num_blocks);
    for (size_t addr = 0; addr < config_.num_blocks; ++addr) {
        oracle[addr] = MakeBlock(config_.block_size, static_cast<uint8_t>(0x30 + addr * 9));
        client.Write(addr, oracle[addr]);
    }

    const size_t total_accesses = config_.a * 10 + 19;
    for (size_t round = 0; round < total_accesses; ++round) {
        const uint64_t addr = (round * 5 + 1) % config_.num_blocks;
        if (round % 3 == 0 || round % 7 == 0) {
            oracle[addr] =
                MakeBlock(config_.block_size, static_cast<uint8_t>((round * 11 + addr * 17) & 0xFF));
            client.Write(addr, oracle[addr]);
        } else {
            EXPECT_EQ(client.Read(addr), oracle[addr]);
        }
    }

    for (size_t addr = 0; addr < config_.num_blocks; ++addr) {
        EXPECT_EQ(client.Read(addr), oracle[addr]);
    }
}

}  // namespace
}  // namespace oram::onion_ring
