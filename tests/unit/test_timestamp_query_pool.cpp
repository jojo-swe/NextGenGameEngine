#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_timestamp_query_pool.h"

using namespace nge::rhi;

TEST(TimestampQueryPool, InitAndShutdown) {
    TimestampQueryPool pool;
    EXPECT_TRUE(pool.Init());

    auto stats = pool.GetStats();
    EXPECT_EQ(stats.totalFramesProfiled, 0u);
    EXPECT_EQ(stats.totalPairsResolved, 0u);

    pool.Shutdown();
}

TEST(TimestampQueryPool, BeginEndFrame) {
    TimestampQueryPool pool;
    pool.Init();

    pool.BeginFrame(0);
    pool.EndFrame();

    auto stats = pool.GetStats();
    EXPECT_EQ(stats.totalFramesProfiled, 1u);

    pool.Shutdown();
}

TEST(TimestampQueryPool, BeginEndPass) {
    TimestampQueryPool pool;
    pool.Init();

    pool.BeginFrame(0);
    u32 id = pool.BeginPass("GBuffer");
    EXPECT_NE(id, UINT32_MAX);
    pool.EndPass(id);

    u32 id2 = pool.BeginPass("Lighting");
    pool.EndPass(id2);
    pool.EndFrame();

    auto stats = pool.GetStats();
    EXPECT_EQ(stats.queriesUsedThisFrame, 4u); // 2 pairs * 2 queries

    pool.Shutdown();
}

TEST(TimestampQueryPool, ResolveAndGetPassTime) {
    TimestampQueryPool pool;
    TimestampQueryPoolConfig config;
    config.queriesPerFrame = 64;
    config.frameLatency = 3;
    config.timestampPeriodNs = 1.0; // 1ns per tick
    pool.Init(config);

    pool.BeginFrame(0);
    u32 p0 = pool.BeginPass("GBuffer");
    pool.EndPass(p0);
    u32 p1 = pool.BeginPass("Lighting");
    pool.EndPass(p1);
    pool.EndFrame();

    // Simulate query results: [begin0, end0, begin1, end1]
    // GBuffer: 1000000ns = 1ms
    // Lighting: 2000000ns = 2ms
    u64 results[4] = {0, 1000000, 2000000, 4000000};
    pool.ResolveFrame(0, results, 4);

    EXPECT_NEAR(pool.GetPassTimeMs("GBuffer"), 1.0, 0.001);
    EXPECT_NEAR(pool.GetPassTimeMs("Lighting"), 2.0, 0.001);
    EXPECT_NEAR(pool.GetLastFrameGpuTimeMs(), 3.0, 0.001);

    pool.Shutdown();
}

TEST(TimestampQueryPool, GetAllPassTimings) {
    TimestampQueryPool pool;
    TimestampQueryPoolConfig config;
    config.timestampPeriodNs = 1.0;
    pool.Init(config);

    pool.BeginFrame(0);
    u32 p0 = pool.BeginPass("Shadow");
    pool.EndPass(p0);
    u32 p1 = pool.BeginPass("PostProcess");
    pool.EndPass(p1);
    pool.EndFrame();

    u64 results[4] = {0, 500000, 1000000, 1500000};
    pool.ResolveFrame(0, results, 4);

    auto timings = pool.GetAllPassTimings();
    EXPECT_EQ(timings.size(), 2u);

    pool.Shutdown();
}

TEST(TimestampQueryPool, GetSlowestPass) {
    TimestampQueryPool pool;
    TimestampQueryPoolConfig config;
    config.timestampPeriodNs = 1.0;
    pool.Init(config);

    pool.BeginFrame(0);
    u32 p0 = pool.BeginPass("Fast");
    pool.EndPass(p0);
    u32 p1 = pool.BeginPass("Slow");
    pool.EndPass(p1);
    pool.EndFrame();

    // Fast: 0.5ms, Slow: 3.0ms
    u64 results[4] = {0, 500000, 1000000, 4000000};
    pool.ResolveFrame(0, results, 4);

    EXPECT_EQ(pool.GetSlowestPass(), "Slow");

    pool.Shutdown();
}

TEST(TimestampQueryPool, MovingAverage) {
    TimestampQueryPool pool;
    TimestampQueryPoolConfig config;
    config.timestampPeriodNs = 1.0;
    config.enableMovingAverage = true;
    config.movingAverageWindow = 3;
    config.frameLatency = 4;
    pool.Init(config);

    // Frame 0: GBuffer = 1.0ms
    pool.BeginFrame(0);
    u32 p = pool.BeginPass("GBuffer");
    pool.EndPass(p);
    pool.EndFrame();
    u64 r0[2] = {0, 1000000};
    pool.ResolveFrame(0, r0, 2);

    // Frame 1: GBuffer = 2.0ms
    pool.BeginFrame(1);
    p = pool.BeginPass("GBuffer");
    pool.EndPass(p);
    pool.EndFrame();
    u64 r1[2] = {0, 2000000};
    pool.ResolveFrame(1, r1, 2);

    // Frame 2: GBuffer = 3.0ms
    pool.BeginFrame(2);
    p = pool.BeginPass("GBuffer");
    pool.EndPass(p);
    pool.EndFrame();
    u64 r2[2] = {0, 3000000};
    pool.ResolveFrame(2, r2, 2);

    // Average should be (1+2+3)/3 = 2.0ms
    EXPECT_NEAR(pool.GetPassMovingAvgMs("GBuffer"), 2.0, 0.001);

    pool.Shutdown();
}

