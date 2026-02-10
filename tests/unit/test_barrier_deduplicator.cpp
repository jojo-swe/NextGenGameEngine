#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_barrier_deduplicator.h"

using namespace nge::rhi;

static BarrierDesc MakeImageBarrier(u64 handle, u32 srcStage, u32 dstStage,
                                     u32 srcAccess, u32 dstAccess,
                                     ImageLayoutBarrier oldLayout, ImageLayoutBarrier newLayout,
                                     u32 baseMip = 0, u32 mipCount = 1,
                                     u32 baseLayer = 0, u32 layerCount = 1) {
    BarrierDesc b{};
    b.resourceHandle = handle;
    b.resourceType = BarrierResourceType::Image;
    b.srcStageMask = srcStage;
    b.dstStageMask = dstStage;
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamily = UINT32_MAX;
    b.dstQueueFamily = UINT32_MAX;
    b.baseMipLevel = baseMip;
    b.mipCount = mipCount;
    b.baseArrayLayer = baseLayer;
    b.layerCount = layerCount;
    return b;
}

static BarrierDesc MakeBufferBarrier(u64 handle, u32 srcStage, u32 dstStage,
                                      u32 srcAccess, u32 dstAccess) {
    BarrierDesc b{};
    b.resourceHandle = handle;
    b.resourceType = BarrierResourceType::Buffer;
    b.srcStageMask = srcStage;
    b.dstStageMask = dstStage;
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    b.srcQueueFamily = UINT32_MAX;
    b.dstQueueFamily = UINT32_MAX;
    return b;
}

TEST(BarrierDeduplicator, InitAndShutdown) {
    BarrierDeduplicator dedup;
    EXPECT_TRUE(dedup.Init());

    EXPECT_EQ(dedup.GetPendingCount(), 0u);
    auto stats = dedup.GetStats();
    EXPECT_EQ(stats.totalBarriersSubmitted, 0u);

    dedup.Shutdown();
}

TEST(BarrierDeduplicator, QueueAndFlush) {
    BarrierDeduplicator dedup;
    dedup.Init();

    auto b = MakeImageBarrier(100, 1, 2, 1, 2,
                               ImageLayoutBarrier::ColorAttachment,
                               ImageLayoutBarrier::ShaderReadOnly);
    dedup.QueueBarrier(b);
    EXPECT_EQ(dedup.GetPendingCount(), 1u);

    auto result = dedup.Flush();
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(dedup.GetPendingCount(), 0u);

    dedup.Shutdown();
}

TEST(BarrierDeduplicator, RedundantImageBarrierRemoved) {
    BarrierDeduplicator dedup;
    BarrierDedupConfig config;
    config.removeRedundant = true;
    dedup.Init(config);

    // Same layout, same access, same stage -> redundant
    auto b = MakeImageBarrier(100, 1, 1, 2, 2,
                               ImageLayoutBarrier::ShaderReadOnly,
                               ImageLayoutBarrier::ShaderReadOnly);
    dedup.QueueBarrier(b);

    EXPECT_EQ(dedup.GetPendingCount(), 0u); // Removed immediately

    auto stats = dedup.GetStats();
    EXPECT_EQ(stats.totalBarriersSubmitted, 1u);
    EXPECT_EQ(stats.redundantRemoved, 1u);

    dedup.Shutdown();
}

TEST(BarrierDeduplicator, NonRedundantImageBarrierKept) {
    BarrierDeduplicator dedup;
    dedup.Init();

    // Different layout -> not redundant
    auto b = MakeImageBarrier(100, 1, 2, 1, 2,
                               ImageLayoutBarrier::ColorAttachment,
                               ImageLayoutBarrier::ShaderReadOnly);
    dedup.QueueBarrier(b);

    EXPECT_EQ(dedup.GetPendingCount(), 1u);

    dedup.Shutdown();
}

TEST(BarrierDeduplicator, RedundantBufferBarrierRemoved) {
    BarrierDeduplicator dedup;
    dedup.Init();

    // Same stage and access -> redundant for buffer
    auto b = MakeBufferBarrier(200, 4, 4, 8, 8);
    dedup.QueueBarrier(b);

    EXPECT_EQ(dedup.GetPendingCount(), 0u);
    EXPECT_EQ(dedup.GetStats().redundantRemoved, 1u);

    dedup.Shutdown();
}

