#include "oram/ring_oram/RingBucket.h"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace oram::ring_oram {

namespace {

std::vector<size_t> MakePermutation(size_t size, std::mt19937_64* prng) {
    std::vector<size_t> permutation(size, 0);
    std::iota(permutation.begin(), permutation.end(), 0);
    if (prng != nullptr) {
        std::shuffle(permutation.begin(), permutation.end(), *prng);
    }
    return permutation;
}

RingBlock NormalizeBlock(RingBlock block, size_t block_size) {
    block.data.resize(block_size, 0);
    return block;
}

RingBlock MakeDummyBlock(size_t block_size, std::mt19937_64* prng) {
    RingBlock block = RingBlock::Dummy(block_size);
    if (prng == nullptr) {
        return block;
    }

    std::uniform_int_distribution<int> byte_dist(0, 255);
    for (auto& byte : block.data) {
        byte = static_cast<uint8_t>(byte_dist(*prng));
    }
    return block;
}

}  // namespace

RingBlock RingBlock::Dummy(size_t block_size) {
    return RingBlock{kDummyAddress, 0, std::vector<uint8_t>(block_size, 0)};
}

RingBucket::RingBucket() = default;

RingBucket::RingBucket(size_t z, size_t s, size_t block_size)
    : count(0),
      valids(z + s, true),
      addrs(z, kDummyAddress),
      leaves(z, 0),
      ptrs(z, 0),
      data(z + s, RingBlock::Dummy(block_size)),
      z_(z),
      s_(s),
      block_size_(block_size) {
    for (size_t i = 0; i < ptrs.size(); ++i) {
        ptrs[i] = i;
    }
}

void RingBucket::ResetWithBlocks(const std::vector<RingBlock>& blocks, std::mt19937_64* prng) {
    if (blocks.size() > z_) {
        throw std::invalid_argument("Too many real blocks for Ring ORAM bucket");
    }
    for (const auto& block : blocks) {
        if (block.IsDummy()) {
            throw std::invalid_argument("Ring ORAM bucket real block list contains a dummy");
        }
    }

    count = 0;
    std::fill(valids.begin(), valids.end(), true);
    std::fill(addrs.begin(), addrs.end(), kDummyAddress);
    std::fill(leaves.begin(), leaves.end(), 0);
    data.clear();
    data.reserve(SlotCount());
    for (size_t i = 0; i < SlotCount(); ++i) {
        data.push_back(MakeDummyBlock(block_size_, prng));
    }

    const auto permutation = MakePermutation(SlotCount(), prng);
    for (size_t i = 0; i < ptrs.size(); ++i) {
        ptrs[i] = permutation[i];
    }

    for (size_t i = 0; i < blocks.size(); ++i) {
        const size_t offset = ptrs[i];
        RingBlock block = NormalizeBlock(blocks[i], block_size_);
        addrs[i] = block.addr;
        leaves[i] = block.leaf;
        data[offset] = std::move(block);
    }
}

}  // namespace oram::ring_oram
