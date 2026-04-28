#include "oram/ring_oram/RingORAMClient.h"

#include <algorithm>
#include <stdexcept>

namespace oram::ring_oram {

RingORAMClient::RingORAMClient(RingORAMServer& server, const RuntimeConfig& config, uint64_t seed)
    : server_(server), config_(config), prng_(seed) {
    if (server_.Config().num_blocks != config_.num_blocks ||
        server_.Config().tree_depth != config_.tree_depth || server_.Config().z != config_.z ||
        server_.Config().s != config_.s || server_.Config().a != config_.a ||
        server_.Config().block_size != config_.block_size) {
        throw std::invalid_argument("Ring ORAM client/server configuration mismatch");
    }

    for (uint64_t addr = 0; addr < config_.num_blocks; ++addr) {
        position_map_.emplace(addr, GetRandomLeaf());
    }
}

uint64_t RingORAMClient::GetRandomLeaf() {
    std::uniform_int_distribution<uint64_t> dist(0, server_.NumLeaves() - 1);
    return dist(prng_);
}

void RingORAMClient::ValidateAddress(uint64_t addr) const {
    if (addr >= config_.num_blocks) {
        throw std::out_of_range("Ring ORAM address out of range");
    }
}

std::vector<uint8_t> RingORAMClient::Access(core::Op op, uint64_t addr,
                                            const std::vector<uint8_t>& data) {
    ValidateAddress(addr);
    if (op == core::Op::WRITE && data.size() != config_.block_size) {
        throw std::invalid_argument("Ring ORAM write size does not match block size");
    }

    const uint64_t old_leaf = position_map_.at(addr);
    const uint64_t new_leaf = GetRandomLeaf();
    position_map_[addr] = new_leaf;

    const auto path_data = ReadPath(old_leaf, addr);
    RingBlock block;
    if (path_data.has_value()) {
        block = RingBlock{addr, new_leaf, *path_data};
        RemoveBlockFromStash(addr);
    } else {
        auto stashed = TakeBlockFromStash(addr);
        if (stashed.has_value()) {
            block = *stashed;
            block.leaf = new_leaf;
        } else {
            block = RingBlock{addr, new_leaf, std::vector<uint8_t>(config_.block_size, 0)};
        }
    }

    if (op == core::Op::WRITE) {
        block.data = data;
    }
    block.addr = addr;
    block.leaf = new_leaf;
    PutBlockInStash(block);

    round_ = (round_ + 1) % config_.a;
    if (round_ == 0) {
        EvictPath();
    }
    EarlyReshuffle(old_leaf);

    return block.data;
}

std::vector<uint8_t> RingORAMClient::Read(uint64_t addr) { return Access(core::Op::READ, addr, {}); }

void RingORAMClient::Write(uint64_t addr, const std::vector<uint8_t>& data) {
    Access(core::Op::WRITE, addr, data);
}

std::optional<std::vector<uint8_t>> RingORAMClient::ReadPath(uint64_t leaf, uint64_t addr) {
    auto path = server_.GetPathIndices(leaf);
    std::optional<std::vector<uint8_t>> result;

    for (size_t bucket_idx : path) {
        RingBucket& bucket = server_.GetBucket(bucket_idx);
        const size_t offset = GetBlockOffset(bucket, addr);
        if (offset >= bucket.valids.size()) {
            throw std::out_of_range("Ring ORAM selected slot out of range");
        }

        RingBlock& selected = bucket.data[offset];
        if (bucket.valids[offset] && !selected.IsDummy() && selected.addr == addr) {
            result = selected.data;
        }
        bucket.valids[offset] = false;
        ++bucket.count;
    }

    return result;
}

void RingORAMClient::EvictPath() {
    const uint64_t leaf = eviction_counter_ % server_.NumLeaves();
    ++eviction_counter_;

    const auto path = server_.GetPathIndices(leaf);
    for (size_t bucket_idx : path) {
        ReadBucket(server_.GetBucket(bucket_idx));
    }
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        WriteBucket(*it, server_.GetBucket(*it));
    }
    for (size_t bucket_idx : path) {
        server_.GetBucket(bucket_idx).count = 0;
    }
}

void RingORAMClient::EarlyReshuffle(uint64_t leaf) {
    const auto path = server_.GetPathIndices(leaf);
    for (size_t bucket_idx : path) {
        RingBucket& bucket = server_.GetBucket(bucket_idx);
        if (bucket.count >= config_.s) {
            ReadBucket(bucket);
            WriteBucket(bucket_idx, bucket);
            bucket.count = 0;
        }
    }
}

