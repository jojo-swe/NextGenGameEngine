#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_occlusion_query_batch.h"

using namespace nge::rhi;

TEST(OcclusionQueryBatch, InitAndShutdown) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 64;
    EXPECT_TRUE(mgr.Init(config));
    EXPECT_EQ(mgr.GetAllocatedCount(), 0u);
    EXPECT_EQ(mgr.GetFreeCount(), 64u);
    mgr.Shutdown();
}

TEST(OcclusionQueryBatch, AllocateQuery) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 64;
    mgr.Init(config);

    u32 slot = mgr.AllocateQuery(42);
    EXPECT_NE(slot, UINT32_MAX);
    EXPECT_EQ(mgr.GetAllocatedCount(), 1u);

    const auto* info = mgr.GetSlot(slot);
    EXPECT_NE(info, nullptr);
    EXPECT_EQ(info->objectId, 42u);

    mgr.Shutdown();
}

TEST(OcclusionQueryBatch, AllocateSameObjectReturnsExisting) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 64;
    mgr.Init(config);

    u32 slot1 = mgr.AllocateQuery(42);
    u32 slot2 = mgr.AllocateQuery(42); // Same object
    EXPECT_EQ(slot1, slot2);
    EXPECT_EQ(mgr.GetAllocatedCount(), 1u);

    mgr.Shutdown();
}

TEST(OcclusionQueryBatch, MarkIssuedAndSubmitResult) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 64;
    config.visibilityThreshold = 0;
    mgr.Init(config);

    u32 slot = mgr.AllocateQuery(10);
    mgr.MarkIssued(slot, 0);

    const auto* info = mgr.GetSlot(slot);
    EXPECT_EQ(info->state, QueryState::Pending);

    mgr.SubmitResult(slot, 1000, 1);

    info = mgr.GetSlot(slot);
    EXPECT_EQ(info->state, QueryState::ResultReady);
    EXPECT_EQ(info->samplesPassed, 1000u);
    EXPECT_TRUE(info->visible);

    mgr.Shutdown();
}

TEST(OcclusionQueryBatch, IsVisibleAfterResult) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 64;
    config.visibilityThreshold = 0;
    mgr.Init(config);

    u32 slot = mgr.AllocateQuery(10);
    mgr.MarkIssued(slot, 0);
    mgr.SubmitResult(slot, 500, 1); // Visible

    EXPECT_TRUE(mgr.IsVisible(10));

    mgr.Shutdown();
}

TEST(OcclusionQueryBatch, IsOccludedAfterZeroSamples) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 64;
    config.visibilityThreshold = 0;
    mgr.Init(config);

    u32 slot = mgr.AllocateQuery(10);
    mgr.MarkIssued(slot, 0);
    mgr.SubmitResult(slot, 0, 1); // Zero samples -> occluded

    EXPECT_FALSE(mgr.IsVisible(10));

    mgr.Shutdown();
}

TEST(OcclusionQueryBatch, VisibilityThreshold) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 64;
    config.visibilityThreshold = 100; // Need >100 samples
    mgr.Init(config);

    u32 slot = mgr.AllocateQuery(10);
    mgr.MarkIssued(slot, 0);
    mgr.SubmitResult(slot, 50, 1); // Below threshold -> occluded

    EXPECT_FALSE(mgr.IsVisible(10));

    // Now submit above threshold
    mgr.SubmitResult(slot, 200, 2);
    EXPECT_TRUE(mgr.IsVisible(10));

    mgr.Shutdown();
}

TEST(OcclusionQueryBatch, ConservativeDefaultVisible) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 64;
    config.conservativeDefault = true;
    mgr.Init(config);

    // No query allocated -> conservative default
    EXPECT_TRUE(mgr.IsVisible(999));

    // Query allocated but no result yet
    u32 slot = mgr.AllocateQuery(10);
    mgr.MarkIssued(slot, 0);
    EXPECT_TRUE(mgr.IsVisible(10)); // Pending -> conservative = true

    mgr.Shutdown();
}

TEST(OcclusionQueryBatch, ConservativeDefaultOccluded) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 64;
    config.conservativeDefault = false;
    mgr.Init(config);

    // No result -> conservative default = false
    EXPECT_FALSE(mgr.IsVisible(999));

    mgr.Shutdown();
}

