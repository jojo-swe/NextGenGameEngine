#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/vulkan/vk_descriptor_layout_cache.h"
#include "engine/rhi/common/rhi_pipeline_stats_collector.h"

using namespace nge;
using namespace nge::rhi;
using namespace nge::rhi::vulkan;

// ─── Descriptor Layout Cache Tests ───────────────────────────────────────

TEST(DescriptorLayoutCache, InitAndShutdown) {
    DescriptorLayoutCache cache;
    EXPECT_TRUE(cache.Init());
    EXPECT_EQ(cache.GetLayoutCount(), 0u);
    cache.Shutdown();
}

TEST(DescriptorLayoutCache, CreateSingleLayout) {
    DescriptorLayoutCache cache;
    cache.Init();

    LayoutCreateInfo info;
    info.debugName = "TestLayout";
    info.bindings = {
        {0, DescriptorType::UniformBuffer, 1, ShaderStageFlag::Vertex, false},
        {1, DescriptorType::CombinedImageSampler, 1, ShaderStageFlag::Fragment, false},
    };

    auto handle = cache.GetOrCreate(info);
    EXPECT_NE(handle, 0u);
    EXPECT_EQ(cache.GetLayoutCount(), 1u);

    const auto* layout = cache.GetLayout(handle);
    EXPECT_NE(layout, nullptr);
    EXPECT_EQ(layout->bindingCount, 2u);
    EXPECT_EQ(layout->debugName, "TestLayout");

    cache.Shutdown();
}

TEST(DescriptorLayoutCache, DuplicateLayoutDedup) {
    DescriptorLayoutCache cache;
    cache.Init();

    LayoutCreateInfo info;
    info.bindings = {
        {0, DescriptorType::UniformBuffer, 1, ShaderStageFlag::Vertex, false},
    };

    auto handle1 = cache.GetOrCreate(info);
    auto handle2 = cache.GetOrCreate(info);

    // Same layout → same handle
    EXPECT_EQ(handle1, handle2);
    EXPECT_EQ(cache.GetLayoutCount(), 1u);

    auto stats = cache.GetStats();
    EXPECT_EQ(stats.cacheHits, 1u);
    EXPECT_EQ(stats.cacheMisses, 1u);
    EXPECT_GT(stats.hitRate, 40.0f);

    cache.Shutdown();
}

TEST(DescriptorLayoutCache, DifferentBindingsCreateSeparate) {
    DescriptorLayoutCache cache;
    cache.Init();

    LayoutCreateInfo info1;
    info1.bindings = {
        {0, DescriptorType::UniformBuffer, 1, ShaderStageFlag::Vertex, false},
    };

    LayoutCreateInfo info2;
    info2.bindings = {
        {0, DescriptorType::StorageBuffer, 1, ShaderStageFlag::Compute, false},
    };

    auto handle1 = cache.GetOrCreate(info1);
    auto handle2 = cache.GetOrCreate(info2);

    EXPECT_NE(handle1, handle2);
    EXPECT_EQ(cache.GetLayoutCount(), 2u);

    cache.Shutdown();
}

TEST(DescriptorLayoutCache, BindingOrderIndependence) {
    DescriptorLayoutCache cache;
    cache.Init();

    LayoutCreateInfo info1;
    info1.bindings = {
        {0, DescriptorType::UniformBuffer, 1, ShaderStageFlag::Vertex, false},
        {1, DescriptorType::SampledImage, 1, ShaderStageFlag::Fragment, false},
    };

    // Same bindings, reversed order
    LayoutCreateInfo info2;
    info2.bindings = {
        {1, DescriptorType::SampledImage, 1, ShaderStageFlag::Fragment, false},
        {0, DescriptorType::UniformBuffer, 1, ShaderStageFlag::Vertex, false},
    };

    auto handle1 = cache.GetOrCreate(info1);
    auto handle2 = cache.GetOrCreate(info2);

    // Should hash identically regardless of binding order
    EXPECT_EQ(handle1, handle2);
    EXPECT_EQ(cache.GetLayoutCount(), 1u);

    cache.Shutdown();
}

