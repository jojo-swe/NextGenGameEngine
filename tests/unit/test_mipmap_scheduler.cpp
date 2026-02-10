#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_mipmap_scheduler.h"

using namespace nge;
using namespace nge::rhi;

static MipGenRequest MakeRequest(u64 handle, u32 w, u32 h, MipFormat fmt = MipFormat::RGBA8_UNORM,
                                  MipFilter filter = MipFilter::Box, u32 mips = 0) {
    MipGenRequest req;
    req.textureHandle = handle;
    req.debugName = "Tex_" + std::to_string(handle);
    req.width = w;
    req.height = h;
    req.mipLevels = mips;
    req.arrayLayers = 1;
    req.format = fmt;
    req.filter = filter;
    req.priority = MipPriority::Normal;
    req.useAsyncCompute = true;
    return req;
}

TEST(MipmapScheduler, InitAndShutdown) {
    MipmapScheduler sched;
    EXPECT_TRUE(sched.Init());

    auto stats = sched.GetStats();
    EXPECT_EQ(stats.totalJobsSubmitted, 0u);
    EXPECT_EQ(stats.pendingJobs, 0u);

    sched.Shutdown();
}

TEST(MipmapScheduler, ComputeMipLevels) {
    EXPECT_EQ(MipmapScheduler::ComputeMipLevels(1, 1), 1u);
    EXPECT_EQ(MipmapScheduler::ComputeMipLevels(2, 2), 2u);
    EXPECT_EQ(MipmapScheduler::ComputeMipLevels(4, 4), 3u);
    EXPECT_EQ(MipmapScheduler::ComputeMipLevels(256, 256), 9u);
    EXPECT_EQ(MipmapScheduler::ComputeMipLevels(1024, 1024), 11u);
    EXPECT_EQ(MipmapScheduler::ComputeMipLevels(512, 256), 10u); // max(512,256)=512, log2(512)+1=10
    EXPECT_EQ(MipmapScheduler::ComputeMipLevels(0, 0), 0u);
}

TEST(MipmapScheduler, ComputeDispatchSize) {
    u32 gx, gy;

    MipmapScheduler::ComputeDispatchSize(256, 256, 0, gx, gy);
    EXPECT_EQ(gx, 32u); // 256/8
    EXPECT_EQ(gy, 32u);

    MipmapScheduler::ComputeDispatchSize(256, 256, 1, gx, gy);
    EXPECT_EQ(gx, 16u); // 128/8
    EXPECT_EQ(gy, 16u);

    MipmapScheduler::ComputeDispatchSize(256, 256, 8, gx, gy);
    EXPECT_EQ(gx, 1u); // 1/8 -> ceil = 1
    EXPECT_EQ(gy, 1u);

    // Non-power-of-two
    MipmapScheduler::ComputeDispatchSize(100, 50, 0, gx, gy);
    EXPECT_EQ(gx, 13u); // ceil(100/8)
    EXPECT_EQ(gy, 7u);  // ceil(50/8)
}

TEST(MipmapScheduler, SubmitAndPending) {
    MipmapScheduler sched;
    sched.Init();

    u64 id = sched.Submit(MakeRequest(1, 256, 256));
    EXPECT_EQ(id, 1u);
    EXPECT_EQ(sched.GetPendingCount(), 1u);

    sched.Submit(MakeRequest(2, 512, 512));
    EXPECT_EQ(sched.GetPendingCount(), 2u);

    auto stats = sched.GetStats();
    EXPECT_EQ(stats.totalJobsSubmitted, 2u);

    sched.Shutdown();
}

TEST(MipmapScheduler, DuplicateSubmitIgnored) {
    MipmapScheduler sched;
    sched.Init();

    sched.Submit(MakeRequest(1, 256, 256));
    sched.Submit(MakeRequest(1, 256, 256)); // Duplicate

    EXPECT_EQ(sched.GetPendingCount(), 1u);
    EXPECT_EQ(sched.GetStats().totalJobsSubmitted, 1u);

    sched.Shutdown();
}

TEST(MipmapScheduler, CancelJob) {
    MipmapScheduler sched;
    sched.Init();

    sched.Submit(MakeRequest(1, 256, 256));
    EXPECT_EQ(sched.GetPendingCount(), 1u);

    EXPECT_TRUE(sched.Cancel(1));
    // After cancel, the job is marked completed
    EXPECT_FALSE(sched.Cancel(999)); // Non-existent

    sched.Shutdown();
}

TEST(MipmapScheduler, ProcessFrameGeneratesMips) {
    MipmapScheduler sched;
    MipSchedulerConfig config;
    config.maxMipsPerFrame = 64;
    config.batchSize = 8;
    sched.Init(config);

    // 4x4 texture: 3 mip levels (4x4 -> 2x2 -> 1x1)
    sched.Submit(MakeRequest(1, 4, 4));

    u32 generated = sched.ProcessFrame();
    EXPECT_GT(generated, 0u);

    auto stats = sched.GetStats();
    EXPECT_GT(stats.totalMipsGenerated, 0u);
    EXPECT_GT(stats.batchesDispatched, 0u);

    sched.Shutdown();
}