TEST(BarrierDeduplicator, MergeBarriersOnSameResource) {
    BarrierDeduplicator dedup;
    BarrierDedupConfig config;
    config.mergeCompatible = true;
    dedup.Init(config);

    // Two barriers on same image, overlapping subresources
    auto b1 = MakeImageBarrier(100, 1, 4, 1, 8,
                                ImageLayoutBarrier::Undefined,
                                ImageLayoutBarrier::ColorAttachment,
                                0, 1, 0, 1);
    auto b2 = MakeImageBarrier(100, 2, 8, 2, 16,
                                ImageLayoutBarrier::ColorAttachment,
                                ImageLayoutBarrier::ShaderReadOnly,
                                0, 1, 0, 1);

    dedup.QueueBarrier(b1);
    dedup.QueueBarrier(b2);
    EXPECT_EQ(dedup.GetPendingCount(), 2u);

    auto result = dedup.Flush();
    EXPECT_EQ(result.size(), 1u); // Merged into one

    // Merged stage masks should be union
    EXPECT_EQ(result[0].srcStageMask, 1u | 2u);
    EXPECT_EQ(result[0].dstStageMask, 4u | 8u);
    EXPECT_EQ(result[0].srcAccessMask, 1u | 2u);
    EXPECT_EQ(result[0].dstAccessMask, 8u | 16u);

    EXPECT_EQ(dedup.GetStats().merged, 1u);

    dedup.Shutdown();
}

TEST(BarrierDeduplicator, NoMergeDifferentResources) {
    BarrierDeduplicator dedup;
    BarrierDedupConfig config;
    config.mergeCompatible = true;
    dedup.Init(config);

    auto b1 = MakeImageBarrier(100, 1, 2, 1, 2,
                                ImageLayoutBarrier::Undefined,
                                ImageLayoutBarrier::ColorAttachment);
    auto b2 = MakeImageBarrier(200, 1, 2, 1, 2,
                                ImageLayoutBarrier::Undefined,
                                ImageLayoutBarrier::ShaderReadOnly);

    dedup.QueueBarrier(b1);
    dedup.QueueBarrier(b2);

    auto result = dedup.Flush();
    EXPECT_EQ(result.size(), 2u); // Different resources, no merge

    dedup.Shutdown();
}

TEST(BarrierDeduplicator, NoMergeDifferentTypes) {
    BarrierDeduplicator dedup;
    BarrierDedupConfig config;
    config.mergeCompatible = true;
    dedup.Init(config);

    auto imgBarrier = MakeImageBarrier(100, 1, 2, 1, 2,
                                        ImageLayoutBarrier::Undefined,
                                        ImageLayoutBarrier::ColorAttachment);
    auto bufBarrier = MakeBufferBarrier(100, 1, 4, 1, 8); // Same handle but different type

    dedup.QueueBarrier(imgBarrier);
    dedup.QueueBarrier(bufBarrier);

    auto result = dedup.Flush();
    EXPECT_EQ(result.size(), 2u); // Different resource types

    dedup.Shutdown();
}

TEST(BarrierDeduplicator, MergeDisabled) {
    BarrierDeduplicator dedup;
    BarrierDedupConfig config;
    config.mergeCompatible = false;
    dedup.Init(config);

    auto b1 = MakeImageBarrier(100, 1, 4, 1, 8,
                                ImageLayoutBarrier::Undefined,
                                ImageLayoutBarrier::ColorAttachment);
    auto b2 = MakeImageBarrier(100, 2, 8, 2, 16,
                                ImageLayoutBarrier::ColorAttachment,
                                ImageLayoutBarrier::ShaderReadOnly);

    dedup.QueueBarrier(b1);
    dedup.QueueBarrier(b2);

    auto result = dedup.Flush();
    EXPECT_EQ(result.size(), 2u); // Merge disabled

    dedup.Shutdown();
}

TEST(BarrierDeduplicator, MergeExpandsSubresourceRange) {
    BarrierDeduplicator dedup;
    dedup.Init();

    // Barrier on mip 0, and another on mip 1 -> should merge to mip 0-1
    auto b1 = MakeImageBarrier(100, 1, 2, 1, 2,
                                ImageLayoutBarrier::Undefined,
                                ImageLayoutBarrier::ShaderReadOnly,
                                0, 1, 0, 1);
    auto b2 = MakeImageBarrier(100, 1, 2, 1, 2,
                                ImageLayoutBarrier::Undefined,
                                ImageLayoutBarrier::ShaderReadOnly,
                                1, 1, 0, 1);

    dedup.QueueBarrier(b1);
    dedup.QueueBarrier(b2);

    auto result = dedup.Flush();
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].baseMipLevel, 0u);
    EXPECT_EQ(result[0].mipCount, 2u);

    dedup.Shutdown();
}

