#include <gtest/gtest.h>

#include "engine/core/types.h"
#include "engine/core/containers/optional.h"

TEST(CoreTypes, FixedWidthAliases) {
    EXPECT_EQ(sizeof(nge::u8), 1u);
    EXPECT_EQ(sizeof(nge::u16), 2u);
    EXPECT_EQ(sizeof(nge::u32), 4u);
    EXPECT_EQ(sizeof(nge::u64), 8u);
    EXPECT_EQ(sizeof(nge::f32), 4u);
    EXPECT_EQ(sizeof(nge::f64), 8u);
}

TEST(Optional, BasicLifecycle) {
    nge::Optional<int> value;
    EXPECT_FALSE(value.HasValue());

    value.Emplace(42);
    ASSERT_TRUE(value.HasValue());
    EXPECT_EQ(value.Value(), 42);

    value.Reset();
    EXPECT_FALSE(value.HasValue());
}
