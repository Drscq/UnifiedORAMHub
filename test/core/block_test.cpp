#include "oram/core/Block.h"

#include <gtest/gtest.h>

TEST(BlockTest, Initialization) {
    uint64_t expected_id = 42;
    std::vector<uint8_t> expected_data = {0xDE, 0xAD, 0xBE, 0xEF};

    oram::core::Block block(expected_id, expected_data);

    EXPECT_EQ(block.GetId(), expected_id);
    EXPECT_EQ(block.GetData(), expected_data);
}
