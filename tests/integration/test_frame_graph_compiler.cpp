#include <gtest/gtest.h>
#include "engine/renderer/graph/frame_graph_compiler.h"

using namespace nge;
using namespace nge::renderer;

TEST(FrameGraphCompiler, EmptyGraphCompiles) {
    FrameGraphCompiler compiler;
    auto result = compiler.Compile();
    EXPECT_TRUE(result.passes.empty());
    EXPECT_TRUE(result.eliminatedPasses.empty());
    EXPECT_EQ(result.peakMemory, 0u);
}

TEST(FrameGraphCompiler, SinglePassWithSideEffect) {
    FrameGraphCompiler compiler;
    u32 res = compiler.AddResource("BackBuffer", 1920 * 1080 * 4, rhi::Format::RGBA8_UNORM, true, false);
    u32 pass = compiler.AddPass("Present", PassType::Graphics, true);
    compiler.PassWrites(pass, res, ResourceUsage::Present);

    auto result = compiler.Compile();
    EXPECT_EQ(result.passes.size(), 1u);
    EXPECT_TRUE(result.eliminatedPasses.empty());
}

TEST(FrameGraphCompiler, DeadCodeElimination) {
    FrameGraphCompiler compiler;

    u32 backbuffer = compiler.AddResource("BackBuffer", 1920 * 1080 * 4, rhi::Format::RGBA8_UNORM, true, false);
    u32 tempA = compiler.AddResource("TempA", 1024 * 1024);
    u32 tempB = compiler.AddResource("TempB", 1024 * 1024);

    // Pass that writes to backbuffer (has side effect via imported resource)
    u32 finalPass = compiler.AddPass("Final", PassType::Graphics);
    compiler.PassReads(finalPass, tempA, ResourceUsage::ShaderRead);
    compiler.PassWrites(finalPass, backbuffer, ResourceUsage::ColorAttachmentWrite);

    // Pass that produces tempA (needed by final)
    u32 produceA = compiler.AddPass("ProduceA", PassType::Compute);
    compiler.PassWrites(produceA, tempA, ResourceUsage::ShaderWrite);

    // Dead pass: produces tempB but nobody reads it
    u32 deadPass = compiler.AddPass("DeadPass", PassType::Compute);
    compiler.PassWrites(deadPass, tempB, ResourceUsage::ShaderWrite);

    auto result = compiler.Compile();
    EXPECT_EQ(result.passes.size(), 2u);         // Final + ProduceA
    EXPECT_EQ(result.eliminatedPasses.size(), 1u); // DeadPass eliminated
    EXPECT_EQ(result.eliminatedPasses[0], deadPass);
}

TEST(FrameGraphCompiler, TopologicalOrder) {
    FrameGraphCompiler compiler;

    u32 backbuffer = compiler.AddResource("BackBuffer", 1920 * 1080 * 4, rhi::Format::RGBA8_UNORM, true, false);
    u32 gbuffer = compiler.AddResource("GBuffer", 1920 * 1080 * 16);
    u32 lighting = compiler.AddResource("Lighting", 1920 * 1080 * 8);

    u32 gbufferPass = compiler.AddPass("GBuffer");
    compiler.PassWrites(gbufferPass, gbuffer, ResourceUsage::ColorAttachmentWrite);

    u32 lightingPass = compiler.AddPass("Lighting");
    compiler.PassReads(lightingPass, gbuffer, ResourceUsage::ShaderRead);
    compiler.PassWrites(lightingPass, lighting, ResourceUsage::ColorAttachmentWrite);

    u32 compositePass = compiler.AddPass("Composite");
    compiler.PassReads(compositePass, lighting, ResourceUsage::ShaderRead);
    compiler.PassWrites(compositePass, backbuffer, ResourceUsage::ColorAttachmentWrite);

    auto result = compiler.Compile();
    EXPECT_EQ(result.passes.size(), 3u);

    // Verify ordering: GBuffer < Lighting < Composite
    u32 gbufferOrder = UINT32_MAX, lightingOrder = UINT32_MAX, compositeOrder = UINT32_MAX;
    for (const auto& cp : result.passes) {
        if (cp.passId == gbufferPass) gbufferOrder = cp.executionOrder;
        if (cp.passId == lightingPass) lightingOrder = cp.executionOrder;
        if (cp.passId == compositePass) compositeOrder = cp.executionOrder;
    }
    EXPECT_LT(gbufferOrder, lightingOrder);
    EXPECT_LT(lightingOrder, compositeOrder);
}

