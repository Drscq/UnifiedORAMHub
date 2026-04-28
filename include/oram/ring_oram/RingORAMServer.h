#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "oram/ring_oram/Config.h"
#include "oram/ring_oram/RingBucket.h"

namespace oram::ring_oram {

class RingORAMServer {
   public:
    explicit RingORAMServer(const RuntimeConfig& config, uint64_t seed = 0x52696E67ULL);

    void Init();

    const RuntimeConfig& Config() const { return config_; }
    size_t NumNodes() const { return tree_.size(); }
    size_t NumLeaves() const { return config_.NumLeaves(); }

    RingBucket& GetBucket(size_t bucket_idx);
    const RingBucket& GetBucket(size_t bucket_idx) const;

    std::vector<size_t> GetPathIndices(uint64_t leaf) const;
    size_t GetBucketIndex(uint64_t leaf, size_t level) const;
    size_t GetNodeLevel(size_t bucket_idx) const;
    bool IsBucketOnLeafPath(size_t bucket_idx, uint64_t leaf) const;

   private:
    void ValidateConfig() const;

    RuntimeConfig config_;
    std::vector<RingBucket> tree_;
    std::mt19937_64 prng_;
};

}  // namespace oram::ring_oram
