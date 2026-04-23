#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>

#include "oram/onion_ring/TFHEAdapter.h"

#include "numeric_functions.h"
#include "tgsw.h"
#include "tlwe.h"

namespace oram::onion_ring {
namespace {

TEST(TFHE64BackendTest, TorusTypeIsEightBytes) {
    EXPECT_EQ(sizeof(Torus32), sizeof(int64_t));
}

TEST(TFHE64BackendTest, ModSwitchRoundTripsLargeMessageSpaces) {
    for (int msize : {2, 8, 16, 128, 2048, 1 << 21}) {
        for (int value = 0; value < std::min(msize, 32); ++value) {
            EXPECT_EQ(modSwitchFromTorus32(modSwitchToTorus32(value, msize), msize), value)
                << "msize=" << msize << " value=" << value;
        }
    }
}

TEST(TFHE64BackendTest, PaperSwapGadgetRowsRemainNonZeroAfterPackingScale) {
    RuntimeConfig cfg;
    cfg.use_recursive_packed_swap_bits = true;
    auto ctx = TFHEContext::CreateClientContext(cfg);

    for (int row = 0; row < ctx.swap_tgsw_params->l; ++row) {
        EXPECT_GT(ctx.swap_tgsw_params->h[row] / ctx.tlwe_params->N, 0)
            << "row=" << row << " h=" << ctx.swap_tgsw_params->h[row];
    }
}

}  // namespace
}  // namespace oram::onion_ring
