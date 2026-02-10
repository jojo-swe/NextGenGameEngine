#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_ring_allocator.h"

using namespace nge::rhi;

TEST(RingAllocator, InitAndShutdown) {
    DynamicRingAllocator alloc;
    RingAllocatorConfig config;
    config.bufferSize = 1024;
    config.alignment = 16;
    EXPECT_TRUE(alloc.Init(config));
    EXPECT_EQ(alloc.GetWriteOffset(), 0u);
    alloc.Shutdown();
}

TEST(RingAllocator, BasicAllocation) {
    DynamicRingAllocator alloc;
    RingAllocatorConfig config;
    config.bufferSize = 4096;
    config.alignment = 16;
    alloc.Init(config);

    auto result = alloc.Allocate(64);
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.offset, 0u);
    EXPECT_EQ(result.size, 64u);
    EXPECT_GE(result.alignedSize, 64u);

    alloc.Shutdown();
}

TEST(RingAllocator, AlignmentPadding) {
    DynamicRingAllocator alloc;
    RingAllocatorConfig config;
    config.bufferSize = 4096;
    config.alignment = 256;
    alloc.Init(config);

    auto r1 = alloc.Allocate(100); // 100 bytes -> aligned to 256
    EXPECT_TRUE(r1.valid);
    EXPECT_EQ(r1.alignedSize, 256u);

    auto r2 = alloc.Allocate(50);
    EXPECT_TRUE(r2.valid);
    EXPECT_EQ(r2.offset, 256u); // After first aligned allocation

    alloc.Shutdown();
}

TEST(RingAllocator, MultipleAllocations) {
    DynamicRingAllocator alloc;
    RingAllocatorConfig config;
    config.bufferSize = 4096;
    config.alignment = 16;
    alloc.Init(config);

    auto r1 = alloc.Allocate(128);
    auto r2 = alloc.Allocate(256);
    auto r3 = alloc.Allocate(64);

    EXPECT_TRUE(r1.valid);
    EXPECT_TRUE(r2.valid);
    EXPECT_TRUE(r3.valid);

    // Offsets should be sequential
    EXPECT_LT(r1.offset, r2.offset);
    EXPECT_LT(r2.offset, r3.offset);

    alloc.Shutdown();
}

TEST(RingAllocator, AllocationFailsWhenFull) {
    DynamicRingAllocator alloc;
    RingAllocatorConfig config;
    config.bufferSize = 512;
    config.alignment = 16;
    alloc.Init(config);

    auto r1 = alloc.Allocate(400);
    EXPECT_TRUE(r1.valid);

    auto r2 = alloc.Allocate(400); // Won't fit
    EXPECT_FALSE(r2.valid);

    auto stats = alloc.GetStats();
    EXPECT_EQ(stats.allocationsFailed, 1u);

    alloc.Shutdown();
}

TEST(RingAllocator, ZeroSizeAllocation) {
    DynamicRingAllocator alloc;
    RingAllocatorConfig config;
    config.bufferSize = 1024;
    alloc.Init(config);

    auto result = alloc.Allocate(0);
    EXPECT_FALSE(result.valid);

    alloc.Shutdown();
}

TEST(RingAllocator, CanAllocateCheck) {
    DynamicRingAllocator alloc;
    RingAllocatorConfig config;
    config.bufferSize = 1024;
    config.alignment = 16;
    alloc.Init(config);

    EXPECT_TRUE(alloc.CanAllocate(512));
    EXPECT_TRUE(alloc.CanAllocate(1024));
    EXPECT_FALSE(alloc.CanAllocate(2048));

    alloc.Shutdown();
}

TEST(RingAllocator, BeginFrameAdvancesCounter) {
    DynamicRingAllocator alloc;
    RingAllocatorConfig config;
    config.bufferSize = 4096;
    alloc.Init(config);

    EXPECT_EQ(alloc.GetCurrentFrame(), 0u);

    alloc.BeginFrame();
    EXPECT_EQ(alloc.GetCurrentFrame(), 1u);

    alloc.BeginFrame();
    EXPECT_EQ(alloc.GetCurrentFrame(), 2u);

    alloc.Shutdown();
}

TEST(RingAllocator, FrameCompletedReclaimsSpace) {
    DynamicRingAllocator alloc;
    RingAllocatorConfig config;
    config.bufferSize = 1024;
    config.alignment = 16;
    alloc.Init(config);

    alloc.BeginFrame(); // Frame 0
    alloc.Allocate(512);

    u64 freeBeforeReclaim = alloc.GetFreeSpace();

    alloc.FrameCompleted(0); // Reclaim frame 0

    u64 freeAfterReclaim = alloc.GetFreeSpace();
    EXPECT_GE(freeAfterReclaim, freeBeforeReclaim);

    alloc.Shutdown();
}

