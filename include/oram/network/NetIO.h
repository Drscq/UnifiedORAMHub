#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "oram/network/IOChannel.h"

namespace oram::network {

class NetIO : public IOChannel<NetIO> {
   public:
    // is_server: true if this instance acts as server (binds/listens), false if client (connects)
    NetIO(const std::string& address, int port, bool is_server, bool quiet = false);
    ~NetIO();

    void Sync();
    void Flush();
    void SetNoDelay();
    void SetDelay();

    // Internal methods used by IOChannel (CRTP)
    void SendDataInternal(const void* data, size_t len);
    void RecvDataInternal(void* data, size_t len);

   private:
    bool is_server_;
    int mysocket_ = -1;
    int consocket_ = -1;
    FILE* stream_ = nullptr;
    char* buffer_ = nullptr;
    bool has_sent_ = false;

    // Constants
    static constexpr int kNetworkBufferSize = 65536;
};

}  // namespace oram::network
