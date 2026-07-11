#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_command_buffer_recycler.h"
#include "engine/rhi/common/rhi_buffer_usage_tracker.h"

using namespace nge;
using namespace nge::rhi;

// ─── Command Buffer Recycler Tests ───────────────────────────────────────

TEST(CommandBufferRecycler, InitCreatesPool) {
    CommandBufferRecycler recycler;
    CommandBufferRecyclerConfig config;
    config.initialSecondaryCount = 16;
    config.maxSecondaryCount = 128;
    EXPECT_TRUE(recycler.Init(nullptr, config));

    auto stats = recycler.GetStats();
    EXPECT_EQ(stats.totalBuffers, 16u);
    EXPECT_EQ(stats.inUseBuffers, 0u);
    EXPECT_EQ(stats.availableBuffers, 16u);

    recycler.Shutdown();
}

TEST(CommandBufferRecycler, AcquireAndRelease) {
    CommandBufferRecycler recycler;
    recycler.Init(nullptr);

    u32 id0 = recycler.AcquireSecondary(0, "ShadowPass");
    u32 id1 = recycler.AcquireSecondary(0, "GBufferPass");
    EXPECT_NE(id0, UINT32_MAX);
    EXPECT_NE(id1, UINT32_MAX);
    EXPECT_NE(id0, id1);

    auto stats = recycler.GetStats();
    EXPECT_EQ(stats.inUseBuffers, 2u);

    recycler.Release(id0);
    stats = recycler.GetStats();
    EXPECT_EQ(stats.inUseBuffers, 1u);

    recycler.Release(id1);
    stats = recycler.GetStats();
    EXPECT_EQ(stats.inUseBuffers, 0u);

    recycler.Shutdown();
}

TEST(CommandBufferRecycler, MarkRecordedAndDirty) {
    CommandBufferRecycler recycler;
    recycler.Init(nullptr);

    u32 id = recycler.AcquireSecondary(0, "StaticGeo");
    EXPECT_FALSE(recycler.IsRecorded(id));

    recycler.MarkRecorded(id);
    EXPECT_TRUE(recycler.IsRecorded(id));

    recycler.MarkDirty(id);
    EXPECT_FALSE(recycler.IsRecorded(id));

    recycler.Shutdown();
}

TEST(CommandBufferRecycler, GetHandle) {
    CommandBufferRecycler recycler;
    recycler.Init(nullptr);

    u32 id = recycler.AcquireSecondary(0, "Test");
    u64 handle = recycler.GetHandle(id);
    EXPECT_NE(handle, 0u);

    EXPECT_EQ(recycler.GetHandle(9999), 0u);

    recycler.Shutdown();
}

TEST(CommandBufferRecycler, GrowthWhenExhausted) {
    CommandBufferRecycler recycler;
    CommandBufferRecyclerConfig config;
    config.initialSecondaryCount = 2;
    config.maxSecondaryCount = 64;
    config.allowGrowth = true;
    recycler.Init(nullptr, config);

    recycler.AcquireSecondary(0, "A");
    recycler.AcquireSecondary(0, "B");
    EXPECT_EQ(recycler.GetStats().availableBuffers, 0u);

    u32 id = recycler.AcquireSecondary(0, "C");
    EXPECT_NE(id, UINT32_MAX);
    EXPECT_EQ(recycler.GetStats().growthEvents, 1u);

    recycler.Shutdown();
}

TEST(CommandBufferRecycler, NoGrowthReturnsInvalid) {
    CommandBufferRecycler recycler;
    CommandBufferRecyclerConfig config;
    config.initialSecondaryCount = 1;
    config.maxSecondaryCount = 1;
    config.allowGrowth = false;
    recycler.Init(nullptr, config);

    recycler.AcquireSecondary(0, "Only");
    u32 overflow = recycler.AcquireSecondary(0, "Overflow");
    EXPECT_EQ(overflow, UINT32_MAX);

    recycler.Shutdown();
}

