#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "oram/onion_ring/TFHEAdapter.h"
#include "oram/onion_ring/WaksmanNetwork.h"

namespace oram::onion_ring {
namespace {

std::vector<size_t> ApplySwapBits(const std::vector<size_t>& input,
                                  const std::vector<WaksmanGate>& gates,
                                  const std::vector<bool>& swap_bits) {
    std::vector<size_t> output = input;
    for (const auto& gate : gates) {
        if (swap_bits[gate.bit_idx]) {
            std::swap(output[gate.i], output[gate.j]);
        }
    }
    return output;
}

std::vector<uint8_t> BlockByte(uint8_t value, size_t block_size) {
    return std::vector<uint8_t>(block_size, value);
}

TEST(WaksmanTest, GenerateSwapBitsPermutesFourElements) {
    WaksmanNetwork network(4);
    std::vector<size_t> permutation = {2, 0, 3, 1};
    std::vector<size_t> input = {0, 1, 2, 3};

    auto swap_bits = network.GenerateSwapBits(permutation);
    auto output = ApplySwapBits(input, network.Gates(), swap_bits);

    EXPECT_EQ(output, permutation);
}

TEST(WaksmanTest, GenerateSwapBitsPermutesEightElements) {
    WaksmanNetwork network(8);
    std::vector<size_t> permutation = {5, 2, 7, 1, 6, 0, 4, 3};
    std::vector<size_t> input = {0, 1, 2, 3, 4, 5, 6, 7};

    auto swap_bits = network.GenerateSwapBits(permutation);
    auto output = ApplySwapBits(input, network.Gates(), swap_bits);

    EXPECT_EQ(output, permutation);
}

TEST(WaksmanTest, EvalWaksmanMatchesPermutationForEncryptedBlocks) {
    RuntimeConfig cfg;
    cfg.block_size = 16;
    auto ctx = TFHEContext::CreateClientContext(cfg);

    WaksmanNetwork network(4);
    std::vector<size_t> permutation = {2, 0, 3, 1};
    auto swap_bits_plain = network.GenerateSwapBits(permutation);

    std::vector<RLWECiphertext> encrypted_blocks;
    encrypted_blocks.emplace_back(EncryptBlock(BlockByte(0x01, cfg.block_size), ctx));
    encrypted_blocks.emplace_back(EncryptBlock(BlockByte(0x02, cfg.block_size), ctx));
    encrypted_blocks.emplace_back(EncryptBlock(BlockByte(0x03, cfg.block_size), ctx));
    encrypted_blocks.emplace_back(EncryptBlock(BlockByte(0x04, cfg.block_size), ctx));

    std::vector<RGSWCiphertext> encrypted_swap_bits;
    encrypted_swap_bits.reserve(swap_bits_plain.size());
    for (bool bit : swap_bits_plain) {
        encrypted_swap_bits.emplace_back(EncryptBit(bit, ctx));
    }

    std::vector<TLweSample*> block_ptrs;
    block_ptrs.reserve(encrypted_blocks.size());
    for (auto& block : encrypted_blocks) {
        block_ptrs.push_back(block.Get());
    }

    std::vector<TGswSample*> swap_ptrs;
    swap_ptrs.reserve(encrypted_swap_bits.size());
    for (auto& bit : encrypted_swap_bits) {
        swap_ptrs.push_back(bit.Get());
    }

    WaksmanNetwork::EvalWaksman(block_ptrs, swap_ptrs, ctx.tgsw_params);

    EXPECT_EQ(DecryptBlock(encrypted_blocks[0], ctx, cfg.block_size), BlockByte(0x03, cfg.block_size));
    EXPECT_EQ(DecryptBlock(encrypted_blocks[1], ctx, cfg.block_size), BlockByte(0x01, cfg.block_size));
    EXPECT_EQ(DecryptBlock(encrypted_blocks[2], ctx, cfg.block_size), BlockByte(0x04, cfg.block_size));
    EXPECT_EQ(DecryptBlock(encrypted_blocks[3], ctx, cfg.block_size), BlockByte(0x02, cfg.block_size));
}

}  // namespace
}  // namespace oram::onion_ring
