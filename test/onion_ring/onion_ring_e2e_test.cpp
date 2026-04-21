#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "oram/onion_ring/OnionRingClient.h"
#include "oram/onion_ring/OnionRingServer.h"

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

}  // namespace
}  // namespace oram::onion_ring