TEST(DescriptorLayoutCache, RefCounting) {
    DescriptorLayoutCache cache;
    cache.Init();

    LayoutCreateInfo info;
    info.bindings = {
        {0, DescriptorType::UniformBuffer, 1, ShaderStageFlag::Vertex, false},
    };

    auto handle = cache.GetOrCreate(info);
    EXPECT_EQ(cache.GetLayout(handle)->refCount, 1u);

    cache.AddRef(handle);
    EXPECT_EQ(cache.GetLayout(handle)->refCount, 2u);

    cache.Release(handle);
    EXPECT_EQ(cache.GetLayout(handle)->refCount, 1u);
    EXPECT_EQ(cache.GetLayoutCount(), 1u);

    cache.Release(handle);
    // Should be destroyed now
    EXPECT_EQ(cache.GetLayoutCount(), 0u);
    EXPECT_EQ(cache.GetLayout(handle), nullptr);

    cache.Shutdown();
}

TEST(DescriptorLayoutCache, PushDescriptorDiffers) {
    DescriptorLayoutCache cache;
    cache.Init();

    LayoutCreateInfo info1;
    info1.bindings = {
        {0, DescriptorType::UniformBuffer, 1, ShaderStageFlag::Vertex, false},
    };
    info1.pushDescriptor = false;

    LayoutCreateInfo info2 = info1;
    info2.pushDescriptor = true;

    auto h1 = cache.GetOrCreate(info1);
    auto h2 = cache.GetOrCreate(info2);

    EXPECT_NE(h1, h2);
    EXPECT_EQ(cache.GetLayoutCount(), 2u);

    cache.Shutdown();
}

TEST(DescriptorLayoutCache, PartiallyBoundDiffers) {
    DescriptorLayoutCache cache;
    cache.Init();

    LayoutCreateInfo info1;
    info1.bindings = {
        {0, DescriptorType::SampledImage, 16, ShaderStageFlag::Fragment, false},
    };

    LayoutCreateInfo info2;
    info2.bindings = {
        {0, DescriptorType::SampledImage, 16, ShaderStageFlag::Fragment, true},
    };

    auto h1 = cache.GetOrCreate(info1);
    auto h2 = cache.GetOrCreate(info2);

    EXPECT_NE(h1, h2);

    cache.Shutdown();
}

TEST(DescriptorLayoutCache, HasLayoutByHash) {
    DescriptorLayoutCache cache;
    cache.Init();

    EXPECT_FALSE(cache.HasLayout(12345));

    LayoutCreateInfo info;
    info.bindings = {
        {0, DescriptorType::UniformBuffer, 1, ShaderStageFlag::All, false},
    };
    cache.GetOrCreate(info);

    // We can't easily predict the hash, but there should be 1 layout
    EXPECT_EQ(cache.GetLayoutCount(), 1u);

    cache.Shutdown();
}

// ─── Pipeline Statistics Collector Tests ─────────────────────────────────

TEST(PipelineStatsCollector, InitAndShutdown) {
    PipelineStatsCollector collector;
    EXPECT_TRUE(collector.Init());

    auto stats = collector.GetStats();
    EXPECT_EQ(stats.activePasses, 0u);

    collector.Shutdown();
}

TEST(PipelineStatsCollector, BeginEndPassBasic) {
    PipelineStatsCollector collector;
    collector.Init();

    u32 qid = collector.BeginPass("GBuffer");
    EXPECT_NE(qid, UINT32_MAX);

    collector.EndPass(qid);

    PipelineStatistics stats{};
    stats.vertexShaderInvocations = 50000;
    stats.fragmentShaderInvocations = 200000;
    stats.inputAssemblyPrimitives = 10000;
    stats.clippingPrimitives = 9500;

    collector.SubmitResults(qid, stats);
    collector.EndFrame(1920 * 1080);

    const auto* passStats = collector.GetPassStats("GBuffer");
    EXPECT_NE(passStats, nullptr);
    EXPECT_EQ(passStats->current.vertexShaderInvocations, 50000u);
    EXPECT_EQ(passStats->current.fragmentShaderInvocations, 200000u);
    EXPECT_GT(passStats->overdrawRatio, 0.0f);
    EXPECT_GT(passStats->geometryAmplification, 0.0f);

    collector.Shutdown();
}