TEST(RingAllocator, MultiFrameReclamation) {
    DynamicRingAllocator alloc;
    RingAllocatorConfig config;
    config.bufferSize = 4096;
    config.alignment = 256;
    alloc.Init(config);

    // Frame 0
    alloc.BeginFrame();
    alloc.Allocate(256);

    // Frame 1
    alloc.BeginFrame();
    alloc.Allocate(256);

    // Frame 2
    alloc.BeginFrame();
    alloc.Allocate(256);

    // Complete frame 0 -> should reclaim its space
    alloc.FrameCompleted(0);

    auto stats = alloc.GetStats();
    EXPECT_EQ(stats.totalFramesCompleted, 1u);
    EXPECT_EQ(stats.totalAllocations, 3u);

    alloc.Shutdown();
}

TEST(RingAllocator, GetFreeSpace) {
    DynamicRingAllocator alloc;
    RingAllocatorConfig config;
    config.bufferSize = 1024;
    config.alignment = 16;
    alloc.Init(config);

    EXPECT_EQ(alloc.GetFreeSpace(), 1024u);

    alloc.Allocate(256);
    EXPECT_LT(alloc.GetFreeSpace(), 1024u);

    alloc.Shutdown();
}

TEST(RingAllocator, GetUsedSpace) {
    DynamicRingAllocator alloc;
    RingAllocatorConfig config;
    config.bufferSize = 1024;
    config.alignment = 256;
    alloc.Init(config);

    EXPECT_EQ(alloc.GetUsedSpace(), 0u);

    alloc.Allocate(100); // Aligned to 256
    EXPECT_EQ(alloc.GetUsedSpace(), 256u);

    alloc.Shutdown();
}

TEST(RingAllocator, PeakUsageTracking) {
    DynamicRingAllocator alloc;
    RingAllocatorConfig config;
    config.bufferSize = 4096;
    config.alignment = 16;
    alloc.Init(config);

    alloc.BeginFrame();
    alloc.Allocate(1024);
    alloc.Allocate(512);

    auto stats = alloc.GetStats();
    EXPECT_GE(stats.peakUsedBytes, 1536u);

    alloc.Shutdown();
}

TEST(RingAllocator, StatsTracking) {
    DynamicRingAllocator alloc;
    RingAllocatorConfig config;
    config.bufferSize = 4096;
    config.alignment = 16;
    alloc.Init(config);

    alloc.BeginFrame();
    alloc.Allocate(128);
    alloc.Allocate(256);

    auto stats = alloc.GetStats();
    EXPECT_EQ(stats.bufferSize, 4096u);
    EXPECT_EQ(stats.totalAllocations, 2u);
    EXPECT_GT(stats.usedBytes, 0u);
    EXPECT_GT(stats.utilization, 0.0f);

    alloc.Shutdown();
}

TEST(RingAllocator, ResetClearsAll) {
    DynamicRingAllocator alloc;
    RingAllocatorConfig config;
    config.bufferSize = 4096;
    config.alignment = 16;
    alloc.Init(config);

    alloc.BeginFrame();
    alloc.Allocate(1024);

    alloc.Reset();

    EXPECT_EQ(alloc.GetWriteOffset(), 0u);
    EXPECT_EQ(alloc.GetUsedSpace(), 0u);
    EXPECT_EQ(alloc.GetCurrentFrame(), 0u);

    auto stats = alloc.GetStats();
    EXPECT_EQ(stats.totalAllocations, 0u);
    EXPECT_EQ(stats.peakUsedBytes, 0u);

    alloc.Shutdown();
}

TEST(RingAllocator, AllocationFrameIndex) {
    DynamicRingAllocator alloc;
    RingAllocatorConfig config;
    config.bufferSize = 4096;
    config.alignment = 16;
    alloc.Init(config);

    alloc.BeginFrame(); // Frame 0 -> currentFrame becomes 1

    auto r = alloc.Allocate(64);
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.frameIndex, 1u); // Allocated during frame 1

    alloc.Shutdown();
}

TEST(RingAllocator, LargeAlignment) {
    DynamicRingAllocator alloc;
    RingAllocatorConfig config;
    config.bufferSize = 65536;
    config.alignment = 4096; // Page alignment
    alloc.Init(config);

    auto r1 = alloc.Allocate(100);
    EXPECT_TRUE(r1.valid);
    EXPECT_EQ(r1.alignedSize, 4096u);
    EXPECT_EQ(r1.offset % 4096, 0u);

    auto r2 = alloc.Allocate(200);
    EXPECT_TRUE(r2.valid);
    EXPECT_EQ(r2.offset % 4096, 0u);

    alloc.Shutdown();
}
