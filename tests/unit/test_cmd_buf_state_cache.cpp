#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_cmd_buf_state_cache.h"

using namespace nge::rhi;

TEST(CmdBufStateCache, InitAndShutdown) {
    CommandBufferStateCache cache;
    EXPECT_TRUE(cache.Init());
    EXPECT_EQ(cache.GetBoundPipeline(), 0u);
    cache.Shutdown();
}

TEST(CmdBufStateCache, BindPipelineFirstTime) {
    CommandBufferStateCache cache;
    cache.Init();

    EXPECT_TRUE(cache.BindPipeline(100)); // First bind -> changed
    EXPECT_EQ(cache.GetBoundPipeline(), 100u);

    cache.Shutdown();
}

TEST(CmdBufStateCache, BindPipelineRedundant) {
    CommandBufferStateCache cache;
    cache.Init();

    EXPECT_TRUE(cache.BindPipeline(100));  // First bind
    EXPECT_FALSE(cache.BindPipeline(100)); // Same pipeline -> redundant

    auto stats = cache.GetStats();
    EXPECT_EQ(stats.pipelineBinds, 1u);
    EXPECT_EQ(stats.pipelineBindsAvoided, 1u);

    cache.Shutdown();
}

TEST(CmdBufStateCache, BindPipelineChanged) {
    CommandBufferStateCache cache;
    cache.Init();

    EXPECT_TRUE(cache.BindPipeline(100));
    EXPECT_TRUE(cache.BindPipeline(200)); // Different pipeline -> changed

    EXPECT_EQ(cache.GetBoundPipeline(), 200u);

    auto stats = cache.GetStats();
    EXPECT_EQ(stats.pipelineBinds, 2u);
    EXPECT_EQ(stats.pipelineBindsAvoided, 0u);

    cache.Shutdown();
}

TEST(CmdBufStateCache, PipelineChangeDirtiesPushConstants) {
    CommandBufferStateCache cache;
    cache.Init();

    cache.BindPipeline(100);
    EXPECT_TRUE(cache.IsPushConstantDirty()); // Dirty after pipeline change

    cache.Shutdown();
}

TEST(CmdBufStateCache, BindDescriptorSetFirstTime) {
    CommandBufferStateCache cache;
    cache.Init();

    EXPECT_TRUE(cache.BindDescriptorSet(0, 500)); // First bind
    EXPECT_EQ(cache.GetBoundDescriptorSet(0), 500u);

    cache.Shutdown();
}

TEST(CmdBufStateCache, BindDescriptorSetRedundant) {
    CommandBufferStateCache cache;
    cache.Init();

    EXPECT_TRUE(cache.BindDescriptorSet(0, 500));
    EXPECT_FALSE(cache.BindDescriptorSet(0, 500)); // Same -> redundant

    auto stats = cache.GetStats();
    EXPECT_EQ(stats.descriptorSetBinds, 1u);
    EXPECT_EQ(stats.descriptorSetBindsAvoided, 1u);

    cache.Shutdown();
}

TEST(CmdBufStateCache, BindDescriptorSetDifferentIndex) {
    CommandBufferStateCache cache;
    cache.Init();

    EXPECT_TRUE(cache.BindDescriptorSet(0, 500));
    EXPECT_TRUE(cache.BindDescriptorSet(1, 600)); // Different set index

    EXPECT_EQ(cache.GetBoundDescriptorSet(0), 500u);
    EXPECT_EQ(cache.GetBoundDescriptorSet(1), 600u);

    cache.Shutdown();
}

TEST(CmdBufStateCache, BindVertexBufferFirstTime) {
    CommandBufferStateCache cache;
    cache.Init();

    EXPECT_TRUE(cache.BindVertexBuffer(0, 300, 0));

    cache.Shutdown();
}

TEST(CmdBufStateCache, BindVertexBufferRedundant) {
    CommandBufferStateCache cache;
    cache.Init();

    EXPECT_TRUE(cache.BindVertexBuffer(0, 300, 0));
    EXPECT_FALSE(cache.BindVertexBuffer(0, 300, 0)); // Same

    auto stats = cache.GetStats();
    EXPECT_EQ(stats.vertexBufferBinds, 1u);
    EXPECT_EQ(stats.vertexBufferBindsAvoided, 1u);

    cache.Shutdown();
}

