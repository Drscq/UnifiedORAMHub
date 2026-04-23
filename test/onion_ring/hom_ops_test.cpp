#include <gtest/gtest.h>

#include <vector>

#include "oram/onion_ring/TFHEAdapter.h"
#include "oram/onion_ring/HomOps.h"

namespace oram::onion_ring {
namespace {

TEST(HomOpsTest, ExternalProductMatchesEncryptedBitSelection) {
    RuntimeConfig cfg;
    auto ctx = TFHEContext::CreateClientContext(cfg);
    auto data = EncryptBlock(std::vector<uint8_t>(cfg.block_size, 0x2A), ctx);
    auto zero_bit = EncryptBit(false, ctx);
    auto one_bit = EncryptBit(true, ctx);

    RLWECiphertext zero_out(ctx.tlwe_params);
    RLWECiphertext one_out(ctx.tlwe_params);

    ExternalProduct(zero_out.Get(), zero_bit.Get(), data.Get(), ctx.tgsw_params);
    ExternalProduct(one_out.Get(), one_bit.Get(), data.Get(), ctx.tgsw_params);

    EXPECT_EQ(DecryptBlock(zero_out, ctx, cfg.block_size),
              std::vector<uint8_t>(cfg.block_size, 0x00));
    EXPECT_EQ(DecryptBlock(one_out, ctx, cfg.block_size),
              std::vector<uint8_t>(cfg.block_size, 0x2A));
}

TEST(HomOpsTest, CMuxSelectsD0WhenControlIsZero) {
    RuntimeConfig cfg;
    auto ctx = TFHEContext::CreateClientContext(cfg);
    auto d0 = EncryptBlock(std::vector<uint8_t>(cfg.block_size, 0x10), ctx);
    auto d1 = EncryptBlock(std::vector<uint8_t>(cfg.block_size, 0x20), ctx);
    auto control = EncryptBit(false, ctx);
    RLWECiphertext out(ctx.tlwe_params);

    CMux(out.Get(), control.Get(), d1.Get(), d0.Get(), ctx.tgsw_params);

    EXPECT_EQ(DecryptBlock(out, ctx, cfg.block_size),
              std::vector<uint8_t>(cfg.block_size, 0x10));
}

TEST(HomOpsTest, CMuxSelectsD1WhenControlIsOne) {
    RuntimeConfig cfg;
    auto ctx = TFHEContext::CreateClientContext(cfg);
    auto d0 = EncryptBlock(std::vector<uint8_t>(cfg.block_size, 0x10), ctx);
    auto d1 = EncryptBlock(std::vector<uint8_t>(cfg.block_size, 0x20), ctx);
    auto control = EncryptBit(true, ctx);
    RLWECiphertext out(ctx.tlwe_params);

    CMux(out.Get(), control.Get(), d1.Get(), d0.Get(), ctx.tgsw_params);

    EXPECT_EQ(DecryptBlock(out, ctx, cfg.block_size),
              std::vector<uint8_t>(cfg.block_size, 0x20));
}

TEST(HomOpsTest, SubsMatchesPolynomialAutomorphismForOddAi) {
    RuntimeConfig cfg;
    cfg.block_size = 8;
    auto ctx = TFHEContext::CreateClientContext(cfg);

    TorusPolynomial* plain = new_TorusPolynomial(ctx.tlwe_params->N);
    torusPolynomialClear(plain);
    for (size_t i = 0; i < cfg.block_size; ++i) {
        plain->coefsT[i] = modSwitchToTorus32(static_cast<int>(i + 1), 256);
    }

    RLWECiphertext input(ctx.tlwe_params);
    RLWECiphertext rotated(ctx.tlwe_params);
    tLweSymEncrypt(input.Get(), plain, ctx.alpha, ctx.tlwe_key);

    const int ai = 3;
    Subs(rotated.Get(), input.Get(), ai, ctx.tlwe_params);

    TorusPolynomial* expected = new_TorusPolynomial(ctx.tlwe_params->N);
    TorusPolynomial* actual = new_TorusPolynomial(ctx.tlwe_params->N);
    torusPolynomialClear(expected);
    for (int coeff = 0; coeff < ctx.tlwe_params->N; ++coeff) {
        const int mapped = (coeff * ai) % (2 * ctx.tlwe_params->N);
        if (mapped < ctx.tlwe_params->N) {
            expected->coefsT[mapped] = plain->coefsT[coeff];
        } else {
            expected->coefsT[mapped - ctx.tlwe_params->N] = -plain->coefsT[coeff];
        }
    }

    TLweKey* transformed_key = new_TLweKey(ctx.tlwe_params);
    for (int coeff = 0; coeff < ctx.tlwe_params->N; ++coeff) {
        transformed_key->key[0].coefs[coeff] = 0;
    }
    for (int coeff = 0; coeff < ctx.tlwe_params->N; ++coeff) {
        const int mapped = (coeff * ai) % (2 * ctx.tlwe_params->N);
        if (mapped < ctx.tlwe_params->N) {
            transformed_key->key[0].coefs[mapped] = ctx.tlwe_key->key[0].coefs[coeff];
        } else {
            transformed_key->key[0].coefs[mapped - ctx.tlwe_params->N] =
                -ctx.tlwe_key->key[0].coefs[coeff];
        }
    }
    tLweSymDecrypt(actual, rotated.Get(), transformed_key, 256);

    for (int i = 0; i < ctx.tlwe_params->N; ++i) {
        EXPECT_EQ(actual->coefsT[i], expected->coefsT[i]) << "Mismatch at coefficient " << i;
    }

    delete_TLweKey(transformed_key);
    delete_TorusPolynomial(actual);
    delete_TorusPolynomial(expected);
    delete_TorusPolynomial(plain);
}

}  // namespace
}  // namespace oram::onion_ring