TEST(TimestampQueryPool, RingBufferWraparound) {
    TimestampQueryPool pool;
    TimestampQueryPoolConfig config;
    config.frameLatency = 2;
    config.timestampPeriodNs = 1.0;
    pool.Init(config);

    // Frame 0
    pool.BeginFrame(0);
    u32 p = pool.BeginPass("Pass");
    pool.EndPass(p);
    pool.EndFrame();

    // Frame 1
    pool.BeginFrame(1);
    p = pool.BeginPass("Pass");
    pool.EndPass(p);
    pool.EndFrame();

    // Frame 2 wraps around to slot 0
    pool.BeginFrame(2);
    p = pool.BeginPass("Pass");
    pool.EndPass(p);
    pool.EndFrame();

    u64 r[2] = {0, 500000};
    pool.ResolveFrame(2, r, 2);

    EXPECT_NEAR(pool.GetPassTimeMs("Pass"), 0.5, 0.001);

    pool.Shutdown();
}

TEST(TimestampQueryPool, MaxQueriesPerFrame) {
    TimestampQueryPool pool;
    TimestampQueryPoolConfig config;
    config.queriesPerFrame = 4; // Max 2 pairs
    pool.Init(config);

    pool.BeginFrame(0);
    u32 p0 = pool.BeginPass("A");
    pool.EndPass(p0);
    u32 p1 = pool.BeginPass("B");
    pool.EndPass(p1);
    u32 p2 = pool.BeginPass("C"); // Should exceed limit
    EXPECT_EQ(p2, UINT32_MAX);
    pool.EndFrame();

    pool.Shutdown();
}

TEST(TimestampQueryPool, NoResolvedFrameReturnsZero) {
    TimestampQueryPool pool;
    pool.Init();

    EXPECT_NEAR(pool.GetLastFrameGpuTimeMs(), 0.0, 0.001);
    EXPECT_NEAR(pool.GetPassTimeMs("Nonexistent"), 0.0, 0.001);
    EXPECT_TRUE(pool.GetAllPassTimings().empty());
    EXPECT_TRUE(pool.GetSlowestPass().empty());
    EXPECT_NEAR(pool.GetPassMovingAvgMs("Nonexistent"), 0.0, 0.001);

    pool.Shutdown();
}

TEST(TimestampQueryPool, PeakGpuTime) {
    TimestampQueryPool pool;
    TimestampQueryPoolConfig config;
    config.timestampPeriodNs = 1.0;
    config.frameLatency = 3;
    pool.Init(config);

    // Frame 0: 1ms
    pool.BeginFrame(0);
    u32 p = pool.BeginPass("A");
    pool.EndPass(p);
    pool.EndFrame();
    u64 r0[2] = {0, 1000000};
    pool.ResolveFrame(0, r0, 2);

    // Frame 1: 5ms (peak)
    pool.BeginFrame(1);
    p = pool.BeginPass("A");
    pool.EndPass(p);
    pool.EndFrame();
    u64 r1[2] = {0, 5000000};
    pool.ResolveFrame(1, r1, 2);

    // Frame 2: 2ms
    pool.BeginFrame(2);
    p = pool.BeginPass("A");
    pool.EndPass(p);
    pool.EndFrame();
    u64 r2[2] = {0, 2000000};
    pool.ResolveFrame(2, r2, 2);

    auto stats = pool.GetStats();
    EXPECT_NEAR(stats.peakFrameGpuTimeMs, 5.0, 0.001);

    pool.Shutdown();
}

TEST(TimestampQueryPool, ResetClearsAll) {
    TimestampQueryPool pool;
    TimestampQueryPoolConfig config;
    config.timestampPeriodNs = 1.0;
    pool.Init(config);

    pool.BeginFrame(0);
    u32 p = pool.BeginPass("Test");
    pool.EndPass(p);
    pool.EndFrame();
    u64 r[2] = {0, 1000000};
    pool.ResolveFrame(0, r, 2);

    pool.Reset();

    auto stats = pool.GetStats();
    EXPECT_EQ(stats.totalFramesProfiled, 0u);
    EXPECT_EQ(stats.totalPairsResolved, 0u);
    EXPECT_NEAR(stats.peakFrameGpuTimeMs, 0.0, 0.001);
    EXPECT_NEAR(pool.GetLastFrameGpuTimeMs(), 0.0, 0.001);
    EXPECT_NEAR(pool.GetPassMovingAvgMs("Test"), 0.0, 0.001);

    pool.Shutdown();
}

TEST(TimestampQueryPool, StatsTracking) {
    TimestampQueryPool pool;
    TimestampQueryPoolConfig config;
    config.timestampPeriodNs = 1.0;
    pool.Init(config);

    pool.BeginFrame(0);
    u32 p0 = pool.BeginPass("A");
    pool.EndPass(p0);
    u32 p1 = pool.BeginPass("B");
    pool.EndPass(p1);
    pool.EndFrame();

    u64 r[4] = {0, 1000000, 2000000, 3000000};
    pool.ResolveFrame(0, r, 4);

    auto stats = pool.GetStats();
    EXPECT_EQ(stats.totalFramesProfiled, 1u);
    EXPECT_EQ(stats.totalPairsResolved, 2u);
    EXPECT_NEAR(stats.lastFrameGpuTimeMs, 2.0, 0.001); // 1ms + 1ms

    pool.Shutdown();
}
