#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_frame_resource_tracker.h"

using namespace nge::rhi;

TEST(FrameResourceTracker, InitAndShutdown) {
    FrameResourceTracker tracker;
    EXPECT_TRUE(tracker.Init());

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.currentResourceCount, 0u);
    EXPECT_EQ(stats.currentBytesAlive, 0u);

    tracker.Shutdown();
}

TEST(FrameResourceTracker, TrackCreateAndDestroy) {
    FrameResourceTracker tracker;
    tracker.Init();

    tracker.OnResourceCreated(1, FrameResourceType::Buffer, 1024, "TestBuffer");
    tracker.OnResourceCreated(2, FrameResourceType::Image, 4096, "TestImage");

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.currentResourceCount, 2u);
    EXPECT_EQ(stats.currentBytesAlive, 5120u);

    tracker.OnResourceDestroyed(1);

    stats = tracker.GetStats();
    EXPECT_EQ(stats.currentResourceCount, 1u);
    EXPECT_EQ(stats.currentBytesAlive, 4096u);

    tracker.Shutdown();
}

TEST(FrameResourceTracker, PeakTracking) {
    FrameResourceTracker tracker;
    tracker.Init();

    tracker.OnResourceCreated(1, FrameResourceType::Buffer, 2048, "A");
    tracker.OnResourceCreated(2, FrameResourceType::Buffer, 2048, "B");
    tracker.OnResourceCreated(3, FrameResourceType::Buffer, 2048, "C");

    // Peak: 3 resources, 6144 bytes
    tracker.OnResourceDestroyed(2);

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.peakResourceCount, 3u);
    EXPECT_EQ(stats.peakBytesAlive, 6144u);
    EXPECT_EQ(stats.currentResourceCount, 2u);
    EXPECT_EQ(stats.currentBytesAlive, 4096u);

    tracker.Shutdown();
}

TEST(FrameResourceTracker, GetResourceByHandle) {
    FrameResourceTracker tracker;
    tracker.Init();

    tracker.OnResourceCreated(42, FrameResourceType::Pipeline, 0, "MyPipeline");

    const auto* res = tracker.GetResource(42);
    EXPECT_NE(res, nullptr);
    EXPECT_EQ(res->handle, 42u);
    EXPECT_EQ(res->type, FrameResourceType::Pipeline);
    EXPECT_EQ(res->debugName, "MyPipeline");

    EXPECT_EQ(tracker.GetResource(999), nullptr);

    tracker.Shutdown();
}

TEST(FrameResourceTracker, CountByType) {
    FrameResourceTracker tracker;
    tracker.Init();

    tracker.OnResourceCreated(1, FrameResourceType::Buffer, 100, "Buf1");
    tracker.OnResourceCreated(2, FrameResourceType::Buffer, 200, "Buf2");
    tracker.OnResourceCreated(3, FrameResourceType::Image, 400, "Img1");
    tracker.OnResourceCreated(4, FrameResourceType::Sampler, 0, "Samp1");

    EXPECT_EQ(tracker.GetCountByType(FrameResourceType::Buffer), 2u);
    EXPECT_EQ(tracker.GetCountByType(FrameResourceType::Image), 1u);
    EXPECT_EQ(tracker.GetCountByType(FrameResourceType::Sampler), 1u);
    EXPECT_EQ(tracker.GetCountByType(FrameResourceType::Pipeline), 0u);

    tracker.Shutdown();
}

TEST(FrameResourceTracker, EndFrameRecordsSnapshot) {
    FrameResourceTracker tracker;
    tracker.Init();

    tracker.OnResourceCreated(1, FrameResourceType::Buffer, 512, "Buf");
    tracker.EndFrame(0);

    auto& history = tracker.GetHistory();
    EXPECT_EQ(history.size(), 1u);
    EXPECT_EQ(history[0].frameIndex, 0u);
    EXPECT_EQ(history[0].totalAllocations, 1u);
    EXPECT_EQ(history[0].totalBytesAllocated, 512u);
    EXPECT_EQ(history[0].netBytesAlive, 512u);

    tracker.OnResourceDestroyed(1);
    tracker.EndFrame(1);

    EXPECT_EQ(history.size(), 2u);
    EXPECT_EQ(history[1].totalDeallocations, 1u);
    EXPECT_EQ(history[1].netBytesAlive, 0u);

    tracker.Shutdown();
}

