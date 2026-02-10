#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_shader_hot_reload.h"

using namespace nge::rhi;

TEST(ShaderHotReload, InitAndShutdown) {
    ShaderHotReloadManager mgr;
    EXPECT_TRUE(mgr.Init());
    EXPECT_EQ(mgr.GetWatchedCount(), 0u);
    mgr.Shutdown();
}

TEST(ShaderHotReload, WatchShader) {
    ShaderHotReloadManager mgr;
    mgr.Init();

    EXPECT_TRUE(mgr.WatchShader(0, "shaders/test.hlsl", ShaderStage::Vertex));
    EXPECT_EQ(mgr.GetWatchedCount(), 1u);

    const auto* info = mgr.GetShaderInfo(0);
    EXPECT_NE(info, nullptr);
    EXPECT_EQ(info->sourcePath, "shaders/test.hlsl");
    EXPECT_EQ(info->stage, ShaderStage::Vertex);

    mgr.Shutdown();
}

TEST(ShaderHotReload, UnwatchShader) {
    ShaderHotReloadManager mgr;
    mgr.Init();

    mgr.WatchShader(0, "shaders/a.hlsl", ShaderStage::Fragment);
    mgr.UnwatchShader(0);

    EXPECT_EQ(mgr.GetWatchedCount(), 0u);
    EXPECT_EQ(mgr.GetShaderInfo(0), nullptr);

    mgr.Shutdown();
}

TEST(ShaderHotReload, MarkDirtyAndNeedsReload) {
    ShaderHotReloadManager mgr;
    mgr.Init();

    mgr.WatchShader(0, "shaders/test.hlsl", ShaderStage::Compute);

    EXPECT_FALSE(mgr.NeedsReload(0));

    mgr.MarkDirty(0);

    EXPECT_TRUE(mgr.NeedsReload(0));

    mgr.Shutdown();
}

TEST(ShaderHotReload, MarkReloaded) {
    ShaderHotReloadManager mgr;
    mgr.Init();

    mgr.WatchShader(0, "shaders/test.hlsl", ShaderStage::Vertex);
    mgr.MarkDirty(0);
    EXPECT_TRUE(mgr.NeedsReload(0));

    mgr.MarkReloaded(0, 0xDEADBEEF);
    EXPECT_FALSE(mgr.NeedsReload(0));

    const auto* info = mgr.GetShaderInfo(0);
    EXPECT_EQ(info->compiledHash, 0xDEADBEEFu);
    EXPECT_EQ(info->reloadCount, 1u);

    mgr.Shutdown();
}

TEST(ShaderHotReload, MarkFailed) {
    ShaderHotReloadManager mgr;
    mgr.Init();

    mgr.WatchShader(0, "shaders/broken.hlsl", ShaderStage::Fragment);
    mgr.MarkDirty(0);
    mgr.MarkFailed(0);

    const auto* info = mgr.GetShaderInfo(0);
    EXPECT_EQ(info->failCount, 1u);
    EXPECT_TRUE(info->needsReload); // Still dirty after failure

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalFailures, 1u);

    mgr.Shutdown();
}

TEST(ShaderHotReload, GetPendingReloads) {
    ShaderHotReloadManager mgr;
    mgr.Init();

    mgr.WatchShader(0, "a.hlsl", ShaderStage::Vertex);
    mgr.WatchShader(1, "b.hlsl", ShaderStage::Fragment);
    mgr.WatchShader(2, "c.hlsl", ShaderStage::Compute);

    mgr.MarkDirty(0);
    mgr.MarkDirty(2);

    auto pending = mgr.GetPendingReloads();
    EXPECT_EQ(pending.size(), 2u);

    mgr.Shutdown();
}

TEST(ShaderHotReload, SimulateFileChange) {
    ShaderHotReloadManager mgr;
    mgr.Init();

    mgr.WatchShader(0, "shaders/test.hlsl", ShaderStage::Vertex);
    EXPECT_FALSE(mgr.NeedsReload(0));

    mgr.SimulateFileChange(0, 12345);
    EXPECT_TRUE(mgr.NeedsReload(0));

    // Same timestamp should not re-dirty after reload
    mgr.MarkReloaded(0, 0xABC);
    mgr.SimulateFileChange(0, 12345);
    EXPECT_FALSE(mgr.NeedsReload(0)); // Same timestamp

    mgr.SimulateFileChange(0, 12346); // New timestamp
    EXPECT_TRUE(mgr.NeedsReload(0));

    mgr.Shutdown();
}