TEST(MipmapScheduler, ProgressTracking) {
    MipmapScheduler sched;
    sched.Init();

    sched.Submit(MakeRequest(1, 256, 256));
    EXPECT_NEAR(sched.GetProgress(1), 0.0f, 0.01f);

    // Process some mips
    sched.ProcessFrame();
    float progress = sched.GetProgress(1);
    EXPECT_GT(progress, 0.0f);

    // Non-existent texture
    EXPECT_NEAR(sched.GetProgress(999), 0.0f, 0.01f);

    sched.Shutdown();
}

TEST(MipmapScheduler, MarkCompleted) {
    MipmapScheduler sched;
    sched.Init();

    sched.Submit(MakeRequest(1, 256, 256));
    EXPECT_FALSE(sched.IsComplete(1));

    sched.MarkCompleted(1);
    EXPECT_TRUE(sched.IsComplete(1));
    EXPECT_NEAR(sched.GetProgress(1), 1.0f, 0.01f);

    sched.Shutdown();
}

TEST(MipmapScheduler, MaxPendingJobsLimit) {
    MipmapScheduler sched;
    MipSchedulerConfig config;
    config.maxPendingJobs = 3;
    sched.Init(config);

    sched.Submit(MakeRequest(1, 64, 64));
    sched.Submit(MakeRequest(2, 64, 64));
    sched.Submit(MakeRequest(3, 64, 64));
    u64 overflow = sched.Submit(MakeRequest(4, 64, 64)); // Exceeds limit

    EXPECT_EQ(overflow, 0u);
    EXPECT_EQ(sched.GetStats().totalJobsSubmitted, 3u);

    sched.Shutdown();
}

TEST(MipmapScheduler, MaxMipsPerFrameLimit) {
    MipmapScheduler sched;
    MipSchedulerConfig config;
    config.maxMipsPerFrame = 2;
    config.batchSize = 16;
    sched.Init(config);

    // Submit textures that need many mips
    sched.Submit(MakeRequest(1, 1024, 1024)); // 11 mips
    sched.Submit(MakeRequest(2, 1024, 1024)); // 11 mips

    u32 generated = sched.ProcessFrame();
    EXPECT_LE(generated, 2u); // Should not exceed maxMipsPerFrame

    auto stats = sched.GetStats();
    EXPECT_EQ(stats.mipsGeneratedThisFrame, generated);

    sched.Shutdown();
}

TEST(MipmapScheduler, ExplicitMipLevels) {
    MipmapScheduler sched;
    sched.Init();

    // Request specific mip count instead of auto-compute
    auto req = MakeRequest(1, 256, 256);
    req.mipLevels = 3; // Only generate 3 levels
    sched.Submit(req);

    // Process until complete
    for (int i = 0; i < 10; ++i) sched.ProcessFrame();

    // Should have completed with only 3 mip levels
    EXPECT_TRUE(sched.IsComplete(1));

    sched.Shutdown();
}

TEST(MipmapScheduler, FormatVariety) {
    MipmapScheduler sched;
    sched.Init();

    sched.Submit(MakeRequest(1, 64, 64, MipFormat::RGBA8_SRGB));
    sched.Submit(MakeRequest(2, 64, 64, MipFormat::RGBA16_FLOAT));
    sched.Submit(MakeRequest(3, 64, 64, MipFormat::R32_FLOAT, MipFilter::Min));

    EXPECT_EQ(sched.GetPendingCount(), 3u);

    sched.ProcessFrame();
    auto stats = sched.GetStats();
    EXPECT_GT(stats.totalMipsGenerated, 0u);

    sched.Shutdown();
}

TEST(MipmapScheduler, ResetClearsAll) {
    MipmapScheduler sched;
    sched.Init();

    sched.Submit(MakeRequest(1, 256, 256));
    sched.ProcessFrame();

    sched.Reset();

    auto stats = sched.GetStats();
    EXPECT_EQ(stats.totalJobsSubmitted, 0u);
    EXPECT_EQ(stats.totalJobsCompleted, 0u);
    EXPECT_EQ(stats.totalMipsGenerated, 0u);
    EXPECT_EQ(stats.pendingJobs, 0u);
    EXPECT_EQ(stats.batchesDispatched, 0u);

    sched.Shutdown();
}

TEST(MipmapScheduler, AsyncVsGraphicsDispatchCounting) {
    MipmapScheduler sched;
    MipSchedulerConfig config;
    config.preferAsyncCompute = true;
    sched.Init(config);

    sched.Submit(MakeRequest(1, 4, 4));
    sched.ProcessFrame();

    auto stats = sched.GetStats();
    EXPECT_GT(stats.asyncDispatches, 0u);
    EXPECT_EQ(stats.graphicsDispatches, 0u);

    sched.Reset();

    config.preferAsyncCompute = false;
    sched.Init(config);

    sched.Submit(MakeRequest(2, 4, 4));
    sched.ProcessFrame();

    stats = sched.GetStats();
    EXPECT_EQ(stats.asyncDispatches, 0u);
    EXPECT_GT(stats.graphicsDispatches, 0u);

    sched.Shutdown();
}
