#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "oram/network/NetIO.h"
#include "oram/onion_ring/OnionBucket.h"
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

std::vector<uint8_t> PatternBlock(uint8_t seed, size_t block_size) {
    std::vector<uint8_t> block(block_size, 0);
    for (size_t i = 0; i < block.size(); ++i) {
        block[i] = static_cast<uint8_t>(seed + i);
    }
    return block;
}

std::vector<uint8_t> SerializeUint64Vector(const std::vector<uint64_t>& values) {
    std::vector<uint8_t> bytes(values.size() * sizeof(uint64_t));
    if (!bytes.empty()) {
        std::memcpy(bytes.data(), values.data(), bytes.size());
    }
    return bytes;
}

std::vector<uint64_t> DeserializeUint64Vector(const std::vector<uint8_t>& bytes) {
    std::vector<uint64_t> values(bytes.size() / sizeof(uint64_t), 0);
    if (!bytes.empty()) {
        std::memcpy(values.data(), bytes.data(), bytes.size());
    }
    return values;
}

int NextPort() {
    static std::atomic<int> g_port_counter{56100};
    return g_port_counter.fetch_add(1, std::memory_order_relaxed);
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

TEST(WaksmanTest, RepeatedEvalWaksmanPreservesEncryptedBlocksAcrossRounds) {
    RuntimeConfig cfg;
    cfg.block_size = 16;
    auto ctx = TFHEContext::CreateClientContext(cfg);

    WaksmanNetwork network(8);
    std::vector<RLWECiphertext> encrypted_blocks;
    std::vector<uint8_t> expected_values;
    for (uint8_t value = 1; value <= 8; ++value) {
        encrypted_blocks.emplace_back(EncryptBlock(BlockByte(value, cfg.block_size), ctx));
        expected_values.push_back(value);
    }

    for (size_t round = 0; round < 4; ++round) {
        std::vector<size_t> permutation = {7, 0, 6, 1, 5, 2, 4, 3};
        auto swap_bits_plain = network.GenerateSwapBits(permutation);

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

        std::vector<uint8_t> next_expected(expected_values.size(), 0);
        for (size_t dest = 0; dest < permutation.size(); ++dest) {
            next_expected[dest] = expected_values[permutation[dest]];
        }
        expected_values = std::move(next_expected);

        for (size_t i = 0; i < expected_values.size(); ++i) {
            SCOPED_TRACE(round);
            EXPECT_EQ(DecryptBlock(encrypted_blocks[i], ctx, cfg.block_size),
                      BlockByte(expected_values[i], cfg.block_size));
        }
    }
}

TEST(WaksmanTest, RepeatedEvalWaksmanPreservesSparseBucketsAcrossRounds) {
    RuntimeConfig cfg;
    cfg.block_size = 32;
    auto ctx = TFHEContext::CreateClientContext(cfg);

    WaksmanNetwork network(8);
    std::vector<RLWECiphertext> encrypted_blocks;
    std::vector<std::vector<uint8_t>> expected_blocks;
    for (uint8_t value = 1; value <= 3; ++value) {
        encrypted_blocks.emplace_back(EncryptBlock(BlockByte(value * 0x11, cfg.block_size), ctx));
        expected_blocks.push_back(BlockByte(value * 0x11, cfg.block_size));
    }
    for (size_t i = 0; i < 5; ++i) {
        encrypted_blocks.emplace_back(EncryptBlock(BlockByte(0x00, cfg.block_size), ctx));
        expected_blocks.push_back(BlockByte(0x00, cfg.block_size));
    }

    for (size_t round = 0; round < 4; ++round) {
        std::vector<size_t> permutation = {5, 2, 7, 1, 6, 0, 4, 3};
        auto swap_bits_plain = network.GenerateSwapBits(permutation);

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

        std::vector<std::vector<uint8_t>> next_expected(expected_blocks.size());
        for (size_t dest = 0; dest < permutation.size(); ++dest) {
            next_expected[dest] = expected_blocks[permutation[dest]];
        }
        expected_blocks = std::move(next_expected);

        for (size_t i = 0; i < expected_blocks.size(); ++i) {
            SCOPED_TRACE(round);
            EXPECT_EQ(DecryptBlock(encrypted_blocks[i], ctx, cfg.block_size), expected_blocks[i]);
        }
    }
}

TEST(WaksmanTest, EvalWaksmanMatchesPermutationAfterSwapBitSerialization) {
    RuntimeConfig cfg;
    cfg.block_size = 32;
    auto ctx = TFHEContext::CreateClientContext(cfg);

    WaksmanNetwork network(8);
    std::vector<size_t> permutation = {5, 2, 7, 1, 6, 0, 4, 3};
    auto swap_bits_plain = network.GenerateSwapBits(permutation);

    std::vector<RLWECiphertext> encrypted_blocks;
    encrypted_blocks.emplace_back(EncryptBlock(BlockByte(0x11, cfg.block_size), ctx));
    encrypted_blocks.emplace_back(EncryptBlock(BlockByte(0x22, cfg.block_size), ctx));
    encrypted_blocks.emplace_back(EncryptBlock(BlockByte(0x33, cfg.block_size), ctx));
    for (size_t i = 0; i < 5; ++i) {
        encrypted_blocks.emplace_back(EncryptBlock(BlockByte(0x00, cfg.block_size), ctx));
    }

    std::vector<RGSWCiphertext> serialized_swap_bits;
    serialized_swap_bits.reserve(swap_bits_plain.size());
    for (bool bit : swap_bits_plain) {
        RGSWCiphertext original = EncryptBit(bit, ctx);
        serialized_swap_bits.emplace_back(
            RGSWCiphertext::Deserialize(original.Serialize(), ctx.tgsw_params));
    }

    std::vector<TLweSample*> block_ptrs;
    block_ptrs.reserve(encrypted_blocks.size());
    for (auto& block : encrypted_blocks) {
        block_ptrs.push_back(block.Get());
    }

    std::vector<TGswSample*> swap_ptrs;
    swap_ptrs.reserve(serialized_swap_bits.size());
    for (auto& bit : serialized_swap_bits) {
        swap_ptrs.push_back(bit.Get());
    }

    WaksmanNetwork::EvalWaksman(block_ptrs, swap_ptrs, ctx.tgsw_params);

    EXPECT_EQ(DecryptBlock(encrypted_blocks[0], ctx, cfg.block_size), BlockByte(0x00, cfg.block_size));
    EXPECT_EQ(DecryptBlock(encrypted_blocks[1], ctx, cfg.block_size), BlockByte(0x33, cfg.block_size));
    EXPECT_EQ(DecryptBlock(encrypted_blocks[2], ctx, cfg.block_size), BlockByte(0x00, cfg.block_size));
    EXPECT_EQ(DecryptBlock(encrypted_blocks[3], ctx, cfg.block_size), BlockByte(0x22, cfg.block_size));
    EXPECT_EQ(DecryptBlock(encrypted_blocks[4], ctx, cfg.block_size), BlockByte(0x00, cfg.block_size));
    EXPECT_EQ(DecryptBlock(encrypted_blocks[5], ctx, cfg.block_size), BlockByte(0x11, cfg.block_size));
    EXPECT_EQ(DecryptBlock(encrypted_blocks[6], ctx, cfg.block_size), BlockByte(0x00, cfg.block_size));
    EXPECT_EQ(DecryptBlock(encrypted_blocks[7], ctx, cfg.block_size), BlockByte(0x00, cfg.block_size));
}

TEST(WaksmanTest, RepeatedEvalWaksmanPreservesBucketsWithTrivialZeroPadding) {
    RuntimeConfig cfg;
    cfg.block_size = 32;
    auto ctx = TFHEContext::CreateClientContext(cfg);

    WaksmanNetwork network(8);
    std::vector<RLWECiphertext> encrypted_blocks;
    std::vector<std::vector<uint8_t>> expected_blocks;
    for (uint8_t value = 1; value <= 3; ++value) {
        encrypted_blocks.emplace_back(EncryptBlock(BlockByte(value * 0x11, cfg.block_size), ctx));
        expected_blocks.push_back(BlockByte(value * 0x11, cfg.block_size));
    }
    for (size_t i = 0; i < 5; ++i) {
        encrypted_blocks.emplace_back(ctx.tlwe_params);
        tLweClear(encrypted_blocks.back().Get(), ctx.tlwe_params);
        expected_blocks.push_back(BlockByte(0x00, cfg.block_size));
    }

    for (size_t round = 0; round < 4; ++round) {
        std::vector<size_t> permutation = {5, 2, 7, 1, 6, 0, 4, 3};
        auto swap_bits_plain = network.GenerateSwapBits(permutation);

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

        std::vector<std::vector<uint8_t>> next_expected(expected_blocks.size());
        for (size_t dest = 0; dest < permutation.size(); ++dest) {
            next_expected[dest] = expected_blocks[permutation[dest]];
        }
        expected_blocks = std::move(next_expected);

        for (size_t i = 0; i < expected_blocks.size(); ++i) {
            SCOPED_TRACE(round);
            EXPECT_EQ(DecryptBlock(encrypted_blocks[i], ctx, cfg.block_size), expected_blocks[i]);
        }
    }
}

TEST(WaksmanTest, EvalWaksmanPreservesPatternBlocksWithTrivialZeroPadding) {
    RuntimeConfig cfg;
    cfg.block_size = 32;
    auto ctx = TFHEContext::CreateClientContext(cfg);

    WaksmanNetwork network(8);
    std::vector<size_t> permutation = {6, 1, 3, 0, 7, 5, 2, 4};
    auto swap_bits_plain = network.GenerateSwapBits(permutation);

    std::vector<RLWECiphertext> encrypted_blocks;
    std::vector<std::vector<uint8_t>> expected_blocks;
    encrypted_blocks.emplace_back(EncryptBlock(PatternBlock(0x50, cfg.block_size), ctx));
    encrypted_blocks.emplace_back(EncryptBlock(PatternBlock(0x57, cfg.block_size), ctx));
    encrypted_blocks.emplace_back(EncryptBlock(PatternBlock(0x5E, cfg.block_size), ctx));
    expected_blocks.push_back(PatternBlock(0x50, cfg.block_size));
    expected_blocks.push_back(PatternBlock(0x57, cfg.block_size));
    expected_blocks.push_back(PatternBlock(0x5E, cfg.block_size));
    for (size_t i = 0; i < 5; ++i) {
        encrypted_blocks.emplace_back(ctx.tlwe_params);
        tLweClear(encrypted_blocks.back().Get(), ctx.tlwe_params);
        expected_blocks.push_back(BlockByte(0x00, cfg.block_size));
    }

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

    std::vector<std::vector<uint8_t>> next_expected(expected_blocks.size());
    for (size_t dest = 0; dest < permutation.size(); ++dest) {
        next_expected[dest] = expected_blocks[permutation[dest]];
    }

    for (size_t i = 0; i < next_expected.size(); ++i) {
        EXPECT_EQ(DecryptBlock(encrypted_blocks[i], ctx, cfg.block_size), next_expected[i]);
    }
}

TEST(WaksmanTest, EvalWaksmanPreservesTracedTripletCase) {
    RuntimeConfig cfg;
    cfg.block_size = 32;
    auto ctx = TFHEContext::CreateClientContext(cfg);

    WaksmanNetwork network(8);
    std::vector<size_t> permutation = {7, 3, 6, 2, 5, 1, 4, 0};
    auto swap_bits_plain = network.GenerateSwapBits(permutation);

    std::vector<RLWECiphertext> encrypted_blocks;
    std::vector<std::vector<uint8_t>> expected_blocks;
    encrypted_blocks.emplace_back(EncryptBlock(PatternBlock(0x50, cfg.block_size), ctx));
    encrypted_blocks.emplace_back(EncryptBlock(PatternBlock(0x57, cfg.block_size), ctx));
    encrypted_blocks.emplace_back(EncryptBlock(PatternBlock(0x5E, cfg.block_size), ctx));
    expected_blocks.push_back(PatternBlock(0x50, cfg.block_size));
    expected_blocks.push_back(PatternBlock(0x57, cfg.block_size));
    expected_blocks.push_back(PatternBlock(0x5E, cfg.block_size));
    for (size_t i = 0; i < 5; ++i) {
        encrypted_blocks.emplace_back(ctx.tlwe_params);
        tLweClear(encrypted_blocks.back().Get(), ctx.tlwe_params);
        expected_blocks.push_back(BlockByte(0x00, cfg.block_size));
    }

    std::vector<RGSWCiphertext> encrypted_swap_bits;
    encrypted_swap_bits.reserve(swap_bits_plain.size());
    for (bool bit : swap_bits_plain) {
        encrypted_swap_bits.emplace_back(EncryptBit(bit, ctx));
    }

    std::vector<TLweSample*> block_ptrs;
    for (auto& block : encrypted_blocks) {
        block_ptrs.push_back(block.Get());
    }
    std::vector<TGswSample*> swap_ptrs;
    for (auto& bit : encrypted_swap_bits) {
        swap_ptrs.push_back(bit.Get());
    }

    WaksmanNetwork::EvalWaksman(block_ptrs, swap_ptrs, ctx.tgsw_params);

    std::vector<std::vector<uint8_t>> next_expected(expected_blocks.size());
    for (size_t dest = 0; dest < permutation.size(); ++dest) {
        next_expected[dest] = expected_blocks[permutation[dest]];
    }
    for (size_t i = 0; i < next_expected.size(); ++i) {
        EXPECT_EQ(DecryptBlock(encrypted_blocks[i], ctx, cfg.block_size), next_expected[i]);
    }
}

TEST(WaksmanTest, EvalWaksmanPreservesTracedTripletCaseAcrossIndependentContexts) {
    RuntimeConfig cfg;
    cfg.block_size = 32;
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);

    WaksmanNetwork network(8);
    std::vector<size_t> permutation = {7, 3, 6, 2, 5, 1, 4, 0};
    auto swap_bits_plain = network.GenerateSwapBits(permutation);

    std::vector<RLWECiphertext> server_blocks;
    std::vector<std::vector<uint8_t>> expected_blocks;
    {
        RLWECiphertext block0 = EncryptBlock(PatternBlock(0x50, cfg.block_size), client_ctx);
        RLWECiphertext block1 = EncryptBlock(PatternBlock(0x57, cfg.block_size), client_ctx);
        RLWECiphertext block2 = EncryptBlock(PatternBlock(0x5E, cfg.block_size), client_ctx);
        server_blocks.emplace_back(
            RLWECiphertext::Deserialize(block0.Serialize(), server_ctx.tlwe_params));
        server_blocks.emplace_back(
            RLWECiphertext::Deserialize(block1.Serialize(), server_ctx.tlwe_params));
        server_blocks.emplace_back(
            RLWECiphertext::Deserialize(block2.Serialize(), server_ctx.tlwe_params));
        expected_blocks.push_back(PatternBlock(0x50, cfg.block_size));
        expected_blocks.push_back(PatternBlock(0x57, cfg.block_size));
        expected_blocks.push_back(PatternBlock(0x5E, cfg.block_size));
    }
    for (size_t i = 0; i < 5; ++i) {
        server_blocks.emplace_back(server_ctx.tlwe_params);
        tLweClear(server_blocks.back().Get(), server_ctx.tlwe_params);
        expected_blocks.push_back(BlockByte(0x00, cfg.block_size));
    }

    std::vector<RGSWCiphertext> server_swap_bits;
    for (bool bit : swap_bits_plain) {
        RGSWCiphertext client_bit = EncryptBit(bit, client_ctx);
        server_swap_bits.emplace_back(
            RGSWCiphertext::Deserialize(client_bit.Serialize(), server_ctx.tgsw_params));
    }

    std::vector<TLweSample*> block_ptrs;
    for (auto& block : server_blocks) {
        block_ptrs.push_back(block.Get());
    }
    std::vector<TGswSample*> swap_ptrs;
    for (auto& bit : server_swap_bits) {
        swap_ptrs.push_back(bit.Get());
    }

    WaksmanNetwork::EvalWaksman(block_ptrs, swap_ptrs, server_ctx.tgsw_params);

    std::vector<std::vector<uint8_t>> next_expected(expected_blocks.size());
    for (size_t dest = 0; dest < permutation.size(); ++dest) {
        next_expected[dest] = expected_blocks[permutation[dest]];
    }
    for (size_t i = 0; i < next_expected.size(); ++i) {
        EXPECT_EQ(DecryptBlock(server_blocks[i], client_ctx, cfg.block_size), next_expected[i]);
    }
}

