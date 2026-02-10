#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_frame_resource_gc.h"

using namespace nge::rhi;

TEST(FrameResourceGC, InitAndShutdown) {
    FrameResourceGC gc;
    EXPECT_TRUE(gc.Init());
    EXPECT_EQ(gc.GetPendingCount(), 0u);
    gc.Shutdown();
}

TEST(FrameResourceGC, QueueDestroy) {
    FrameResourceGC gc;
    gc.Init();

    bool destroyed = false;
    EXPECT_TRUE(gc.QueueDestroy(100, GCResourceType::Buffer, 0,
                                 [&](u64 h) { destroyed = true; }, "TestBuffer"));
    EXPECT_EQ(gc.GetPendingCount(), 1u);
    EXPECT_TRUE(gc.IsPending(100));

    gc.FlushAll();
    EXPECT_TRUE(destroyed);

    gc.Shutdown();
}

TEST(FrameResourceGC, CollectAfterDeferFrames) {
    FrameResourceGC gc;
    FrameResourceGCConfig config;
    config.framesToDefer = 3;
    gc.Init(config);

    u32 destroyCount = 0;
    gc.QueueDestroy(1, GCResourceType::Image, 0, [&](u64) { destroyCount++; });
    gc.QueueDestroy(2, GCResourceType::ImageView, 1, [&](u64) { destroyCount++; });

    // Frame 0, 1, 2: too early for resource queued at frame 0
    EXPECT_EQ(gc.Collect(0), 0u);
    EXPECT_EQ(gc.Collect(1), 0u);
    EXPECT_EQ(gc.Collect(2), 0u);

    // Frame 3: resource queued at frame 0 can be destroyed (0 + 3 = 3)
    EXPECT_EQ(gc.Collect(3), 1u);
    EXPECT_EQ(destroyCount, 1u);

    // Frame 4: resource queued at frame 1 can be destroyed (1 + 3 = 4)
    EXPECT_EQ(gc.Collect(4), 1u);
    EXPECT_EQ(destroyCount, 2u);

    EXPECT_EQ(gc.GetPendingCount(), 0u);

    gc.Shutdown();
}

TEST(FrameResourceGC, CollectNothingWhenEmpty) {
    FrameResourceGC gc;
    gc.Init();

    EXPECT_EQ(gc.Collect(100), 0u);

    gc.Shutdown();
}

TEST(FrameResourceGC, FlushAllDestroysEverything) {
    FrameResourceGC gc;
    FrameResourceGCConfig config;
    config.framesToDefer = 100; // Very long defer
    gc.Init(config);

    u32 destroyCount = 0;
    gc.QueueDestroy(1, GCResourceType::Buffer, 0, [&](u64) { destroyCount++; });
    gc.QueueDestroy(2, GCResourceType::Pipeline, 0, [&](u64) { destroyCount++; });
    gc.QueueDestroy(3, GCResourceType::Sampler, 0, [&](u64) { destroyCount++; });

    u32 flushed = gc.FlushAll();
    EXPECT_EQ(flushed, 3u);
    EXPECT_EQ(destroyCount, 3u);
    EXPECT_EQ(gc.GetPendingCount(), 0u);

    gc.Shutdown();
}

TEST(FrameResourceGC, DestructorReceivesHandle) {
    FrameResourceGC gc;
    FrameResourceGCConfig config;
    config.framesToDefer = 0;
    gc.Init(config);

    u64 receivedHandle = 0;
    gc.QueueDestroy(42, GCResourceType::Buffer, 0, [&](u64 h) { receivedHandle = h; });

    gc.Collect(0);
    EXPECT_EQ(receivedHandle, 42u);

    gc.Shutdown();
}

TEST(FrameResourceGC, IsPending) {
    FrameResourceGC gc;
    gc.Init();

    gc.QueueDestroy(100, GCResourceType::Buffer, 0, [](u64) {});
    gc.QueueDestroy(200, GCResourceType::Image, 0, [](u64) {});

    EXPECT_TRUE(gc.IsPending(100));
    EXPECT_TRUE(gc.IsPending(200));
    EXPECT_FALSE(gc.IsPending(300));

    gc.Shutdown();
}

TEST(FrameResourceGC, GetPendingCountByType) {
    FrameResourceGC gc;
    gc.Init();

    gc.QueueDestroy(1, GCResourceType::Buffer, 0, [](u64) {});
    gc.QueueDestroy(2, GCResourceType::Buffer, 0, [](u64) {});
    gc.QueueDestroy(3, GCResourceType::Image, 0, [](u64) {});
    gc.QueueDestroy(4, GCResourceType::Pipeline, 0, [](u64) {});

    EXPECT_EQ(gc.GetPendingCountByType(GCResourceType::Buffer), 2u);
    EXPECT_EQ(gc.GetPendingCountByType(GCResourceType::Image), 1u);
    EXPECT_EQ(gc.GetPendingCountByType(GCResourceType::Pipeline), 1u);
    EXPECT_EQ(gc.GetPendingCountByType(GCResourceType::Sampler), 0u);

    gc.Shutdown();
}

