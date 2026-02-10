#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_draw_call_compactor.h"

using namespace nge::rhi;

static CompactDrawArgs MakeDraw(u32 indexCount, u32 instanceCount, u32 firstIndex = 0,
                                 i32 vertexOffset = 0, u32 firstInstance = 0,
                                 u32 materialId = 0, u32 meshId = 0, u64 pipelineKey = 0) {
    CompactDrawArgs d;
    d.indexCount = indexCount;
    d.instanceCount = instanceCount;
    d.firstIndex = firstIndex;
    d.vertexOffset = vertexOffset;
    d.firstInstance = firstInstance;
    d.materialId = materialId;
    d.meshId = meshId;
    d.pipelineKey = pipelineKey;
    return d;
}

TEST(DrawCallCompactor, InitAndShutdown) {
    DrawCallCompactor compactor;
    EXPECT_TRUE(compactor.Init());
    EXPECT_EQ(compactor.GetOriginalCount(), 0u);
    EXPECT_FALSE(compactor.IsCompacted());
    compactor.Shutdown();
}

TEST(DrawCallCompactor, AddDraw) {
    DrawCallCompactor compactor;
    compactor.Init();

    EXPECT_TRUE(compactor.AddDraw(MakeDraw(36, 1)));
    EXPECT_EQ(compactor.GetOriginalCount(), 1u);

    compactor.Shutdown();
}

TEST(DrawCallCompactor, AddDrawsBatch) {
    DrawCallCompactor compactor;
    compactor.Init();

    std::vector<CompactDrawArgs> draws = {
        MakeDraw(36, 1), MakeDraw(100, 5), MakeDraw(200, 10)
    };
    EXPECT_TRUE(compactor.AddDraws(draws));
    EXPECT_EQ(compactor.GetOriginalCount(), 3u);

    compactor.Shutdown();
}

TEST(DrawCallCompactor, RemoveZeroInstanceDraws) {
    DrawCallCompactor compactor;
    compactor.Init();

    compactor.AddDraw(MakeDraw(36, 1));   // Valid
    compactor.AddDraw(MakeDraw(100, 0));  // Zero instances -> removed
    compactor.AddDraw(MakeDraw(200, 5));  // Valid

    compactor.Compact();

    EXPECT_EQ(compactor.GetCompactedCount(), 2u);

    auto stats = compactor.GetStats();
    EXPECT_EQ(stats.removedZeroInstance, 1u);

    compactor.Shutdown();
}

TEST(DrawCallCompactor, RemoveZeroVertexDraws) {
    DrawCallCompactor compactor;
    compactor.Init();

    compactor.AddDraw(MakeDraw(36, 1));  // Valid
    compactor.AddDraw(MakeDraw(0, 5));   // Zero indices -> removed
    compactor.AddDraw(MakeDraw(100, 2)); // Valid

    compactor.Compact();

    EXPECT_EQ(compactor.GetCompactedCount(), 2u);

    auto stats = compactor.GetStats();
    EXPECT_EQ(stats.removedZeroVertex, 1u);

    compactor.Shutdown();
}

TEST(DrawCallCompactor, RemoveBothZeroTypes) {
    DrawCallCompactor compactor;
    compactor.Init();

    compactor.AddDraw(MakeDraw(36, 1));
    compactor.AddDraw(MakeDraw(100, 0));  // Zero instance
    compactor.AddDraw(MakeDraw(0, 3));    // Zero vertex
    compactor.AddDraw(MakeDraw(200, 5));

    compactor.Compact();

    EXPECT_EQ(compactor.GetCompactedCount(), 2u);

    compactor.Shutdown();
}

TEST(DrawCallCompactor, NoRemovalWhenDisabled) {
    DrawCallCompactor compactor;
    CompactorConfig config;
    config.removeZeroInstance = false;
    config.removeZeroVertex = false;
    config.mergeSameMesh = false;
    config.sortByPipeline = false;
    compactor.Init(config);

    compactor.AddDraw(MakeDraw(36, 0));
    compactor.AddDraw(MakeDraw(0, 5));

    compactor.Compact();

    EXPECT_EQ(compactor.GetCompactedCount(), 2u); // Nothing removed

    compactor.Shutdown();
}