TEST(WaksmanTest, TracedTripletPipelinePreservesDataThroughBucketCopies) {
    RuntimeConfig cfg;
    cfg.block_size = 32;
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);

    OnionBucket source_bucket(8, server_ctx.tlwe_params);
    OnionBucket child_bucket(8, server_ctx.tlwe_params);
    OnionBucket dest_bucket(8, server_ctx.tlwe_params);
    for (size_t slot = 0; slot < 8; ++slot) {
        tLweClear(source_bucket[slot].rlwe_ct.Get(), server_ctx.tlwe_params);
        tLweClear(child_bucket[slot].rlwe_ct.Get(), server_ctx.tlwe_params);
        tLweClear(dest_bucket[slot].rlwe_ct.Get(), server_ctx.tlwe_params);
    }

    RLWECiphertext block0 = EncryptBlock(PatternBlock(0x50, cfg.block_size), client_ctx);
    RLWECiphertext block1 = EncryptBlock(PatternBlock(0x57, cfg.block_size), client_ctx);
    RLWECiphertext block2 = EncryptBlock(PatternBlock(0x5E, cfg.block_size), client_ctx);
    source_bucket[0].rlwe_ct = RLWECiphertext::Deserialize(block0.Serialize(), server_ctx.tlwe_params);
    source_bucket[1].rlwe_ct = RLWECiphertext::Deserialize(block1.Serialize(), server_ctx.tlwe_params);
    source_bucket[2].rlwe_ct = RLWECiphertext::Deserialize(block2.Serialize(), server_ctx.tlwe_params);

    std::vector<RLWECiphertext> assembled;
    assembled.reserve(8);
    for (size_t slot = 0; slot < 3; ++slot) {
        assembled.emplace_back(server_ctx.tlwe_params);
        tLweCopy(assembled.back().Get(), source_bucket[slot].rlwe_ct.Get(), server_ctx.tlwe_params);
    }
    for (size_t slot = 3; slot < 8; ++slot) {
        assembled.emplace_back(server_ctx.tlwe_params);
        tLweClear(assembled.back().Get(), server_ctx.tlwe_params);
    }

    WaksmanNetwork network(8);
    std::vector<size_t> permutation = {7, 3, 6, 2, 5, 1, 4, 0};
    auto swap_bits_plain = network.GenerateSwapBits(permutation);
    std::vector<RGSWCiphertext> server_swap_bits;
    for (bool bit : swap_bits_plain) {
        RGSWCiphertext client_bit = EncryptBit(bit, client_ctx);
        server_swap_bits.emplace_back(
            RGSWCiphertext::Deserialize(client_bit.Serialize(), server_ctx.tgsw_params));
    }

    std::vector<TLweSample*> slot_ptrs;
    for (auto& slot : assembled) {
        slot_ptrs.push_back(slot.Get());
    }
    std::vector<TGswSample*> swap_ptrs;
    for (auto& bit : server_swap_bits) {
        swap_ptrs.push_back(bit.Get());
    }
    WaksmanNetwork::EvalWaksman(slot_ptrs, swap_ptrs, server_ctx.tgsw_params);

    for (size_t slot = 0; slot < 8; ++slot) {
        tLweCopy(dest_bucket[slot].rlwe_ct.Get(), assembled[slot].Get(), server_ctx.tlwe_params);
    }

    std::vector<std::vector<uint8_t>> expected(8, BlockByte(0x00, cfg.block_size));
    expected[3] = PatternBlock(0x5E, cfg.block_size);
    expected[5] = PatternBlock(0x57, cfg.block_size);
    expected[7] = PatternBlock(0x50, cfg.block_size);
    for (size_t slot = 0; slot < 8; ++slot) {
        EXPECT_EQ(DecryptBlock(dest_bucket[slot].rlwe_ct, client_ctx, cfg.block_size), expected[slot]);
    }
}

