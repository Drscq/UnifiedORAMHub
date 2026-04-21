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

class OnionRingE2ETest : public ::testing::Test {
   protected:
    void SetUp() override {
        config_ = AccessOnlyConfig();
        port_ = NextPort();
        server_thread_ = std::thread([this]() {
            OnionRingServer server("127.0.0.1", port_, config_);
            server.Init();
            server.HandleRequests();
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    void TearDown() override {
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    RuntimeConfig config_;
    int port_{};
    std::thread server_thread_;
};

TEST_F(OnionRingE2ETest, WriteThenReadSingleBlock) {
    OnionRingClient client("127.0.0.1", port_, config_);
    std::vector<uint8_t> data(config_.block_size, 0x2B);

    client.Write(3, data);
    auto read_back = client.Read(3);

    EXPECT_EQ(read_back, data);
}

TEST_F(OnionRingE2ETest, OverwriteBlockReturnsLatestValue) {
    OnionRingClient client("127.0.0.1", port_, config_);
    std::vector<uint8_t> first(config_.block_size, 0x11);
    std::vector<uint8_t> second(config_.block_size, 0x22);

    client.Write(1, first);
    client.Write(1, second);
    auto read_back = client.Read(1);

    EXPECT_EQ(read_back, second);
}

TEST_F(OnionRingE2ETest, MultipleWritesAndReadsWorkBeforeEvictionThreshold) {
    OnionRingClient client("127.0.0.1", port_, config_);

    std::vector<uint8_t> a(config_.block_size, 0xA1);
    std::vector<uint8_t> b(config_.block_size, 0xB2);

    client.Write(0, a);
    client.Write(5, b);

    EXPECT_EQ(client.Read(0), a);
    EXPECT_EQ(client.Read(5), b);
}

}  // namespace
}  // namespace oram::onion_ring
