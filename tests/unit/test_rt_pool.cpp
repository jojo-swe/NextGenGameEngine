#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_rt_pool.h"

using namespace nge;
using namespace nge::rhi;

static RenderTargetDesc MakeDesc(u32 w, u32 h, RTFormat fmt, u8 flags = 0x01, u32 mips = 1, u32 layers = 1, u32 samples = 1) {
    RenderTargetDesc d;
    d.width = w;
    d.height = h;
    d.format = fmt;
    d.flags = flags;
    d.mipLevels = mips;
    d.arrayLayers = layers;
    d.sampleCount = samples;
    return d;
}

TEST(RenderTargetPool, InitAndShutdown) {
    RenderTargetPool pool;
    EXPECT_TRUE(pool.Init());
    EXPECT_EQ(pool.GetTotalCount(), 0u);
    pool.Shutdown();
}

TEST(RenderTargetPool, AcquireNewTarget) {
    RenderTargetPool pool;
    pool.Init();

    auto desc = MakeDesc(1920, 1080, RTFormat::RGBA8_Unorm);
    desc.debugName = "GBuffer0";

    u32 id = pool.Acquire(desc);
    EXPECT_NE(id, UINT32_MAX);
    EXPECT_EQ(pool.GetTotalCount(), 1u);
    EXPECT_EQ(pool.GetInUseCount(), 1u);

    const auto* target = pool.GetTarget(id);
    EXPECT_NE(target, nullptr);
    EXPECT_EQ(target->desc.width, 1920u);
    EXPECT_EQ(target->desc.format, RTFormat::RGBA8_Unorm);
    EXPECT_TRUE(target->inUse);

    pool.Shutdown();
}

TEST(RenderTargetPool, ReleaseAndReuse) {
    RenderTargetPool pool;
    pool.Init();

    auto desc = MakeDesc(1920, 1080, RTFormat::RGBA16_Float);
    u32 id1 = pool.Acquire(desc);
    pool.Release(id1);

    EXPECT_EQ(pool.GetInUseCount(), 0u);
    EXPECT_EQ(pool.GetFreeCount(), 1u);

    // Acquire same format -> should reuse
    u32 id2 = pool.Acquire(desc);
    EXPECT_EQ(id2, id1); // Same target reused
    EXPECT_EQ(pool.GetTotalCount(), 1u); // No new allocation

    auto stats = pool.GetStats();
    EXPECT_EQ(stats.totalAllocations, 1u);
    EXPECT_EQ(stats.totalReuses, 1u);

    pool.Shutdown();
}

TEST(RenderTargetPool, NoReuseIncompatible) {
    RenderTargetPool pool;
    pool.Init();

    auto desc1 = MakeDesc(1920, 1080, RTFormat::RGBA8_Unorm);
    u32 id1 = pool.Acquire(desc1);
    pool.Release(id1);

    // Different format -> cannot reuse
    auto desc2 = MakeDesc(1920, 1080, RTFormat::RGBA16_Float);
    u32 id2 = pool.Acquire(desc2);
    EXPECT_NE(id2, id1);
    EXPECT_EQ(pool.GetTotalCount(), 2u);

    pool.Shutdown();
}

TEST(RenderTargetPool, NoReuseDifferentSize) {
    RenderTargetPool pool;
    pool.Init();

    auto desc1 = MakeDesc(1920, 1080, RTFormat::RGBA8_Unorm);
    u32 id1 = pool.Acquire(desc1);
    pool.Release(id1);

    // Different resolution -> cannot reuse
    auto desc2 = MakeDesc(1280, 720, RTFormat::RGBA8_Unorm);
    u32 id2 = pool.Acquire(desc2);
    EXPECT_NE(id2, id1);

    pool.Shutdown();
}

TEST(RenderTargetPool, NoReuseInUse) {
    RenderTargetPool pool;
    pool.Init();

    auto desc = MakeDesc(1920, 1080, RTFormat::RGBA8_Unorm);
    u32 id1 = pool.Acquire(desc);
    // Don't release -> still in use

    u32 id2 = pool.Acquire(desc); // Must allocate new
    EXPECT_NE(id2, id1);
    EXPECT_EQ(pool.GetTotalCount(), 2u);

    pool.Shutdown();
}