TEST(WaksmanTest, SequentialTwoChildTripletReplayPreservesFirstChild) {
    RuntimeConfig cfg;
    cfg.block_size = 32;
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);

    OnionBucket source_bucket(8, server_ctx.tlwe_params);
    OnionBucket left_child(8, server_ctx.tlwe_params);
    OnionBucket right_child(8, server_ctx.tlwe_params);
    for (size_t slot = 0; slot < 8; ++slot) {
        tLweClear(source_bucket[slot].rlwe_ct.Get(), server_ctx.tlwe_params);
        tLweClear(left_child[slot].rlwe_ct.Get(), server_ctx.tlwe_params);
        tLweClear(right_child[slot].rlwe_ct.Get(), server_ctx.tlwe_params);
    }

    RLWECiphertext block0 = EncryptBlock(PatternBlock(0x50, cfg.block_size), client_ctx);
    RLWECiphertext block1 = EncryptBlock(PatternBlock(0x57, cfg.block_size), client_ctx);
    RLWECiphertext block2 = EncryptBlock(PatternBlock(0x5E, cfg.block_size), client_ctx);
    source_bucket[0].rlwe_ct = RLWECiphertext::Deserialize(block0.Serialize(), server_ctx.tlwe_params);
    source_bucket[1].rlwe_ct = RLWECiphertext::Deserialize(block1.Serialize(), server_ctx.tlwe_params);
    source_bucket[2].rlwe_ct = RLWECiphertext::Deserialize(block2.Serialize(), server_ctx.tlwe_params);

    auto apply_child = [&](OnionBucket* dest_bucket, const std::vector<uint64_t>& source_slots,
                           const std::vector<size_t>& permutation) {
        std::vector<RLWECiphertext> assembled;
        assembled.reserve(8);
        for (uint64_t slot : source_slots) {
            assembled.emplace_back(server_ctx.tlwe_params);
            tLweCopy(assembled.back().Get(), source_bucket[slot].rlwe_ct.Get(), server_ctx.tlwe_params);
        }
        while (assembled.size() < 8) {
            assembled.emplace_back(server_ctx.tlwe_params);
            tLweClear(assembled.back().Get(), server_ctx.tlwe_params);
        }

        WaksmanNetwork network(8);
        auto swap_bits_plain = network.GenerateSwapBits(permutation);
        std::vector<RGSWCiphertext> swap_bits;
        for (bool bit : swap_bits_plain) {
            RGSWCiphertext client_bit = EncryptBit(bit, client_ctx);
            swap_bits.emplace_back(
                RGSWCiphertext::Deserialize(client_bit.Serialize(), server_ctx.tgsw_params));
        }

        std::vector<TLweSample*> slot_ptrs;
        for (auto& slot : assembled) {
            slot_ptrs.push_back(slot.Get());
        }
        std::vector<TGswSample*> swap_ptrs;
        for (auto& bit : swap_bits) {
            swap_ptrs.push_back(bit.Get());
        }
        WaksmanNetwork::EvalWaksman(slot_ptrs, swap_ptrs, server_ctx.tgsw_params);

        for (size_t slot = 0; slot < 8; ++slot) {
            tLweCopy((*dest_bucket)[slot].rlwe_ct.Get(), assembled[slot].Get(), server_ctx.tlwe_params);
        }
    };

    apply_child(&left_child, {0, 1, 2}, {7, 3, 6, 2, 5, 1, 4, 0});
    apply_child(&right_child, {}, {3, 2, 5, 6, 0, 4, 7, 1});

    std::vector<std::vector<uint8_t>> expected(8, BlockByte(0x00, cfg.block_size));
    expected[3] = PatternBlock(0x5E, cfg.block_size);
    expected[5] = PatternBlock(0x57, cfg.block_size);
    expected[7] = PatternBlock(0x50, cfg.block_size);
    for (size_t slot = 0; slot < 8; ++slot) {
        EXPECT_EQ(DecryptBlock(left_child[slot].rlwe_ct, client_ctx, cfg.block_size), expected[slot]);
    }
}

