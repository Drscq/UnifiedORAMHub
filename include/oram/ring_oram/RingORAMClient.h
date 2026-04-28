#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

#include "oram/core/RAM.h"
#include "oram/ring_oram/Config.h"
#include "oram/ring_oram/RingBucket.h"
#include "oram/ring_oram/RingORAMServer.h"

namespace oram::ring_oram {

class RingORAMClient : public core::RAM {
   public:
    RingORAMClient(RingORAMServer& server, const RuntimeConfig& config,
                   uint64_t seed = 0x434C49454E54ULL);

    std::vector<uint8_t> Access(core::Op op, uint64_t addr,
                                const std::vector<uint8_t>& data) override;
    std::vector<uint8_t> Read(uint64_t addr) override;
    void Write(uint64_t addr, const std::vector<uint8_t>& data) override;

    std::optional<std::vector<uint8_t>> ReadPath(uint64_t leaf, uint64_t addr);
    void EvictPath();
    void EarlyReshuffle(uint64_t leaf);

    size_t GetBlockOffset(const RingBucket& bucket, uint64_t addr);
    void ReadBucket(RingBucket& bucket);
    void WriteBucket(size_t bucket_idx, RingBucket& bucket);

    bool CanResideInBucket(uint64_t block_leaf, size_t bucket_idx) const;

    const std::unordered_map<uint64_t, uint64_t>& PositionMap() const { return position_map_; }
    const std::vector<RingBlock>& Stash() const { return stash_; }
    size_t Round() const { return round_; }
    uint64_t EvictionCounter() const { return eviction_counter_; }

   private:
    uint64_t GetRandomLeaf();
    void ValidateAddress(uint64_t addr) const;
    void PutBlockInStash(const RingBlock& block);
    std::optional<RingBlock> TakeBlockFromStash(uint64_t addr);
    void RemoveBlockFromStash(uint64_t addr);
    std::vector<size_t> ValidRealOffsets(const RingBucket& bucket) const;
    std::vector<size_t> ValidDummyOffsets(const RingBucket& bucket) const;

    RingORAMServer& server_;
    RuntimeConfig config_;
    std::unordered_map<uint64_t, uint64_t> position_map_;
    std::vector<RingBlock> stash_;
    size_t round_ = 0;
    uint64_t eviction_counter_ = 0;
    std::mt19937_64 prng_;
};

}  // namespace oram::ring_oram
