#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_mipmap_gen.h"

using namespace nge;
using namespace nge::rhi;

TEST(MipmapGen, InitAndShutdown) {
    MipmapGenManager mgr;
    EXPECT_TRUE(mgr.Init());
    EXPECT_EQ(mgr.GetTextureCount(), 0u);
    mgr.Shutdown();
}

TEST(MipmapGen, RegisterTexture) {
    MipmapGenManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterTexture(1024, 1024, 11, 1, MipFilter::Box, false, "Albedo");
    EXPECT_NE(id, UINT32_MAX);
    EXPECT_EQ(mgr.GetTextureCount(), 1u);

    const auto* info = mgr.GetTextureInfo(id);
    EXPECT_NE(info, nullptr);
    EXPECT_EQ(info->width, 1024u);
    EXPECT_EQ(info->height, 1024u);
    EXPECT_EQ(info->mipLevels, 11u);
    EXPECT_EQ(info->filter, MipFilter::Box);
    EXPECT_FALSE(info->isDynamic);
    EXPECT_EQ(info->state, MipGenState::Pending);

    mgr.Shutdown();
}

TEST(MipmapGen, CalculateMipCount) {
    EXPECT_EQ(MipmapGenManager::CalculateMipCount(1024, 1024), 11u);
    EXPECT_EQ(MipmapGenManager::CalculateMipCount(512, 512), 10u);
    EXPECT_EQ(MipmapGenManager::CalculateMipCount(1, 1), 1u);
    EXPECT_EQ(MipmapGenManager::CalculateMipCount(2048, 1024), 12u);
    EXPECT_EQ(MipmapGenManager::CalculateMipCount(0, 0), 1u);
}

TEST(MipmapGen, RequestGeneration) {
    MipmapGenManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterTexture(256, 256, 9);

    EXPECT_TRUE(mgr.RequestGeneration(id));
    EXPECT_TRUE(mgr.RequestGeneration(id, 2, 3)); // Specific range

    // Invalid texture
    EXPECT_FALSE(mgr.RequestGeneration(999));

    mgr.Shutdown();
}

TEST(MipmapGen, ProcessFrameReturnsRequests) {
    MipmapGenManager mgr;
    mgr.Init();

    u32 id1 = mgr.RegisterTexture(512, 512, 10, 1, MipFilter::Box, false, "Tex1");
    u32 id2 = mgr.RegisterTexture(256, 256, 9, 1, MipFilter::Kaiser, false, "Tex2");

    mgr.RequestGeneration(id1);
    mgr.RequestGeneration(id2);

    auto requests = mgr.ProcessFrame(0);
    EXPECT_EQ(requests.size(), 2u);

    // After processing, pending list should be empty
    auto empty = mgr.ProcessFrame(1);
    // Only dynamic textures would re-appear
    EXPECT_EQ(empty.size(), 0u);

    mgr.Shutdown();
}

TEST(MipmapGen, MarkComplete) {
    MipmapGenManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterTexture(512, 512, 10);
    mgr.RequestGeneration(id);
    mgr.ProcessFrame(0);

    mgr.MarkComplete(id);

    const auto* info = mgr.GetTextureInfo(id);
    EXPECT_EQ(info->state, MipGenState::UpToDate);

    mgr.Shutdown();
}

TEST(MipmapGen, MarkFailed) {
    MipmapGenManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterTexture(512, 512, 10);
    mgr.RequestGeneration(id);
    mgr.ProcessFrame(0);

    mgr.MarkFailed(id);

    const auto* info = mgr.GetTextureInfo(id);
    EXPECT_EQ(info->state, MipGenState::Failed);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.failedGenerations, 1u);

    mgr.Shutdown();
}

TEST(MipmapGen, Invalidate) {
    MipmapGenManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterTexture(512, 512, 10);
    mgr.RequestGeneration(id);
    mgr.ProcessFrame(0);
    mgr.MarkComplete(id);

    EXPECT_EQ(mgr.GetTextureInfo(id)->state, MipGenState::UpToDate);

    mgr.Invalidate(id);
    EXPECT_EQ(mgr.GetTextureInfo(id)->state, MipGenState::Pending);

    mgr.Shutdown();
}

TEST(MipmapGen, DynamicTextureAutoRegeneration) {
    MipmapGenManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterTexture(256, 256, 9, 1, MipFilter::Box, true, "DynamicRT");

    // Initial generation
    mgr.RequestGeneration(id);
    auto req1 = mgr.ProcessFrame(0);
    EXPECT_GE(req1.size(), 1u);

    mgr.MarkComplete(id);

    // Next frame: dynamic texture should auto-request
    auto req2 = mgr.ProcessFrame(1);
    EXPECT_GE(req2.size(), 1u);

    mgr.Shutdown();
}

TEST(MipmapGen, GetMipLevelCount) {
    MipmapGenManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterTexture(1024, 1024, 11);
    EXPECT_EQ(mgr.GetMipLevelCount(id), 11u);
    EXPECT_EQ(mgr.GetMipLevelCount(999), 0u);

    mgr.Shutdown();
}