size_t RingORAMClient::GetBlockOffset(const RingBucket& bucket, uint64_t addr) {
    for (size_t i = 0; i < bucket.addrs.size(); ++i) {
        const size_t offset = bucket.ptrs[i];
        if (bucket.addrs[i] == addr && offset < bucket.valids.size() && bucket.valids[offset]) {
            return offset;
        }
    }

    const auto dummies = ValidDummyOffsets(bucket);
    if (dummies.empty()) {
        throw std::runtime_error("Ring ORAM bucket has no valid dummy slot");
    }

    std::uniform_int_distribution<size_t> dist(0, dummies.size() - 1);
    return dummies[dist(prng_)];
}

void RingORAMClient::ReadBucket(RingBucket& bucket) {
    size_t reads = 0;
    for (size_t i = 0; i < bucket.addrs.size(); ++i) {
        const size_t offset = bucket.ptrs[i];
        if (bucket.addrs[i] == kDummyAddress || offset >= bucket.valids.size() ||
            !bucket.valids[offset]) {
            continue;
        }

        RingBlock block = bucket.data[offset];
        block.addr = bucket.addrs[i];
        block.leaf = bucket.leaves[i];
        block.data.resize(config_.block_size, 0);
        PutBlockInStash(block);

        bucket.valids[offset] = false;
        ++reads;
    }

    while (reads < config_.z) {
        const auto dummies = ValidDummyOffsets(bucket);
        if (dummies.empty()) {
            throw std::runtime_error("Ring ORAM bucket lacks dummy padding for a full read");
        }

        std::uniform_int_distribution<size_t> dist(0, dummies.size() - 1);
        bucket.valids[dummies[dist(prng_)]] = false;
        ++reads;
    }
}

void RingORAMClient::WriteBucket(size_t bucket_idx, RingBucket& bucket) {
    std::vector<RingBlock> selected;
    selected.reserve(config_.z);

    auto it = stash_.begin();
    while (it != stash_.end() && selected.size() < config_.z) {
        if (!it->IsDummy() && CanResideInBucket(it->leaf, bucket_idx)) {
            selected.push_back(*it);
            it = stash_.erase(it);
        } else {
            ++it;
        }
    }

    bucket.ResetWithBlocks(selected, &prng_);
}

bool RingORAMClient::CanResideInBucket(uint64_t block_leaf, size_t bucket_idx) const {
    return server_.IsBucketOnLeafPath(bucket_idx, block_leaf);
}

void RingORAMClient::PutBlockInStash(const RingBlock& block) {
    if (block.IsDummy()) {
        return;
    }
    RemoveBlockFromStash(block.addr);
    stash_.push_back(block);
}

std::optional<RingBlock> RingORAMClient::TakeBlockFromStash(uint64_t addr) {
    for (auto it = stash_.begin(); it != stash_.end(); ++it) {
        if (it->addr == addr) {
            RingBlock block = *it;
            stash_.erase(it);
            return block;
        }
    }
    return std::nullopt;
}

void RingORAMClient::RemoveBlockFromStash(uint64_t addr) {
    stash_.erase(std::remove_if(stash_.begin(), stash_.end(),
                                [addr](const RingBlock& block) { return block.addr == addr; }),
                 stash_.end());
}

std::vector<size_t> RingORAMClient::ValidRealOffsets(const RingBucket& bucket) const {
    std::vector<size_t> offsets;
    offsets.reserve(bucket.Z());
    for (size_t i = 0; i < bucket.addrs.size(); ++i) {
        const size_t offset = bucket.ptrs[i];
        if (bucket.addrs[i] != kDummyAddress && offset < bucket.valids.size() &&
            bucket.valids[offset]) {
            offsets.push_back(offset);
        }
    }
    return offsets;
}

std::vector<size_t> RingORAMClient::ValidDummyOffsets(const RingBucket& bucket) const {
    std::vector<bool> is_real(bucket.valids.size(), false);
    for (size_t offset : ValidRealOffsets(bucket)) {
        is_real[offset] = true;
    }

    std::vector<size_t> offsets;
    offsets.reserve(bucket.valids.size());
    for (size_t offset = 0; offset < bucket.valids.size(); ++offset) {
        if (bucket.valids[offset] && !is_real[offset]) {
            offsets.push_back(offset);
        }
    }
    return offsets;
}

}  // namespace oram::ring_oram
