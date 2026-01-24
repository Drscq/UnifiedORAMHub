#include "oram/core/Bucket.h"

#include <gtest/gtest.h>

#include "oram/core/Block.h"

TEST(BucketTest, CapacityAndAdd) {
    size_t capacity = 4;
    oram::core::Bucket bucket(capacity);

    EXPECT_EQ(bucket.GetCapacity(), capacity);
    EXPECT_EQ(bucket.GetBlockCount(), 0);

    oram::core::Block b1(1, {0x01});
    EXPECT_TRUE(bucket.AddBlock(b1));
    EXPECT_EQ(bucket.GetBlockCount(), 1);
}

TEST(BucketTest, Overflow) {
    oram::core::Bucket bucket(1);
    oram::core::Block b1(1, {0x01});
    oram::core::Block b2(2, {0x02});

    EXPECT_TRUE(bucket.AddBlock(b1));
    EXPECT_FALSE(bucket.AddBlock(b2));  // Should fail if full
}