TEST(CmdBufStateCache, BindVertexBufferDifferentOffset) {
    CommandBufferStateCache cache;
    cache.Init();

    EXPECT_TRUE(cache.BindVertexBuffer(0, 300, 0));
    EXPECT_TRUE(cache.BindVertexBuffer(0, 300, 1024)); // Same buffer, different offset

    cache.Shutdown();
}

TEST(CmdBufStateCache, BindIndexBufferFirstTime) {
    CommandBufferStateCache cache;
    cache.Init();

    EXPECT_TRUE(cache.BindIndexBuffer(400, 0, 1)); // U32

    cache.Shutdown();
}

TEST(CmdBufStateCache, BindIndexBufferRedundant) {
    CommandBufferStateCache cache;
    cache.Init();

    EXPECT_TRUE(cache.BindIndexBuffer(400, 0, 1));
    EXPECT_FALSE(cache.BindIndexBuffer(400, 0, 1)); // Same

    auto stats = cache.GetStats();
    EXPECT_EQ(stats.indexBufferBinds, 1u);
    EXPECT_EQ(stats.indexBufferBindsAvoided, 1u);

    cache.Shutdown();
}

TEST(CmdBufStateCache, BindIndexBufferDifferentType) {
    CommandBufferStateCache cache;
    cache.Init();

    EXPECT_TRUE(cache.BindIndexBuffer(400, 0, 0));  // U16
    EXPECT_TRUE(cache.BindIndexBuffer(400, 0, 1));  // U32 -> changed

    cache.Shutdown();
}

TEST(CmdBufStateCache, SetViewportFirstTime) {
    CommandBufferStateCache cache;
    cache.Init();

    Viewport vp{0, 0, 1920, 1080, 0, 1};
    EXPECT_TRUE(cache.SetViewport(vp));

    cache.Shutdown();
}

TEST(CmdBufStateCache, SetViewportRedundant) {
    CommandBufferStateCache cache;
    cache.Init();

    Viewport vp{0, 0, 1920, 1080, 0, 1};
    EXPECT_TRUE(cache.SetViewport(vp));
    EXPECT_FALSE(cache.SetViewport(vp)); // Same

    auto stats = cache.GetStats();
    EXPECT_EQ(stats.viewportSets, 1u);
    EXPECT_EQ(stats.viewportSetsAvoided, 1u);

    cache.Shutdown();
}

TEST(CmdBufStateCache, SetScissorFirstTime) {
    CommandBufferStateCache cache;
    cache.Init();

    ScissorRect sc{0, 0, 1920, 1080};
    EXPECT_TRUE(cache.SetScissor(sc));

    cache.Shutdown();
}

TEST(CmdBufStateCache, SetScissorRedundant) {
    CommandBufferStateCache cache;
    cache.Init();

    ScissorRect sc{0, 0, 1920, 1080};
    EXPECT_TRUE(cache.SetScissor(sc));
    EXPECT_FALSE(cache.SetScissor(sc)); // Same

    auto stats = cache.GetStats();
    EXPECT_EQ(stats.scissorSets, 1u);
    EXPECT_EQ(stats.scissorSetsAvoided, 1u);

    cache.Shutdown();
}

TEST(CmdBufStateCache, InvalidateResetsAllState) {
    CommandBufferStateCache cache;
    cache.Init();

    cache.BindPipeline(100);
    cache.BindDescriptorSet(0, 500);
    cache.BindVertexBuffer(0, 300, 0);
    cache.BindIndexBuffer(400, 0, 1);
    cache.SetViewport({0, 0, 1920, 1080, 0, 1});
    cache.SetScissor({0, 0, 1920, 1080});

    cache.Invalidate();

    EXPECT_EQ(cache.GetBoundPipeline(), 0u);
    EXPECT_EQ(cache.GetBoundDescriptorSet(0), 0u);
    EXPECT_TRUE(cache.IsPushConstantDirty());

    // After invalidate, all binds should report changed
    EXPECT_TRUE(cache.BindPipeline(100));
    EXPECT_TRUE(cache.BindDescriptorSet(0, 500));
    EXPECT_TRUE(cache.BindVertexBuffer(0, 300, 0));
    EXPECT_TRUE(cache.BindIndexBuffer(400, 0, 1));
    EXPECT_TRUE(cache.SetViewport({0, 0, 1920, 1080, 0, 1}));
    EXPECT_TRUE(cache.SetScissor({0, 0, 1920, 1080}));

    cache.Shutdown();
}