TEST(OcclusionQueryBatch, GetSamplesPassed) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 64;
    mgr.Init(config);

    u32 slot = mgr.AllocateQuery(10);
    mgr.MarkIssued(slot, 0);
    mgr.SubmitResult(slot, 1234, 1);

    EXPECT_EQ(mgr.GetSamplesPassed(10), 1234u);
    EXPECT_EQ(mgr.GetSamplesPassed(999), 0u); // Unknown object

    mgr.Shutdown();
}

TEST(OcclusionQueryBatch, FreeQuery) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 64;
    mgr.Init(config);

    u32 slot = mgr.AllocateQuery(10);
    EXPECT_EQ(mgr.GetAllocatedCount(), 1u);

    mgr.FreeQuery(slot);
    EXPECT_EQ(mgr.GetAllocatedCount(), 0u);
    EXPECT_EQ(mgr.FindSlotForObject(10), UINT32_MAX);

    mgr.Shutdown();
}

TEST(OcclusionQueryBatch, FreeObjectQueries) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 64;
    mgr.Init(config);

    mgr.AllocateQuery(10);
    mgr.AllocateQuery(20);
    EXPECT_EQ(mgr.GetAllocatedCount(), 2u);

    mgr.FreeObjectQueries(10);
    EXPECT_EQ(mgr.GetAllocatedCount(), 1u);
    EXPECT_EQ(mgr.FindSlotForObject(10), UINT32_MAX);
    EXPECT_NE(mgr.FindSlotForObject(20), UINT32_MAX);

    mgr.Shutdown();
}

TEST(OcclusionQueryBatch, FindSlotForObject) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 64;
    mgr.Init(config);

    u32 slot = mgr.AllocateQuery(42);
    EXPECT_EQ(mgr.FindSlotForObject(42), slot);
    EXPECT_EQ(mgr.FindSlotForObject(99), UINT32_MAX);

    mgr.Shutdown();
}

TEST(OcclusionQueryBatch, PoolExhausted) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 3;
    mgr.Init(config);

    EXPECT_NE(mgr.AllocateQuery(1), UINT32_MAX);
    EXPECT_NE(mgr.AllocateQuery(2), UINT32_MAX);
    EXPECT_NE(mgr.AllocateQuery(3), UINT32_MAX);
    EXPECT_EQ(mgr.AllocateQuery(4), UINT32_MAX); // Pool full

    mgr.Shutdown();
}

TEST(OcclusionQueryBatch, FreeAndReallocate) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 2;
    mgr.Init(config);

    u32 s1 = mgr.AllocateQuery(1);
    u32 s2 = mgr.AllocateQuery(2);
    EXPECT_EQ(mgr.AllocateQuery(3), UINT32_MAX); // Full

    mgr.FreeQuery(s1);
    u32 s3 = mgr.AllocateQuery(3);
    EXPECT_NE(s3, UINT32_MAX); // Slot freed and reused

    mgr.Shutdown();
}

TEST(OcclusionQueryBatch, StatsTracking) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 64;
    config.visibilityThreshold = 0;
    mgr.Init(config);

    u32 s1 = mgr.AllocateQuery(1);
    u32 s2 = mgr.AllocateQuery(2);

    mgr.MarkIssued(s1, 0);
    mgr.MarkIssued(s2, 0);

    mgr.SubmitResult(s1, 500, 1); // Visible
    mgr.SubmitResult(s2, 0, 1);   // Occluded

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalSlots, 64u);
    EXPECT_EQ(stats.slotsInUse, 2u);
    EXPECT_EQ(stats.totalQueriesIssued, 2u);
    EXPECT_EQ(stats.totalResultsRead, 2u);
    EXPECT_EQ(stats.visibleObjects, 1u);
    EXPECT_EQ(stats.occludedObjects, 1u);
    EXPECT_NEAR(stats.occlusionRatio, 0.5f, 0.01f);

    mgr.Shutdown();
}

TEST(OcclusionQueryBatch, ResetClearsAll) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 64;
    mgr.Init(config);

    mgr.AllocateQuery(1);
    mgr.AllocateQuery(2);

    mgr.Reset();

    EXPECT_EQ(mgr.GetAllocatedCount(), 0u);
    EXPECT_EQ(mgr.GetFreeCount(), 64u);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalQueriesIssued, 0u);
    EXPECT_EQ(stats.visibleObjects, 0u);

    mgr.Shutdown();
}

TEST(OcclusionQueryBatch, GetSlotInvalid) {
    OcclusionQueryBatchManager mgr;
    OcclusionQueryBatchConfig config;
    config.poolSize = 8;
    mgr.Init(config);

    EXPECT_EQ(mgr.GetSlot(999), nullptr);

    mgr.Shutdown();
}