TEST(FrameResourceTracker, PotentialLeakDetection) {
    FrameResourceTracker tracker;
    tracker.Init();

    tracker.OnResourceCreated(1, FrameResourceType::Buffer, 256, "LeakyBuffer");
    tracker.EndFrame(0);

    // Not a leak at frame 10 (too young)
    auto leaks = tracker.GetPotentialLeaks(10, 300);
    EXPECT_TRUE(leaks.empty());

    // Leak at frame 500 (created at 0, never used since)
    leaks = tracker.GetPotentialLeaks(500, 300);
    EXPECT_EQ(leaks.size(), 1u);
    EXPECT_EQ(leaks[0].debugName, "LeakyBuffer");

    tracker.Shutdown();
}

TEST(FrameResourceTracker, TransientResourcesExcludedFromLeaks) {
    FrameResourceTracker tracker;
    tracker.Init();

    tracker.OnResourceCreated(1, FrameResourceType::Image, 1024, "TransientRT", true);
    tracker.EndFrame(0);

    auto leaks = tracker.GetPotentialLeaks(1000, 300);
    EXPECT_TRUE(leaks.empty()); // Transients are excluded

    tracker.Shutdown();
}

TEST(FrameResourceTracker, StaleResourceDetection) {
    FrameResourceTracker tracker;
    FrameResourceTrackerConfig config;
    config.staleResourceFrames = 10;
    tracker.Init(config);

    tracker.OnResourceCreated(1, FrameResourceType::Buffer, 256, "StaleBuffer");
    tracker.OnResourceUsed(1);
    tracker.EndFrame(0);

    // Not stale at frame 5
    auto stale = tracker.GetStaleResources(5);
    EXPECT_TRUE(stale.empty());

    // Stale at frame 20 (last used at frame 0, threshold is 10)
    stale = tracker.GetStaleResources(20);
    EXPECT_EQ(stale.size(), 1u);

    // Use it again
    tracker.OnResourceUsed(1);
    tracker.EndFrame(20);

    // No longer stale at frame 25
    stale = tracker.GetStaleResources(25);
    EXPECT_TRUE(stale.empty());

    tracker.Shutdown();
}

TEST(FrameResourceTracker, ResetClearsAll) {
    FrameResourceTracker tracker;
    tracker.Init();

    tracker.OnResourceCreated(1, FrameResourceType::Buffer, 1024, "Buf");
    tracker.EndFrame(0);

    EXPECT_EQ(tracker.GetStats().currentResourceCount, 1u);
    EXPECT_FALSE(tracker.GetHistory().empty());

    tracker.Reset();

    EXPECT_EQ(tracker.GetStats().currentResourceCount, 0u);
    EXPECT_EQ(tracker.GetStats().currentBytesAlive, 0u);
    EXPECT_TRUE(tracker.GetHistory().empty());

    tracker.Shutdown();
}

TEST(FrameResourceTracker, DisabledModeSkipsTracking) {
    FrameResourceTracker tracker;
    FrameResourceTrackerConfig config;
    config.enabled = false;
    tracker.Init(config);

    tracker.OnResourceCreated(1, FrameResourceType::Buffer, 1024, "Ignored");

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.currentResourceCount, 0u);

    tracker.Shutdown();
}

TEST(FrameResourceTracker, MaxTrackedLimit) {
    FrameResourceTracker tracker;
    FrameResourceTrackerConfig config;
    config.maxTrackedResources = 3;
    tracker.Init(config);

    tracker.OnResourceCreated(1, FrameResourceType::Buffer, 100, "A");
    tracker.OnResourceCreated(2, FrameResourceType::Buffer, 100, "B");
    tracker.OnResourceCreated(3, FrameResourceType::Buffer, 100, "C");
    tracker.OnResourceCreated(4, FrameResourceType::Buffer, 100, "D"); // Exceeds limit

    EXPECT_EQ(tracker.GetStats().currentResourceCount, 3u);

    tracker.Shutdown();
}

TEST(FrameResourceTracker, DestroyUnknownHandleNoOp) {
    FrameResourceTracker tracker;
    tracker.Init();

    // Should not crash
    tracker.OnResourceDestroyed(999);

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.currentResourceCount, 0u);

    tracker.Shutdown();
}
