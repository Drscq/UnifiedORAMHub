#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "numeric_functions.h"
#include "polynomials_arithmetic.h"
#include "tgsw_functions.h"
#include "tlwe_functions.h"

#include "oram/onion_ring/HomExpand.h"
#include "oram/onion_ring/HomOps.h"
#include "oram/onion_ring/PermGen.h"
#include "oram/onion_ring/TFHEAdapter.h"
#include "oram/onion_ring/WaksmanNetwork.h"

namespace oram::onion_ring {
namespace {

std::vector<uint8_t> BlockByte(uint8_t value, size_t block_size) {
    return std::vector<uint8_t>(block_size, value);
}

RuntimeConfig PaperRecursiveConfig() {
    RuntimeConfig cfg;
    cfg.use_recursive_packed_swap_bits = true;
    cfg.tlwe_n = 2048;
    cfg.recursive_tlwe_n = 2048;
    cfg.swap_tgsw_bgbit = 3;
    cfg.swap_tgsw_l = 8;
    cfg.neg_sk_tgsw_bgbit = 7;
    cfg.neg_sk_tgsw_l = 7;
    cfg.rlwe_ks_basebit = 3;
    cfg.rlwe_ks_length = 20;
    cfg.alpha = 1e-15;
    return cfg;
}

PackedSwapBitPayload BuildManualRecursivePayload(const std::vector<size_t>& one_indices,
                                                 const TFHEContext& ctx,
                                                 size_t bit_count = 0) {
    PackedSwapBitPayload payload;
    payload.mode = PackedSwapBitMode::kRecursiveRlwe;
    if (bit_count == 0) {
        for (size_t index : one_indices) {
            bit_count = std::max(bit_count, index + 1);
        }
    }
    payload.bit_count = static_cast<uint64_t>(bit_count);
    payload.bits_per_ciphertext = static_cast<uint64_t>(ctx.tlwe_params->N);
    payload.row_count = 1;

    TorusPolynomial* poly = new_TorusPolynomial(ctx.tlwe_params->N);
    torusPolynomialClear(poly);
    const Torus32 scale = ctx.tgsw_params->h[0] / ctx.tlwe_params->N;
    for (size_t index : one_indices) {
        poly->coefsT[index] = scale;
    }

    RLWECiphertext ciphertext(ctx.tlwe_params);
    tLweSymEncrypt(ciphertext.Get(), poly, ctx.alpha, ctx.tlwe_key);
    payload.ciphertexts.push_back(ciphertext.Serialize());
    delete_TorusPolynomial(poly);
    return payload;
}

PackedSwapBitPayload BuildManualRecursivePayloadForRow(const std::vector<size_t>& one_indices,
                                                       size_t target_row,
                                                       const TFHEContext& ctx,
                                                       size_t bit_count = 0) {
    PackedSwapBitPayload payload;
    payload.mode = PackedSwapBitMode::kRecursiveRlwe;
    if (bit_count == 0) {
        for (size_t index : one_indices) {
            bit_count = std::max(bit_count, index + 1);
        }
    }
    payload.bit_count = static_cast<uint64_t>(bit_count);
    payload.bits_per_ciphertext = static_cast<uint64_t>(ctx.tlwe_params->N);
    payload.row_count = static_cast<uint64_t>(ctx.tgsw_params->l);

    for (size_t row = 0; row < payload.row_count; ++row) {
        TorusPolynomial* poly = new_TorusPolynomial(ctx.tlwe_params->N);
        torusPolynomialClear(poly);
        if (row == target_row) {
            const Torus32 scale = ctx.tgsw_params->h[row] / ctx.tlwe_params->N;
            for (size_t index : one_indices) {
                poly->coefsT[index] = scale;
            }
        }

        RLWECiphertext ciphertext(ctx.tlwe_params);
        tLweSymEncrypt(ciphertext.Get(), poly, ctx.alpha, ctx.tlwe_key);
        payload.ciphertexts.push_back(ciphertext.Serialize());
        delete_TorusPolynomial(poly);
    }

    return payload;
}

void ApplyAutomorphismToPoly(TorusPolynomial* result, const TorusPolynomial* input, int32_t power) {
    torusPolynomialClear(result);
    for (int coeff = 0; coeff < input->N; ++coeff) {
        const int64_t mapped = (static_cast<int64_t>(coeff) * power) % (2LL * input->N);
        if (mapped < input->N) {
            result->coefsT[mapped] = input->coefsT[coeff];
        } else {
            result->coefsT[mapped - input->N] = -input->coefsT[coeff];
        }
    }
}

TLweKey* BuildAutomorphedKey(const TFHEContext& ctx, int32_t power) {
    TLweKey* transformed_key = new_TLweKey(ctx.tlwe_params);
    for (int coeff = 0; coeff < ctx.tlwe_params->N; ++coeff) {
        transformed_key->key[0].coefs[coeff] = 0;
    }
    for (int coeff = 0; coeff < ctx.tlwe_params->N; ++coeff) {
        const int64_t mapped = (static_cast<int64_t>(coeff) * power) % (2LL * ctx.tlwe_params->N);
        if (mapped < ctx.tlwe_params->N) {
            transformed_key->key[0].coefs[mapped] = ctx.tlwe_key->key[0].coefs[coeff];
        } else {
            transformed_key->key[0].coefs[mapped - ctx.tlwe_params->N] =
                -ctx.tlwe_key->key[0].coefs[coeff];
        }
    }
    return transformed_key;
}

TEST(HomExpandTest, PackedPayloadExpandsToSameBitsAsDirectOracle) {
    RuntimeConfig cfg;
    cfg.tgsw_bgbit = 7;
    cfg.rlwe_ks_basebit = 3;
    cfg.rlwe_ks_length = 10;
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

TEST(HomExpandTest, RecursiveModeMatchesPracticalOracleBitsForSmallPermutation) {
    RuntimeConfig cfg;
    cfg.tgsw_bgbit = 7;
    cfg.rlwe_ks_basebit = 3;
    cfg.rlwe_ks_length = 10;
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);

    ExpansionBundle bundle = BuildExpansionBundle(client_ctx);
    PackedSwapBitPayload recursive_payload =
        BuildRecursivePackedSwapBitPayload({2, 0, 3, 1}, client_ctx);
    PackedSwapBitPayload practical_payload = BuildPackedSwapBitPayload({2, 0, 3, 1}, client_ctx);

    auto recursive_bits = HomExpandPackedSwapBits(recursive_payload, bundle, server_ctx);
    auto practical_bits = HomExpandPackedSwapBits(practical_payload, bundle, server_ctx);

    ASSERT_EQ(recursive_bits.size(), practical_bits.size());
    for (size_t i = 0; i < recursive_bits.size(); ++i) {
        EXPECT_EQ(DecryptBit(recursive_bits[i], client_ctx), DecryptBit(practical_bits[i], client_ctx));
    }
}

TEST(HomExpandTest, RecursiveLiftMatchesDirectControlRowPhasesForSingleBit) {
    RuntimeConfig cfg = PaperRecursiveConfig();
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);

