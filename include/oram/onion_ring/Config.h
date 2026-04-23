#pragma once

#include <cstddef>
#include <cstdint>

namespace oram::onion_ring {

struct RuntimeConfig {
    size_t z = 254;
    size_t s = 254;
    size_t a = 249;
    size_t tree_height = 10;
    size_t num_blocks = 1024;
    size_t block_size = 256;
    bool use_recursive_packed_swap_bits = false;

    int32_t tlwe_n = 1024;
    int32_t tlwe_k = 1;
    int32_t tgsw_l = 3;
    int32_t tgsw_bgbit = 7;
    int32_t recursive_tlwe_n = 2048;
    int32_t swap_tgsw_l = 8;
    int32_t swap_tgsw_bgbit = 3;
    int32_t neg_sk_tgsw_l = 7;
    int32_t neg_sk_tgsw_bgbit = 7;
    int32_t practical_tgsw_l = 3;
    int32_t practical_tgsw_bgbit = 7;
    int32_t rlwe_ks_basebit = 3;
    int32_t rlwe_ks_length = 10;
    double alpha = 1e-12;

    size_t BucketSlots() const { return z + s; }
    size_t NumTreeNodes() const { return (1ULL << (tree_height + 1)) - 1; }
    size_t NumLeaves() const { return 1ULL << tree_height; }
    size_t LeafOffset() const { return (1ULL << tree_height) - 1; }
};

}  // namespace oram::onion_ring
