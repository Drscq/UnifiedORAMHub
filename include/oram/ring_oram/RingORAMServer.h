#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "oram/network/NetIO.h"
#include "oram/ring_oram/Config.h"
#include "oram/ring_oram/RingBucket.h"

namespace oram::ring_oram {

class RingORAMServer {
   public:
    RingORAMServer(const std::string& address, int port, const RuntimeConfig& config);
    ~RingORAMServer();

    void HandleRequests();
    void Stop();

    const RuntimeConfig& Config() const { return config_; }
    size_t NumNodes() const { return tree_.size(); }
    size_t NumLeaves() const { return config_.NumLeaves(); }

    std::vector<size_t> GetPathIndices(uint64_t leaf) const;
    size_t GetBucketIndex(uint64_t leaf, size_t level) const;
    size_t GetNodeLevel(size_t bucket_idx) const;
    bool IsBucketOnLeafPath(size_t bucket_idx, uint64_t leaf) const;

    uint64_t XorPathReadCount() const { return xor_path_read_count_; }
    size_t LastXorPathSlotCount() const { return last_xor_path_slot_count_; }

   private:
    void ValidateConfig() const;
    void ValidateInitialized() const;
    const EncryptedRingBucket& GetBucket(size_t bucket_idx) const;
    EncryptedRingBucket& GetBucket(size_t bucket_idx);

    void HandleInit();
    void HandleReadPathMetadata();
    void HandleXorPathSlots();
    void HandleReadBucket();
    void HandleWriteBucket();
    void HandleReadBucketCount();
    void HandleReadStats();

    RuntimeConfig config_;
    std::vector<EncryptedRingBucket> tree_;
    std::unique_ptr<network::NetIO> net_io_;
    bool running_ = false;
    uint64_t xor_path_read_count_ = 0;
    size_t last_xor_path_slot_count_ = 0;
};

}  // namespace oram::ring_oram