    ExpansionBundle bundle = BuildRecursiveExpansionBundle(client_ctx);
    PackedSwapBitPayload recursive_payload = BuildRecursivePackedSwapBitPayload({1, 0}, client_ctx);
    auto recursive_bits = HomExpandPackedSwapBits(recursive_payload, bundle, server_ctx);

    ASSERT_FALSE(recursive_bits.empty());
    RGSWCiphertext direct = EncryptBit(true, client_ctx);
    const auto& recursive = recursive_bits.front();

    TorusPolynomial* probe_message = new_TorusPolynomial(client_ctx.tlwe_params->N);
    torusPolynomialClear(probe_message);
    const int row_msize = 1 << client_ctx.tgsw_params->Bgbit;
    probe_message->coefsT[0] = modSwitchToTorus32(1, row_msize);

    RLWECiphertext probe(client_ctx.tlwe_params);
    RLWECiphertext recursive_out(client_ctx.tlwe_params);
    RLWECiphertext direct_out(client_ctx.tlwe_params);
    tLweSymEncrypt(probe.Get(), probe_message, client_ctx.alpha, client_ctx.tlwe_key);

    ExternalProduct(recursive_out.Get(), recursive.Get(), probe.Get(), client_ctx.tgsw_params);
    ExternalProduct(direct_out.Get(), direct.Get(), probe.Get(), client_ctx.tgsw_params);

    TorusPolynomial* recursive_phase = new_TorusPolynomial(client_ctx.tlwe_params->N);
    TorusPolynomial* direct_phase = new_TorusPolynomial(client_ctx.tlwe_params->N);
    tLwePhase(recursive_phase, recursive_out.Get(), client_ctx.tlwe_key);
    tLwePhase(direct_phase, direct_out.Get(), client_ctx.tlwe_key);

    for (int coeff = 0; coeff < client_ctx.tlwe_params->N; ++coeff) {
        EXPECT_EQ(modSwitchFromTorus32(recursive_phase->coefsT[coeff], row_msize),
                  modSwitchFromTorus32(direct_phase->coefsT[coeff], row_msize))
            << "coeff=" << coeff;
    }

