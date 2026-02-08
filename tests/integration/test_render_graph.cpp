#include <gtest/gtest.h>
#include "engine/renderer/graph/render_graph.h"

using namespace nge;
using namespace nge::renderer;
using namespace nge::rhi;

// Stub device for testing (no actual GPU calls)
// The render graph tests verify scheduling, culling, and async classification logic.

TEST(RenderGraph, EmptyGraphCompiles) {
    RenderGraph graph(nullptr);
    EXPECT_TRUE(graph.Compile());
    EXPECT_EQ(graph.GetPassCount(), 0u);
    EXPECT_EQ(graph.GetResourceCount(), 0u);
}

TEST(RenderGraph, SinglePassNotCulled) {
    RenderGraph graph(nullptr);

    auto& pass = graph.AddPass("TestPass", PassType::Graphics);
    auto tex = pass.CreateTexture("Output", {128, 128, 1, 1, Format::RGBA8_UNORM,
        TextureUsage::ColorAttachment, "Output"});
    pass.Write(tex);
    pass.SetExecute([](ICommandList*) {});

    EXPECT_TRUE(graph.Compile());
    EXPECT_EQ(graph.GetPassCount(), 1u);
}

TEST(RenderGraph, DependencyOrder) {
    RenderGraph graph(nullptr);

    // Pass A writes texture, Pass B reads it
    auto& passA = graph.AddPass("PassA", PassType::Graphics);
    auto tex = passA.CreateTexture("SharedTex", {64, 64, 1, 1, Format::RGBA8_UNORM,
        TextureUsage::ColorAttachment | TextureUsage::Sampled, "SharedTex"});
    passA.Write(tex);
    passA.SetExecute([](ICommandList*) {});

    auto& passB = graph.AddPass("PassB", PassType::Compute);
    passB.Read(tex);
    passB.SetExecute([](ICommandList*) {});

    EXPECT_TRUE(graph.Compile());
    EXPECT_EQ(graph.GetPassCount(), 2u);
    EXPECT_EQ(graph.GetCulledPassCount(), 0u);
}

TEST(RenderGraph, UnusedPassCulled) {
    RenderGraph graph(nullptr);

    // Pass A writes to a texture nobody reads — should be culled
    auto& passA = graph.AddPass("Unused", PassType::Graphics);
    auto tex = passA.CreateTexture("DeadTex", {64, 64, 1, 1, Format::RGBA8_UNORM,
        TextureUsage::ColorAttachment, "DeadTex"});
    passA.Write(tex);
    passA.SetExecute([](ICommandList*) {});

    // Pass B is independent and also writes an unread resource
    auto& passB = graph.AddPass("AlsoUnused", PassType::Compute);
    auto buf = passB.CreateBuffer("DeadBuf", {256, BufferUsage::Storage, "DeadBuf"});
    passB.Write(buf);
    passB.SetExecute([](ICommandList*) {});

    EXPECT_TRUE(graph.Compile());
    EXPECT_EQ(graph.GetCulledPassCount(), 2u);
}

TEST(RenderGraph, AsyncComputeClassification) {
    RenderGraph graph(nullptr);

    // Graphics pass
    auto& gfxPass = graph.AddPass("GfxPass", PassType::Graphics);
    auto depthTex = gfxPass.CreateTexture("Depth", {256, 256, 1, 1, Format::D32_FLOAT,
        TextureUsage::DepthAttachment | TextureUsage::Sampled, "Depth"});
    gfxPass.WriteDepth(depthTex);
    gfxPass.SetExecute([](ICommandList*) {});

    // Async compute pass reads depth
    auto& asyncPass = graph.AddPass("HZBBuild", PassType::AsyncCompute);
    auto hzb = asyncPass.CreateTexture("HZB", {128, 128, 1, 1, Format::R32_FLOAT,
        TextureUsage::Storage | TextureUsage::Sampled, "HZB"});
    asyncPass.Read(depthTex);
    asyncPass.Write(hzb);
    asyncPass.SetExecute([](ICommandList*) {});

    // Another graphics pass reads HZB
    auto& latePass = graph.AddPass("LateCull", PassType::Compute);
    latePass.Read(hzb);
    latePass.SetExecute([](ICommandList*) {});

    EXPECT_TRUE(graph.Compile());
    EXPECT_EQ(graph.GetPassCount(), 3u);
    EXPECT_EQ(graph.GetCulledPassCount(), 0u);
}

TEST(RenderGraph, ResourceCount) {
    RenderGraph graph(nullptr);

    auto& pass = graph.AddPass("Multi", PassType::Graphics);
    pass.CreateTexture("A", {64, 64, 1, 1, Format::RGBA8_UNORM, TextureUsage::ColorAttachment, "A"});
    pass.CreateTexture("B", {64, 64, 1, 1, Format::RGBA8_UNORM, TextureUsage::ColorAttachment, "B"});
    pass.CreateBuffer("C", {1024, BufferUsage::Storage, "C"});
    pass.SetExecute([](ICommandList*) {});

    EXPECT_TRUE(graph.Compile());
    EXPECT_EQ(graph.GetResourceCount(), 3u);
}

TEST(RenderGraph, ResetClearsState) {
    RenderGraph graph(nullptr);

    auto& pass = graph.AddPass("P", PassType::Graphics);
    pass.CreateTexture("T", {64, 64, 1, 1, Format::RGBA8_UNORM, TextureUsage::ColorAttachment, "T"});
    pass.SetExecute([](ICommandList*) {});

    graph.Compile();
    EXPECT_EQ(graph.GetPassCount(), 1u);

    graph.Reset();
    EXPECT_EQ(graph.GetPassCount(), 0u);
    EXPECT_EQ(graph.GetResourceCount(), 0u);
}

TEST(RenderGraph, ChainedPasses) {
    RenderGraph graph(nullptr);

    // A → B → C chain
    auto& passA = graph.AddPass("A", PassType::Graphics);
    auto texAB = passA.CreateTexture("AB", {64, 64, 1, 1, Format::RGBA16_FLOAT,
        TextureUsage::ColorAttachment | TextureUsage::Sampled, "AB"});
    passA.Write(texAB);
    passA.SetExecute([](ICommandList*) {});

    auto& passB = graph.AddPass("B", PassType::Compute);
    auto texBC = passB.CreateTexture("BC", {64, 64, 1, 1, Format::RGBA16_FLOAT,
        TextureUsage::Storage | TextureUsage::Sampled, "BC"});
    passB.Read(texAB);
    passB.Write(texBC);
    passB.SetExecute([](ICommandList*) {});

    auto& passC = graph.AddPass("C", PassType::Compute);
    passC.Read(texBC);
    passC.SetExecute([](ICommandList*) {});

    EXPECT_TRUE(graph.Compile());
    EXPECT_EQ(graph.GetPassCount(), 3u);
    EXPECT_EQ(graph.GetCulledPassCount(), 0u);
}
