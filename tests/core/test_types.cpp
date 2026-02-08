#include <gtest/gtest.h>
#include "engine/core/types.h"

using namespace nge;

TEST(Types, AlignUp) {
    EXPECT_EQ(AlignUp(0, 16), 0u);
    EXPECT_EQ(AlignUp(1, 16), 16u);
    EXPECT_EQ(AlignUp(16, 16), 16u);
    EXPECT_EQ(AlignUp(17, 16), 32u);
    EXPECT_EQ(AlignUp(255, 256), 256u);
}

TEST(Types, AlignDown) {
    EXPECT_EQ(AlignDown(0, 16), 0u);
    EXPECT_EQ(AlignDown(1, 16), 0u);
    EXPECT_EQ(AlignDown(16, 16), 16u);
    EXPECT_EQ(AlignDown(31, 16), 16u);
}

TEST(Types, IsAligned) {
    EXPECT_TRUE(IsAligned(0, 16));
    EXPECT_TRUE(IsAligned(16, 16));
    EXPECT_TRUE(IsAligned(256, 16));
    EXPECT_FALSE(IsAligned(1, 16));
    EXPECT_FALSE(IsAligned(15, 16));
}

TEST(Types, IsPowerOfTwo) {
    EXPECT_TRUE(IsPowerOfTwo(1));
    EXPECT_TRUE(IsPowerOfTwo(2));
    EXPECT_TRUE(IsPowerOfTwo(4));
    EXPECT_TRUE(IsPowerOfTwo(1024));
    EXPECT_FALSE(IsPowerOfTwo(0));
    EXPECT_FALSE(IsPowerOfTwo(3));
    EXPECT_FALSE(IsPowerOfTwo(6));
}

TEST(Types, NextPowerOfTwo) {
    EXPECT_EQ(NextPowerOfTwo(1), 1u);
    EXPECT_EQ(NextPowerOfTwo(2), 2u);
    EXPECT_EQ(NextPowerOfTwo(3), 4u);
    EXPECT_EQ(NextPowerOfTwo(5), 8u);
    EXPECT_EQ(NextPowerOfTwo(1000), 1024u);
}

TEST(Types, Log2) {
    EXPECT_EQ(Log2(1), 0u);
    EXPECT_EQ(Log2(2), 1u);
    EXPECT_EQ(Log2(4), 2u);
    EXPECT_EQ(Log2(8), 3u);
    EXPECT_EQ(Log2(1024), 10u);
}

TEST(Types, TypeId) {
    u64 intId    = TypeId<int>::Value();
    u64 floatId  = TypeId<float>::Value();
    u64 doubleId = TypeId<double>::Value();

    EXPECT_NE(intId, floatId);
    EXPECT_NE(intId, doubleId);
    EXPECT_NE(floatId, doubleId);

    // Same type returns same ID
    EXPECT_EQ(intId, TypeId<int>::Value());
}