    delete_TorusPolynomial(direct_phase);
    delete_TorusPolynomial(recursive_phase);
    delete_TorusPolynomial(probe_message);
}

TEST(HomExpandTest, ExpandRlweRecoversOneBitPerGateForSmallPermutation) {
    RuntimeConfig cfg;
    cfg.tgsw_bgbit = 7;
    cfg.rlwe_ks_basebit = 3;
    cfg.rlwe_ks_length = 10;
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
    cfg.tgsw_bgbit = 7;
    cfg.rlwe_ks_basebit = 3;
    cfg.rlwe_ks_length = 10;
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

TEST(HomExpandTest, RecursiveExpandRlweIsolatesGateBitsInOrder) {
    RuntimeConfig cfg = PaperRecursiveConfig();
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);

    ExpansionBundle bundle = BuildRecursiveExpansionBundle(client_ctx);
    std::vector<size_t> permutation = {2, 0, 3, 1};
    PackedSwapBitPayload payload = BuildRecursivePackedSwapBitPayload(permutation, client_ctx);
    const auto expected_bits = WaksmanNetwork(permutation.size()).GenerateSwapBits(permutation);

    const int row0_msize = 1 << client_ctx.tgsw_params->Bgbit;
    auto isolated = ExpandPackedRlweForTest(payload, bundle, server_ctx);

    ASSERT_EQ(isolated.size(), expected_bits.size());
    for (size_t i = 0; i < isolated.size(); ++i) {
        TorusPolynomial* poly = new_TorusPolynomial(client_ctx.tlwe_params->N);
        tLweSymDecrypt(poly, isolated[i].Get(), client_ctx.tlwe_key, row0_msize);

        EXPECT_EQ(modSwitchFromTorus32(poly->coefsT[0], row0_msize),
                  expected_bits[i] ? 1 : 0)
            << "gate=" << i;
        for (int coeff = 1; coeff < client_ctx.tlwe_params->N; ++coeff) {
            EXPECT_EQ(modSwitchFromTorus32(poly->coefsT[coeff], row0_msize), 0)
                << "gate=" << i << " coeff=" << coeff;
        }

        delete_TorusPolynomial(poly);
    }
}

TEST(HomExpandTest, RecursiveSubstitutionKeySwitchReturnsToOriginalSecretKey) {
    RuntimeConfig cfg = PaperRecursiveConfig();
    cfg.block_size = 8;
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);
    ExpansionBundle bundle = BuildRecursiveExpansionBundle(client_ctx);

    TorusPolynomial* plain = new_TorusPolynomial(client_ctx.tlwe_params->N);
    torusPolynomialClear(plain);
    for (size_t i = 0; i < cfg.block_size; ++i) {
        plain->coefsT[i] = modSwitchToTorus32(static_cast<int>(i + 1), 256);
    }

    RLWECiphertext input(client_ctx.tlwe_params);
    tLweSymEncrypt(input.Get(), plain, client_ctx.alpha, client_ctx.tlwe_key);

    TorusPolynomial* expected = new_TorusPolynomial(client_ctx.tlwe_params->N);
    TorusPolynomial* actual = new_TorusPolynomial(client_ctx.tlwe_params->N);
    TorusPolynomial* direct_subs = new_TorusPolynomial(client_ctx.tlwe_params->N);
    for (const auto& key : bundle.recursive_ks_keys) {
        const int32_t power = key.substitution_power;
        RLWECiphertext keyed =
            ApplyRecursiveSubstitutionKeySwitchForTest(input, bundle, server_ctx, power);

        ApplyAutomorphismToPoly(expected, plain, power);
        tLweSymDecrypt(actual, keyed.Get(), client_ctx.tlwe_key, 256);
        for (int coeff = 0; coeff < client_ctx.tlwe_params->N; ++coeff) {
            EXPECT_EQ(actual->coefsT[coeff], expected->coefsT[coeff])
                << "power=" << power << " coeff=" << coeff;
        }

        TLweKey* transformed_key = BuildAutomorphedKey(client_ctx, power);
        RLWECiphertext substituted(client_ctx.tlwe_params);
        Subs(substituted.Get(), input.Get(), power, client_ctx.tlwe_params);
        tLweSymDecrypt(direct_subs, substituted.Get(), transformed_key, 256);
        for (int coeff = 0; coeff < client_ctx.tlwe_params->N; ++coeff) {
            EXPECT_EQ(direct_subs->coefsT[coeff], expected->coefsT[coeff])
                << "power=" << power << " direct coeff=" << coeff;
        }
        delete_TLweKey(transformed_key);
    }

    delete_TorusPolynomial(direct_subs);
    delete_TorusPolynomial(actual);
    delete_TorusPolynomial(expected);
    delete_TorusPolynomial(plain);
}

