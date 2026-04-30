#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "oram/core/RAM.h"
#include "oram/ring_oram/RingBucket.h"
#include "oram/ring_oram/RingORAMClient.h"
#include "oram/ring_oram/RingORAMServer.h"

namespace oram::ring_oram {
namespace {

std::atomic<int> g_port_counter{56400};

int NextPort() { return g_port_counter.fetch_add(1, std::memory_order_relaxed); }

RuntimeConfig SmallConfig() {
    RuntimeConfig cfg;
    cfg.num_blocks = 8;
    cfg.tree_depth = 3;
    cfg.z = 3;
    cfg.s = 3;
    cfg.a = 2;
    cfg.block_size = 16;
    return cfg;
}

std::vector<uint8_t> MakeBlock(size_t block_size, uint8_t seed) {
    std::vector<uint8_t> data(block_size, 0);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>(seed + i);
    }
    return data;
}

std::vector<size_t> PathIndices(const RuntimeConfig& cfg, uint64_t leaf) {
    std::vector<size_t> path;
    path.reserve(cfg.tree_depth + 1);

    size_t node = 0;
    path.push_back(node);
    for (size_t level = 0; level < cfg.tree_depth; ++level) {
        const size_t bit = (leaf >> (cfg.tree_depth - 1 - level)) & 1ULL;
        node = 2 * node + 1 + bit;
        path.push_back(node);
    }
    return path;
}

class RingORAMNetworkTest : public ::testing::Test {
   protected:
    void TearDown() override {
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        for (const auto& path : temp_paths_) {
            std::filesystem::remove_all(path);
        }
    }

