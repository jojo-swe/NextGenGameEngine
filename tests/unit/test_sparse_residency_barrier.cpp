#include <gtest/gtest.h>
#include "engine/rhi/vulkan/vk_sparse_residency.h"
#include "engine/rhi/common/rhi_barrier_batch_optimizer.h"

using namespace nge::rhi;
using namespace nge::rhi::vulkan;

// ─── Sparse Residency Manager Tests ──────────────────────────────────────

TEST(SparseResidency, InitAndShutdown) {
    SparseResidencyManager mgr;
    SparseResidencyConfig config;
    config.physicalMemoryBudget = 64 * 1024 * 1024; // 64 MB for test
    EXPECT_TRUE(mgr.Init(nullptr, config));

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalTextures, 0u);
    EXPECT_EQ(stats.residentPages, 0u);
    EXPECT_EQ(stats.physicalMemoryBudget, 64u * 1024 * 1024);

    mgr.Shutdown();
}

TEST(SparseResidency, RegisterTexture) {
    SparseResidencyManager mgr;
    mgr.Init(nullptr);

    SparseTextureInfo info;
    info.width = 4096;
    info.height = 4096;
    info.mipLevels = 12;
    info.layers = 1;
    info.format = 44;
    info.pageWidth = 128;
    info.pageHeight = 128;
    info.debugName = "TerrainAlbedo";

    u32 id = mgr.RegisterTexture(info);
    EXPECT_GT(id, 0u);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalTextures, 1u);

    const auto* texInfo = mgr.GetTextureInfo(id);
    EXPECT_NE(texInfo, nullptr);
    EXPECT_EQ(texInfo->pagesX, 32u); // 4096/128
    EXPECT_EQ(texInfo->pagesY, 32u);

    mgr.Shutdown();
}

TEST(SparseResidency, UnregisterTexture) {
    SparseResidencyManager mgr;
    mgr.Init(nullptr);

    SparseTextureInfo info;
    info.width = 1024;
    info.height = 1024;
    info.mipLevels = 10;
    info.layers = 1;
    info.format = 44;
    info.pageWidth = 128;
    info.pageHeight = 128;
    info.debugName = "Temp";

    u32 id = mgr.RegisterTexture(info);
    EXPECT_EQ(mgr.GetStats().totalTextures, 1u);

    mgr.UnregisterTexture(id);
    EXPECT_EQ(mgr.GetStats().totalTextures, 0u);
    EXPECT_EQ(mgr.GetTextureInfo(id), nullptr);

    mgr.Shutdown();
}

TEST(SparseResidency, RequestAndExecuteBinds) {
    SparseResidencyManager mgr;
    SparseResidencyConfig config;
    config.physicalMemoryBudget = 128 * 1024 * 1024;
    config.minResidentMip = 0; // Don't auto-resident top mips for this test
    mgr.Init(nullptr, config);

    SparseTextureInfo info;
    info.width = 512;
    info.height = 512;
    info.mipLevels = 9;
    info.layers = 1;
    info.format = 44;
    info.pageWidth = 128;
    info.pageHeight = 128;
    info.debugName = "TestTex";

    u32 texId = mgr.RegisterTexture(info);

    SparsePageId page;
    page.textureId = texId;
    page.mipLevel = 0;
    page.layerIndex = 0;
    page.pageX = 0;
    page.pageY = 0;

    EXPECT_FALSE(mgr.IsResident(page));
    EXPECT_EQ(mgr.GetPageState(page), PageState::NonResident);

    mgr.RequestPage(page);
    EXPECT_EQ(mgr.GetPageState(page), PageState::Pending);

    mgr.ExecuteBinds();
    EXPECT_EQ(mgr.GetPageState(page), PageState::Resident);
    EXPECT_TRUE(mgr.IsResident(page));

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.residentPages, 1u);
    EXPECT_GT(stats.physicalMemoryUsed, 0u);

    mgr.Shutdown();
}

TEST(SparseResidency, ProcessFeedback) {
    SparseResidencyManager mgr;
    SparseResidencyConfig config;
    config.minResidentMip = 0;
    mgr.Init(nullptr, config);

    SparseTextureInfo info;
    info.width = 256;
    info.height = 256;
    info.mipLevels = 8;
    info.layers = 1;
    info.format = 44;
    info.pageWidth = 128;
    info.pageHeight = 128;
    info.debugName = "FeedbackTest";

    u32 texId = mgr.RegisterTexture(info);

    // Simulate GPU feedback
    SparsePageId requests[3];
    requests[0] = {texId, 0, 0, 0, 0};
    requests[1] = {texId, 0, 0, 1, 0};
    requests[2] = {texId, 1, 0, 0, 0};

    mgr.ProcessFeedback(requests, 3, 0);
    mgr.ExecuteBinds();

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.residentPages, 3u);
    EXPECT_EQ(stats.feedbackRequestsThisFrame, 3u);

    mgr.Shutdown();
}