TEST(RenderTargetPool, ProcessFrameRecycles) {
    RenderTargetPool pool;
    RTPoolConfig config;
    config.recycleAfterFrames = 3;
    pool.Init(config);

    auto desc = MakeDesc(1920, 1080, RTFormat::RGBA8_Unorm);
    u32 id = pool.Acquire(desc);

    // Use it at frame 0
    pool.ProcessFrame(0);
    pool.Release(id);

    // Frames 1-3: not used
    pool.ProcessFrame(1);
    pool.ProcessFrame(2);
    pool.ProcessFrame(3);
    EXPECT_EQ(pool.GetTotalCount(), 1u); // Still in pool

    // Frame 4: exceeds threshold
    pool.ProcessFrame(4);
    EXPECT_EQ(pool.GetTotalCount(), 0u); // Recycled

    auto stats = pool.GetStats();
    EXPECT_EQ(stats.totalRecycled, 1u);

    pool.Shutdown();
}

TEST(RenderTargetPool, RecyclingDisabled) {
    RenderTargetPool pool;
    RTPoolConfig config;
    config.enableRecycling = false;
    config.recycleAfterFrames = 1;
    pool.Init(config);

    auto desc = MakeDesc(1920, 1080, RTFormat::RGBA8_Unorm);
    u32 id = pool.Acquire(desc);
    pool.ProcessFrame(0);
    pool.Release(id);

    for (u32 f = 1; f < 100; ++f) {
        pool.ProcessFrame(f);
    }

    EXPECT_EQ(pool.GetTotalCount(), 1u); // Never recycled

    pool.Shutdown();
}

TEST(RenderTargetPool, MaxTargetsLimit) {
    RenderTargetPool pool;
    RTPoolConfig config;
    config.maxTargets = 3;
    config.vramBudget = 1024 * 1024 * 1024ULL; // Large budget
    pool.Init(config);

    for (u32 i = 0; i < 3; ++i) {
        auto desc = MakeDesc(64, 64, RTFormat::RGBA8_Unorm);
        EXPECT_NE(pool.Acquire(desc), UINT32_MAX);
    }

    auto desc = MakeDesc(64, 64, RTFormat::RGBA8_Unorm);
    EXPECT_EQ(pool.Acquire(desc), UINT32_MAX);

    pool.Shutdown();
}

TEST(RenderTargetPool, VRAMBudgetEnforced) {
    RenderTargetPool pool;
    RTPoolConfig config;
    config.vramBudget = 1920 * 1080 * 4 + 100; // Just over one RGBA8 target
    config.maxTargets = 100;
    pool.Init(config);

    auto desc = MakeDesc(1920, 1080, RTFormat::RGBA8_Unorm);
    EXPECT_NE(pool.Acquire(desc), UINT32_MAX); // Fits

    EXPECT_EQ(pool.Acquire(desc), UINT32_MAX); // Exceeds budget

    pool.Shutdown();
}

TEST(RenderTargetPool, HasAvailable) {
    RenderTargetPool pool;
    pool.Init();

    auto desc = MakeDesc(1920, 1080, RTFormat::RGBA8_Unorm);
    EXPECT_FALSE(pool.HasAvailable(desc)); // Empty pool

    u32 id = pool.Acquire(desc);
    EXPECT_FALSE(pool.HasAvailable(desc)); // In use

    pool.Release(id);
    EXPECT_TRUE(pool.HasAvailable(desc)); // Free

    pool.Shutdown();
}

TEST(RenderTargetPool, CountAvailable) {
    RenderTargetPool pool;
    pool.Init();

    auto desc = MakeDesc(1920, 1080, RTFormat::RGBA8_Unorm);

    u32 id1 = pool.Acquire(desc);
    [[maybe_unused]] u32 id2 = pool.Acquire(desc);
    u32 id3 = pool.Acquire(desc);

    pool.Release(id1);
    pool.Release(id3);

    EXPECT_EQ(pool.CountAvailable(desc), 2u);

    pool.Shutdown();
}