TEST(CmdBufStateCache, MarkPushConstantDirty) {
    CommandBufferStateCache cache;
    cache.Init();

    cache.MarkPushConstantDirty();
    EXPECT_TRUE(cache.IsPushConstantDirty());

    auto stats = cache.GetStats();
    EXPECT_EQ(stats.pushConstantUpdates, 1u);

    cache.Shutdown();
}

TEST(CmdBufStateCache, ResetStatsClears) {
    CommandBufferStateCache cache;
    cache.Init();

    cache.BindPipeline(100);
    cache.BindPipeline(100); // Avoided

    auto stats = cache.GetStats();
    EXPECT_EQ(stats.pipelineBinds, 1u);
    EXPECT_EQ(stats.pipelineBindsAvoided, 1u);

    cache.ResetStats();

    stats = cache.GetStats();
    EXPECT_EQ(stats.pipelineBinds, 0u);
    EXPECT_EQ(stats.pipelineBindsAvoided, 0u);
    EXPECT_EQ(stats.totalAvoided, 0u);
    EXPECT_EQ(stats.totalIssued, 0u);

    cache.Shutdown();
}

TEST(CmdBufStateCache, AvoidanceRatio) {
    CommandBufferStateCache cache;
    cache.Init();

    cache.BindPipeline(100);        // Issued
    cache.BindPipeline(100);        // Avoided
    cache.BindPipeline(100);        // Avoided
    cache.BindDescriptorSet(0, 50); // Issued

    auto stats = cache.GetStats();
    // 2 issued, 2 avoided -> ratio = 2/4 = 0.5
    EXPECT_EQ(stats.totalIssued, 2u);
    EXPECT_EQ(stats.totalAvoided, 2u);
    EXPECT_NEAR(stats.avoidanceRatio, 0.5f, 0.01f);

    cache.Shutdown();
}

TEST(CmdBufStateCache, GetBoundDescriptorSetOutOfRange) {
    CommandBufferStateCache cache;
    cache.Init();

    EXPECT_EQ(cache.GetBoundDescriptorSet(99), 0u);

    cache.Shutdown();
}

TEST(CmdBufStateCache, MixedStateChanges) {
    CommandBufferStateCache cache;
    cache.Init();

    // Simulate a draw sequence
    cache.BindPipeline(1);
    cache.BindDescriptorSet(0, 10);
    cache.BindDescriptorSet(1, 20);
    cache.BindVertexBuffer(0, 100, 0);
    cache.BindIndexBuffer(200, 0, 1);
    cache.SetViewport({0, 0, 1920, 1080, 0, 1});
    cache.SetScissor({0, 0, 1920, 1080});

    // Second draw with same state
    EXPECT_FALSE(cache.BindPipeline(1));
    EXPECT_FALSE(cache.BindDescriptorSet(0, 10));
    EXPECT_FALSE(cache.BindDescriptorSet(1, 20));
    EXPECT_FALSE(cache.BindVertexBuffer(0, 100, 0));
    EXPECT_FALSE(cache.BindIndexBuffer(200, 0, 1));
    EXPECT_FALSE(cache.SetViewport({0, 0, 1920, 1080, 0, 1}));
    EXPECT_FALSE(cache.SetScissor({0, 0, 1920, 1080}));

    // Third draw with different material (set 1 changes)
    EXPECT_FALSE(cache.BindPipeline(1));           // Same
    EXPECT_FALSE(cache.BindDescriptorSet(0, 10));  // Same
    EXPECT_TRUE(cache.BindDescriptorSet(1, 30));   // Changed
    EXPECT_FALSE(cache.BindVertexBuffer(0, 100, 0)); // Same

    auto stats = cache.GetStats();
    EXPECT_GT(stats.totalAvoided, 0u);
    EXPECT_GT(stats.avoidanceRatio, 0.0f);

    cache.Shutdown();
}
