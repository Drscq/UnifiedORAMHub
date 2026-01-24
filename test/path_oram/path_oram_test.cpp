#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "oram/core/Block.h"
#include "oram/path_oram/PathORAMClient.h"
#include "oram/path_oram/PathORAMServer.h"

using namespace oram;

class PathORAMTest : public ::testing::Test {
   protected:
    static constexpr int kPort = 54321;
    static constexpr size_t kTreeHeight = 4;  // Small tree for testing (16 leaves)
    static constexpr size_t kNumBlocks = 16;

    void SetUp() override {
        // Start server in a thread
        server_thread_ = std::thread([]() {
            path_oram::PathORAMServer server("127.0.0.1", kPort, kTreeHeight);

            // Initialize with some real blocks
            std::vector<core::Block> initial_blocks;
            for (size_t i = 0; i < 8; ++i) {
                std::vector<uint8_t> data(path_oram::Config::kBlockSize);
                for (size_t j = 0; j < data.size(); ++j) {
                    data[j] = static_cast<uint8_t>(i + j);
                }
                initial_blocks.emplace_back(i, data);
            }

            server.Init(initial_blocks);
            server.HandleRequests();
        });

        // Give server time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    void TearDown() override {
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    std::thread server_thread_;
};

TEST_F(PathORAMTest, BasicReadWrite) {
    path_oram::PathORAMClient client("127.0.0.1", kPort, kTreeHeight, kNumBlocks);

    // Write some data
    std::vector<uint8_t> write_data(path_oram::Config::kBlockSize);
    for (size_t i = 0; i < write_data.size(); ++i) {
        write_data[i] = static_cast<uint8_t>(42 + i);
    }

    client.Write(10, write_data);

    // Read it back
    auto read_data = client.Read(10);

    EXPECT_EQ(read_data, write_data);
}

TEST_F(PathORAMTest, MultipleAccesses) {
    path_oram::PathORAMClient client("127.0.0.1", kPort, kTreeHeight, kNumBlocks);

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
    path_oram::PathORAMClient client("127.0.0.1", kPort, kTreeHeight, kNumBlocks);

    uint64_t addr = 3;

    // Write initial data
    std::vector<uint8_t> data1(path_oram::Config::kBlockSize, 0xAA);
    client.Write(addr, data1);

    // Overwrite with new data
    std::vector<uint8_t> data2(path_oram::Config::kBlockSize, 0xBB);
    client.Write(addr, data2);

    // Read should return latest data
    auto result = client.Read(addr);
    EXPECT_EQ(result, data2);
}