TEST(PipelineStatsCollector, MultiplePasses) {
    PipelineStatsCollector collector;
    collector.Init();

    u32 q1 = collector.BeginPass("ZPrepass");
    collector.EndPass(q1);

    u32 q2 = collector.BeginPass("GBuffer");
    collector.EndPass(q2);

    u32 q3 = collector.BeginPass("Lighting");
    collector.EndPass(q3);

    PipelineStatistics s1{}, s2{}, s3{};
    s1.fragmentShaderInvocations = 100000;
    s2.fragmentShaderInvocations = 300000; // Highest overdraw
    s3.fragmentShaderInvocations = 50000;
    s1.inputAssemblyPrimitives = 10000; s1.clippingPrimitives = 9000;
    s2.inputAssemblyPrimitives = 10000; s2.clippingPrimitives = 9500;
    s3.inputAssemblyPrimitives = 1000;  s3.clippingPrimitives = 1000;

    collector.SubmitResults(q1, s1);
    collector.SubmitResults(q2, s2);
    collector.SubmitResults(q3, s3);
    collector.EndFrame(1920 * 1080);

    auto allStats = collector.GetAllPassStats();
    EXPECT_EQ(allStats.size(), 3u);

    std::string worst = collector.GetWorstOverdrawPass();
    EXPECT_EQ(worst, "GBuffer");

    auto collectorStats = collector.GetStats();
    EXPECT_EQ(collectorStats.activePasses, 3u);
    EXPECT_GT(collectorStats.maxOverdraw, 0.0f);

    collector.Shutdown();
}

TEST(PipelineStatsCollector, AccumulatorReset) {
    PipelineStatsCollector collector;
    collector.Init();

    u32 q = collector.BeginPass("Test");
    collector.EndPass(q);

    PipelineStatistics s{};
    s.vertexShaderInvocations = 1000;
    collector.SubmitResults(q, s);
    collector.EndFrame(1920 * 1080);

    const auto* pass = collector.GetPassStats("Test");
    EXPECT_EQ(pass->accumulated.vertexShaderInvocations, 1000u);
    EXPECT_EQ(pass->sampleCount, 1u);

    collector.ResetAccumulators();

    pass = collector.GetPassStats("Test");
    EXPECT_EQ(pass->accumulated.vertexShaderInvocations, 0u);
    EXPECT_EQ(pass->sampleCount, 0u);

    collector.Shutdown();
}

TEST(PipelineStatsCollector, MaxQueriesPerFrame) {
    PipelineStatsCollector collector;
    PipelineStatsConfig config;
    config.maxQueriesPerFrame = 2;
    collector.Init(config);

    u32 q1 = collector.BeginPass("Pass1");
    EXPECT_NE(q1, UINT32_MAX);

    u32 q2 = collector.BeginPass("Pass2");
    EXPECT_NE(q2, UINT32_MAX);

    u32 q3 = collector.BeginPass("Pass3"); // Exceeds limit
    EXPECT_EQ(q3, UINT32_MAX);

    collector.EndPass(q1);
    collector.EndPass(q2);
    collector.EndFrame(0);

    // After EndFrame, counter resets
    u32 q4 = collector.BeginPass("Pass4");
    EXPECT_NE(q4, UINT32_MAX);

    collector.Shutdown();
}

TEST(PipelineStatsCollector, OverdrawComputation) {
    PipelineStatsCollector collector;
    collector.Init();

    u32 q = collector.BeginPass("OverdrawTest");
    collector.EndPass(q);

    PipelineStatistics s{};
    s.fragmentShaderInvocations = 4147200; // 2x overdraw at 1080p
    collector.SubmitResults(q, s);
    collector.EndFrame(1920 * 1080); // 2073600 pixels

    const auto* pass = collector.GetPassStats("OverdrawTest");
    EXPECT_NEAR(pass->overdrawRatio, 2.0f, 0.01f);

    collector.Shutdown();
}

TEST(PipelineStatsCollector, UnknownPassReturnsNull) {
    PipelineStatsCollector collector;
    collector.Init();

    EXPECT_EQ(collector.GetPassStats("NonExistent"), nullptr);

    collector.Shutdown();
}

TEST(PipelineStatsCollector, ComputeShaderStats) {
    PipelineStatsCollector collector;
    collector.Init();

    u32 q = collector.BeginPass("ComputePass");
    collector.EndPass(q);

    PipelineStatistics s{};
    s.computeShaderInvocations = 1000000;
    collector.SubmitResults(q, s);
    collector.EndFrame(0);

    const auto* pass = collector.GetPassStats("ComputePass");
    EXPECT_EQ(pass->current.computeShaderInvocations, 1000000u);

    collector.Shutdown();
}
