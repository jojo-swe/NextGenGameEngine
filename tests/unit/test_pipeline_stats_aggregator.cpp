#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_pipeline_stats_aggregator.h"
#include <cstring>

using namespace nge::rhi;

static PipelineStatistics MakeStats(u64 vtx, u64 frag, u64 compute = 0, u64 prims = 0) {
    PipelineStatistics s;
    std::memset(&s, 0, sizeof(s));
    s.vertexShaderInvocations = vtx;
    s.fragmentShaderInvocations = frag;
    s.computeShaderInvocations = compute;
    s.inputAssemblyPrimitives = prims;
    s.inputAssemblyVertices = vtx;
    return s;
}

TEST(PipelineStatsAggregator, InitAndShutdown) {
    PipelineStatsAggregator agg;
    EXPECT_TRUE(agg.Init());
    EXPECT_EQ(agg.GetPassCount(), 0u);
    EXPECT_EQ(agg.GetFrameCount(), 0u);
    agg.Shutdown();
}

TEST(PipelineStatsAggregator, RecordPass) {
    PipelineStatsAggregator agg;
    agg.Init();

    agg.RecordPass("GBuffer", MakeStats(10000, 50000), 0);
    EXPECT_EQ(agg.GetPassCount(), 1u);
    EXPECT_EQ(agg.GetFrameCount(), 1u);

    agg.Shutdown();
}

TEST(PipelineStatsAggregator, RecordMultiplePasses) {
    PipelineStatsAggregator agg;
    agg.Init();

    agg.RecordPass("GBuffer", MakeStats(10000, 50000), 0);
    agg.RecordPass("Shadow", MakeStats(8000, 0), 0);
    agg.RecordPass("Lighting", MakeStats(6, 200000), 0);

    EXPECT_EQ(agg.GetPassCount(), 3u);

    agg.Shutdown();
}

TEST(PipelineStatsAggregator, GetLatestPassStats) {
    PipelineStatsAggregator agg;
    agg.Init();

    agg.RecordPass("GBuffer", MakeStats(10000, 50000), 0);
    agg.RecordPass("GBuffer", MakeStats(12000, 55000), 1);

    auto latest = agg.GetLatestPassStats("GBuffer");
    EXPECT_EQ(latest.vertexShaderInvocations, 12000u);
    EXPECT_EQ(latest.fragmentShaderInvocations, 55000u);

    agg.Shutdown();
}

TEST(PipelineStatsAggregator, GetLatestPassStatsUnknown) {
    PipelineStatsAggregator agg;
    agg.Init();

    auto latest = agg.GetLatestPassStats("NonExistent");
    EXPECT_EQ(latest.vertexShaderInvocations, 0u);

    agg.Shutdown();
}

TEST(PipelineStatsAggregator, GetPassStatsAveraged) {
    PipelineStatsAggregator agg;
    agg.Init();

    agg.RecordPass("GBuffer", MakeStats(10000, 50000), 0);
    agg.RecordPass("GBuffer", MakeStats(20000, 60000), 1);

    auto aggStats = agg.GetPassStats("GBuffer");
    EXPECT_EQ(aggStats.sampleCount, 2u);
    EXPECT_EQ(aggStats.avg.vertexShaderInvocations, 15000u); // (10000+20000)/2
    EXPECT_EQ(aggStats.avg.fragmentShaderInvocations, 55000u);

    agg.Shutdown();
}

TEST(PipelineStatsAggregator, MinMaxTracking) {
    PipelineStatsAggregator agg;
    PipelineStatsConfig config;
    config.trackMinMax = true;
    agg.Init(config);

    agg.RecordPass("GBuffer", MakeStats(10000, 50000), 0);
    agg.RecordPass("GBuffer", MakeStats(20000, 40000), 1);
    agg.RecordPass("GBuffer", MakeStats(15000, 60000), 2);

    auto stats = agg.GetPassStats("GBuffer");
    EXPECT_EQ(stats.min.vertexShaderInvocations, 10000u);
    EXPECT_EQ(stats.max.vertexShaderInvocations, 20000u);
    EXPECT_EQ(stats.min.fragmentShaderInvocations, 40000u);
    EXPECT_EQ(stats.max.fragmentShaderInvocations, 60000u);

    agg.Shutdown();
}

TEST(PipelineStatsAggregator, GetFrameTotal) {
    PipelineStatsAggregator agg;
    agg.Init();

    agg.RecordPass("GBuffer", MakeStats(10000, 50000), 0);
    agg.RecordPass("Shadow", MakeStats(8000, 0), 0);
    agg.RecordPass("Lighting", MakeStats(6, 200000), 0);

    auto total = agg.GetFrameTotal(0);
    EXPECT_EQ(total.vertexShaderInvocations, 18006u);
    EXPECT_EQ(total.fragmentShaderInvocations, 250000u);

    agg.Shutdown();
}

TEST(PipelineStatsAggregator, GetFrameTotalWrongFrame) {
    PipelineStatsAggregator agg;
    agg.Init();

    agg.RecordPass("GBuffer", MakeStats(10000, 50000), 0);

    auto total = agg.GetFrameTotal(999);
    EXPECT_EQ(total.vertexShaderInvocations, 0u);

    agg.Shutdown();
}

