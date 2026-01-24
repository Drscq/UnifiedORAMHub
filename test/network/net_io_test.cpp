#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

#include "oram/network/NetIO.h"

// Test basic send/recv
TEST(NetIOTest, SendRecv) {
    int port = 12345;
    std::string address = "127.0.0.1";
    std::vector<uint8_t> data_to_send = {0x01, 0x02, 0x03, 0x04};

    // Server thread
    std::thread server_thread([&]() {
        oram::network::NetIO server(address, port, true, true);  // true = server, true = quiet
        std::vector<uint8_t> received_data(4);
        server.RecvData(received_data.data(), 4);

        // Check data inside thread usually requires care/promises,
        // but for simplicity we'll just echo it back or rely on client check
        // if we assume full duplex. Let's just verify on client side for now.
        // Or better: Server sends back.
        server.SendData(received_data.data(), 4);
    });

    // Client (Main thread)
    // Give server a moment to bind? NetIO retry logic in client handles connection
    // but bind needs to happen before connect succeeds.
    // However, NetIO client retries connect loop.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    oram::network::NetIO client(address, port, false, true);  // false = client
    client.SendData(data_to_send.data(), 4);

    std::vector<uint8_t> echo_data(4);
    client.RecvData(echo_data.data(), 4);

    EXPECT_EQ(data_to_send, echo_data);

    server_thread.join();
}

TEST(NetIOTest, Sync) {
    int port = 12346;
    std::string address = "127.0.0.1";

    std::thread server_thread([&]() {
        oram::network::NetIO server(address, port, true, true);
        server.Sync();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    oram::network::NetIO client(address, port, false, true);
    client.Sync();

    server_thread.join();
    // If we reach here without hanging/crashing, Sync worked
    SUCCEED();
}
