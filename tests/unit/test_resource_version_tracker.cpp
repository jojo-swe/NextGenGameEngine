#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_resource_version_tracker.h"

using namespace nge;
using namespace nge::rhi;

TEST(ResourceVersionTracker, InitAndShutdown) {
    ResourceVersionTracker tracker;
    EXPECT_TRUE(tracker.Init());

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.aliveResources, 0u);
    EXPECT_EQ(stats.totalCreated, 0u);

    tracker.Shutdown();
}

TEST(ResourceVersionTracker, RegisterAndValidate) {
    ResourceVersionTracker tracker;
    tracker.Init();

    auto vh = tracker.Register(100, "TestBuffer");
    EXPECT_EQ(vh.handle, 100u);
    EXPECT_EQ(vh.generation, 1u);
    EXPECT_TRUE(tracker.IsValid(vh));
    EXPECT_TRUE(tracker.IsAlive(100));
    EXPECT_EQ(tracker.GetDebugName(100), "TestBuffer");

    tracker.Shutdown();
}

TEST(ResourceVersionTracker, DestroyInvalidatesHandle) {
    ResourceVersionTracker tracker;
    tracker.Init();

    auto vh = tracker.Register(1, "Buf");
    EXPECT_TRUE(tracker.IsValid(vh));

    tracker.Destroy(1);

    EXPECT_FALSE(tracker.IsValid(vh)); // Generation mismatch
    EXPECT_FALSE(tracker.IsAlive(1));
    EXPECT_EQ(tracker.GetGeneration(1), 2u); // Bumped to 2

    tracker.Shutdown();
}

TEST(ResourceVersionTracker, ReregisterBumpsGeneration) {
    ResourceVersionTracker tracker;
    tracker.Init();

    auto vh1 = tracker.Register(5, "OriginalBuffer");
    tracker.Destroy(5);

    auto vh2 = tracker.Reregister(5, "RecycledBuffer");
    EXPECT_EQ(vh2.handle, 5u);
    EXPECT_EQ(vh2.generation, 3u); // Was 1, destroy bumped to 2, reregister bumps to 3

    EXPECT_FALSE(tracker.IsValid(vh1)); // Old generation
    EXPECT_TRUE(tracker.IsValid(vh2));  // New generation
    EXPECT_TRUE(tracker.IsAlive(5));
    EXPECT_EQ(tracker.GetDebugName(5), "RecycledBuffer");

    tracker.Shutdown();
}

TEST(ResourceVersionTracker, ReregisterNewHandle) {
    ResourceVersionTracker tracker;
    tracker.Init();

    // Reregister a handle that was never registered
    auto vh = tracker.Reregister(999, "NewResource");
    EXPECT_EQ(vh.handle, 999u);
    EXPECT_EQ(vh.generation, 1u);
    EXPECT_TRUE(tracker.IsValid(vh));

    tracker.Shutdown();
}

TEST(ResourceVersionTracker, StaleAccessRecording) {
    ResourceVersionTracker tracker;
    tracker.Init();

    auto vh = tracker.Register(10, "StaleTarget");
    tracker.Destroy(10);

    // Try to use the old handle
    EXPECT_FALSE(tracker.IsValid(vh));
    tracker.RecordStaleAccess(vh);

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.staleAccessesDetected, 1u);

    tracker.Shutdown();
}

TEST(ResourceVersionTracker, DestroyedHistory) {
    ResourceVersionTracker tracker;
    ResourceVersionConfig config;
    config.trackDestroyedHistory = true;
    config.destroyedHistorySize = 5;
    tracker.Init(config);

    for (u64 i = 1; i <= 7; ++i) {
        tracker.Register(i, "Res" + std::to_string(i));
        tracker.Destroy(i);
    }

    auto history = tracker.GetDestroyedHistory();
    EXPECT_EQ(history.size(), 5u); // Capped at 5

    // Most recent should be Res7
    EXPECT_NE(history.back().find("Res7"), std::string::npos);

    tracker.Shutdown();
}

TEST(ResourceVersionTracker, GetGenerationUnknownHandle) {
    ResourceVersionTracker tracker;
    tracker.Init();

    EXPECT_EQ(tracker.GetGeneration(12345), 0u);
    EXPECT_FALSE(tracker.IsAlive(12345));
    EXPECT_EQ(tracker.GetDebugName(12345), "");

    tracker.Shutdown();
}

TEST(ResourceVersionTracker, MultipleRegistrations) {
    ResourceVersionTracker tracker;
    tracker.Init();

    auto vh1 = tracker.Register(1, "A");
    auto vh2 = tracker.Register(2, "B");
    auto vh3 = tracker.Register(3, "C");

    EXPECT_TRUE(tracker.IsValid(vh1));
    EXPECT_TRUE(tracker.IsValid(vh2));
    EXPECT_TRUE(tracker.IsValid(vh3));

    tracker.Destroy(2);

    EXPECT_TRUE(tracker.IsValid(vh1));
    EXPECT_FALSE(tracker.IsValid(vh2));
    EXPECT_TRUE(tracker.IsValid(vh3));

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.aliveResources, 2u);
    EXPECT_EQ(stats.totalCreated, 3u);
    EXPECT_EQ(stats.totalDestroyed, 1u);

    tracker.Shutdown();
}

TEST(ResourceVersionTracker, ResetClearsAll) {
    ResourceVersionTracker tracker;
    tracker.Init();

    tracker.Register(1, "A");
    tracker.Register(2, "B");
    tracker.Destroy(1);

    EXPECT_EQ(tracker.GetStats().aliveResources, 1u);

    tracker.Reset();

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.aliveResources, 0u);
    EXPECT_EQ(stats.totalCreated, 0u);
    EXPECT_EQ(stats.totalDestroyed, 0u);
    EXPECT_EQ(stats.staleAccessesDetected, 0u);
    EXPECT_TRUE(tracker.GetDestroyedHistory().empty());

    tracker.Shutdown();
}

TEST(ResourceVersionTracker, MaxGenerationTracked) {
    ResourceVersionTracker tracker;
    tracker.Init();

    auto vh = tracker.Register(1, "Recycled");
    tracker.Destroy(1);               // gen 2
    tracker.Reregister(1, "Recycled");// gen 3
    tracker.Destroy(1);               // gen 4
    tracker.Reregister(1, "Recycled");// gen 5

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.maxGeneration, 5u);

    tracker.Shutdown();
}

TEST(ResourceVersionTracker, DestroyUnknownHandleNoOp) {
    ResourceVersionTracker tracker;
    tracker.Init();

    // Should not crash
    tracker.Destroy(999);

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.totalDestroyed, 0u);

    tracker.Shutdown();
}

TEST(ResourceVersionTracker, InvalidHandleNotValid) {
    ResourceVersionTracker tracker;
    tracker.Init();

    VersionedHandle fake;
    fake.handle = 42;
    fake.generation = 1;

    EXPECT_FALSE(tracker.IsValid(fake));

    tracker.Shutdown();
}