TEST(CommandBufferRecycler, ResetAllReclaims) {
    CommandBufferRecycler recycler;
    CommandBufferRecyclerConfig config;
    config.initialSecondaryCount = 8;
    recycler.Init(nullptr, config);

    recycler.AcquireSecondary(0, "A");
    recycler.AcquireSecondary(0, "B");
    recycler.AcquireSecondary(0, "C");
    EXPECT_EQ(recycler.GetStats().inUseBuffers, 3u);

    recycler.ResetAll();
    EXPECT_EQ(recycler.GetStats().inUseBuffers, 0u);
    EXPECT_EQ(recycler.GetStats().availableBuffers, 8u);

    recycler.Shutdown();
}

TEST(CommandBufferRecycler, RecordedCountTracking) {
    CommandBufferRecycler recycler;
    recycler.Init(nullptr);

    u32 id0 = recycler.AcquireSecondary(0, "A");
    [[maybe_unused]] u32 id1 = recycler.AcquireSecondary(0, "B");
    u32 id2 = recycler.AcquireSecondary(0, "C");

    recycler.MarkRecorded(id0);
    recycler.MarkRecorded(id2);

    EXPECT_EQ(recycler.GetStats().recordedBuffers, 2u);

    recycler.Shutdown();
}

// ─── Buffer Usage Tracker Tests ──────────────────────────────────────────

TEST(BufferUsageTracker, InitAndShutdown) {
    BufferUsageTracker tracker;
    EXPECT_TRUE(tracker.Init());

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.trackedBuffers, 0u);
    EXPECT_EQ(stats.accessRecordsThisFrame, 0u);

    tracker.Shutdown();
}

TEST(BufferUsageTracker, RecordAccessCreatesLifetime) {
    BufferUsageTracker tracker;
    tracker.Init();

    tracker.RecordAccess(100, 0, "DepthPrepass", AccessType::Write, 0, 4096, "DepthBuffer");
    tracker.RecordAccess(100, 2, "Lighting", AccessType::Read, 0, 4096, "DepthBuffer");

    auto lifetime = tracker.GetLifetime(100);
    EXPECT_EQ(lifetime.bufferHandle, 100u);
    EXPECT_EQ(lifetime.firstPassIndex, 0u);
    EXPECT_EQ(lifetime.lastPassIndex, 2u);
    EXPECT_EQ(lifetime.writeCount, 1u);
    EXPECT_EQ(lifetime.readCount, 1u);
    EXPECT_EQ(lifetime.sizeBytes, 4096u);

    tracker.Shutdown();
}

TEST(BufferUsageTracker, MultipleBufferLifetimes) {
    BufferUsageTracker tracker;
    tracker.Init();

    tracker.RecordAccess(1, 0, "Pass0", AccessType::Write, 0, 1024, "BufA");
    tracker.RecordAccess(1, 1, "Pass1", AccessType::Read, 0, 1024, "BufA");
    tracker.RecordAccess(2, 3, "Pass3", AccessType::Write, 0, 2048, "BufB");
    tracker.RecordAccess(2, 5, "Pass5", AccessType::Read, 0, 2048, "BufB");

    auto all = tracker.GetAllLifetimes();
    EXPECT_EQ(all.size(), 2u);

    tracker.Shutdown();
}

TEST(BufferUsageTracker, AliasingCandidates) {
    BufferUsageTracker tracker;
    BufferUsageTrackerConfig config;
    config.enableAliasingAnalysis = true;
    tracker.Init(config);

    // Buffer A: passes 0-2, Buffer B: passes 4-6 (non-overlapping)
    tracker.RecordAccess(1, 0, "P0", AccessType::Write, 0, 1024, "A");
    tracker.RecordAccess(1, 2, "P2", AccessType::Read, 0, 1024, "A");
    tracker.RecordAccess(2, 4, "P4", AccessType::Write, 0, 1024, "B");
    tracker.RecordAccess(2, 6, "P6", AccessType::Read, 0, 1024, "B");

    tracker.EndFrame(0);

    auto candidates = tracker.GetAliasingCandidates();
    EXPECT_GE(candidates.size(), 1u);
    if (!candidates.empty()) {
        EXPECT_TRUE(candidates[0].canAlias);
        EXPECT_EQ(candidates[0].overlapFraction, 0.0f);
    }

    tracker.Shutdown();
}

