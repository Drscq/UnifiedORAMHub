#include "oram/ring_oram/RingORAMServer.h"

#include <stdexcept>

namespace oram::ring_oram {

RingORAMServer::RingORAMServer(const RuntimeConfig& config, uint64_t seed)
    : config_(config), prng_(seed) {
    ValidateConfig();
    Init();
}

void RingORAMServer::ValidateConfig() const {
    if (config_.num_blocks == 0) {
        throw std::invalid_argument("Ring ORAM requires at least one block");
    }
    if (config_.tree_depth >= 63) {
        throw std::invalid_argument("Ring ORAM tree depth is too large");
    }
    if (config_.z == 0) {
        throw std::invalid_argument("Ring ORAM bucket capacity Z must be positive");
    }
    if (config_.s == 0) {
        throw std::invalid_argument("Ring ORAM dummy slot count S must be positive");
    }
    if (config_.a == 0) {
        throw std::invalid_argument("Ring ORAM eviction rate A must be positive");
    }
    if (config_.block_size == 0) {
        throw std::invalid_argument("Ring ORAM block size must be positive");
    }
}

void RingORAMServer::Init() {
    tree_.clear();
    tree_.reserve(config_.NumTreeNodes());
    for (size_t i = 0; i < config_.NumTreeNodes(); ++i) {
        tree_.emplace_back(config_.z, config_.s, config_.block_size);
        tree_.back().ResetWithBlocks({}, &prng_);
    }
}

RingBucket& RingORAMServer::GetBucket(size_t bucket_idx) {
    if (bucket_idx >= tree_.size()) {
        throw std::out_of_range("Ring ORAM bucket index out of range");
    }
    return tree_[bucket_idx];
}

const RingBucket& RingORAMServer::GetBucket(size_t bucket_idx) const {
    if (bucket_idx >= tree_.size()) {
        throw std::out_of_range("Ring ORAM bucket index out of range");
    }
    return tree_[bucket_idx];
}

std::vector<size_t> RingORAMServer::GetPathIndices(uint64_t leaf) const {
    if (leaf >= config_.NumLeaves()) {
        throw std::out_of_range("Ring ORAM leaf index out of range");
    }

    std::vector<size_t> path;
    path.reserve(config_.tree_depth + 1);

    size_t node = 0;
    path.push_back(node);
    for (size_t level = 0; level < config_.tree_depth; ++level) {
        const size_t bit = (leaf >> (config_.tree_depth - 1 - level)) & 1ULL;
        node = 2 * node + 1 + bit;
        path.push_back(node);
    }
    return path;
}

size_t RingORAMServer::GetBucketIndex(uint64_t leaf, size_t level) const {
    if (leaf >= config_.NumLeaves()) {
        throw std::out_of_range("Ring ORAM leaf index out of range");
    }
    if (level > config_.tree_depth) {
        throw std::out_of_range("Ring ORAM level out of range");
    }

    size_t node = 0;
    for (size_t depth = 0; depth < level; ++depth) {
        const size_t bit = (leaf >> (config_.tree_depth - 1 - depth)) & 1ULL;
        node = 2 * node + 1 + bit;
    }
    return node;
}

size_t RingORAMServer::GetNodeLevel(size_t bucket_idx) const {
    if (bucket_idx >= tree_.size()) {
        throw std::out_of_range("Ring ORAM bucket index out of range");
    }

    size_t level = 0;
    while (bucket_idx > 0) {
        bucket_idx = (bucket_idx - 1) / 2;
        ++level;
    }
    return level;
}

bool RingORAMServer::IsBucketOnLeafPath(size_t bucket_idx, uint64_t leaf) const {
    const size_t level = GetNodeLevel(bucket_idx);
    return GetBucketIndex(leaf, level) == bucket_idx;
}

}  // namespace oram::ring_oram