TEST(ShaderHotReload, IncludeDependencyTracking) {
    ShaderHotReloadManager mgr;
    mgr.Init();

    // Shader A includes common.hlsl
    mgr.WatchShader(0, "shaders/a.hlsl", ShaderStage::Vertex, {"shaders/common.hlsl"});
    // Shader B also includes common.hlsl
    mgr.WatchShader(1, "shaders/b.hlsl", ShaderStage::Fragment, {"shaders/common.hlsl"});
    // Shader C (common.hlsl itself)
    mgr.WatchShader(2, "shaders/common.hlsl", ShaderStage::Vertex);

    // Simulate common.hlsl changed
    mgr.SimulateFileChange(2, 99999);

    // Shader A and B should be marked dirty because they include common.hlsl
    EXPECT_TRUE(mgr.NeedsReload(0));
    EXPECT_TRUE(mgr.NeedsReload(1));
    EXPECT_TRUE(mgr.NeedsReload(2));

    mgr.Shutdown();
}

TEST(ShaderHotReload, ReloadCallback) {
    ShaderHotReloadManager mgr;
    mgr.Init();

    u32 callbackShaderId = UINT32_MAX;
    std::string callbackPath;

    mgr.RegisterCallback([&](u32 id, const std::string& path) {
        callbackShaderId = id;
        callbackPath = path;
    });

    mgr.WatchShader(42, "shaders/cool.hlsl", ShaderStage::Compute);
    mgr.MarkDirty(42);
    mgr.MarkReloaded(42, 0x1234);

    EXPECT_EQ(callbackShaderId, 42u);
    EXPECT_EQ(callbackPath, "shaders/cool.hlsl");

    mgr.Shutdown();
}

TEST(ShaderHotReload, MaxWatchedLimit) {
    ShaderHotReloadManager mgr;
    ShaderHotReloadConfig config;
    config.maxWatchedShaders = 2;
    mgr.Init(config);

    EXPECT_TRUE(mgr.WatchShader(0, "a.hlsl", ShaderStage::Vertex));
    EXPECT_TRUE(mgr.WatchShader(1, "b.hlsl", ShaderStage::Fragment));
    EXPECT_FALSE(mgr.WatchShader(2, "c.hlsl", ShaderStage::Compute)); // Exceeds

    mgr.Shutdown();
}

TEST(ShaderHotReload, PollChangesCount) {
    ShaderHotReloadManager mgr;
    mgr.Init();

    mgr.WatchShader(0, "a.hlsl", ShaderStage::Vertex);
    mgr.WatchShader(1, "b.hlsl", ShaderStage::Fragment);

    EXPECT_EQ(mgr.PollChanges(), 0u);

    mgr.MarkDirty(0);
    EXPECT_EQ(mgr.PollChanges(), 1u);

    mgr.MarkDirty(1);
    EXPECT_EQ(mgr.PollChanges(), 2u);

    mgr.Shutdown();
}

TEST(ShaderHotReload, StatsTracking) {
    ShaderHotReloadManager mgr;
    mgr.Init();

    mgr.WatchShader(0, "a.hlsl", ShaderStage::Vertex, {"common.hlsl"});
    mgr.WatchShader(1, "b.hlsl", ShaderStage::Fragment, {"common.hlsl", "utils.hlsl"});

    mgr.MarkDirty(0);
    mgr.MarkReloaded(0, 0x1);
    mgr.MarkDirty(1);
    mgr.MarkFailed(1);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.watchedShaders, 2u);
    EXPECT_EQ(stats.totalReloads, 1u);
    EXPECT_EQ(stats.totalFailures, 1u);
    EXPECT_EQ(stats.pendingReloads, 1u); // Shader 1 still dirty
    EXPECT_EQ(stats.includesTracked, 3u);

    mgr.Shutdown();
}

TEST(ShaderHotReload, ResetClearsAll) {
    ShaderHotReloadManager mgr;
    mgr.Init();

    mgr.WatchShader(0, "a.hlsl", ShaderStage::Vertex);
    mgr.MarkDirty(0);
    mgr.MarkReloaded(0, 0x1);

    mgr.Reset();

    EXPECT_EQ(mgr.GetWatchedCount(), 0u);
    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalReloads, 0u);

    mgr.Shutdown();
}

TEST(ShaderHotReload, NeedsReloadUnknownShader) {
    ShaderHotReloadManager mgr;
    mgr.Init();

    EXPECT_FALSE(mgr.NeedsReload(999));
    EXPECT_EQ(mgr.GetShaderInfo(999), nullptr);

    mgr.Shutdown();
}

TEST(ShaderHotReload, MultipleReloads) {
    ShaderHotReloadManager mgr;
    mgr.Init();

    mgr.WatchShader(0, "test.hlsl", ShaderStage::Compute);

    mgr.MarkDirty(0);
    mgr.MarkReloaded(0, 0xA);
    mgr.MarkDirty(0);
    mgr.MarkReloaded(0, 0xB);
    mgr.MarkDirty(0);
    mgr.MarkReloaded(0, 0xC);

    const auto* info = mgr.GetShaderInfo(0);
    EXPECT_EQ(info->reloadCount, 3u);
    EXPECT_EQ(info->compiledHash, 0xCu);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalReloads, 3u);

    mgr.Shutdown();
}
