#pragma once

#include <memory>
#include <string>
#include <vector>

#include "oram/network/NetIO.h"
#include "oram/onion_ring/Config.h"
#include "oram/onion_ring/OnionBucket.h"
#include "oram/onion_ring/TFHEAdapter.h"

namespace oram::onion_ring {

class OnionRingServer {
   public:
    OnionRingServer(const std::string& address, int port, const RuntimeConfig& config);
    ~OnionRingServer();

    void Init();
    void HandleRequests();
    void Stop();

   private:
    RuntimeConfig config_;
    size_t num_nodes_;
    std::vector<OnionBucket> tree_;
    TFHEContext ctx_;
    std::unique_ptr<network::NetIO> net_io_;
    bool running_ = false;

    std::vector<size_t> GetPathIndices(uint64_t leaf) const;
    void HandleAccess();
    void HandleWriteSlot();
};

}  // namespace oram::onion_ring
