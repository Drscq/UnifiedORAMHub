#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>

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
    TLweParams* tlwe_params = new_TLweParams(2048, 1, 1e-12, 1.0 / 16.0);
    TGswParams* swap_params = new_TGswParams(8, 3, tlwe_params);

    for (int row = 0; row < swap_params->l; ++row) {
        EXPECT_GT(swap_params->h[row] / tlwe_params->N, 0)
            << "row=" << row << " h=" << swap_params->h[row];
    }

    delete_TGswParams(swap_params);
    delete_TLweParams(tlwe_params);
}

}  // namespace
}  // namespace oram::onion_ring