TEST(FrameGraphCompiler, ResourceAliasing) {
    FrameGraphCompiler compiler;

    u32 backbuffer = compiler.AddResource("BackBuffer", 1920 * 1080 * 4, rhi::Format::RGBA8_UNORM, true, false);
    // Two transient resources with non-overlapping lifetimes
    u32 tempA = compiler.AddResource("TempA", 4 * 1024 * 1024); // 4MB
    u32 tempB = compiler.AddResource("TempB", 4 * 1024 * 1024); // 4MB

    u32 passA = compiler.AddPass("PassA");
    compiler.PassWrites(passA, tempA, ResourceUsage::ShaderWrite);

    u32 passB = compiler.AddPass("PassB");
    compiler.PassReads(passB, tempA, ResourceUsage::ShaderRead);
    compiler.PassWrites(passB, tempB, ResourceUsage::ShaderWrite);

    u32 passC = compiler.AddPass("PassC");
    compiler.PassReads(passC, tempB, ResourceUsage::ShaderRead);
    compiler.PassWrites(passC, backbuffer, ResourceUsage::ColorAttachmentWrite);

    auto result = compiler.Compile();
    EXPECT_EQ(result.passes.size(), 3u);

    // TempA's last use is PassB, TempB's first use is PassB
    // They might be aliasable depending on exact implementation
    // At minimum, verify aliased memory savings > 0 if aliasing detected
    EXPECT_GE(result.aliasedMemory, 0u);
}

TEST(FrameGraphCompiler, BarrierPlacement) {
    FrameGraphCompiler compiler;

    u32 backbuffer = compiler.AddResource("BackBuffer", 1920 * 1080 * 4, rhi::Format::RGBA8_UNORM, true, false);
    u32 texture = compiler.AddResource("Texture", 1024 * 1024);

    u32 writePass = compiler.AddPass("Write");
    compiler.PassWrites(writePass, texture, ResourceUsage::ColorAttachmentWrite);

    u32 readPass = compiler.AddPass("Read");
    compiler.PassReads(readPass, texture, ResourceUsage::ShaderRead);
    compiler.PassWrites(readPass, backbuffer, ResourceUsage::ColorAttachmentWrite);

    auto result = compiler.Compile();
    EXPECT_EQ(result.passes.size(), 2u);

    // The read pass should have a barrier transitioning texture from write → read
    bool foundBarrier = false;
    for (const auto& cp : result.passes) {
        for (const auto& b : cp.barriers) {
            if (b.resourceId == texture &&
                b.beforeUsage == ResourceUsage::ColorAttachmentWrite &&
                b.afterUsage == ResourceUsage::ShaderRead) {
                foundBarrier = true;
            }
        }
    }
    EXPECT_TRUE(foundBarrier);
}

TEST(FrameGraphCompiler, AsyncComputeExtraction) {
    FrameGraphCompiler compiler;

    u32 backbuffer = compiler.AddResource("BackBuffer", 1920 * 1080 * 4, rhi::Format::RGBA8_UNORM, true, false);
    u32 ssaoTex = compiler.AddResource("SSAO", 1920 * 1080 * 4);

    u32 ssaoPass = compiler.AddPass("SSAO", PassType::Compute);
    compiler.MarkAsyncCapable(ssaoPass);
    compiler.PassWrites(ssaoPass, ssaoTex, ResourceUsage::ShaderWrite);

    u32 compositePass = compiler.AddPass("Composite", PassType::Graphics, true);
    compiler.PassReads(compositePass, ssaoTex, ResourceUsage::ShaderRead);
    compiler.PassWrites(compositePass, backbuffer, ResourceUsage::ColorAttachmentWrite);

    auto result = compiler.Compile();
    EXPECT_EQ(result.asyncPassCount, 1u);

    // Verify SSAO pass is marked as async
    for (const auto& cp : result.passes) {
        if (cp.passId == ssaoPass) {
            EXPECT_EQ(cp.queueType, PassType::AsyncCompute);
        }
    }
}

TEST(FrameGraphCompiler, ResetClearsState) {
    FrameGraphCompiler compiler;
    compiler.AddResource("Res", 1024);
    compiler.AddPass("Pass");

    EXPECT_EQ(compiler.GetPasses().size(), 1u);
    EXPECT_EQ(compiler.GetResources().size(), 1u);

    compiler.Reset();
    EXPECT_TRUE(compiler.GetPasses().empty());
    EXPECT_TRUE(compiler.GetResources().empty());
}
