#include <gtest/gtest.h>

#include <vector>

#include "oram/onion_ring/TFHEAdapter.h"

namespace oram::onion_ring {
namespace {

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
}

TEST(TFHEAdapterTest, TlweRoundTripBlockPayload) {
    RuntimeConfig cfg;
    auto ctx = TFHEContext::CreateClientContext(cfg);
    std::vector<uint8_t> message(cfg.block_size, 0x5A);

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

}  // namespace
}  // namespace oram::onion_ring
