#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

namespace oram::ring_oram {

constexpr uint64_t kDummyAddress = std::numeric_limits<uint64_t>::max();

struct RingBlock {
    uint64_t addr = kDummyAddress;
    uint64_t leaf = 0;
    std::vector<uint8_t> data;

    bool IsDummy() const { return addr == kDummyAddress; }

    static RingBlock Dummy(size_t block_size);
};

class RingBucket {
   public:
    RingBucket();
    RingBucket(size_t z, size_t s, size_t block_size);

    size_t Z() const { return z_; }
    size_t S() const { return s_; }
    size_t SlotCount() const { return z_ + s_; }
    size_t BlockSize() const { return block_size_; }

    void ResetWithBlocks(const std::vector<RingBlock>& blocks, std::mt19937_64* prng);

    size_t count = 0;
    std::vector<bool> valids;
    std::vector<uint64_t> addrs;
    std::vector<uint64_t> leaves;
    std::vector<size_t> ptrs;
    std::vector<RingBlock> data;

   private:
    size_t z_ = 0;
    size_t s_ = 0;
    size_t block_size_ = 0;
};

}  // namespace oram::ring_oram
