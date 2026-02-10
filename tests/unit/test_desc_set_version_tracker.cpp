#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_desc_set_version_tracker.h"

using namespace nge;
using namespace nge::rhi;

TEST(DescSetVersionTracker, InitAndShutdown) {
    DescriptorSetVersionTracker tracker;
    EXPECT_TRUE(tracker.Init());
    EXPECT_EQ(tracker.GetSetCount(), 0u);
    tracker.Shutdown();
}

TEST(DescSetVersionTracker, RegisterSet) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterSet(4, "MaterialSet");
    EXPECT_NE(id, UINT32_MAX);
    EXPECT_EQ(tracker.GetSetCount(), 1u);

    const auto* info = tracker.GetSetInfo(id);
    EXPECT_NE(info, nullptr);
    EXPECT_EQ(info->bindingCount, 4u);
    EXPECT_EQ(info->currentVersion, 0u);
    EXPECT_EQ(info->debugName, "MaterialSet");

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, MarkUpdatedIncrementsVersion) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterSet(2);
    EXPECT_EQ(tracker.GetVersion(id), 0u);

    tracker.MarkUpdated(id, 0);
    EXPECT_EQ(tracker.GetVersion(id), 1u);

    tracker.MarkUpdated(id, 1);
    EXPECT_EQ(tracker.GetVersion(id), 2u);

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, NeedsRebindStale) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterSet(2);
    tracker.MarkUpdated(id, 0); // Version -> 1

    EXPECT_TRUE(tracker.NeedsRebind(id, 0));  // Consumer at 0, current at 1
    EXPECT_FALSE(tracker.NeedsRebind(id, 1)); // Consumer at 1, current at 1

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, NeedsRebindUnknownSet) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    EXPECT_TRUE(tracker.NeedsRebind(999, 0)); // Unknown -> always rebind

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, MarkBindingUpdatedWithHash) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterSet(4);

    tracker.MarkBindingUpdated(id, 0, 0xABCD);
    EXPECT_EQ(tracker.GetBindingVersion(id, 0), 1u);
    EXPECT_EQ(tracker.GetBindingContentHash(id, 0), 0xABCDu);

    // Set-level version also bumped
    EXPECT_EQ(tracker.GetVersion(id), 1u);

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, BindingUpdateAvoidedSameHash) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterSet(4);

    tracker.MarkBindingUpdated(id, 0, 0xABCD);
    tracker.MarkBindingUpdated(id, 0, 0xABCD); // Same hash -> avoided

    EXPECT_EQ(tracker.GetBindingVersion(id, 0), 1u); // Still 1

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.bindingUpdatesAvoided, 1u);

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, BindingUpdateDifferentHash) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterSet(4);

    tracker.MarkBindingUpdated(id, 0, 0xABCD);
    tracker.MarkBindingUpdated(id, 0, 0x1234); // Different hash -> updated

    EXPECT_EQ(tracker.GetBindingVersion(id, 0), 2u);
    EXPECT_EQ(tracker.GetBindingContentHash(id, 0), 0x1234u);

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, BindingChangedCheck) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterSet(4);

    tracker.MarkBindingUpdated(id, 0, 0xABCD); // Binding 0 version -> 1
    tracker.MarkBindingUpdated(id, 1, 0x5678); // Binding 1 version -> 1

    EXPECT_TRUE(tracker.BindingChanged(id, 0, 0));  // Consumer at 0, binding at 1
    EXPECT_FALSE(tracker.BindingChanged(id, 0, 1)); // Consumer at 1, binding at 1
    EXPECT_TRUE(tracker.BindingChanged(id, 1, 0));  // Consumer at 0, binding at 1

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, BindingChangedUnknownSet) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    EXPECT_TRUE(tracker.BindingChanged(999, 0, 0)); // Unknown -> changed

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, MultipleBindingsIndependent) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterSet(4);

    tracker.MarkBindingUpdated(id, 0, 100);
    tracker.MarkBindingUpdated(id, 2, 200);

    EXPECT_EQ(tracker.GetBindingVersion(id, 0), 1u);
    EXPECT_EQ(tracker.GetBindingVersion(id, 1), 0u); // Untouched
    EXPECT_EQ(tracker.GetBindingVersion(id, 2), 1u);
    EXPECT_EQ(tracker.GetBindingVersion(id, 3), 0u); // Untouched

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, GetVersionUnknownSet) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    EXPECT_EQ(tracker.GetVersion(999), 0u);

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, GetBindingVersionOutOfRange) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterSet(2);
    EXPECT_EQ(tracker.GetBindingVersion(id, 99), 0u); // Out of range

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, RecordConsumeStale) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterSet(2);
    tracker.MarkUpdated(id, 0); // Version -> 1

    tracker.RecordConsume(id, 0); // Consumer at 0, current at 1 -> stale

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.staleDetections, 1u);

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, RecordConsumeUpToDate) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterSet(2);
    tracker.MarkUpdated(id, 0); // Version -> 1

    tracker.RecordConsume(id, 1); // Consumer at 1, current at 1 -> avoided

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.updatesAvoided, 1u);

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, Unregister) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterSet(2, "Temp");
    EXPECT_EQ(tracker.GetSetCount(), 1u);

    tracker.Unregister(id);
    EXPECT_EQ(tracker.GetSetCount(), 0u);
    EXPECT_EQ(tracker.GetSetInfo(id), nullptr);

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, MaxSetsLimit) {
    DescriptorSetVersionTracker tracker;
    DescSetVersionConfig config;
    config.maxSets = 3;
    tracker.Init(config);

    EXPECT_NE(tracker.RegisterSet(1), UINT32_MAX);
    EXPECT_NE(tracker.RegisterSet(1), UINT32_MAX);
    EXPECT_NE(tracker.RegisterSet(1), UINT32_MAX);
    EXPECT_EQ(tracker.RegisterSet(1), UINT32_MAX); // Exceeds

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, PerBindingDisabled) {
    DescriptorSetVersionTracker tracker;
    DescSetVersionConfig config;
    config.trackPerBinding = false;
    tracker.Init(config);

    u32 id = tracker.RegisterSet(4);

    // Binding updates should be no-ops when per-binding tracking disabled
    tracker.MarkBindingUpdated(id, 0, 0xABCD);
    EXPECT_EQ(tracker.GetBindingVersion(id, 0), 0u); // Not tracked

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, StatsTracking) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterSet(4);
    tracker.MarkUpdated(id, 0);
    tracker.MarkUpdated(id, 1);
    tracker.MarkBindingUpdated(id, 0, 100);
    tracker.MarkBindingUpdated(id, 0, 100); // Avoided

    tracker.RecordConsume(id, 0); // Stale
    tracker.RecordConsume(id, 4); // Up to date

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.totalSets, 1u);
    EXPECT_EQ(stats.totalUpdates, 2u);
    EXPECT_EQ(stats.totalBindingUpdates, 1u);
    EXPECT_EQ(stats.bindingUpdatesAvoided, 1u);
    EXPECT_EQ(stats.staleDetections, 1u);
    EXPECT_EQ(stats.updatesAvoided, 1u);
    EXPECT_GT(stats.avoidanceRatio, 0.0f);

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, ResetClearsAll) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    tracker.RegisterSet(4);
    tracker.MarkUpdated(0, 0);

    tracker.Reset();

    EXPECT_EQ(tracker.GetSetCount(), 0u);
    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.totalUpdates, 0u);
    EXPECT_EQ(stats.staleDetections, 0u);

    tracker.Shutdown();
}

TEST(DescSetVersionTracker, GetSetInfoInvalid) {
    DescriptorSetVersionTracker tracker;
    tracker.Init();

    EXPECT_EQ(tracker.GetSetInfo(999), nullptr);

    tracker.Shutdown();
}