TEST(MipmapGen, GetPendingTextures) {
    MipmapGenManager mgr;
    mgr.Init();

    u32 id1 = mgr.RegisterTexture(256, 256, 9); // Starts Pending
    u32 id2 = mgr.RegisterTexture(512, 512, 10); // Starts Pending

    auto pending = mgr.GetPendingTextures();
    EXPECT_EQ(pending.size(), 2u);

    mgr.RequestGeneration(id1);
    mgr.ProcessFrame(0);
    mgr.MarkComplete(id1);

    pending = mgr.GetPendingTextures();
    EXPECT_EQ(pending.size(), 1u); // Only id2 still pending

    mgr.Shutdown();
}

TEST(MipmapGen, SetFilter) {
    MipmapGenManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterTexture(256, 256, 9, 1, MipFilter::Box);
    EXPECT_EQ(mgr.GetTextureInfo(id)->filter, MipFilter::Box);

    mgr.SetFilter(id, MipFilter::Lanczos);
    EXPECT_EQ(mgr.GetTextureInfo(id)->filter, MipFilter::Lanczos);

    mgr.Shutdown();
}

TEST(MipmapGen, Unregister) {
    MipmapGenManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterTexture(256, 256, 9);
    EXPECT_EQ(mgr.GetTextureCount(), 1u);

    mgr.Unregister(id);
    EXPECT_EQ(mgr.GetTextureCount(), 0u);
    EXPECT_EQ(mgr.GetTextureInfo(id), nullptr);

    mgr.Shutdown();
}

TEST(MipmapGen, MaxTexturesLimit) {
    MipmapGenManager mgr;
    MipGenConfig config;
    config.maxTextures = 3;
    mgr.Init(config);

    for (u32 i = 0; i < 3; ++i) {
        EXPECT_NE(mgr.RegisterTexture(64, 64, 7), UINT32_MAX);
    }
    EXPECT_EQ(mgr.RegisterTexture(64, 64, 7), UINT32_MAX);

    mgr.Shutdown();
}

TEST(MipmapGen, MaxRequestsPerFrame) {
    MipmapGenManager mgr;
    MipGenConfig config;
    config.maxRequestsPerFrame = 2;
    mgr.Init(config);

    u32 id1 = mgr.RegisterTexture(64, 64, 7);
    u32 id2 = mgr.RegisterTexture(64, 64, 7);
    u32 id3 = mgr.RegisterTexture(64, 64, 7);

    EXPECT_TRUE(mgr.RequestGeneration(id1));
    EXPECT_TRUE(mgr.RequestGeneration(id2));
    EXPECT_FALSE(mgr.RequestGeneration(id3)); // Exceeds limit

    mgr.Shutdown();
}

TEST(MipmapGen, RequestSpecificMipRange) {
    MipmapGenManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterTexture(1024, 1024, 11);
    mgr.RequestGeneration(id, 3, 4); // Generate mips 3-6

    auto requests = mgr.ProcessFrame(0);
    EXPECT_EQ(requests.size(), 1u);
    EXPECT_EQ(requests[0].baseMip, 3u);
    EXPECT_EQ(requests[0].mipCount, 4u);

    mgr.Shutdown();
}

TEST(MipmapGen, RequestAllMips) {
    MipmapGenManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterTexture(256, 256, 9);
    mgr.RequestGeneration(id, 0, 0); // 0 = all remaining

    auto requests = mgr.ProcessFrame(0);
    EXPECT_EQ(requests.size(), 1u);
    EXPECT_EQ(requests[0].baseMip, 0u);
    EXPECT_EQ(requests[0].mipCount, 8u); // 9 levels - base 0 - 1 = 8

    mgr.Shutdown();
}

TEST(MipmapGen, ArrayLayerTexture) {
    MipmapGenManager mgr;
    mgr.Init();

    u32 id = mgr.RegisterTexture(256, 256, 9, 6, MipFilter::Linear, false, "CubeMap");

    const auto* info = mgr.GetTextureInfo(id);
    EXPECT_EQ(info->arrayLayers, 6u);

    mgr.Shutdown();
}

TEST(MipmapGen, StatsTracking) {
    MipmapGenManager mgr;
    mgr.Init();

    u32 id1 = mgr.RegisterTexture(256, 256, 9, 1, MipFilter::Box, true, "Dynamic");
    u32 id2 = mgr.RegisterTexture(512, 512, 10, 1, MipFilter::Kaiser, false, "Static");

    mgr.RequestGeneration(id1);
    mgr.RequestGeneration(id2);
    mgr.ProcessFrame(0);
    mgr.MarkComplete(id1);
    mgr.MarkComplete(id2);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalTextures, 2u);
    EXPECT_EQ(stats.texturesUpToDate, 2u);
    EXPECT_GT(stats.totalMipsGenerated, 0u);
    EXPECT_EQ(stats.dynamicTextures, 1u);
    EXPECT_GE(stats.batchesDispatched, 1u);

    mgr.Shutdown();
}

TEST(MipmapGen, ResetClearsAll) {
    MipmapGenManager mgr;
    mgr.Init();

    mgr.RegisterTexture(256, 256, 9);
    mgr.RequestGeneration(0);
    mgr.ProcessFrame(0);

    mgr.Reset();

    EXPECT_EQ(mgr.GetTextureCount(), 0u);
    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalMipsGenerated, 0u);
    EXPECT_EQ(stats.batchesDispatched, 0u);

    mgr.Shutdown();
}

TEST(MipmapGen, GetTextureInfoInvalid) {
    MipmapGenManager mgr;
    mgr.Init();

    EXPECT_EQ(mgr.GetTextureInfo(999), nullptr);

    mgr.Shutdown();
}
