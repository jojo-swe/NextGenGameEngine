#include <gtest/gtest.h>
#include "engine/renderer/pipeline/draw_call_merger.h"

using namespace nge;
using namespace nge::renderer;

TEST(DrawCallMerger, EmptyProducesNoBatches) {
    DrawCallMerger merger;
    auto batches = merger.Merge();
    EXPECT_TRUE(batches.empty());
}

TEST(DrawCallMerger, SingleDrawOneBatch) {
    DrawCallMerger merger;
    merger.AddDraw({1, 1, 1, 36, 0, 0, 1, 0, 0.0f});
    merger.Sort();
    auto batches = merger.Merge();
    EXPECT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].draws.size(), 1u);
    EXPECT_EQ(batches[0].totalInstances, 1u);
}

TEST(DrawCallMerger, SamePipelineMaterialMerges) {
    DrawCallMerger merger;
    merger.AddDraw({1, 1, 10, 36, 0, 0, 1, 0, 5.0f});
    merger.AddDraw({1, 1, 11, 36, 0, 0, 1, 1, 3.0f});
    merger.AddDraw({1, 1, 12, 36, 0, 0, 1, 2, 7.0f});
    merger.Sort();
    auto batches = merger.Merge();

    EXPECT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].draws.size(), 3u);
    EXPECT_EQ(batches[0].totalInstances, 3u);
    EXPECT_EQ(batches[0].pipelineId, 1u);
    EXPECT_EQ(batches[0].materialId, 1u);
}

TEST(DrawCallMerger, DifferentPipelinesSeparateBatches) {
    DrawCallMerger merger;
    merger.AddDraw({1, 1, 10, 36, 0, 0, 1, 0, 0.0f});
    merger.AddDraw({2, 1, 10, 36, 0, 0, 1, 0, 0.0f});
    merger.Sort();
    auto batches = merger.Merge();

    EXPECT_EQ(batches.size(), 2u);
}

TEST(DrawCallMerger, DifferentMaterialsSeparateBatches) {
    DrawCallMerger merger;
    merger.AddDraw({1, 1, 10, 36, 0, 0, 1, 0, 0.0f});
    merger.AddDraw({1, 2, 10, 36, 0, 0, 1, 0, 0.0f});
    merger.Sort();
    auto batches = merger.Merge();

    EXPECT_EQ(batches.size(), 2u);
}

TEST(DrawCallMerger, SortByPipelineThenMaterialThenDistance) {
    DrawCallMerger merger;
    merger.AddDraw({2, 1, 10, 36, 0, 0, 1, 0, 5.0f});
    merger.AddDraw({1, 2, 10, 36, 0, 0, 1, 0, 1.0f});
    merger.AddDraw({1, 1, 10, 36, 0, 0, 1, 0, 3.0f});
    merger.AddDraw({1, 1, 11, 36, 0, 0, 1, 0, 1.0f});
    merger.Sort();
    auto batches = merger.Merge();

    // Pipeline 1, Material 1 (2 draws, sorted by distance)
    EXPECT_EQ(batches[0].pipelineId, 1u);
    EXPECT_EQ(batches[0].materialId, 1u);
    EXPECT_EQ(batches[0].draws.size(), 2u);
    EXPECT_LE(batches[0].draws[0].sortKey, batches[0].draws[1].sortKey);

    // Pipeline 1, Material 2 (1 draw)
    EXPECT_EQ(batches[1].pipelineId, 1u);
    EXPECT_EQ(batches[1].materialId, 2u);

    // Pipeline 2, Material 1 (1 draw)
    EXPECT_EQ(batches[2].pipelineId, 2u);
}

TEST(DrawCallMerger, TriangleCountAccumulates) {
    DrawCallMerger merger;
    merger.AddDraw({1, 1, 10, 36, 0, 0, 2, 0, 0.0f}); // 12 tris × 2 instances
    merger.AddDraw({1, 1, 11, 24, 0, 0, 3, 0, 0.0f}); // 8 tris × 3 instances
    merger.Sort();
    auto batches = merger.Merge();

    EXPECT_EQ(batches[0].totalTriangles, 12u * 2 + 8u * 3);
    EXPECT_EQ(batches[0].totalInstances, 5u);
}

TEST(DrawCallMerger, StatsReduction) {
    DrawCallMerger merger;
    // 4 draws, 2 share pipeline+material → 3 batches
    merger.AddDraw({1, 1, 10, 36, 0, 0, 1, 0, 0.0f});
    merger.AddDraw({1, 1, 11, 36, 0, 0, 1, 0, 0.0f});
    merger.AddDraw({2, 1, 10, 36, 0, 0, 1, 0, 0.0f});
    merger.AddDraw({3, 1, 10, 36, 0, 0, 1, 0, 0.0f});
    merger.Sort();

    auto stats = merger.GetStats();
    EXPECT_EQ(stats.inputDrawCalls, 4u);
    EXPECT_EQ(stats.outputBatches, 3u);
    EXPECT_EQ(stats.mergedDrawCalls, 1u);
    EXPECT_GT(stats.reductionPercent, 0.0f);
}

TEST(DrawCallMerger, ResetClearsState) {
    DrawCallMerger merger;
    merger.AddDraw({1, 1, 10, 36, 0, 0, 1, 0, 0.0f});
    EXPECT_EQ(merger.GetDrawCallCount(), 1u);

    merger.Reset();
    EXPECT_EQ(merger.GetDrawCallCount(), 0u);
    auto batches = merger.Merge();
    EXPECT_TRUE(batches.empty());
}
