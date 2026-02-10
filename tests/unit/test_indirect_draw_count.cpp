#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_indirect_draw_count.h"

using namespace nge::rhi;

TEST(IndirectDrawCount, InitAndShutdown) {
    IndirectDrawCountManager mgr;
    EXPECT_TRUE(mgr.Init());

    EXPECT_EQ(mgr.GetBatchCount(), 0u);
    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalBatches, 0u);
    EXPECT_EQ(stats.totalSubmissions, 0u);

    mgr.Shutdown();
}

TEST(IndirectDrawCount, RegisterBatch) {
    IndirectDrawCountManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterBatch(IndirectDrawType::Draw, 100, 0, 20, 200, 0, 1024, "OpaqueDraws");
    EXPECT_NE(id, UINT32_MAX);
    EXPECT_EQ(mgr.GetBatchCount(), 1u);

    const auto* batch = mgr.GetBatch(id);
    EXPECT_NE(batch, nullptr);
    EXPECT_EQ(batch->type, IndirectDrawType::Draw);
    EXPECT_EQ(batch->argBufferHandle, 100u);
    EXPECT_EQ(batch->argStride, 20u);
    EXPECT_EQ(batch->countBufferHandle, 200u);
    EXPECT_EQ(batch->maxDrawCount, 1024u);
    EXPECT_EQ(batch->debugName, "OpaqueDraws");

    mgr.Shutdown();
}

TEST(IndirectDrawCount, RegisterDrawIndexedBatch) {
    IndirectDrawCountManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterBatch(IndirectDrawType::DrawIndexed, 300, 64, 32, 400, 0, 2048, "IndexedBatch");

    const auto* batch = mgr.GetBatch(id);
    EXPECT_EQ(batch->type, IndirectDrawType::DrawIndexed);
    EXPECT_EQ(batch->argBufferOffset, 64u);
    EXPECT_EQ(batch->argStride, 32u);

    mgr.Shutdown();
}

TEST(IndirectDrawCount, MultipleBatches) {
    IndirectDrawCountManager mgr;
    mgr.Init();

    u32 id0 = mgr.RegisterBatch(IndirectDrawType::Draw, 100, 0, 20, 200, 0, 1024);
    u32 id1 = mgr.RegisterBatch(IndirectDrawType::DrawIndexed, 300, 0, 32, 400, 0, 2048);
    u32 id2 = mgr.RegisterBatch(IndirectDrawType::Draw, 500, 0, 20, 600, 0, 512);

    EXPECT_EQ(mgr.GetBatchCount(), 3u);
    EXPECT_NE(id0, id1);
    EXPECT_NE(id1, id2);

    mgr.Shutdown();
}

TEST(IndirectDrawCount, MaxBatchesLimit) {
    IndirectDrawCountManager mgr;
    IndirectDrawCountConfig config;
    config.maxBatches = 2;
    mgr.Init(config);

    EXPECT_NE(mgr.RegisterBatch(IndirectDrawType::Draw, 1, 0, 20, 2, 0, 100), UINT32_MAX);
    EXPECT_NE(mgr.RegisterBatch(IndirectDrawType::Draw, 3, 0, 20, 4, 0, 100), UINT32_MAX);
    EXPECT_EQ(mgr.RegisterBatch(IndirectDrawType::Draw, 5, 0, 20, 6, 0, 100), UINT32_MAX);

    mgr.Shutdown();
}

TEST(IndirectDrawCount, UpdateBatch) {
    IndirectDrawCountManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterBatch(IndirectDrawType::Draw, 100, 0, 20, 200, 0, 1024);

    mgr.UpdateBatch(id, 500, 128, 600, 64);

    const auto* batch = mgr.GetBatch(id);
    EXPECT_EQ(batch->argBufferHandle, 500u);
    EXPECT_EQ(batch->argBufferOffset, 128u);
    EXPECT_EQ(batch->countBufferHandle, 600u);
    EXPECT_EQ(batch->countBufferOffset, 64u);

    mgr.Shutdown();
}

TEST(IndirectDrawCount, SetMaxDrawCount) {
    IndirectDrawCountManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterBatch(IndirectDrawType::Draw, 100, 0, 20, 200, 0, 1024);
    EXPECT_EQ(mgr.GetBatch(id)->maxDrawCount, 1024u);

    mgr.SetMaxDrawCount(id, 4096);
    EXPECT_EQ(mgr.GetBatch(id)->maxDrawCount, 4096u);

    mgr.Shutdown();
}

TEST(IndirectDrawCount, RemoveBatch) {
    IndirectDrawCountManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterBatch(IndirectDrawType::Draw, 100, 0, 20, 200, 0, 1024);
    EXPECT_EQ(mgr.GetBatchCount(), 1u);

    mgr.RemoveBatch(id);
    EXPECT_EQ(mgr.GetBatchCount(), 0u);
    EXPECT_EQ(mgr.GetBatch(id), nullptr);

    mgr.Shutdown();
}

TEST(IndirectDrawCount, SlotReuse) {
    IndirectDrawCountManager mgr;
    mgr.Init();

    u32 id0 = mgr.RegisterBatch(IndirectDrawType::Draw, 100, 0, 20, 200, 0, 1024, "Batch0");
    u32 id1 = mgr.RegisterBatch(IndirectDrawType::Draw, 300, 0, 20, 400, 0, 1024, "Batch1");

    mgr.RemoveBatch(id0);

    // New batch should reuse slot 0
    u32 id2 = mgr.RegisterBatch(IndirectDrawType::Draw, 500, 0, 20, 600, 0, 512, "Batch2");
    EXPECT_EQ(id2, id0); // Reused slot

    EXPECT_EQ(mgr.GetBatch(id2)->debugName, "Batch2");
    EXPECT_EQ(mgr.GetBatchCount(), 2u);

    mgr.Shutdown();
}