TEST(SparseResidency, LRUEviction) {
    SparseResidencyManager mgr;
    SparseResidencyConfig config;
    config.physicalMemoryBudget = 1024 * 1024; // 1 MB — very small
    config.minResidentMip = 0;
    config.evictionBatchSize = 2;
    mgr.Init(nullptr, config);

    SparseTextureInfo info;
    info.width = 256;
    info.height = 256;
    info.mipLevels = 1;
    info.layers = 1;
    info.format = 44;
    info.pageWidth = 128;
    info.pageHeight = 128;
    info.debugName = "EvictTest";

    u32 texId = mgr.RegisterTexture(info);

    // Request several pages at different frames
    for (u32 i = 0; i < 4; ++i) {
        SparsePageId page = {texId, 0, 0, i % 2, i / 2};
        mgr.BeginFrame(i);
        mgr.RequestPage(page);
        mgr.ExecuteBinds();
    }

    u32 residentBefore = mgr.GetStats().residentPages;

    // Evict oldest 2
    u32 evicted = mgr.EvictLRU(2);
    EXPECT_GE(evicted, 1u); // At least some should be evicted

    EXPECT_LT(mgr.GetStats().residentPages, residentBefore);

    mgr.Shutdown();
}

TEST(SparseResidency, GetResidentPages) {
    SparseResidencyManager mgr;
    SparseResidencyConfig config;
    config.minResidentMip = 0;
    mgr.Init(nullptr, config);

    SparseTextureInfo info;
    info.width = 256;
    info.height = 256;
    info.mipLevels = 1;
    info.layers = 1;
    info.format = 44;
    info.pageWidth = 128;
    info.pageHeight = 128;
    info.debugName = "ResidentQuery";

    u32 texId = mgr.RegisterTexture(info);

    SparsePageId p0 = {texId, 0, 0, 0, 0};
    SparsePageId p1 = {texId, 0, 0, 1, 0};
    mgr.RequestPage(p0);
    mgr.RequestPage(p1);
    mgr.ExecuteBinds();

    auto resident = mgr.GetResidentPages(texId);
    EXPECT_EQ(resident.size(), 2u);

    mgr.Shutdown();
}

// ─── Barrier Batch Optimizer Tests ───────────────────────────────────────

TEST(BarrierOptimizer, InitAndShutdown) {
    BarrierBatchOptimizer opt;
    EXPECT_TRUE(opt.Init());
    opt.Shutdown();
}

TEST(BarrierOptimizer, RedundancyElimination) {
    BarrierBatchOptimizer opt;
    opt.Init();

    std::vector<ResourceBarrier> barriers;

    // Redundant: src == dst
    ResourceBarrier redundant;
    redundant.resourceHandle = 1;
    redundant.srcState = ResourceState::ShaderRead;
    redundant.dstState = ResourceState::ShaderRead;
    redundant.passIndex = 0;
    barriers.push_back(redundant);

    // Valid transition
    ResourceBarrier valid;
    valid.resourceHandle = 2;
    valid.srcState = ResourceState::RenderTarget;
    valid.dstState = ResourceState::ShaderRead;
    valid.passIndex = 1;
    barriers.push_back(valid);

    opt.Submit(barriers);
    opt.Optimize();

    auto stats = opt.GetStats();
    EXPECT_EQ(stats.inputBarriers, 2u);
    EXPECT_EQ(stats.redundantEliminated, 1u);
    EXPECT_EQ(stats.outputBarriers, 1u); // Only the valid one (may be split)

    opt.Shutdown();
}

TEST(BarrierOptimizer, MergeChainedBarriers) {
    BarrierBatchOptimizer opt;
    BarrierOptimizerConfig config;
    config.enableSplitBarriers = false; // Disable split for this test
    opt.Init(config);

    std::vector<ResourceBarrier> barriers;

    // Same resource, two transitions: A→B, B→C should merge to A→C
    ResourceBarrier b1;
    b1.resourceHandle = 100;
    b1.subresource = UINT32_MAX;
    b1.srcState = ResourceState::RenderTarget;
    b1.dstState = ResourceState::ShaderRead;
    b1.passIndex = 0;
    barriers.push_back(b1);

    ResourceBarrier b2;
    b2.resourceHandle = 100;
    b2.subresource = UINT32_MAX;
    b2.srcState = ResourceState::ShaderRead;
    b2.dstState = ResourceState::CopySrc;
    b2.passIndex = 2;
    barriers.push_back(b2);

    opt.Submit(barriers);
    opt.Optimize();

    auto optimized = opt.GetOptimizedBarriers();
    EXPECT_EQ(optimized.size(), 1u);
    if (!optimized.empty()) {
        EXPECT_EQ(static_cast<u32>(optimized[0].srcState), static_cast<u32>(ResourceState::RenderTarget));
        EXPECT_EQ(static_cast<u32>(optimized[0].dstState), static_cast<u32>(ResourceState::CopySrc));
    }

    EXPECT_EQ(opt.GetStats().merged, 1u);

    opt.Shutdown();
}

