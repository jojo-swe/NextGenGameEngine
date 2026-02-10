#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_frame_graph_aliaser.h"

using namespace nge::rhi;

TEST(FrameGraphAliaser, InitAndShutdown) {
    FrameGraphResourceAliaser aliaser;
    EXPECT_TRUE(aliaser.Init());

    EXPECT_EQ(aliaser.GetResourceCount(), 0u);

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, DeclareResource) {
    FrameGraphResourceAliaser aliaser;
    aliaser.Init();

    u32 id = aliaser.DeclareResource(ResourceType::RenderTarget, 1024 * 1024 * 4, 0, 3, "GBufferAlbedo");
    EXPECT_EQ(id, 0u);
    EXPECT_EQ(aliaser.GetResourceCount(), 1u);

    const auto* res = aliaser.GetResource(id);
    EXPECT_NE(res, nullptr);
    EXPECT_EQ(res->type, ResourceType::RenderTarget);
    EXPECT_EQ(res->sizeBytes, 1024u * 1024 * 4);
    EXPECT_EQ(res->firstPassIndex, 0u);
    EXPECT_EQ(res->lastPassIndex, 3u);
    EXPECT_EQ(res->debugName, "GBufferAlbedo");

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, DeclareImageResource) {
    FrameGraphResourceAliaser aliaser;
    aliaser.Init();

    u32 id = aliaser.DeclareImageResource(ResourceType::RenderTarget, 1920, 1080, 44, 1, 0, 2, "HDRColor");
    EXPECT_EQ(id, 0u);

    const auto* res = aliaser.GetResource(id);
    EXPECT_EQ(res->width, 1920u);
    EXPECT_EQ(res->height, 1080u);
    EXPECT_EQ(res->format, 44u);
    EXPECT_EQ(res->sampleCount, 1u);
    EXPECT_GT(res->sizeBytes, 0u);

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, OverlappingLifetimes) {
    FrameGraphResourceAliaser aliaser;
    aliaser.Init();

    u32 a = aliaser.DeclareResource(ResourceType::RenderTarget, 1000, 0, 3);
    u32 b = aliaser.DeclareResource(ResourceType::RenderTarget, 1000, 2, 5);
    u32 c = aliaser.DeclareResource(ResourceType::RenderTarget, 1000, 4, 6);

    EXPECT_TRUE(aliaser.Overlaps(a, b));   // [0,3] overlaps [2,5]
    EXPECT_FALSE(aliaser.Overlaps(a, c));  // [0,3] does NOT overlap [4,6]
    EXPECT_TRUE(aliaser.Overlaps(b, c));   // [2,5] overlaps [4,6]

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, NonOverlappingCanAlias) {
    FrameGraphResourceAliaser aliaser;
    AliaserConfig config;
    config.enableAliasing = true;
    config.requireSameType = true;
    aliaser.Init(config);

    // A: passes 0-2, B: passes 3-5 (no overlap -> can alias)
    u32 a = aliaser.DeclareResource(ResourceType::RenderTarget, 4096, 0, 2, "TempA");
    u32 b = aliaser.DeclareResource(ResourceType::RenderTarget, 4096, 3, 5, "TempB");

    auto groups = aliaser.ComputeAliasing();

    // Should be in the same group since they don't overlap
    EXPECT_EQ(aliaser.GetAliasGroup(a), aliaser.GetAliasGroup(b));
    EXPECT_EQ(groups.size(), 1u);

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, OverlappingCannotAlias) {
    FrameGraphResourceAliaser aliaser;
    AliaserConfig config;
    config.enableAliasing = true;
    aliaser.Init(config);

    // A: passes 0-3, B: passes 2-5 (overlap -> cannot alias)
    u32 a = aliaser.DeclareResource(ResourceType::RenderTarget, 4096, 0, 3);
    u32 b = aliaser.DeclareResource(ResourceType::RenderTarget, 4096, 2, 5);

    auto groups = aliaser.ComputeAliasing();

    EXPECT_NE(aliaser.GetAliasGroup(a), aliaser.GetAliasGroup(b));
    EXPECT_EQ(groups.size(), 2u);

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, AliasingDisabled) {
    FrameGraphResourceAliaser aliaser;
    AliaserConfig config;
    config.enableAliasing = false;
    aliaser.Init(config);

    aliaser.DeclareResource(ResourceType::RenderTarget, 4096, 0, 2);
    aliaser.DeclareResource(ResourceType::RenderTarget, 4096, 3, 5);

    auto groups = aliaser.ComputeAliasing();

    // Each resource gets its own group
    EXPECT_EQ(groups.size(), 2u);

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, RequireSameTypeBlocks) {
    FrameGraphResourceAliaser aliaser;
    AliaserConfig config;
    config.enableAliasing = true;
    config.requireSameType = true;
    aliaser.Init(config);

    // Non-overlapping but different types
    u32 a = aliaser.DeclareResource(ResourceType::RenderTarget, 4096, 0, 2);
    u32 b = aliaser.DeclareResource(ResourceType::StorageBuffer, 4096, 3, 5);

    auto groups = aliaser.ComputeAliasing();

    // Different types -> separate groups despite non-overlapping lifetimes
    EXPECT_NE(aliaser.GetAliasGroup(a), aliaser.GetAliasGroup(b));

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, PhysicalSizeIsMaxOfGroup) {
    FrameGraphResourceAliaser aliaser;
    aliaser.Init();

    // Non-overlapping resources of different sizes
    aliaser.DeclareResource(ResourceType::RenderTarget, 1000, 0, 1);
    aliaser.DeclareResource(ResourceType::RenderTarget, 5000, 2, 3);
    aliaser.DeclareResource(ResourceType::RenderTarget, 3000, 4, 5);

    auto groups = aliaser.ComputeAliasing();

    // All should be in one group, physical size = max(5000)
    EXPECT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].physicalSize, 5000u);
    EXPECT_EQ(groups[0].resourceIds.size(), 3u);

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, ComplexGraphAliasing) {
    FrameGraphResourceAliaser aliaser;
    aliaser.Init();

    // Simulate a render graph:
    // Pass 0: Write GBuffer albedo + normal
    // Pass 1: Read GBuffer, write lighting
    // Pass 2: Read lighting, write post-process temp
    // Pass 3: Read post-process, write final

    u32 albedo   = aliaser.DeclareResource(ResourceType::RenderTarget, 8000, 0, 1, "Albedo");
    u32 normal   = aliaser.DeclareResource(ResourceType::RenderTarget, 8000, 0, 1, "Normal");
    u32 lighting = aliaser.DeclareResource(ResourceType::RenderTarget, 8000, 1, 2, "Lighting");
    u32 postTemp = aliaser.DeclareResource(ResourceType::RenderTarget, 8000, 2, 3, "PostTemp");
    u32 finalRT  = aliaser.DeclareResource(ResourceType::RenderTarget, 8000, 3, 3, "Final");

    auto groups = aliaser.ComputeAliasing();

    // Albedo [0,1] and postTemp [2,3] don't overlap -> can alias
    // Normal [0,1] and finalRT [3,3] don't overlap -> can alias
    // Lighting [1,2] overlaps with albedo/normal and postTemp

    // At minimum we need:
    // - Group for albedo+postTemp (or albedo+final, etc.)
    // - Group for normal+final (or similar)
    // - Group for lighting (overlaps with everything except final)
    // Exact grouping depends on greedy order, but should have fewer groups than 5

    EXPECT_LT(groups.size(), 5u);

    auto stats = aliaser.GetStats();
    EXPECT_GT(stats.memorySaved, 0u);
    EXPECT_GT(stats.savingsRatio, 0.0f);

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, MemorySavingsStats) {
    FrameGraphResourceAliaser aliaser;
    aliaser.Init();

    // 3 non-overlapping resources of 10000 bytes each
    aliaser.DeclareResource(ResourceType::RenderTarget, 10000, 0, 1);
    aliaser.DeclareResource(ResourceType::RenderTarget, 10000, 2, 3);
    aliaser.DeclareResource(ResourceType::RenderTarget, 10000, 4, 5);

    aliaser.ComputeAliasing();

    auto stats = aliaser.GetStats();
    EXPECT_EQ(stats.totalLogicalSize, 30000u);
    EXPECT_EQ(stats.totalPhysicalSize, 10000u); // All aliased into 1 group
    EXPECT_EQ(stats.memorySaved, 20000u);
    EXPECT_NEAR(stats.savingsRatio, 2.0f / 3.0f, 0.01f);

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, MaxOverlapStat) {
    FrameGraphResourceAliaser aliaser;
    aliaser.Init();

    // All 3 alive at pass 1
    aliaser.DeclareResource(ResourceType::RenderTarget, 1000, 0, 2);
    aliaser.DeclareResource(ResourceType::RenderTarget, 1000, 1, 3);
    aliaser.DeclareResource(ResourceType::RenderTarget, 1000, 0, 1);

    aliaser.ComputeAliasing();

    auto stats = aliaser.GetStats();
    EXPECT_EQ(stats.maxOverlap, 3u); // All alive at pass 1

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, MaxResourcesLimit) {
    FrameGraphResourceAliaser aliaser;
    AliaserConfig config;
    config.maxResources = 3;
    aliaser.Init(config);

    EXPECT_NE(aliaser.DeclareResource(ResourceType::RenderTarget, 100, 0, 1), UINT32_MAX);
    EXPECT_NE(aliaser.DeclareResource(ResourceType::RenderTarget, 100, 0, 1), UINT32_MAX);
    EXPECT_NE(aliaser.DeclareResource(ResourceType::RenderTarget, 100, 0, 1), UINT32_MAX);
    EXPECT_EQ(aliaser.DeclareResource(ResourceType::RenderTarget, 100, 0, 1), UINT32_MAX);

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, ClearResetsResources) {
    FrameGraphResourceAliaser aliaser;
    aliaser.Init();

    aliaser.DeclareResource(ResourceType::RenderTarget, 1000, 0, 1);
    aliaser.DeclareResource(ResourceType::RenderTarget, 1000, 2, 3);

    aliaser.Clear();

    EXPECT_EQ(aliaser.GetResourceCount(), 0u);

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, GetResourceInvalid) {
    FrameGraphResourceAliaser aliaser;
    aliaser.Init();

    EXPECT_EQ(aliaser.GetResource(0), nullptr);
    EXPECT_EQ(aliaser.GetResource(999), nullptr);

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, GetAliasGroupBeforeCompute) {
    FrameGraphResourceAliaser aliaser;
    aliaser.Init();

    aliaser.DeclareResource(ResourceType::RenderTarget, 1000, 0, 1);

    // Before ComputeAliasing, group should be UINT32_MAX
    EXPECT_EQ(aliaser.GetAliasGroup(0), UINT32_MAX);

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, SingleResourceOneGroup) {
    FrameGraphResourceAliaser aliaser;
    aliaser.Init();

    u32 id = aliaser.DeclareResource(ResourceType::RenderTarget, 4096, 0, 5);

    auto groups = aliaser.ComputeAliasing();
    EXPECT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].resourceIds.size(), 1u);
    EXPECT_EQ(groups[0].resourceIds[0], id);
    EXPECT_EQ(groups[0].physicalSize, 4096u);

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, AreCompatibleQuery) {
    FrameGraphResourceAliaser aliaser;
    AliaserConfig config;
    config.requireSameType = true;
    aliaser.Init(config);

    u32 a = aliaser.DeclareResource(ResourceType::RenderTarget, 1000, 0, 1);
    u32 b = aliaser.DeclareResource(ResourceType::RenderTarget, 1000, 2, 3);
    u32 c = aliaser.DeclareResource(ResourceType::StorageBuffer, 1000, 4, 5);

    EXPECT_TRUE(aliaser.AreCompatible(a, b));  // Same type
    EXPECT_FALSE(aliaser.AreCompatible(a, c)); // Different type

    aliaser.Shutdown();
}

TEST(FrameGraphAliaser, MinResourceSizeFilter) {
    FrameGraphResourceAliaser aliaser;
    AliaserConfig config;
    config.enableAliasing = true;
    config.minResourceSize = 500;
    aliaser.Init(config);

    // Small resource below threshold
    u32 a = aliaser.DeclareResource(ResourceType::RenderTarget, 100, 0, 1);
    u32 b = aliaser.DeclareResource(ResourceType::RenderTarget, 100, 2, 3);

    auto groups = aliaser.ComputeAliasing();

    // Small resources should not be aliased (below minResourceSize)
    EXPECT_EQ(groups.size(), 2u);

    aliaser.Shutdown();
}