TEST(BufferUsageTracker, NoAliasingWhenOverlapping) {
    BufferUsageTracker tracker;
    BufferUsageTrackerConfig config;
    config.enableAliasingAnalysis = true;
    tracker.Init(config);

    // Buffer A: passes 0-5, Buffer B: passes 3-7 (overlapping)
    tracker.RecordAccess(1, 0, "P0", AccessType::Write, 0, 1024, "A");
    tracker.RecordAccess(1, 5, "P5", AccessType::Read, 0, 1024, "A");
    tracker.RecordAccess(2, 3, "P3", AccessType::Write, 0, 1024, "B");
    tracker.RecordAccess(2, 7, "P7", AccessType::Read, 0, 1024, "B");

    tracker.EndFrame(0);

    auto candidates = tracker.GetAliasingCandidates();
    // Should have no valid aliasing candidates
    bool anyCanAlias = false;
    for (const auto& c : candidates) {
        if (c.canAlias) anyCanAlias = true;
    }
    EXPECT_FALSE(anyCanAlias);

    tracker.Shutdown();
}

TEST(BufferUsageTracker, CrossQueueHazardDetection) {
    BufferUsageTracker tracker;
    BufferUsageTrackerConfig config;
    config.enableHazardDetection = true;
    tracker.Init(config);

    // Write on queue 0, read on queue 1 — cross-queue RAW hazard
    tracker.RecordAccess(100, 0, "ComputePass", AccessType::Write, 0, 4096, "SSBO");
    tracker.RecordAccess(100, 1, "RenderPass", AccessType::Read, 1, 4096, "SSBO");

    tracker.EndFrame(0);

    auto warnings = tracker.GetHazardWarnings();
    EXPECT_GE(warnings.size(), 1u);

    tracker.Shutdown();
}

TEST(BufferUsageTracker, SameQueueNoHazardWarning) {
    BufferUsageTracker tracker;
    BufferUsageTrackerConfig config;
    config.enableHazardDetection = true;
    tracker.Init(config);

    // Write then read on same queue — not a cross-queue hazard
    tracker.RecordAccess(100, 0, "PassA", AccessType::Write, 0, 1024, "Buf");
    tracker.RecordAccess(100, 1, "PassB", AccessType::Read, 0, 1024, "Buf");

    tracker.EndFrame(0);

    auto warnings = tracker.GetHazardWarnings();
    EXPECT_EQ(warnings.size(), 0u);

    tracker.Shutdown();
}

TEST(BufferUsageTracker, AccessHistory) {
    BufferUsageTracker tracker;
    tracker.Init();

    tracker.RecordAccess(42, 0, "Shadow", AccessType::Write, 0, 512, "ShadowBuf");
    tracker.RecordAccess(42, 3, "Lighting", AccessType::Read, 0, 512, "ShadowBuf");
    tracker.RecordAccess(99, 1, "Other", AccessType::Write, 0, 256, "OtherBuf");

    auto history = tracker.GetAccessHistory(42);
    EXPECT_EQ(history.size(), 2u);

    auto otherHistory = tracker.GetAccessHistory(99);
    EXPECT_EQ(otherHistory.size(), 1u);

    tracker.Shutdown();
}

TEST(BufferUsageTracker, ClearResetsAll) {
    BufferUsageTracker tracker;
    tracker.Init();

    tracker.RecordAccess(1, 0, "P0", AccessType::Write, 0, 1024, "Buf");
    EXPECT_EQ(tracker.GetStats().trackedBuffers, 1u);

    tracker.Clear();
    EXPECT_EQ(tracker.GetStats().trackedBuffers, 0u);
    EXPECT_EQ(tracker.GetStats().accessRecordsThisFrame, 0u);

    tracker.Shutdown();
}
