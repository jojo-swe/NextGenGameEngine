#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_texture_stream_priority.h"

using namespace nge::rhi;

TEST(TextureStreamPriority, InitAndShutdown) {
    TextureStreamPriorityManager mgr;
    EXPECT_TRUE(mgr.Init());

    EXPECT_EQ(mgr.GetTextureCount(), 0u);
    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalTextures, 0u);

    mgr.Shutdown();
}

TEST(TextureStreamPriority, RegisterTexture) {
    TextureStreamPriorityManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterTexture(10, 4096, 1.0f, "Albedo");
    EXPECT_NE(id, UINT32_MAX);
    EXPECT_EQ(mgr.GetTextureCount(), 1u);

    const auto* info = mgr.GetTextureInfo(id);
    EXPECT_NE(info, nullptr);
    EXPECT_EQ(info->totalMipLevels, 10u);
    EXPECT_EQ(info->residentMipLevel, 9u); // Starts at lowest
    EXPECT_EQ(info->perMipSize, 4096u);
    EXPECT_FLOAT_EQ(info->importance, 1.0f);
    EXPECT_EQ(info->debugName, "Albedo");

    mgr.Shutdown();
}

TEST(TextureStreamPriority, MaxTexturesLimit) {
    TextureStreamPriorityManager mgr;
    TextureStreamConfig config;
    config.maxTextures = 3;
    mgr.Init(config);

    for (u32 i = 0; i < 3; ++i) {
        EXPECT_NE(mgr.RegisterTexture(8, 1024), UINT32_MAX);
    }
    EXPECT_EQ(mgr.RegisterTexture(8, 1024), UINT32_MAX);

    mgr.Shutdown();
}

TEST(TextureStreamPriority, UpdateUsage) {
    TextureStreamPriorityManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterTexture(10, 4096);
    mgr.UpdateUsage(id, 0.5f, 10.0f, 1);

    const auto* info = mgr.GetTextureInfo(id);
    EXPECT_FLOAT_EQ(info->screenCoverage, 0.5f);
    EXPECT_FLOAT_EQ(info->distanceToCamera, 10.0f);
    EXPECT_EQ(info->lastUsedFrame, 1u);
    // With high coverage, desired mip should be low (high quality)
    EXPECT_LT(info->requestedMipLevel, info->totalMipLevels - 1);

    mgr.Shutdown();
}

TEST(TextureStreamPriority, SetResidentMip) {
    TextureStreamPriorityManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterTexture(10, 4096);
    EXPECT_EQ(mgr.GetTextureInfo(id)->residentMipLevel, 9u);

    mgr.SetResidentMip(id, 3);
    EXPECT_EQ(mgr.GetTextureInfo(id)->residentMipLevel, 3u);

    // Clamp to max
    mgr.SetResidentMip(id, 99);
    EXPECT_EQ(mgr.GetTextureInfo(id)->residentMipLevel, 9u);

    mgr.Shutdown();
}

TEST(TextureStreamPriority, ProcessFrameLoads) {
    TextureStreamPriorityManager mgr;
    TextureStreamConfig config;
    config.vramBudget = 1024 * 1024;
    config.maxLoadsPerFrame = 4;
    mgr.Init(config);

    u32 id = mgr.RegisterTexture(8, 1024, 1.0f, "TestTex");
    mgr.UpdateUsage(id, 0.5f, 5.0f, 0); // High coverage -> wants mip 0

    auto cmds = mgr.ProcessFrame(0);

    // Should have at least one load command
    bool hasLoad = false;
    for (const auto& cmd : cmds) {
        if (cmd.action == StreamAction::LoadHigherMip && cmd.textureId == id) {
            hasLoad = true;
            EXPECT_LT(cmd.targetMipLevel, 8u); // Loading to a better mip
        }
    }
    EXPECT_TRUE(hasLoad);

    mgr.Shutdown();
}

TEST(TextureStreamPriority, ProcessFrameEvicts) {
    TextureStreamPriorityManager mgr;
    TextureStreamConfig config;
    config.vramBudget = 100; // Very small budget
    config.evictionFrameThreshold = 5;
    config.maxEvictsPerFrame = 4;
    mgr.Init(config);

    u32 id = mgr.RegisterTexture(8, 1024);
    mgr.SetResidentMip(id, 2); // Has many mips resident
    mgr.UpdateUsage(id, 0.0f, 1000.0f, 0); // Not used recently

    auto cmds = mgr.ProcessFrame(100); // Far future frame

    bool hasEvict = false;
    for (const auto& cmd : cmds) {
        if (cmd.action == StreamAction::EvictToLowerMip && cmd.textureId == id) {
            hasEvict = true;
        }
    }
    EXPECT_TRUE(hasEvict);

    mgr.Shutdown();
}

TEST(TextureStreamPriority, ProcessFrameMaxLoadsPerFrame) {
    TextureStreamPriorityManager mgr;
    TextureStreamConfig config;
    config.vramBudget = 100 * 1024 * 1024;
    config.maxLoadsPerFrame = 2;
    mgr.Init(config);

    for (u32 i = 0; i < 10; ++i) {
        u32 id = mgr.RegisterTexture(8, 1024);
        mgr.UpdateUsage(id, 0.5f, 5.0f, 0);
    }

    auto cmds = mgr.ProcessFrame(0);

    u32 loadCount = 0;
    for (const auto& cmd : cmds) {
        if (cmd.action == StreamAction::LoadHigherMip) loadCount++;
    }
    EXPECT_LE(loadCount, 2u); // Limited by maxLoadsPerFrame

    mgr.Shutdown();
}

