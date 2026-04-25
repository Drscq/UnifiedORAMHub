#include <gtest/gtest.h>

#include <vector>

#include "oram/onion_ring/PermGen.h"
#include "oram/onion_ring/TFHEAdapter.h"

namespace oram::onion_ring {
namespace {

RuntimeConfig SmallGadgetConfig() {
    RuntimeConfig cfg;
    cfg.tlwe_n = 128;
    cfg.block_size = 32;
    cfg.swap_tgsw_l = cfg.tgsw_l;
    cfg.swap_tgsw_bgbit = cfg.tgsw_bgbit;
    cfg.neg_sk_tgsw_l = cfg.tgsw_l;
    cfg.neg_sk_tgsw_bgbit = cfg.tgsw_bgbit;
    cfg.rlwe_ks_length = 3;
    return cfg;
}

TEST(TFHEAdapterSmokeTest, CanConstructClientContext) {
    auto ctx = TFHEContext::CreateClientContext(RuntimeConfig{});
    EXPECT_NE(ctx.tlwe_params, nullptr);
    EXPECT_NE(ctx.tgsw_params, nullptr);
}

TEST(TFHEAdapterTest, ClientContextSharesTlweAndTgswSecretKey) {
    auto ctx = TFHEContext::CreateClientContext(RuntimeConfig{});
    ASSERT_NE(ctx.tgsw_key, nullptr);
    ASSERT_NE(ctx.tlwe_key, nullptr);
    EXPECT_EQ(ctx.tlwe_key, &ctx.tgsw_key->tlwe_key);
    ASSERT_NE(ctx.practical_tgsw_key, nullptr);
    ASSERT_NE(ctx.neg_sk_tgsw_key, nullptr);
    EXPECT_EQ(ctx.practical_tgsw_key->tlwe_key.key[0].coefs[0],
              ctx.tlwe_key->key[0].coefs[0]);
    EXPECT_EQ(ctx.neg_sk_tgsw_key->tlwe_key.key[0].coefs[0],
              ctx.tlwe_key->key[0].coefs[0]);
}

TEST(TFHEAdapterTest, TlweRoundTripBlockPayload) {
    RuntimeConfig cfg;
    auto ctx = TFHEContext::CreateClientContext(cfg);
    std::vector<uint8_t> message(cfg.block_size, 0x5A);

    auto ct = EncryptBlock(message, ctx);
    auto out = DecryptBlock(ct, ctx, cfg.block_size);

    EXPECT_EQ(out, message);
}

TEST(TFHEAdapterTest, TlweRoundTripUsesConfiguredTwelveBitPlaintextPacking) {
    RuntimeConfig cfg;
    cfg.plaintext_bits = 12;
    cfg.block_size = static_cast<size_t>(cfg.recursive_tlwe_n) *
                     static_cast<size_t>(cfg.plaintext_bits) / 8;
    auto ctx = TFHEContext::CreateClientContext(cfg);
    std::vector<uint8_t> message(cfg.block_size, 0);
    for (size_t i = 0; i < message.size(); ++i) {
        message[i] = static_cast<uint8_t>((i * 37 + 0x5A) & 0xFF);
    }

    auto ct = EncryptBlock(message, ctx);
    auto out = DecryptBlock(ct, ctx, cfg.block_size);

    EXPECT_EQ(out, message);
}

TEST(TFHEAdapterTest, TlweSerializationRoundTrip) {
    RuntimeConfig cfg;
    auto ctx = TFHEContext::CreateClientContext(cfg);
    auto ct = EncryptBlock(std::vector<uint8_t>(cfg.block_size, 0x11), ctx);

    auto bytes = ct.Serialize();
    auto restored = RLWECiphertext::Deserialize(bytes, ctx.tlwe_params);

    EXPECT_EQ(DecryptBlock(restored, ctx, cfg.block_size),
              std::vector<uint8_t>(cfg.block_size, 0x11));
}

TEST(TFHEAdapterTest, TgswBitRoundTrip) {
    auto ctx = TFHEContext::CreateClientContext(RuntimeConfig{});
    auto bit = EncryptBit(true, ctx);
    EXPECT_TRUE(DecryptBit(bit, ctx));
}

TEST(TFHEAdapterTest, RecursivePaperConfigCreatesSeparateGadgetParams) {
    RuntimeConfig cfg;
    cfg.use_recursive_packed_swap_bits = true;
    auto ctx = TFHEContext::CreateClientContext(cfg);

    ASSERT_NE(ctx.swap_tgsw_params, nullptr);
    ASSERT_NE(ctx.neg_sk_tgsw_params, nullptr);
    ASSERT_NE(ctx.practical_tgsw_params, nullptr);
    EXPECT_EQ(ctx.tlwe_params->N, cfg.recursive_tlwe_n);
    EXPECT_EQ(ctx.swap_tgsw_params->Bgbit, 3);
    EXPECT_EQ(ctx.swap_tgsw_params->l, 8);
    EXPECT_EQ(ctx.neg_sk_tgsw_params->Bgbit, 7);
    EXPECT_EQ(ctx.neg_sk_tgsw_params->l, 7);
    EXPECT_EQ(ctx.practical_tgsw_params->Bgbit, 7);
    EXPECT_EQ(ctx.practical_tgsw_params->l, 3);
}

TEST(TFHEAdapterTest, ExpansionBundleRoundTripsKeySwitchMaterial) {
    RuntimeConfig cfg = SmallGadgetConfig();
    auto ctx = TFHEContext::CreateClientContext(cfg);

    ExpansionBundle bundle = BuildExpansionBundle(ctx);
    ExpansionBundle restored =
        ExpansionBundle::Deserialize(bundle.Serialize(), cfg, ctx.tlwe_params, ctx.tgsw_params);

    EXPECT_EQ(restored.substitution_keys.size(), bundle.substitution_keys.size());
    EXPECT_EQ(restored.lwe_key_switch_keys.size(), bundle.lwe_key_switch_keys.size());
    EXPECT_FALSE(restored.neg_sk_rgsw_bytes.empty());
}

TEST(TFHEAdapterTest, ExpansionBundleContainsRecursiveMonomialSubstitutionKeys) {
    RuntimeConfig cfg = SmallGadgetConfig();
    auto ctx = TFHEContext::CreateClientContext(cfg);

    ExpansionBundle bundle = BuildExpansionBundle(ctx);

    ASSERT_FALSE(bundle.substitution_keys.empty());
    for (size_t level = 0; level < bundle.substitution_keys.size(); ++level) {
        IntPolynomial* poly = new_IntPolynomial(ctx.tlwe_params->N);
        tGswSymDecrypt(poly, bundle.substitution_keys[level].Get(), ctx.tgsw_key, 2);

        const int expected_index = ctx.tlwe_params->N >> (level + 1);
        ASSERT_GT(expected_index, 0);
        for (int coeff = 0; coeff < ctx.tlwe_params->N; ++coeff) {
            const int expected = (coeff == expected_index) ? 1 : 0;
            EXPECT_EQ(poly->coefs[coeff], expected) << "level=" << level << " coeff=" << coeff;
        }

        delete_IntPolynomial(poly);
    }
}

}  // namespace
}  // namespace oram::onion_ring