TEST(PipelineStatsAggregator, BottleneckRatioFragmentBound) {
    PipelineStatsAggregator agg;
    agg.Init();

    // Many more fragment invocations than vertex -> fragment-bound
    agg.RecordPass("GBuffer", MakeStats(1000, 100000), 0);

    float ratio = agg.GetBottleneckRatio("GBuffer");
    EXPECT_GT(ratio, 1.0f); // Fragment-bound

    agg.Shutdown();
}

TEST(PipelineStatsAggregator, BottleneckRatioVertexBound) {
    PipelineStatsAggregator agg;
    agg.Init();

    // More vertex invocations than fragment -> vertex-bound
    agg.RecordPass("Shadow", MakeStats(100000, 1000), 0);

    float ratio = agg.GetBottleneckRatio("Shadow");
    EXPECT_LT(ratio, 1.0f); // Vertex-bound

    agg.Shutdown();
}

TEST(PipelineStatsAggregator, BottleneckRatioUnknownPass) {
    PipelineStatsAggregator agg;
    agg.Init();

    float ratio = agg.GetBottleneckRatio("NonExistent");
    EXPECT_NEAR(ratio, 1.0f, 0.01f);

    agg.Shutdown();
}

TEST(PipelineStatsAggregator, GetPassNames) {
    PipelineStatsAggregator agg;
    agg.Init();

    agg.RecordPass("GBuffer", MakeStats(1000, 5000), 0);
    agg.RecordPass("Shadow", MakeStats(800, 0), 0);
    agg.RecordPass("PostProcess", MakeStats(6, 200000), 0);

    auto names = agg.GetPassNames();
    EXPECT_EQ(names.size(), 3u);

    agg.Shutdown();
}

TEST(PipelineStatsAggregator, HistoryWindowTrimming) {
    PipelineStatsAggregator agg;
    PipelineStatsConfig config;
    config.historyFrames = 3;
    agg.Init(config);

    agg.RecordPass("GBuffer", MakeStats(1000, 5000), 0);
    agg.RecordPass("GBuffer", MakeStats(2000, 6000), 1);
    agg.RecordPass("GBuffer", MakeStats(3000, 7000), 2);
    agg.RecordPass("GBuffer", MakeStats(4000, 8000), 3); // Oldest trimmed

    auto stats = agg.GetPassStats("GBuffer");
    EXPECT_EQ(stats.sampleCount, 3u); // Only 3 kept

    // Average should be (2000+3000+4000)/3 = 3000
    EXPECT_EQ(stats.avg.vertexShaderInvocations, 3000u);

    agg.Shutdown();
}

TEST(PipelineStatsAggregator, ClearPassHistory) {
    PipelineStatsAggregator agg;
    agg.Init();

    agg.RecordPass("GBuffer", MakeStats(1000, 5000), 0);
    agg.RecordPass("Shadow", MakeStats(800, 0), 0);

    agg.ClearPassHistory("GBuffer");

    EXPECT_EQ(agg.GetPassCount(), 1u);
    auto stats = agg.GetPassStats("GBuffer");
    EXPECT_EQ(stats.sampleCount, 0u);

    agg.Shutdown();
}

TEST(PipelineStatsAggregator, ComputeShaderStats) {
    PipelineStatsAggregator agg;
    agg.Init();

    agg.RecordPass("Culling", MakeStats(0, 0, 50000), 0);

    auto latest = agg.GetLatestPassStats("Culling");
    EXPECT_EQ(latest.computeShaderInvocations, 50000u);

    agg.Shutdown();
}

TEST(PipelineStatsAggregator, MaxPassesLimit) {
    PipelineStatsAggregator agg;
    PipelineStatsConfig config;
    config.maxPasses = 2;
    agg.Init(config);

    agg.RecordPass("Pass1", MakeStats(100, 200), 0);
    agg.RecordPass("Pass2", MakeStats(100, 200), 0);
    agg.RecordPass("Pass3", MakeStats(100, 200), 0); // Rejected

    EXPECT_EQ(agg.GetPassCount(), 2u);

    agg.Shutdown();
}

TEST(PipelineStatsAggregator, StatsTracking) {
    PipelineStatsAggregator agg;
    agg.Init();

    agg.RecordPass("GBuffer", MakeStats(10000, 50000, 0), 0);
    agg.RecordPass("Compute", MakeStats(0, 0, 30000), 0);
    agg.RecordPass("GBuffer", MakeStats(12000, 55000, 0), 1);

    auto stats = agg.GetStats();
    EXPECT_EQ(stats.totalPasses, 2u);
    EXPECT_EQ(stats.totalFramesRecorded, 2u);
    EXPECT_EQ(stats.totalVertexInvocations, 22000u);
    EXPECT_EQ(stats.totalFragmentInvocations, 105000u);
    EXPECT_EQ(stats.totalComputeInvocations, 30000u);
    EXPECT_GT(stats.avgVertexPerFrame, 0.0f);
    EXPECT_GT(stats.avgFragmentPerFrame, 0.0f);

    agg.Shutdown();
}

TEST(PipelineStatsAggregator, ResetClearsAll) {
    PipelineStatsAggregator agg;
    agg.Init();

    agg.RecordPass("GBuffer", MakeStats(10000, 50000), 0);

    agg.Reset();

    EXPECT_EQ(agg.GetPassCount(), 0u);
    EXPECT_EQ(agg.GetFrameCount(), 0u);

    auto stats = agg.GetStats();
    EXPECT_EQ(stats.totalVertexInvocations, 0u);

    agg.Shutdown();
}