TEST(TextureStreamPriority, HighCoverageHigherPriority) {
    TextureStreamPriorityManager mgr;
    TextureStreamConfig config;
    config.vramBudget = 100 * 1024 * 1024;
    config.maxLoadsPerFrame = 1;
    mgr.Init(config);

    u32 lowCov = mgr.RegisterTexture(8, 1024, 1.0f, "LowCoverage");
    u32 highCov = mgr.RegisterTexture(8, 1024, 1.0f, "HighCoverage");

    mgr.UpdateUsage(lowCov, 0.01f, 100.0f, 0);
    mgr.UpdateUsage(highCov, 0.8f, 2.0f, 0);

    auto cmds = mgr.ProcessFrame(0);

    // With only 1 load per frame, the high coverage texture should be loaded first
    EXPECT_GE(cmds.size(), 1u);
    if (!cmds.empty()) {
        bool firstLoadIsHighCov = false;
        for (const auto& cmd : cmds) {
            if (cmd.action == StreamAction::LoadHigherMip) {
                firstLoadIsHighCov = (cmd.textureId == highCov);
                break;
            }
        }
        EXPECT_TRUE(firstLoadIsHighCov);
    }

    mgr.Shutdown();
}

TEST(TextureStreamPriority, VRAMEstimation) {
    TextureStreamPriorityManager mgr;
    mgr.Init();

    u32 id1 = mgr.RegisterTexture(4, 1024); // resident at mip 3 -> 1 mip resident
    u32 id2 = mgr.RegisterTexture(4, 2048);

    mgr.SetResidentMip(id1, 0); // All 4 mips resident
    mgr.SetResidentMip(id2, 2); // 2 mips resident

    u64 vram = mgr.GetEstimatedVRAMUsage();
    // id1: 4 * 1024 = 4096, id2: 2 * 2048 = 4096
    EXPECT_EQ(vram, 4096u + 4096u);

    mgr.Shutdown();
}

TEST(TextureStreamPriority, Unregister) {
    TextureStreamPriorityManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterTexture(8, 1024);
    EXPECT_EQ(mgr.GetTextureCount(), 1u);

    mgr.Unregister(id);
    EXPECT_EQ(mgr.GetTextureCount(), 0u);
    EXPECT_EQ(mgr.GetTextureInfo(id), nullptr);

    mgr.Shutdown();
}

TEST(TextureStreamPriority, StatsTracking) {
    TextureStreamPriorityManager mgr;
    TextureStreamConfig config;
    config.vramBudget = 100 * 1024 * 1024;
    mgr.Init(config);

    u32 id1 = mgr.RegisterTexture(8, 1024);
    u32 id2 = mgr.RegisterTexture(8, 1024);

    mgr.SetResidentMip(id1, 0); // Full res
    mgr.UpdateUsage(id2, 0.5f, 5.0f, 0);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalTextures, 2u);
    EXPECT_EQ(stats.texturesAtFullRes, 1u);
    EXPECT_GT(stats.totalVRAMUsed, 0u);
    EXPECT_EQ(stats.vramBudget, 100u * 1024 * 1024);

    mgr.Shutdown();
}

TEST(TextureStreamPriority, ResetClearsAll) {
    TextureStreamPriorityManager mgr;
    mgr.Init();

    mgr.RegisterTexture(8, 1024);
    mgr.RegisterTexture(8, 1024);

    mgr.Reset();

    EXPECT_EQ(mgr.GetTextureCount(), 0u);
    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalLoadsIssued, 0u);
    EXPECT_EQ(stats.totalEvictsIssued, 0u);

    mgr.Shutdown();
}

TEST(TextureStreamPriority, GetTextureInfoInvalid) {
    TextureStreamPriorityManager mgr;
    mgr.Init();

    EXPECT_EQ(mgr.GetTextureInfo(999), nullptr);

    mgr.Shutdown();
}

TEST(TextureStreamPriority, ZeroCoverageLowestMip) {
    TextureStreamPriorityManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterTexture(10, 4096);
    mgr.UpdateUsage(id, 0.0f, 1000.0f, 0); // Zero coverage

    const auto* info = mgr.GetTextureInfo(id);
    EXPECT_EQ(info->requestedMipLevel, 9u); // Lowest quality

    mgr.Shutdown();
}

TEST(TextureStreamPriority, ImportanceAffectsPriority) {
    TextureStreamPriorityManager mgr;
    TextureStreamConfig config;
    config.vramBudget = 100 * 1024 * 1024;
    config.maxLoadsPerFrame = 1;
    config.importanceWeight = 10.0f; // High importance weight
    mgr.Init(config);

    u32 normal = mgr.RegisterTexture(8, 1024, 1.0f, "NormalTex");
    u32 hero = mgr.RegisterTexture(8, 1024, 5.0f, "HeroTex");

    // Same coverage and distance
    mgr.UpdateUsage(normal, 0.1f, 50.0f, 0);
    mgr.UpdateUsage(hero, 0.1f, 50.0f, 0);

    auto cmds = mgr.ProcessFrame(0);

    // Hero texture should be loaded first due to higher importance
    if (!cmds.empty()) {
        bool firstLoadIsHero = false;
        for (const auto& cmd : cmds) {
            if (cmd.action == StreamAction::LoadHigherMip) {
                firstLoadIsHero = (cmd.textureId == hero);
                break;
            }
        }
        EXPECT_TRUE(firstLoadIsHero);
    }

    mgr.Shutdown();
}