TEST(HomExpandTest, RecursiveExpandRlweKeepsOneHotCoefficientIndices) {
    RuntimeConfig cfg = PaperRecursiveConfig();
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);
    ExpansionBundle bundle = BuildRecursiveExpansionBundle(client_ctx);

    const int row0_msize = 1 << client_ctx.tgsw_params->Bgbit;
    for (size_t hot = 0; hot < 6; ++hot) {
        PackedSwapBitPayload payload = BuildManualRecursivePayload({hot}, client_ctx, 6);
        auto isolated = ExpandPackedRlweForTest(payload, bundle, server_ctx);

        std::vector<size_t> recovered;
        for (size_t i = 0; i < isolated.size(); ++i) {
            TorusPolynomial* poly = new_TorusPolynomial(client_ctx.tlwe_params->N);
            tLweSymDecrypt(poly, isolated[i].Get(), client_ctx.tlwe_key, row0_msize);
            if (modSwitchFromTorus32(poly->coefsT[0], row0_msize) == 1) {
                recovered.push_back(i);
            }
            delete_TorusPolynomial(poly);
        }

        ASSERT_EQ(recovered.size(), 1U) << "hot=" << hot;
        EXPECT_EQ(recovered[0], hot) << "hot=" << hot;
    }
}

TEST(HomExpandTest, RecursiveExpandRlweKeepsOneHotCoefficientIndicesAcrossRows) {
    RuntimeConfig cfg = PaperRecursiveConfig();
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);
    ExpansionBundle bundle = BuildRecursiveExpansionBundle(client_ctx);

    const std::vector<size_t> hot_indices = {0, 2, 5};
    for (size_t row = 0; row < static_cast<size_t>(client_ctx.tgsw_params->l); ++row) {
        const int row_msize = 1 << (client_ctx.tgsw_params->Bgbit * static_cast<int>(row + 1));
        for (size_t hot : hot_indices) {
            PackedSwapBitPayload payload =
                BuildManualRecursivePayloadForRow({hot}, row, client_ctx, 6);
            auto isolated = ExpandPackedRlweRowForTest(payload, bundle, server_ctx, row);

            std::vector<size_t> recovered;
            for (size_t i = 0; i < isolated.size(); ++i) {
                TorusPolynomial* poly = new_TorusPolynomial(client_ctx.tlwe_params->N);
                tLweSymDecrypt(poly, isolated[i].Get(), client_ctx.tlwe_key, row_msize);
                if (modSwitchFromTorus32(poly->coefsT[0], row_msize) == 1) {
                    recovered.push_back(i);
                }
                delete_TorusPolynomial(poly);
            }

            ASSERT_EQ(recovered.size(), 1U) << "row=" << row << " hot=" << hot;
            EXPECT_EQ(recovered[0], hot) << "row=" << row << " hot=" << hot;
        }
    }
}

TEST(HomExpandTest, RecursiveExpandRlweIsolatesGateBitsAcrossRowsInOrder) {
    RuntimeConfig cfg = PaperRecursiveConfig();
    auto client_ctx = TFHEContext::CreateClientContext(cfg);
    auto server_ctx = TFHEContext::CreateServerContext(cfg);

    ExpansionBundle bundle = BuildRecursiveExpansionBundle(client_ctx);
    std::vector<size_t> permutation = {2, 0, 3, 1};
    PackedSwapBitPayload payload = BuildRecursivePackedSwapBitPayload(permutation, client_ctx);
    const auto expected_bits = WaksmanNetwork(permutation.size()).GenerateSwapBits(permutation);

    for (size_t row = 0; row < static_cast<size_t>(client_ctx.tgsw_params->l); ++row) {
        const int row_msize = 1 << (client_ctx.tgsw_params->Bgbit * static_cast<int>(row + 1));
        auto isolated = ExpandPackedRlweRowForTest(payload, bundle, server_ctx, row);

        ASSERT_EQ(isolated.size(), expected_bits.size()) << "row=" << row;
        for (size_t i = 0; i < isolated.size(); ++i) {
            TorusPolynomial* poly = new_TorusPolynomial(client_ctx.tlwe_params->N);
            tLweSymDecrypt(poly, isolated[i].Get(), client_ctx.tlwe_key, row_msize);

            EXPECT_EQ(modSwitchFromTorus32(poly->coefsT[0], row_msize),
                      expected_bits[i] ? 1 : 0)
                << "row=" << row << " gate=" << i;
            int first_bad_coeff = -1;
            int first_bad_value = 0;
            for (int coeff = 1; coeff < client_ctx.tlwe_params->N; ++coeff) {
                const int value = modSwitchFromTorus32(poly->coefsT[coeff], row_msize);
                if (value != 0) {
                    first_bad_coeff = coeff;
                    first_bad_value = value;
                    break;
                }
            }
            EXPECT_EQ(first_bad_coeff, -1)
                << "row=" << row << " gate=" << i << " coeff=" << first_bad_coeff
                << " value=" << first_bad_value;

            delete_TorusPolynomial(poly);
        }
    }
}

}  // namespace
}  // namespace oram::onion_ring
