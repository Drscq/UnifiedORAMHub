#include <gtest/gtest.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "oram/core/RAM.h"
#include "oram/ring_oram/RingBucket.h"
#include "oram/ring_oram/RingORAMClient.h"
#include "oram/ring_oram/RingORAMServer.h"

namespace oram::ring_oram {
namespace {

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

TEST(RingORAMClientTest, WriteThenReadSingleBlock) {
    const RuntimeConfig cfg = SmallConfig();
    RingORAMServer server(cfg, 0xC001);
    RingORAMClient client(server, cfg, 0xBEEF);

    const auto expected = MakeBlock(cfg.block_size, 0x20);
    client.Write(3, expected);

    EXPECT_EQ(client.Read(3), expected);
}

TEST(RingORAMClientTest, OverwriteReturnsLatestData) {
    const RuntimeConfig cfg = SmallConfig();
    RingORAMServer server(cfg, 0xC002);
    RingORAMClient client(server, cfg, 0xBEEF);

    client.Write(1, MakeBlock(cfg.block_size, 0x10));
    const auto latest = MakeBlock(cfg.block_size, 0x80);
    client.Write(1, latest);

    EXPECT_EQ(client.Read(1), latest);
}

TEST(RingORAMClientTest, PeriodicEvictionPreservesManyBlocks) {
    RuntimeConfig cfg = SmallConfig();
    cfg.num_blocks = 16;
    cfg.tree_depth = 4;
    cfg.z = 4;
    cfg.s = 4;
    cfg.a = 2;
    cfg.block_size = 24;

    RingORAMServer server(cfg, 0xC003);
    RingORAMClient client(server, cfg, 0xBEEF);

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

TEST(RingORAMClientTest, EarlyReshuffleResetsTouchedBucket) {
    RuntimeConfig cfg = SmallConfig();
    cfg.s = 1;
    cfg.a = 64;

    RingORAMServer server(cfg, 0xC004);
    RingORAMClient client(server, cfg, 0xBEEF);

    const uint64_t old_leaf = client.PositionMap().at(0);
    const auto path = server.GetPathIndices(old_leaf);

    (void)client.Read(0);

    for (size_t bucket_idx : path) {
        EXPECT_EQ(server.GetBucket(bucket_idx).count, 0U);
    }
}

TEST(RingORAMClientTest, ReadPathUsesOneServerXorAggregate) {
    RuntimeConfig cfg = SmallConfig();
    cfg.a = 64;

    RingORAMServer server(cfg, 0xC005);
    RingORAMClient client(server, cfg, 0xBEEF);

    const auto expected = MakeBlock(cfg.block_size, 0x31);
    client.Write(2, expected);
    const uint64_t xor_reads_after_write = server.XorPathReadCount();

    EXPECT_EQ(client.Read(2), expected);

    EXPECT_EQ(server.XorPathReadCount(), xor_reads_after_write + 1);
    EXPECT_EQ(server.LastXorPathSlotCount(), cfg.tree_depth + 1);
    EXPECT_EQ(client.ReadPathCount(), 2U);
}

TEST(RingORAMClientTest, TenEvictionFrequenciesKeepReadPathEarlyReshuffleAndEvictionTogether) {
    RuntimeConfig cfg = SmallConfig();
    cfg.num_blocks = 12;
    cfg.tree_depth = 4;
    cfg.z = 4;
    cfg.s = 2;
    cfg.a = 3;
    cfg.block_size = 20;

    RingORAMServer server(cfg, 0xC006);
    RingORAMClient client(server, cfg, 0xBEEF);

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
    EXPECT_GE(server.XorPathReadCount(), client.ReadPathCount());
}

}  // namespace
}  // namespace oram::ring_oram