TEST(RenderTargetPool, ReleaseAll) {
    RenderTargetPool pool;
    pool.Init();

    auto desc = MakeDesc(1920, 1080, RTFormat::RGBA8_Unorm);
    pool.Acquire(desc);
    pool.Acquire(desc);
    pool.Acquire(desc);

    EXPECT_EQ(pool.GetInUseCount(), 3u);

    pool.ReleaseAll();
    EXPECT_EQ(pool.GetInUseCount(), 0u);
    EXPECT_EQ(pool.GetFreeCount(), 3u);

    pool.Shutdown();
}

TEST(RenderTargetPool, EstimatedVRAM) {
    RenderTargetPool pool;
    pool.Init();

    // RGBA8 1920x1080 = 1920*1080*4 = 8,294,400 bytes
    auto desc = MakeDesc(1920, 1080, RTFormat::RGBA8_Unorm);
    pool.Acquire(desc);

    u64 expected = 1920u * 1080u * 4u;
    EXPECT_EQ(pool.GetEstimatedVRAM(), expected);

    pool.Shutdown();
}

TEST(RenderTargetPool, EstimatedVRAMWithMips) {
    RenderTargetPool pool;
    pool.Init();

    auto desc = MakeDesc(1024, 1024, RTFormat::RGBA8_Unorm, 0x01, 3); // 3 mip levels
    pool.Acquire(desc);

    // Base: 1024*1024*4 = 4,194,304
    // Mip1: 4,194,304/4 = 1,048,576
    // Mip2: 1,048,576/4 = 262,144
    // Total = 5,505,024
    u64 expected = 1024u * 1024u * 4u + 1024u * 1024u + 1024u * 1024u / 4u;
    EXPECT_EQ(pool.GetEstimatedVRAM(), expected);

    pool.Shutdown();
}

TEST(RenderTargetPool, DepthFormats) {
    RenderTargetPool pool;
    pool.Init();

    auto descD24 = MakeDesc(1920, 1080, RTFormat::D24_S8, 0x02);
    auto descD32 = MakeDesc(1920, 1080, RTFormat::D32_Float, 0x02);

    u32 id1 = pool.Acquire(descD24);
    u32 id2 = pool.Acquire(descD32);

    EXPECT_NE(id1, UINT32_MAX);
    EXPECT_NE(id2, UINT32_MAX);
    EXPECT_NE(id1, id2); // Different formats

    pool.Shutdown();
}

TEST(RenderTargetPool, StatsTracking) {
    RenderTargetPool pool;
    pool.Init();

    auto desc = MakeDesc(1920, 1080, RTFormat::RGBA8_Unorm);
    u32 id = pool.Acquire(desc);
    pool.Release(id);
    pool.Acquire(desc); // Reuse

    auto stats = pool.GetStats();
    EXPECT_EQ(stats.totalAllocations, 1u);
    EXPECT_EQ(stats.totalReuses, 1u);
    EXPECT_EQ(stats.targetsInUse, 1u);
    EXPECT_EQ(stats.targetsFree, 0u);
    EXPECT_GT(stats.totalVRAMUsed, 0u);

    pool.Shutdown();
}

TEST(RenderTargetPool, ResetClearsAll) {
    RenderTargetPool pool;
    pool.Init();

    auto desc = MakeDesc(1920, 1080, RTFormat::RGBA8_Unorm);
    pool.Acquire(desc);

    pool.Reset();

    EXPECT_EQ(pool.GetTotalCount(), 0u);
    auto stats = pool.GetStats();
    EXPECT_EQ(stats.totalAllocations, 0u);
    EXPECT_EQ(stats.totalReuses, 0u);

    pool.Shutdown();
}

TEST(RenderTargetPool, GetTargetInvalid) {
    RenderTargetPool pool;
    pool.Init();

    EXPECT_EQ(pool.GetTarget(999), nullptr);

    pool.Shutdown();
}