TEST(DrawCallCompactor, MergeCompatibleDraws) {
    DrawCallCompactor compactor;
    CompactorConfig config;
    config.sortByPipeline = false; // Don't sort, test merge directly
    compactor.Init(config);

    // Adjacent index ranges, same mesh/material/pipeline
    compactor.AddDraw(MakeDraw(100, 1, 0, 0, 0, 1, 1, 100));
    compactor.AddDraw(MakeDraw(100, 1, 100, 0, 0, 1, 1, 100)); // Adjacent
    compactor.AddDraw(MakeDraw(50, 1, 200, 0, 0, 1, 1, 100));  // Adjacent

    compactor.Compact();

    EXPECT_EQ(compactor.GetCompactedCount(), 1u); // All merged
    EXPECT_EQ(compactor.GetCompactedDraws()[0].indexCount, 250u);

    auto stats = compactor.GetStats();
    EXPECT_EQ(stats.mergedDraws, 2u);

    compactor.Shutdown();
}

TEST(DrawCallCompactor, NoMergeDifferentMesh) {
    DrawCallCompactor compactor;
    CompactorConfig config;
    config.sortByPipeline = false;
    compactor.Init(config);

    compactor.AddDraw(MakeDraw(100, 1, 0, 0, 0, 1, 1, 100));
    compactor.AddDraw(MakeDraw(100, 1, 100, 0, 0, 1, 2, 100)); // Different mesh

    compactor.Compact();

    EXPECT_EQ(compactor.GetCompactedCount(), 2u); // Not merged

    compactor.Shutdown();
}

TEST(DrawCallCompactor, NoMergeDifferentMaterial) {
    DrawCallCompactor compactor;
    CompactorConfig config;
    config.sortByPipeline = false;
    compactor.Init(config);

    compactor.AddDraw(MakeDraw(100, 1, 0, 0, 0, 1, 1, 100));
    compactor.AddDraw(MakeDraw(100, 1, 100, 0, 0, 2, 1, 100)); // Different material

    compactor.Compact();

    EXPECT_EQ(compactor.GetCompactedCount(), 2u);

    compactor.Shutdown();
}

TEST(DrawCallCompactor, SortByPipeline) {
    DrawCallCompactor compactor;
    CompactorConfig config;
    config.mergeSameMesh = false;
    config.sortByPipeline = true;
    compactor.Init(config);

    compactor.AddDraw(MakeDraw(36, 1, 0, 0, 0, 0, 0, 300));
    compactor.AddDraw(MakeDraw(36, 1, 0, 0, 0, 0, 0, 100));
    compactor.AddDraw(MakeDraw(36, 1, 0, 0, 0, 0, 0, 200));

    compactor.Compact();

    const auto& draws = compactor.GetCompactedDraws();
    EXPECT_EQ(draws[0].pipelineKey, 100u);
    EXPECT_EQ(draws[1].pipelineKey, 200u);
    EXPECT_EQ(draws[2].pipelineKey, 300u);

    compactor.Shutdown();
}

TEST(DrawCallCompactor, SortByMaterial) {
    DrawCallCompactor compactor;
    CompactorConfig config;
    config.mergeSameMesh = false;
    config.sortByPipeline = false;
    config.sortByMaterial = true;
    compactor.Init(config);

    compactor.AddDraw(MakeDraw(36, 1, 0, 0, 0, 30, 0, 0));
    compactor.AddDraw(MakeDraw(36, 1, 0, 0, 0, 10, 0, 0));
    compactor.AddDraw(MakeDraw(36, 1, 0, 0, 0, 20, 0, 0));

    compactor.Compact();

    const auto& draws = compactor.GetCompactedDraws();
    EXPECT_EQ(draws[0].materialId, 10u);
    EXPECT_EQ(draws[1].materialId, 20u);
    EXPECT_EQ(draws[2].materialId, 30u);

    compactor.Shutdown();
}

