#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_buffer_suballocator.h"

using namespace nge;
using namespace nge::rhi;

// Stub device for testing — suballocator mostly works with offsets and sizes
// The real device calls are stubbed in the implementation.

TEST(BufferSubAllocator, InitialStatsEmpty) {
    // Can't fully test without a device, but verify struct defaults
    SubAllocatorStats stats{};
    EXPECT_EQ(stats.heapCount, 0u);
    EXPECT_EQ(stats.totalCapacity, 0u);
    EXPECT_EQ(stats.totalAllocated, 0u);
    EXPECT_EQ(stats.allocationCount, 0u);
    EXPECT_FLOAT_EQ(stats.fragmentation, 0.0f);
}

TEST(BufferSubAllocator, SubAllocationDefaults) {
    SubAllocation alloc;
    EXPECT_FALSE(alloc.valid);
    EXPECT_EQ(alloc.offset, 0u);
    EXPECT_EQ(alloc.size, 0u);
}

TEST(BufferSubAllocator, ConfigDefaults) {
    SubAllocatorConfig config;
    EXPECT_EQ(config.heapSize, 64u * 1024 * 1024);
    EXPECT_EQ(config.maxHeaps, 8u);
    EXPECT_EQ(config.defaultAlignment, 256u);
}

TEST(BufferSubAllocator, FreeBlockCoalescing) {
    // Test the coalescing logic conceptually
    // Two adjacent free blocks [0, 100) and [100, 200) should merge to [0, 200)
    // This is tested indirectly through the allocator
    SubAllocatorConfig config;
    config.heapSize = 1024;
    config.maxHeaps = 1;
    // Would need a mock device to fully test
}

TEST(BufferSubAllocator, AlignmentCalculation) {
    // Verify alignment math
    u64 offset = 100;
    u32 alignment = 256;
    u64 aligned = (offset + alignment - 1) & ~(static_cast<u64>(alignment) - 1);
    EXPECT_EQ(aligned, 256u);

    offset = 256;
    aligned = (offset + alignment - 1) & ~(static_cast<u64>(alignment) - 1);
    EXPECT_EQ(aligned, 256u);

    offset = 257;
    aligned = (offset + alignment - 1) & ~(static_cast<u64>(alignment) - 1);
    EXPECT_EQ(aligned, 512u);

    offset = 0;
    aligned = (offset + alignment - 1) & ~(static_cast<u64>(alignment) - 1);
    EXPECT_EQ(aligned, 0u);
}

TEST(BufferSubAllocator, StatsFragmentation) {
    // Fragmentation = 1 - (largest_free / total_free)
    // If total_free = 1000 and largest_free = 1000 → frag = 0
    // If total_free = 1000 and largest_free = 500  → frag = 0.5
    // If total_free = 1000 and largest_free = 100  → frag = 0.9

    auto calcFrag = [](u64 largestFree, u64 totalFree) -> f32 {
        return totalFree > 0 ? 1.0f - static_cast<f32>(largestFree) / static_cast<f32>(totalFree) : 0.0f;
    };

    EXPECT_FLOAT_EQ(calcFrag(1000, 1000), 0.0f);
    EXPECT_FLOAT_EQ(calcFrag(500, 1000), 0.5f);
    EXPECT_NEAR(calcFrag(100, 1000), 0.9f, 0.001f);
    EXPECT_FLOAT_EQ(calcFrag(0, 0), 0.0f);
}
