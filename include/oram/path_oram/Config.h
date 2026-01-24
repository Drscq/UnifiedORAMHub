#pragma once

#include <cstddef>
#include <cstdint>

namespace oram::path_oram {

// Path ORAM Configuration Parameters
struct Config {
    // Bucket capacity (number of blocks per bucket)
    static constexpr size_t kBucketSize = 4;

    // Block size in bytes
    static constexpr size_t kBlockSize = 256;

    // Tree height (will be set based on capacity)
    // For N blocks, height L = ceil(log2(N))
    static constexpr size_t kDefaultTreeHeight = 10;  // Supports up to 1024 blocks

    // Total number of blocks (capacity)
    static constexpr size_t kDefaultNumBlocks = 1024;

    // Calculate number of tree nodes
    // Binary tree with height L has 2^(L+1) - 1 nodes
    static constexpr size_t GetNumTreeNodes(size_t height) { return (1ULL << (height + 1)) - 1; }

    // Calculate number of leaf nodes
    static constexpr size_t GetNumLeaves(size_t height) { return 1ULL << height; }

    // Get leaf index offset (first leaf in flat array representation)
    static constexpr size_t GetLeafOffset(size_t height) { return (1ULL << height) - 1; }
};

}  // namespace oram::path_oram
