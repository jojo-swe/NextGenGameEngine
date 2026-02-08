#include <gtest/gtest.h>
#include "engine/core/memory/frame_allocator.h"
#include <cstring>

using namespace nge;
using namespace nge::memory;

TEST(FrameAllocator, BasicAllocation) {
    FrameAllocator alloc(1024);
    alloc.BeginFrame();

    void* ptr = alloc.Allocate(64);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(alloc.GetAllocatedBytes(), 64u);
}

TEST(FrameAllocator, AlignedAllocation) {
    FrameAllocator alloc(4096);
    alloc.BeginFrame();

    // Allocate 1 byte, then request 256-byte aligned allocation
    alloc.Allocate(1, 1);
    void* ptr = alloc.Allocate(32, 256);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 256, 0u);
}

TEST(FrameAllocator, TypedAllocation) {
    FrameAllocator alloc(4096);
    alloc.BeginFrame();

    struct alignas(16) TestStruct {
        float x, y, z, w;
    };

    auto* data = alloc.Allocate<TestStruct>(10);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(data) % alignof(TestStruct), 0u);

    // Write and verify
    for (int i = 0; i < 10; ++i) {
        data[i] = {static_cast<float>(i), 0, 0, 0};
    }
    EXPECT_FLOAT_EQ(data[5].x, 5.0f);
}

TEST(FrameAllocator, ResetOnBeginFrame) {
    FrameAllocator alloc(1024);
    alloc.BeginFrame();

    alloc.Allocate(512);
    EXPECT_EQ(alloc.GetAllocatedBytes(), 512u);

    alloc.BeginFrame(); // Should reset
    EXPECT_EQ(alloc.GetAllocatedBytes(), 0u);
    EXPECT_EQ(alloc.GetRemainingBytes(), 1024u);
}

TEST(FrameAllocator, DoubleBuffering) {
    FrameAllocator alloc(256);

    // Frame 0: write to block 0
    alloc.BeginFrame();
    auto* frame0Data = alloc.Allocate<u32>(4);
    frame0Data[0] = 0xDEADBEEF;

    // Frame 1: writes to block 1, block 0 data still valid
    alloc.BeginFrame();
    auto* frame1Data = alloc.Allocate<u32>(4);
    frame1Data[0] = 0xCAFEBABE;

    // Both should be different pointers
    EXPECT_NE(frame0Data, frame1Data);

    // Previous frame data should still be intact
    EXPECT_EQ(frame0Data[0], 0xDEADBEEF);
    EXPECT_EQ(frame1Data[0], 0xCAFEBABE);
}

TEST(FrameAllocator, PeakUsageTracking) {
    FrameAllocator alloc(4096);

    alloc.BeginFrame();
    alloc.Allocate(1000);
    alloc.BeginFrame(); // Peak = 1000

    alloc.Allocate(500);
    alloc.BeginFrame(); // Peak still 1000

    EXPECT_EQ(alloc.GetPeakUsage(), 1000u);

    alloc.ResetStats();
    EXPECT_EQ(alloc.GetPeakUsage(), 0u);
}

TEST(FrameAllocator, RemainingBytes) {
    FrameAllocator alloc(1024);
    alloc.BeginFrame();

    EXPECT_EQ(alloc.GetRemainingBytes(), 1024u);

    alloc.Allocate(256);
    EXPECT_EQ(alloc.GetRemainingBytes(), 768u);

    alloc.Allocate(768);
    EXPECT_EQ(alloc.GetRemainingBytes(), 0u);
}

TEST(FrameAllocator, Capacity) {
    FrameAllocator alloc(2048);
    EXPECT_EQ(alloc.GetCapacity(), 2048u);
}

TEST(FrameAllocator, MultipleSmallAllocations) {
    FrameAllocator alloc(4096);
    alloc.BeginFrame();

    for (int i = 0; i < 100; ++i) {
        void* ptr = alloc.Allocate(16);
        ASSERT_NE(ptr, nullptr);
    }

    // At least 100 * 16 = 1600 bytes used (may be more due to alignment)
    EXPECT_GE(alloc.GetAllocatedBytes(), 1600u);
}

TEST(ThreadSafeFrameAllocator, BasicUsage) {
    ThreadSafeFrameAllocator alloc(4096);
    alloc.BeginFrame();

    auto* data = alloc.Allocate<f32>(10);
    ASSERT_NE(data, nullptr);
    EXPECT_GE(alloc.GetRemainingBytes(), 0u);
}