TEST(BarrierOptimizer, CrossQueueDetection) {
    BarrierBatchOptimizer opt;
    BarrierOptimizerConfig config;
    config.enableSplitBarriers = false;
    opt.Init(config);

    std::vector<ResourceBarrier> barriers;

    ResourceBarrier xq;
    xq.resourceHandle = 50;
    xq.srcState = ResourceState::ShaderWrite;
    xq.dstState = ResourceState::ShaderRead;
    xq.srcQueueFamily = 0;
    xq.dstQueueFamily = 1; // Cross-queue
    xq.passIndex = 0;
    barriers.push_back(xq);

    opt.Submit(barriers);
    opt.Optimize();

    EXPECT_EQ(opt.GetStats().crossQueueTransfers, 1u);

    opt.Shutdown();
}

TEST(BarrierOptimizer, BatchByPipelineStage) {
    BarrierBatchOptimizer opt;
    BarrierOptimizerConfig config;
    config.enableSplitBarriers = false;
    opt.Init(config);

    std::vector<ResourceBarrier> barriers;

    // Two barriers with same stage transition
    ResourceBarrier b1;
    b1.resourceHandle = 1;
    b1.srcState = ResourceState::RenderTarget;
    b1.dstState = ResourceState::ShaderRead;
    b1.passIndex = 0;
    barriers.push_back(b1);

    ResourceBarrier b2;
    b2.resourceHandle = 2;
    b2.srcState = ResourceState::RenderTarget;
    b2.dstState = ResourceState::ShaderRead;
    b2.passIndex = 0;
    barriers.push_back(b2);

    // Different stage transition
    ResourceBarrier b3;
    b3.resourceHandle = 3;
    b3.srcState = ResourceState::CopyDst;
    b3.dstState = ResourceState::VertexBuffer;
    b3.passIndex = 1;
    barriers.push_back(b3);

    opt.Submit(barriers);
    opt.Optimize();

    const auto& batches = opt.GetBatches();
    EXPECT_GE(batches.size(), 2u); // At least 2 different stage groups

    opt.Shutdown();
}

TEST(BarrierOptimizer, ClearResetsState) {
    BarrierBatchOptimizer opt;
    opt.Init();

    ResourceBarrier b;
    b.resourceHandle = 1;
    b.srcState = ResourceState::Undefined;
    b.dstState = ResourceState::RenderTarget;
    opt.Submit({b});
    opt.Optimize();

    EXPECT_GT(opt.GetStats().outputBarriers, 0u);

    opt.Clear();
    EXPECT_EQ(opt.GetStats().inputBarriers, 0u);
    EXPECT_EQ(opt.GetStats().outputBarriers, 0u);

    opt.Shutdown();
}

TEST(BarrierOptimizer, ReductionPercent) {
    BarrierBatchOptimizer opt;
    BarrierOptimizerConfig config;
    config.enableSplitBarriers = false;
    opt.Init(config);

    std::vector<ResourceBarrier> barriers;

    // 3 redundant + 1 valid = 75% reduction
    for (int i = 0; i < 3; ++i) {
        ResourceBarrier redundant;
        redundant.resourceHandle = static_cast<u64>(i + 10);
        redundant.srcState = ResourceState::ShaderRead;
        redundant.dstState = ResourceState::ShaderRead;
        redundant.passIndex = static_cast<u32>(i);
        barriers.push_back(redundant);
    }

    ResourceBarrier valid;
    valid.resourceHandle = 99;
    valid.srcState = ResourceState::Undefined;
    valid.dstState = ResourceState::RenderTarget;
    valid.passIndex = 5;
    barriers.push_back(valid);

    opt.Submit(barriers);
    opt.Optimize();

    auto stats = opt.GetStats();
    EXPECT_EQ(stats.inputBarriers, 4u);
    EXPECT_EQ(stats.redundantEliminated, 3u);
    EXPECT_GT(stats.reductionPercent, 50.0f);

    opt.Shutdown();
}
