#include <gtest/gtest.h>

#include <vector>

#include "oram/onion_ring/PermGen.h"
#include "oram/onion_ring/TFHEAdapter.h"

namespace oram::onion_ring {
namespace {

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

}  // namespace
}  // namespace oram::onion_ring