TEST(FrameResourceGC, MaxPendingLimit) {
    FrameResourceGC gc;
    FrameResourceGCConfig config;
    config.maxPendingEntries = 3;
    gc.Init(config);

    EXPECT_TRUE(gc.QueueDestroy(1, GCResourceType::Buffer, 0, [](u64) {}));
    EXPECT_TRUE(gc.QueueDestroy(2, GCResourceType::Buffer, 0, [](u64) {}));
    EXPECT_TRUE(gc.QueueDestroy(3, GCResourceType::Buffer, 0, [](u64) {}));
    EXPECT_FALSE(gc.QueueDestroy(4, GCResourceType::Buffer, 0, [](u64) {})); // Exceeds

    gc.Shutdown();
}

TEST(FrameResourceGC, NullDestructor) {
    FrameResourceGC gc;
    FrameResourceGCConfig config;
    config.framesToDefer = 0;
    gc.Init(config);

    gc.QueueDestroy(1, GCResourceType::Buffer, 0, nullptr);

    // Should not crash
    EXPECT_EQ(gc.Collect(0), 1u);

    gc.Shutdown();
}

TEST(FrameResourceGC, MultipleFrameCollection) {
    FrameResourceGC gc;
    FrameResourceGCConfig config;
    config.framesToDefer = 2;
    gc.Init(config);

    u32 destroyCount = 0;
    auto destructor = [&](u64) { destroyCount++; };

    // Queue resources across multiple frames
    gc.QueueDestroy(1, GCResourceType::Buffer, 0, destructor);
    gc.QueueDestroy(2, GCResourceType::Image, 1, destructor);
    gc.QueueDestroy(3, GCResourceType::Pipeline, 2, destructor);
    gc.QueueDestroy(4, GCResourceType::Sampler, 3, destructor);

    gc.Collect(2); // Destroys resource from frame 0
    EXPECT_EQ(destroyCount, 1u);

    gc.Collect(3); // Destroys resource from frame 1
    EXPECT_EQ(destroyCount, 2u);

    gc.Collect(5); // Destroys resources from frames 2 and 3
    EXPECT_EQ(destroyCount, 4u);

    EXPECT_EQ(gc.GetPendingCount(), 0u);

    gc.Shutdown();
}

TEST(FrameResourceGC, StatsTracking) {
    FrameResourceGC gc;
    FrameResourceGCConfig config;
    config.framesToDefer = 1;
    gc.Init(config);

    gc.QueueDestroy(1, GCResourceType::Buffer, 0, [](u64) {});
    gc.QueueDestroy(2, GCResourceType::Image, 0, [](u64) {});
    gc.QueueDestroy(3, GCResourceType::Buffer, 1, [](u64) {});

    gc.Collect(1); // Destroys 2 from frame 0

    auto stats = gc.GetStats();
    EXPECT_EQ(stats.totalQueued, 3u);
    EXPECT_EQ(stats.totalDestroyed, 2u);
    EXPECT_EQ(stats.pendingEntries, 1u);
    EXPECT_GE(stats.peakPending, 3u);
    EXPECT_EQ(stats.pendingByType[static_cast<u32>(GCResourceType::Buffer)], 1u);

    gc.Shutdown();
}

TEST(FrameResourceGC, ResetClearsAll) {
    FrameResourceGC gc;
    gc.Init();

    gc.QueueDestroy(1, GCResourceType::Buffer, 0, [](u64) {});

    gc.Reset();

    EXPECT_EQ(gc.GetPendingCount(), 0u);
    auto stats = gc.GetStats();
    EXPECT_EQ(stats.totalQueued, 0u);
    EXPECT_EQ(stats.totalDestroyed, 0u);
    EXPECT_EQ(stats.peakPending, 0u);

    gc.Shutdown();
}

TEST(FrameResourceGC, ZeroDeferFrames) {
    FrameResourceGC gc;
    FrameResourceGCConfig config;
    config.framesToDefer = 0;
    gc.Init(config);

    u32 destroyCount = 0;
    gc.QueueDestroy(1, GCResourceType::Buffer, 5, [&](u64) { destroyCount++; });

    EXPECT_EQ(gc.Collect(5), 1u); // Immediate collection
    EXPECT_EQ(destroyCount, 1u);

    gc.Shutdown();
}

TEST(FrameResourceGC, AllResourceTypes) {
    FrameResourceGC gc;
    FrameResourceGCConfig config;
    config.framesToDefer = 0;
    gc.Init(config);

    u32 count = 0;
    auto d = [&](u64) { count++; };

    gc.QueueDestroy(0, GCResourceType::Buffer, 0, d);
    gc.QueueDestroy(1, GCResourceType::Image, 0, d);
    gc.QueueDestroy(2, GCResourceType::ImageView, 0, d);
    gc.QueueDestroy(3, GCResourceType::Sampler, 0, d);
    gc.QueueDestroy(4, GCResourceType::DescriptorSet, 0, d);
    gc.QueueDestroy(5, GCResourceType::Pipeline, 0, d);
    gc.QueueDestroy(6, GCResourceType::RenderPass, 0, d);
    gc.QueueDestroy(7, GCResourceType::Framebuffer, 0, d);
    gc.QueueDestroy(8, GCResourceType::CommandBuffer, 0, d);
    gc.QueueDestroy(9, GCResourceType::Other, 0, d);

    EXPECT_EQ(gc.GetPendingCount(), 10u);
    EXPECT_EQ(gc.Collect(0), 10u);
    EXPECT_EQ(count, 10u);

    gc.Shutdown();
}