TEST(BarrierDeduplicator, NonOverlappingSubresourcesNotMerged) {
    BarrierDeduplicator dedup;
    dedup.Init();

    // Mip 0 layer 0 vs mip 0 layer 5 (non-overlapping layers)
    auto b1 = MakeImageBarrier(100, 1, 2, 1, 2,
                                ImageLayoutBarrier::Undefined,
                                ImageLayoutBarrier::ShaderReadOnly,
                                0, 1, 0, 1);
    auto b2 = MakeImageBarrier(100, 1, 2, 1, 2,
                                ImageLayoutBarrier::Undefined,
                                ImageLayoutBarrier::ShaderReadOnly,
                                0, 1, 5, 1);

    dedup.QueueBarrier(b1);
    dedup.QueueBarrier(b2);

    auto result = dedup.Flush();
    EXPECT_EQ(result.size(), 2u); // Non-overlapping subresources

    dedup.Shutdown();
}

TEST(BarrierDeduplicator, DiscardAll) {
    BarrierDeduplicator dedup;
    dedup.Init();

    dedup.QueueBarrier(MakeBufferBarrier(1, 1, 2, 1, 2));
    dedup.QueueBarrier(MakeBufferBarrier(2, 1, 2, 1, 2));
    EXPECT_EQ(dedup.GetPendingCount(), 2u);

    dedup.DiscardAll();
    EXPECT_EQ(dedup.GetPendingCount(), 0u);

    dedup.Shutdown();
}

TEST(BarrierDeduplicator, FlushEmpty) {
    BarrierDeduplicator dedup;
    dedup.Init();

    auto result = dedup.Flush();
    EXPECT_TRUE(result.empty());

    dedup.Shutdown();
}

TEST(BarrierDeduplicator, ResetClearsAll) {
    BarrierDeduplicator dedup;
    dedup.Init();

    dedup.QueueBarrier(MakeBufferBarrier(1, 1, 2, 1, 2));
    dedup.Flush();

    dedup.Reset();

    EXPECT_EQ(dedup.GetPendingCount(), 0u);
    auto stats = dedup.GetStats();
    EXPECT_EQ(stats.totalBarriersSubmitted, 0u);
    EXPECT_EQ(stats.redundantRemoved, 0u);
    EXPECT_EQ(stats.merged, 0u);

    dedup.Shutdown();
}

TEST(BarrierDeduplicator, StatsReductionRatio) {
    BarrierDeduplicator dedup;
    dedup.Init();

    // 1 redundant + 2 that merge into 1 = 2 removed from 3 submitted
    auto redundant = MakeBufferBarrier(1, 4, 4, 8, 8); // Redundant
    dedup.QueueBarrier(redundant);

    auto b1 = MakeImageBarrier(100, 1, 4, 1, 8,
                                ImageLayoutBarrier::Undefined,
                                ImageLayoutBarrier::ColorAttachment);
    auto b2 = MakeImageBarrier(100, 2, 8, 2, 16,
                                ImageLayoutBarrier::ColorAttachment,
                                ImageLayoutBarrier::ShaderReadOnly);
    dedup.QueueBarrier(b1);
    dedup.QueueBarrier(b2);

    dedup.Flush();

    auto stats = dedup.GetStats();
    EXPECT_EQ(stats.totalBarriersSubmitted, 3u);
    EXPECT_EQ(stats.redundantRemoved, 1u);
    EXPECT_EQ(stats.merged, 1u);
    EXPECT_EQ(stats.barriersAfterDedup, 1u);
    EXPECT_NEAR(stats.reductionRatio, 2.0f / 3.0f, 0.01f);

    dedup.Shutdown();
}

TEST(BarrierDeduplicator, IsRedundantQuery) {
    BarrierDeduplicator dedup;
    dedup.Init();

    auto redundant = MakeImageBarrier(100, 1, 1, 2, 2,
                                       ImageLayoutBarrier::ShaderReadOnly,
                                       ImageLayoutBarrier::ShaderReadOnly);
    EXPECT_TRUE(dedup.IsRedundant(redundant));

    auto notRedundant = MakeImageBarrier(100, 1, 2, 1, 2,
                                          ImageLayoutBarrier::ColorAttachment,
                                          ImageLayoutBarrier::ShaderReadOnly);
    EXPECT_FALSE(dedup.IsRedundant(notRedundant));

    dedup.Shutdown();
}
