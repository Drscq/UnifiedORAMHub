#pragma once

#include <cstddef>

namespace oram::ring_oram {

struct RuntimeConfig {
    size_t num_blocks = 1024;
    size_t tree_depth = 10;
    size_t z = 4;
    size_t s = 4;
    size_t a = 8;
    size_t block_size = 256;

    size_t BucketSlots() const { return z + s; }
    size_t NumLeaves() const { return 1ULL << tree_depth; }
    size_t NumTreeNodes() const { return (1ULL << (tree_depth + 1)) - 1; }
    size_t LeafOffset() const { return (1ULL << tree_depth) - 1; }
};

}  // namespace oram::ring_oram
