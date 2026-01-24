#include <gtest/gtest.h>
#include "MathLib/math_functions.h"

TEST(MathLibTest, AdditionWorks) {
    EXPECT_EQ(MathLib::add(2, 3), 5);
    EXPECT_EQ(MathLib::add(-1, 1), 0);
}
