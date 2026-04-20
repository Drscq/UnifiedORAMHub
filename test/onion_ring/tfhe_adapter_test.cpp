#include <gtest/gtest.h>

#include "oram/onion_ring/TFHEAdapter.h"

namespace oram::onion_ring {
namespace {

TEST(TFHEAdapterSmokeTest, CanConstructClientContext) {
    auto ctx = TFHEContext::CreateClientContext(RuntimeConfig{});
    EXPECT_NE(ctx.tlwe_params, nullptr);
    EXPECT_NE(ctx.tgsw_params, nullptr);
}

}  // namespace
}  // namespace oram::onion_ring
