#include "oram/core/RAM.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

// Basic Mock to verify interface
// We assume RAM has Access, Read, Write virtual methods
class MockRAM : public oram::core::RAM {
   public:
    using Op = oram::core::Op;

    MOCK_METHOD(std::vector<uint8_t>, Access,
                (Op op, uint64_t addr, const std::vector<uint8_t>& data), (override));
    MOCK_METHOD(std::vector<uint8_t>, Read, (uint64_t addr), (override));
    MOCK_METHOD(void, Write, (uint64_t addr, const std::vector<uint8_t>& data), (override));
};

TEST(RAMTest, MockUsage) {
    MockRAM ram;
    EXPECT_CALL(ram, Read(10)).WillOnce(testing::Return(std::vector<uint8_t>{0xBB}));

    auto result = ram.Read(10);
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], 0xBB);
}