TEST(WaksmanTest, TracedTripletPipelinePreservesDataAcrossNetIOTransport) {
    RuntimeConfig cfg;
    cfg.block_size = 32;
    const int port = NextPort();

    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    std::thread server_thread([cfg, port]() {
        auto server_ctx = TFHEContext::CreateServerContext(cfg);
        network::NetIO server("127.0.0.1", port, true, true);

        std::vector<uint8_t> source_slot_bytes;
        std::vector<uint8_t> child_slot_bytes;
        server.RecvVec(source_slot_bytes);
        server.RecvVec(child_slot_bytes);
        const auto source_slots = DeserializeUint64Vector(source_slot_bytes);
        const auto child_slots = DeserializeUint64Vector(child_slot_bytes);
        EXPECT_TRUE(child_slots.empty());

        uint64_t bit_count = 0;
        server.RecvData(&bit_count, sizeof(bit_count));

        OnionBucket source_bucket(8, server_ctx.tlwe_params);
        for (size_t slot = 0; slot < 8; ++slot) {
            tLweClear(source_bucket[slot].rlwe_ct.Get(), server_ctx.tlwe_params);
        }

        for (size_t slot = 0; slot < source_slots.size(); ++slot) {
            std::vector<uint8_t> block_bytes;
            server.RecvVec(block_bytes);
            source_bucket[source_slots[slot]].rlwe_ct =
                RLWECiphertext::Deserialize(block_bytes, server_ctx.tlwe_params);
        }

        std::vector<RGSWCiphertext> swap_bits;
        swap_bits.reserve(bit_count);
        for (uint64_t i = 0; i < bit_count; ++i) {
            std::vector<uint8_t> bit_bytes;
            server.RecvVec(bit_bytes);
            swap_bits.emplace_back(RGSWCiphertext::Deserialize(bit_bytes, server_ctx.tgsw_params));
        }

        std::vector<RLWECiphertext> assembled;
        assembled.reserve(8);
        for (uint64_t slot : source_slots) {
            assembled.emplace_back(server_ctx.tlwe_params);
            tLweCopy(assembled.back().Get(), source_bucket[slot].rlwe_ct.Get(), server_ctx.tlwe_params);
        }
        while (assembled.size() < 8) {
            assembled.emplace_back(server_ctx.tlwe_params);
            tLweClear(assembled.back().Get(), server_ctx.tlwe_params);
        }

        std::vector<TLweSample*> slot_ptrs;
        for (auto& slot : assembled) {
            slot_ptrs.push_back(slot.Get());
        }
        std::vector<TGswSample*> swap_ptrs;
        for (auto& bit : swap_bits) {
            swap_ptrs.push_back(bit.Get());
        }
        WaksmanNetwork::EvalWaksman(slot_ptrs, swap_ptrs, server_ctx.tgsw_params);

        for (auto& slot : assembled) {
            server.SendVec(slot.Serialize());
        }
        server.Flush();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    network::NetIO client("127.0.0.1", port, false, true);
    std::vector<uint64_t> source_slots = {0, 1, 2};
    std::vector<uint64_t> child_slots;
    client.SendVec(SerializeUint64Vector(source_slots));
    client.SendVec(SerializeUint64Vector(child_slots));

    WaksmanNetwork network(8);
    std::vector<size_t> permutation = {7, 3, 6, 2, 5, 1, 4, 0};
    auto swap_bits_plain = network.GenerateSwapBits(permutation);
    uint64_t bit_count = swap_bits_plain.size();
    client.SendData(&bit_count, sizeof(bit_count));

    RLWECiphertext block0 = EncryptBlock(PatternBlock(0x50, cfg.block_size), client_ctx);
    RLWECiphertext block1 = EncryptBlock(PatternBlock(0x57, cfg.block_size), client_ctx);
    RLWECiphertext block2 = EncryptBlock(PatternBlock(0x5E, cfg.block_size), client_ctx);
    client.SendVec(block0.Serialize());
    client.SendVec(block1.Serialize());
    client.SendVec(block2.Serialize());

    for (bool bit : swap_bits_plain) {
        RGSWCiphertext ciphertext = EncryptBit(bit, client_ctx);
        client.SendVec(ciphertext.Serialize());
    }
    client.Flush();

    std::vector<std::vector<uint8_t>> expected(8, BlockByte(0x00, cfg.block_size));
    expected[3] = PatternBlock(0x5E, cfg.block_size);
    expected[5] = PatternBlock(0x57, cfg.block_size);
    expected[7] = PatternBlock(0x50, cfg.block_size);
    for (size_t slot = 0; slot < 8; ++slot) {
        std::vector<uint8_t> bytes;
        client.RecvVec(bytes);
        RLWECiphertext ciphertext = RLWECiphertext::Deserialize(bytes, client_ctx.tlwe_params);
        EXPECT_EQ(DecryptBlock(ciphertext, client_ctx, cfg.block_size), expected[slot]);
    }

    server_thread.join();
}

TEST(WaksmanTest, TracedTripletPipelinePreservesDataAcrossNetIOAfterBidirectionalTraffic) {
    RuntimeConfig cfg;
    cfg.block_size = 32;
    const int port = NextPort();

    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    std::thread server_thread([cfg, port]() {
        auto server_ctx = TFHEContext::CreateServerContext(cfg);
        network::NetIO server("127.0.0.1", port, true, true);

        for (size_t round = 0; round < 6; ++round) {
            char command = 0;
            server.RecvData(&command, sizeof(command));
            EXPECT_EQ(command, 'P');
            uint64_t value = 0;
            server.RecvData(&value, sizeof(value));
            EXPECT_EQ(value, round);
            uint64_t response = value + 10;
            server.SendData(&response, sizeof(response));
            server.Flush();
        }

        std::vector<uint8_t> source_slot_bytes;
        std::vector<uint8_t> child_slot_bytes;
        server.RecvVec(source_slot_bytes);
        server.RecvVec(child_slot_bytes);
        const auto source_slots = DeserializeUint64Vector(source_slot_bytes);
        const auto child_slots = DeserializeUint64Vector(child_slot_bytes);
        EXPECT_TRUE(child_slots.empty());

        uint64_t bit_count = 0;
        server.RecvData(&bit_count, sizeof(bit_count));

        OnionBucket source_bucket(8, server_ctx.tlwe_params);
        for (size_t slot = 0; slot < 8; ++slot) {
            tLweClear(source_bucket[slot].rlwe_ct.Get(), server_ctx.tlwe_params);
        }

        for (size_t slot = 0; slot < source_slots.size(); ++slot) {
            std::vector<uint8_t> block_bytes;
            server.RecvVec(block_bytes);
            source_bucket[source_slots[slot]].rlwe_ct =
                RLWECiphertext::Deserialize(block_bytes, server_ctx.tlwe_params);
        }

        std::vector<RGSWCiphertext> swap_bits;
        swap_bits.reserve(bit_count);
        for (uint64_t i = 0; i < bit_count; ++i) {
            std::vector<uint8_t> bit_bytes;
            server.RecvVec(bit_bytes);
            swap_bits.emplace_back(RGSWCiphertext::Deserialize(bit_bytes, server_ctx.tgsw_params));
        }

        std::vector<RLWECiphertext> assembled;
        assembled.reserve(8);
        for (uint64_t slot : source_slots) {
            assembled.emplace_back(server_ctx.tlwe_params);
            tLweCopy(assembled.back().Get(), source_bucket[slot].rlwe_ct.Get(), server_ctx.tlwe_params);
        }
        while (assembled.size() < 8) {
            assembled.emplace_back(server_ctx.tlwe_params);
            tLweClear(assembled.back().Get(), server_ctx.tlwe_params);
        }

        std::vector<TLweSample*> slot_ptrs;
        for (auto& slot : assembled) {
            slot_ptrs.push_back(slot.Get());
        }
        std::vector<TGswSample*> swap_ptrs;
        for (auto& bit : swap_bits) {
            swap_ptrs.push_back(bit.Get());
        }
        WaksmanNetwork::EvalWaksman(slot_ptrs, swap_ptrs, server_ctx.tgsw_params);

        for (auto& slot : assembled) {
            server.SendVec(slot.Serialize());
        }
        server.Flush();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    network::NetIO client("127.0.0.1", port, false, true);
    for (size_t round = 0; round < 6; ++round) {
        char command = 'P';
        uint64_t value = round;
        client.SendData(&command, sizeof(command));
        client.SendData(&value, sizeof(value));
        client.Flush();
        uint64_t response = 0;
        client.RecvData(&response, sizeof(response));
        EXPECT_EQ(response, value + 10);
    }

    std::vector<uint64_t> source_slots = {0, 1, 2};
    std::vector<uint64_t> child_slots;
    client.SendVec(SerializeUint64Vector(source_slots));
    client.SendVec(SerializeUint64Vector(child_slots));

    WaksmanNetwork network(8);
    std::vector<size_t> permutation = {7, 3, 6, 2, 5, 1, 4, 0};
    auto swap_bits_plain = network.GenerateSwapBits(permutation);
    uint64_t bit_count = swap_bits_plain.size();
    client.SendData(&bit_count, sizeof(bit_count));

    RLWECiphertext block0 = EncryptBlock(PatternBlock(0x50, cfg.block_size), client_ctx);
    RLWECiphertext block1 = EncryptBlock(PatternBlock(0x57, cfg.block_size), client_ctx);
    RLWECiphertext block2 = EncryptBlock(PatternBlock(0x5E, cfg.block_size), client_ctx);
    client.SendVec(block0.Serialize());
    client.SendVec(block1.Serialize());
    client.SendVec(block2.Serialize());

    for (bool bit : swap_bits_plain) {
        RGSWCiphertext ciphertext = EncryptBit(bit, client_ctx);
        client.SendVec(ciphertext.Serialize());
    }
    client.Flush();

    std::vector<std::vector<uint8_t>> expected(8, BlockByte(0x00, cfg.block_size));
    expected[3] = PatternBlock(0x5E, cfg.block_size);
    expected[5] = PatternBlock(0x57, cfg.block_size);
    expected[7] = PatternBlock(0x50, cfg.block_size);
    for (size_t slot = 0; slot < 8; ++slot) {
        std::vector<uint8_t> bytes;
        client.RecvVec(bytes);
        RLWECiphertext ciphertext = RLWECiphertext::Deserialize(bytes, client_ctx.tlwe_params);
        EXPECT_EQ(DecryptBlock(ciphertext, client_ctx, cfg.block_size), expected[slot]);
    }

    server_thread.join();
}

}  // namespace
}  // namespace oram::onion_ring