TEST(IndirectDrawCount, GetActiveBatches) {
    IndirectDrawCountManager mgr;
    mgr.Init();

    u32 id0 = mgr.RegisterBatch(IndirectDrawType::Draw, 1, 0, 20, 2, 0, 100);
    u32 id1 = mgr.RegisterBatch(IndirectDrawType::Draw, 3, 0, 20, 4, 0, 100);
    u32 id2 = mgr.RegisterBatch(IndirectDrawType::Draw, 5, 0, 20, 6, 0, 100);

    mgr.RemoveBatch(id1);

    auto active = mgr.GetActiveBatches();
    EXPECT_EQ(active.size(), 2u);

    // Should contain id0 and id2 but not id1
    bool hasId0 = false, hasId1 = false, hasId2 = false;
    for (u32 id : active) {
        if (id == id0) hasId0 = true;
        if (id == id1) hasId1 = true;
        if (id == id2) hasId2 = true;
    }
    EXPECT_TRUE(hasId0);
    EXPECT_FALSE(hasId1);
    EXPECT_TRUE(hasId2);

    mgr.Shutdown();
}

TEST(IndirectDrawCount, RecordSubmission) {
    IndirectDrawCountManager mgr;
    IndirectDrawCountConfig config;
    config.trackStats = true;
    mgr.Init(config);

    u32 id = mgr.RegisterBatch(IndirectDrawType::Draw, 100, 0, 20, 200, 0, 1024);

    mgr.RecordSubmission(id, 500);
    mgr.RecordSubmission(id, 700);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalSubmissions, 2u);
    EXPECT_EQ(stats.totalDrawCalls, 1200u);
    EXPECT_EQ(stats.totalMaxDrawCalls, 2048u); // 1024 * 2

    mgr.Shutdown();
}

TEST(IndirectDrawCount, UtilizationStats) {
    IndirectDrawCountManager mgr;
    IndirectDrawCountConfig config;
    config.trackStats = true;
    mgr.Init(config);

    u32 id = mgr.RegisterBatch(IndirectDrawType::Draw, 100, 0, 20, 200, 0, 1000);

    mgr.RecordSubmission(id, 500); // 50% utilization

    auto stats = mgr.GetStats();
    EXPECT_NEAR(stats.avgUtilization, 0.5f, 0.01f);

    mgr.Shutdown();
}

TEST(IndirectDrawCount, ValidateMaxCount) {
    IndirectDrawCountManager mgr;
    IndirectDrawCountConfig config;
    config.validateMaxCount = true;
    config.trackStats = true;
    mgr.Init(config);

    u32 id = mgr.RegisterBatch(IndirectDrawType::Draw, 100, 0, 20, 200, 0, 100);

    // Actual count exceeds max -> should be clamped in stats
    mgr.RecordSubmission(id, 500);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalDrawCalls, 100u); // Clamped to maxDrawCount

    mgr.Shutdown();
}

TEST(IndirectDrawCount, ClearAll) {
    IndirectDrawCountManager mgr;
    mgr.Init();

    mgr.RegisterBatch(IndirectDrawType::Draw, 1, 0, 20, 2, 0, 100);
    mgr.RegisterBatch(IndirectDrawType::Draw, 3, 0, 20, 4, 0, 100);

    mgr.ClearAll();
    EXPECT_EQ(mgr.GetBatchCount(), 0u);

    mgr.Shutdown();
}

TEST(IndirectDrawCount, ResetClearsAll) {
    IndirectDrawCountManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterBatch(IndirectDrawType::Draw, 100, 0, 20, 200, 0, 1024);
    mgr.RecordSubmission(id, 500);

    mgr.Reset();

    EXPECT_EQ(mgr.GetBatchCount(), 0u);
    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalSubmissions, 0u);
    EXPECT_EQ(stats.totalDrawCalls, 0u);
    EXPECT_EQ(stats.peakBatches, 0u);

    mgr.Shutdown();
}

TEST(IndirectDrawCount, PeakBatches) {
    IndirectDrawCountManager mgr;
    mgr.Init();

    u32 id0 = mgr.RegisterBatch(IndirectDrawType::Draw, 1, 0, 20, 2, 0, 100);
    mgr.RegisterBatch(IndirectDrawType::Draw, 3, 0, 20, 4, 0, 100);
    mgr.RegisterBatch(IndirectDrawType::Draw, 5, 0, 20, 6, 0, 100);

    mgr.RemoveBatch(id0);

    EXPECT_EQ(mgr.GetBatchCount(), 2u);
    EXPECT_EQ(mgr.GetStats().peakBatches, 3u);

    mgr.Shutdown();
}

TEST(IndirectDrawCount, GetBatchInvalid) {
    IndirectDrawCountManager mgr;
    mgr.Init();

    EXPECT_EQ(mgr.GetBatch(0), nullptr);
    EXPECT_EQ(mgr.GetBatch(999), nullptr);

    mgr.Shutdown();
}

TEST(IndirectDrawCount, DefaultMaxDrawCount) {
    IndirectDrawCountManager mgr;
    IndirectDrawCountConfig config;
    config.defaultMaxDrawCount = 8192;
    mgr.Init(config);

    u32 id = mgr.RegisterBatch(IndirectDrawType::Draw, 100, 0, 20, 200, 0, 0); // 0 = use default

    EXPECT_EQ(mgr.GetBatch(id)->maxDrawCount, 8192u);

    mgr.Shutdown();
}