    void StartServer(const RuntimeConfig& config) {
        config_ = config;
        port_ = NextPort();
        server_thread_ = std::thread([this]() {
            RingORAMServer server("127.0.0.1", port_, config_);
            server.HandleRequests();
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::filesystem::path MakeTempPath(const std::string& label) {
        const auto path = std::filesystem::temp_directory_path() /
                          ("unified_oramhub_ring_oram_" + label + "_" +
                           std::to_string(NextPort()));
        temp_paths_.push_back(path);
        std::filesystem::remove_all(path);
        return path;
    }

    RuntimeConfig config_;
    int port_{};
    std::thread server_thread_;
    std::vector<std::filesystem::path> temp_paths_;
};

TEST(RingORAMBucketTest, BucketMetadataMatchesConfiguredShape) {
    const RuntimeConfig cfg = SmallConfig();
    RingBucket bucket(cfg.z, cfg.s, cfg.block_size);

    EXPECT_EQ(bucket.count, 0U);
    EXPECT_EQ(bucket.valids.size(), cfg.z + cfg.s);
    EXPECT_EQ(bucket.addrs.size(), cfg.z);
    EXPECT_EQ(bucket.leaves.size(), cfg.z);
    EXPECT_EQ(bucket.ptrs.size(), cfg.z);
    EXPECT_EQ(bucket.data.size(), cfg.z + cfg.s);

    for (bool valid : bucket.valids) {
        EXPECT_TRUE(valid);
    }
    for (const auto& block : bucket.data) {
        EXPECT_TRUE(block.IsDummy());
        EXPECT_EQ(block.data.size(), cfg.block_size);
    }
}

TEST_F(RingORAMNetworkTest, ClientInitializesEncryptedServerTreeOverSocket) {
    const RuntimeConfig cfg = SmallConfig();
    StartServer(cfg);

    {
        RingORAMClient client("127.0.0.1", port_, cfg, 0xBEEF);
        EXPECT_EQ(client.ServerBucketCount(0), 0U);
        EXPECT_EQ(client.ServerXorPathReadCount(), 0U);
    }
}

TEST_F(RingORAMNetworkTest, DiskStorageInitializesServerTreeAndClientStashFiles) {
    RuntimeConfig cfg = SmallConfig();
    const auto root = MakeTempPath("init");
    cfg.server_storage_dir = (root / "server").string();
    cfg.stash_file_path = (root / "client" / "stash.bin").string();

    StartServer(cfg);

    {
        RingORAMClient client("127.0.0.1", port_, cfg, 0xBEEF);

        const auto tree_dir = std::filesystem::path(cfg.server_storage_dir) / "tree";
        EXPECT_TRUE(std::filesystem::is_regular_file(tree_dir / "bucket_0.bin"));
        EXPECT_TRUE(std::filesystem::is_regular_file(
            tree_dir / ("bucket_" + std::to_string(cfg.NumTreeNodes() - 1) + ".bin")));

        size_t bucket_file_count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(tree_dir)) {
            if (entry.is_regular_file()) {
                ++bucket_file_count;
            }
        }
        EXPECT_EQ(bucket_file_count, cfg.NumTreeNodes());
        EXPECT_TRUE(std::filesystem::is_regular_file(cfg.stash_file_path));
    }
}

TEST_F(RingORAMNetworkTest, DiskBackedStashPreservesWriteReadWorkload) {
    RuntimeConfig cfg = SmallConfig();
    const auto root = MakeTempPath("stash");
    cfg.server_storage_dir = (root / "server").string();
    cfg.stash_file_path = (root / "client" / "stash.bin").string();

    StartServer(cfg);

    RingORAMClient client("127.0.0.1", port_, cfg, 0xBEEF);

    const auto expected = MakeBlock(cfg.block_size, 0x44);
    client.Write(4, expected);

    EXPECT_EQ(client.Read(4), expected);
    ASSERT_TRUE(std::filesystem::is_regular_file(cfg.stash_file_path));
    EXPECT_GT(std::filesystem::file_size(cfg.stash_file_path), 0U);
}

TEST_F(RingORAMNetworkTest, WriteThenReadSingleBlock) {
    const RuntimeConfig cfg = SmallConfig();
    StartServer(cfg);

    RingORAMClient client("127.0.0.1", port_, cfg, 0xBEEF);

    const auto expected = MakeBlock(cfg.block_size, 0x20);
    client.Write(3, expected);

    EXPECT_EQ(client.Read(3), expected);
}

TEST_F(RingORAMNetworkTest, OverwriteReturnsLatestData) {
    const RuntimeConfig cfg = SmallConfig();
    StartServer(cfg);

    RingORAMClient client("127.0.0.1", port_, cfg, 0xBEEF);

    client.Write(1, MakeBlock(cfg.block_size, 0x10));
    const auto latest = MakeBlock(cfg.block_size, 0x80);
    client.Write(1, latest);

    EXPECT_EQ(client.Read(1), latest);
}

TEST_F(RingORAMNetworkTest, PeriodicEvictionPreservesManyBlocks) {
    RuntimeConfig cfg = SmallConfig();
    cfg.num_blocks = 16;
    cfg.tree_depth = 4;
    cfg.z = 4;
    cfg.s = 4;
    cfg.a = 2;
    cfg.block_size = 24;

    StartServer(cfg);

    RingORAMClient client("127.0.0.1", port_, cfg, 0xBEEF);

    std::vector<std::vector<uint8_t>> oracle(cfg.num_blocks);
    for (size_t addr = 0; addr < cfg.num_blocks; ++addr) {
        oracle[addr] = MakeBlock(cfg.block_size, static_cast<uint8_t>(addr * 9));
        client.Write(static_cast<uint64_t>(addr), oracle[addr]);
    }

    for (size_t round = 0; round < 32; ++round) {
        const uint64_t addr = static_cast<uint64_t>((round * 5) % cfg.num_blocks);
        if (round % 3 == 0) {
            oracle[addr] = MakeBlock(cfg.block_size, static_cast<uint8_t>(0xA0 + round));
            client.Write(addr, oracle[addr]);
        } else {
            EXPECT_EQ(client.Read(addr), oracle[addr]);
        }
    }

    for (size_t addr = 0; addr < cfg.num_blocks; ++addr) {
        EXPECT_EQ(client.Read(static_cast<uint64_t>(addr)), oracle[addr]);
    }
}

TEST_F(RingORAMNetworkTest, EarlyReshuffleResetsTouchedBucket) {
    RuntimeConfig cfg = SmallConfig();
    cfg.s = 1;
    cfg.a = 64;

    StartServer(cfg);

    RingORAMClient client("127.0.0.1", port_, cfg, 0xBEEF);

    const uint64_t old_leaf = client.PositionMap().at(0);
    const auto path = PathIndices(cfg, old_leaf);

    (void)client.Read(0);

    for (size_t bucket_idx : path) {
        EXPECT_EQ(client.ServerBucketCount(bucket_idx), 0U);
    }
}

TEST_F(RingORAMNetworkTest, ReadPathUsesOneServerXorAggregate) {
    RuntimeConfig cfg = SmallConfig();
    cfg.a = 64;

    StartServer(cfg);

    RingORAMClient client("127.0.0.1", port_, cfg, 0xBEEF);

    const auto expected = MakeBlock(cfg.block_size, 0x31);
    client.Write(2, expected);
    const uint64_t xor_reads_after_write = client.ServerXorPathReadCount();

    EXPECT_EQ(client.Read(2), expected);

    EXPECT_EQ(client.ServerXorPathReadCount(), xor_reads_after_write + 1);
    EXPECT_EQ(client.ServerLastXorPathSlotCount(), cfg.tree_depth + 1);
    EXPECT_EQ(client.ReadPathCount(), 2U);
}

TEST_F(RingORAMNetworkTest, TenEvictionFrequenciesKeepReadPathEarlyReshuffleAndEvictionTogether) {
    RuntimeConfig cfg = SmallConfig();
    cfg.num_blocks = 12;
    cfg.tree_depth = 4;
    cfg.z = 4;
    cfg.s = 2;
    cfg.a = 3;
    cfg.block_size = 20;

    StartServer(cfg);

    RingORAMClient client("127.0.0.1", port_, cfg, 0xBEEF);

    std::vector<std::vector<uint8_t>> oracle(cfg.num_blocks,
                                             std::vector<uint8_t>(cfg.block_size, 0));
    for (size_t addr = 0; addr < cfg.num_blocks; ++addr) {
        oracle[addr] = MakeBlock(cfg.block_size, static_cast<uint8_t>(0x20 + addr * 3));
        client.Write(static_cast<uint64_t>(addr), oracle[addr]);
    }

    const size_t start_read_paths = client.ReadPathCount();
    const uint64_t start_evictions = client.EvictionCounter();
    const size_t total_accesses = 10 * cfg.a;

    for (size_t i = 0; i < total_accesses; ++i) {
        const uint64_t addr = static_cast<uint64_t>((i * 7 + 1) % cfg.num_blocks);
        if (i % 4 == 0) {
            oracle[addr] = MakeBlock(cfg.block_size, static_cast<uint8_t>(0xA0 + i));
            client.Write(addr, oracle[addr]);
        } else {
            EXPECT_EQ(client.Read(addr), oracle[addr]) << "round=" << i << " addr=" << addr;
        }
    }

    for (size_t addr = 0; addr < cfg.num_blocks; ++addr) {
        EXPECT_EQ(client.Read(static_cast<uint64_t>(addr)), oracle[addr]) << "addr=" << addr;
    }

    EXPECT_GE(client.ReadPathCount(), start_read_paths + total_accesses);
    EXPECT_GE(client.EvictionCounter(), start_evictions + 10);
    EXPECT_GT(client.EarlyReshuffleCount(), 0U);
    EXPECT_GE(client.ServerXorPathReadCount(), client.ReadPathCount());
}

}  // namespace
}  // namespace oram::ring_oram
