#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "oram/onion_ring/HomExpand.h"
#include "oram/onion_ring/PermGen.h"
#include "oram/onion_ring/TFHEAdapter.h"
#include "oram/onion_ring/WaksmanNetwork.h"

namespace oram::onion_ring {
namespace {

std::vector<uint8_t> BlockByte(uint8_t value, size_t block_size) {
    return std::vector<uint8_t>(block_size, value);
}

TEST(HomExpandTest, PackedPayloadExpandsToSameBitsAsDirectOracle) {
    RuntimeConfig cfg;
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);

    ExpansionBundle bundle = BuildExpansionBundle(client_ctx);
    PackedSwapBitPayload payload = BuildPackedSwapBitPayload({2, 0, 3, 1}, client_ctx);

    auto packed_bits = HomExpandPackedSwapBits(payload, bundle, server_ctx);
    auto direct_bits = DeserializeDirectSwapBitPayload(
        BuildDirectSwapBitPayload({2, 0, 3, 1}, client_ctx), server_ctx.tgsw_params);

    ASSERT_EQ(packed_bits.size(), direct_bits.size());
    for (size_t i = 0; i < packed_bits.size(); ++i) {
        EXPECT_EQ(DecryptBit(packed_bits[i], client_ctx), DecryptBit(direct_bits[i], client_ctx));
    }
}

TEST(HomExpandTest, ExpandRlweRecoversOneBitPerGateForSmallPermutation) {
    RuntimeConfig cfg;
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);
    ExpansionBundle bundle = BuildExpansionBundle(client_ctx);
    PackedSwapBitPayload payload = BuildPackedSwapBitPayload({2, 0, 3, 1}, client_ctx);

    auto expanded = HomExpandPackedSwapBits(payload, bundle, server_ctx);

    ASSERT_EQ(expanded.size(), WaksmanNetwork(4).NumGates());
}

TEST(HomExpandTest, PackedAndDirectPathsDriveIdenticalWaksmanOutputs) {
    RuntimeConfig cfg;
    cfg.block_size = 16;
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);
    ExpansionBundle bundle = BuildExpansionBundle(client_ctx);

    std::vector<size_t> permutation = {2, 0, 3, 1};
    auto packed_controls = HomExpandPackedSwapBits(
        BuildPackedSwapBitPayload(permutation, client_ctx), bundle, server_ctx);
    auto direct_controls = DeserializeDirectSwapBitPayload(
        BuildDirectSwapBitPayload(permutation, client_ctx), server_ctx.tgsw_params);

    std::vector<RLWECiphertext> packed_blocks;
    std::vector<RLWECiphertext> direct_blocks;
    for (uint8_t value = 1; value <= 4; ++value) {
        RLWECiphertext client_block = EncryptBlock(BlockByte(value * 0x11, cfg.block_size), client_ctx);
        packed_blocks.emplace_back(
            RLWECiphertext::Deserialize(client_block.Serialize(), server_ctx.tlwe_params));
        direct_blocks.emplace_back(
            RLWECiphertext::Deserialize(client_block.Serialize(), server_ctx.tlwe_params));
    }

    std::vector<TLweSample*> packed_ptrs;
    std::vector<TLweSample*> direct_ptrs;
    for (auto& block : packed_blocks) {
        packed_ptrs.push_back(block.Get());
    }
    for (auto& block : direct_blocks) {
        direct_ptrs.push_back(block.Get());
    }

    std::vector<TGswSample*> packed_control_ptrs;
    std::vector<TGswSample*> direct_control_ptrs;
    for (auto& control : packed_controls) {
        packed_control_ptrs.push_back(control.Get());
    }
    for (auto& control : direct_controls) {
        direct_control_ptrs.push_back(control.Get());
    }

    WaksmanNetwork::EvalWaksman(packed_ptrs, packed_control_ptrs, server_ctx.tgsw_params);
    WaksmanNetwork::EvalWaksman(direct_ptrs, direct_control_ptrs, server_ctx.tgsw_params);

    for (size_t i = 0; i < packed_blocks.size(); ++i) {
        EXPECT_EQ(DecryptBlock(packed_blocks[i], client_ctx, cfg.block_size),
                  DecryptBlock(direct_blocks[i], client_ctx, cfg.block_size));
    }
}

}  // namespace
}  // namespace oram::onion_ring