TEST(DrawCallCompactor, CompactionRatio) {
    DrawCallCompactor compactor;
    compactor.Init();

    compactor.AddDraw(MakeDraw(36, 1));
    compactor.AddDraw(MakeDraw(100, 0));  // Removed
    compactor.AddDraw(MakeDraw(200, 5));
    compactor.AddDraw(MakeDraw(0, 3));    // Removed

    compactor.Compact();

    auto stats = compactor.GetStats();
    EXPECT_EQ(stats.inputDraws, 4u);
    EXPECT_EQ(stats.outputDraws, 2u);
    EXPECT_NEAR(stats.compactionRatio, 0.5f, 0.01f);

    compactor.Shutdown();
}

TEST(DrawCallCompactor, InstanceCountPreserved) {
    DrawCallCompactor compactor;
    compactor.Init();

    compactor.AddDraw(MakeDraw(36, 5));
    compactor.AddDraw(MakeDraw(100, 10));
    compactor.AddDraw(MakeDraw(200, 0)); // Removed

    compactor.Compact();

    auto stats = compactor.GetStats();
    EXPECT_EQ(stats.totalInstancesBefore, 15u);
    EXPECT_EQ(stats.totalInstancesAfter, 15u); // Zero-instance draw had 0 instances

    compactor.Shutdown();
}

TEST(DrawCallCompactor, MaxDrawsLimit) {
    DrawCallCompactor compactor;
    CompactorConfig config;
    config.maxDraws = 3;
    compactor.Init(config);

    EXPECT_TRUE(compactor.AddDraw(MakeDraw(36, 1)));
    EXPECT_TRUE(compactor.AddDraw(MakeDraw(36, 1)));
    EXPECT_TRUE(compactor.AddDraw(MakeDraw(36, 1)));
    EXPECT_FALSE(compactor.AddDraw(MakeDraw(36, 1))); // Exceeds

    compactor.Shutdown();
}

TEST(DrawCallCompactor, IsCompactedFlag) {
    DrawCallCompactor compactor;
    compactor.Init();

    compactor.AddDraw(MakeDraw(36, 1));
    EXPECT_FALSE(compactor.IsCompacted());

    compactor.Compact();
    EXPECT_TRUE(compactor.IsCompacted());

    compactor.AddDraw(MakeDraw(100, 2)); // Adding invalidates
    EXPECT_FALSE(compactor.IsCompacted());

    compactor.Shutdown();
}

TEST(DrawCallCompactor, ClearRemovesAll) {
    DrawCallCompactor compactor;
    compactor.Init();

    compactor.AddDraw(MakeDraw(36, 1));
    compactor.Compact();

    compactor.Clear();

    EXPECT_EQ(compactor.GetOriginalCount(), 0u);
    EXPECT_EQ(compactor.GetCompactedCount(), 0u);
    EXPECT_FALSE(compactor.IsCompacted());

    compactor.Shutdown();
}

TEST(DrawCallCompactor, ResetClearsAll) {
    DrawCallCompactor compactor;
    compactor.Init();

    compactor.AddDraw(MakeDraw(36, 1));
    compactor.AddDraw(MakeDraw(100, 0));
    compactor.Compact();

    compactor.Reset();

    EXPECT_EQ(compactor.GetOriginalCount(), 0u);
    auto stats = compactor.GetStats();
    EXPECT_EQ(stats.removedZeroInstance, 0u);
    EXPECT_EQ(stats.mergedDraws, 0u);

    compactor.Shutdown();
}

TEST(DrawCallCompactor, EmptyCompaction) {
    DrawCallCompactor compactor;
    compactor.Init();

    compactor.Compact(); // No draws

    EXPECT_EQ(compactor.GetCompactedCount(), 0u);
    EXPECT_TRUE(compactor.IsCompacted());

    compactor.Shutdown();
}

TEST(DrawCallCompactor, AllDrawsRemoved) {
    DrawCallCompactor compactor;
    compactor.Init();

    compactor.AddDraw(MakeDraw(0, 0));
    compactor.AddDraw(MakeDraw(100, 0));
    compactor.AddDraw(MakeDraw(0, 5));

    compactor.Compact();

    EXPECT_EQ(compactor.GetCompactedCount(), 0u);

    compactor.Shutdown();
}
