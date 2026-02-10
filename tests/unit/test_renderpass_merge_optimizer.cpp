#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_renderpass_merge_optimizer.h"

using namespace nge;
using namespace nge::rhi;

static RenderPassDecl MakePass(u32 id, const std::string& name, u32 w, u32 h,
                                u32 samples = 1, bool hasDepth = false) {
    RenderPassDecl pass;
    pass.passId = id;
    pass.debugName = name;
    pass.width = w;
    pass.height = h;
    pass.samples = samples;
    pass.hasDepth = hasDepth;
    pass.usesInputAttachments = false;

    if (hasDepth) {
        pass.depthAttachment.resourceHandle = 9000 + id;
        pass.depthAttachment.format = 124; // D32_SFLOAT placeholder
        pass.depthAttachment.width = w;
        pass.depthAttachment.height = h;
        pass.depthAttachment.loadOp = AttachmentLoadOp::Clear;
        pass.depthAttachment.storeOp = AttachmentStoreOp::Store;
        pass.depthAttachment.usage = AttachmentUsage::DepthStencilOutput;
    }

    return pass;
}

static void AddColorAttachment(RenderPassDecl& pass, u64 handle, u32 format = 44) {
    PassAttachment att;
    att.resourceHandle = handle;
    att.format = format;
    att.width = pass.width;
    att.height = pass.height;
    att.loadOp = AttachmentLoadOp::Clear;
    att.storeOp = AttachmentStoreOp::Store;
    att.usage = AttachmentUsage::ColorOutput;
    pass.colorAttachments.push_back(att);
}

TEST(RenderPassMergeOptimizer, InitAndShutdown) {
    RenderPassMergeOptimizer optimizer;
    EXPECT_TRUE(optimizer.Init());

    auto stats = optimizer.GetStats();
    EXPECT_EQ(stats.totalPassesDeclared, 0u);

    optimizer.Shutdown();
}

TEST(RenderPassMergeOptimizer, SinglePassNoMerge) {
    RenderPassMergeOptimizer optimizer;
    optimizer.Init();

    auto pass = MakePass(0, "GBuffer", 1920, 1080, 1, true);
    AddColorAttachment(pass, 1);
    optimizer.DeclarePass(pass);

    auto result = optimizer.Optimize();
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].subpassCount, 1u);
    EXPECT_EQ(result[0].originalPassIds.size(), 1u);
    EXPECT_EQ(result[0].originalPassIds[0], 0u);

    optimizer.Shutdown();
}

TEST(RenderPassMergeOptimizer, TwoCompatiblePassesMerge) {
    RenderPassMergeOptimizer optimizer;
    optimizer.Init();

    auto p0 = MakePass(0, "GBuffer", 1920, 1080);
    AddColorAttachment(p0, 1);
    auto p1 = MakePass(1, "Lighting", 1920, 1080);
    AddColorAttachment(p1, 2);

    optimizer.DeclarePass(p0);
    optimizer.DeclarePass(p1);

    auto result = optimizer.Optimize();
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].subpassCount, 2u);
    EXPECT_EQ(result[0].originalPassIds.size(), 2u);

    optimizer.Shutdown();
}

TEST(RenderPassMergeOptimizer, DifferentResolutionNotMerged) {
    RenderPassMergeOptimizer optimizer;
    optimizer.Init();

    auto p0 = MakePass(0, "FullRes", 1920, 1080);
    AddColorAttachment(p0, 1);
    auto p1 = MakePass(1, "HalfRes", 960, 540);
    AddColorAttachment(p1, 2);

    optimizer.DeclarePass(p0);
    optimizer.DeclarePass(p1);

    auto result = optimizer.Optimize();
    EXPECT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].subpassCount, 1u);
    EXPECT_EQ(result[1].subpassCount, 1u);

    optimizer.Shutdown();
}

TEST(RenderPassMergeOptimizer, DifferentSampleCountNotMerged) {
    RenderPassMergeOptimizer optimizer;
    optimizer.Init();

    auto p0 = MakePass(0, "MSAA4x", 1920, 1080, 4);
    AddColorAttachment(p0, 1);
    auto p1 = MakePass(1, "NoMSAA", 1920, 1080, 1);
    AddColorAttachment(p1, 2);

    optimizer.DeclarePass(p0);
    optimizer.DeclarePass(p1);

    auto result = optimizer.Optimize();
    EXPECT_EQ(result.size(), 2u);

    optimizer.Shutdown();
}

TEST(RenderPassMergeOptimizer, ThreePassesMergeIntoOne) {
    RenderPassMergeOptimizer optimizer;
    optimizer.Init();

    for (u32 i = 0; i < 3; ++i) {
        auto p = MakePass(i, "Pass" + std::to_string(i), 1920, 1080);
        AddColorAttachment(p, 100 + i);
        optimizer.DeclarePass(p);
    }

    auto result = optimizer.Optimize();
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].subpassCount, 3u);

    optimizer.Shutdown();
}

TEST(RenderPassMergeOptimizer, MaxSubpassesLimit) {
    RenderPassMergeOptimizer optimizer;
    RenderPassMergeConfig config;
    config.rules.maxSubpassesPerMerge = 2;
    optimizer.Init(config);

    for (u32 i = 0; i < 4; ++i) {
        auto p = MakePass(i, "P" + std::to_string(i), 1920, 1080);
        AddColorAttachment(p, 100 + i);
        optimizer.DeclarePass(p);
    }

    auto result = optimizer.Optimize();
    // 4 passes, max 2 subpasses: should produce 2 merged passes
    EXPECT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].subpassCount, 2u);
    EXPECT_EQ(result[1].subpassCount, 2u);

    optimizer.Shutdown();
}

TEST(RenderPassMergeOptimizer, MixedResolutionsGroupCorrectly) {
    RenderPassMergeOptimizer optimizer;
    optimizer.Init();

    auto p0 = MakePass(0, "Full_A", 1920, 1080);
    AddColorAttachment(p0, 1);
    auto p1 = MakePass(1, "Full_B", 1920, 1080);
    AddColorAttachment(p1, 2);
    auto p2 = MakePass(2, "Half_A", 960, 540);
    AddColorAttachment(p2, 3);
    auto p3 = MakePass(3, "Half_B", 960, 540);
    AddColorAttachment(p3, 4);

    optimizer.DeclarePass(p0);
    optimizer.DeclarePass(p1);
    optimizer.DeclarePass(p2);
    optimizer.DeclarePass(p3);

    auto result = optimizer.Optimize();
    // Full_A + Full_B merge, then Half_A + Half_B merge
    EXPECT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].subpassCount, 2u);
    EXPECT_EQ(result[0].width, 1920u);
    EXPECT_EQ(result[1].subpassCount, 2u);
    EXPECT_EQ(result[1].width, 960u);

    optimizer.Shutdown();
}

TEST(RenderPassMergeOptimizer, AreMergeableQuery) {
    RenderPassMergeOptimizer optimizer;
    optimizer.Init();

    auto p0 = MakePass(0, "A", 1920, 1080);
    auto p1 = MakePass(1, "B", 1920, 1080);
    auto p2 = MakePass(2, "C", 960, 540);

    optimizer.DeclarePass(p0);
    optimizer.DeclarePass(p1);
    optimizer.DeclarePass(p2);

    EXPECT_TRUE(optimizer.AreMergeable(0, 1));
    EXPECT_FALSE(optimizer.AreMergeable(0, 2));
    EXPECT_FALSE(optimizer.AreMergeable(99, 0)); // Non-existent

    optimizer.Shutdown();
}

TEST(RenderPassMergeOptimizer, GetPass) {
    RenderPassMergeOptimizer optimizer;
    optimizer.Init();

    auto p0 = MakePass(42, "TestPass", 1920, 1080, 4, true);
    AddColorAttachment(p0, 1);
    optimizer.DeclarePass(p0);

    const auto* pass = optimizer.GetPass(42);
    EXPECT_NE(pass, nullptr);
    EXPECT_EQ(pass->passId, 42u);
    EXPECT_EQ(pass->debugName, "TestPass");
    EXPECT_EQ(pass->width, 1920u);
    EXPECT_EQ(pass->samples, 4u);
    EXPECT_TRUE(pass->hasDepth);

    EXPECT_EQ(optimizer.GetPass(999), nullptr);

    optimizer.Shutdown();
}

TEST(RenderPassMergeOptimizer, MergedPassCollectsUniqueAttachments) {
    RenderPassMergeOptimizer optimizer;
    optimizer.Init();

    auto p0 = MakePass(0, "GBuffer", 1920, 1080, 1, true);
    AddColorAttachment(p0, 1);
    AddColorAttachment(p0, 2);

    auto p1 = MakePass(1, "Lighting", 1920, 1080, 1, true);
    AddColorAttachment(p1, 3);
    // Share depth attachment handle with p0
    p1.depthAttachment.resourceHandle = p0.depthAttachment.resourceHandle;

    optimizer.DeclarePass(p0);
    optimizer.DeclarePass(p1);

    auto result = optimizer.Optimize();
    EXPECT_EQ(result.size(), 1u);

    // Should have unique attachments: color 1, 2, 3 + depth (shared)
    // p0 has colors 1,2 + depth 9000, p1 has color 3 + depth 9000
    // Unique: 1, 2, 9000, 3 = 4
    EXPECT_EQ(result[0].allAttachments.size(), 4u);

    optimizer.Shutdown();
}

TEST(RenderPassMergeOptimizer, ClearResetsAll) {
    RenderPassMergeOptimizer optimizer;
    optimizer.Init();

    optimizer.DeclarePass(MakePass(0, "A", 1920, 1080));
    optimizer.DeclarePass(MakePass(1, "B", 1920, 1080));
    optimizer.Optimize();

    optimizer.Clear();

    auto stats = optimizer.GetStats();
    EXPECT_EQ(stats.totalPassesDeclared, 0u);
    EXPECT_EQ(stats.mergeAttempts, 0u);
    EXPECT_EQ(stats.mergeSuccesses, 0u);

    optimizer.Shutdown();
}

TEST(RenderPassMergeOptimizer, StatsTracking) {
    RenderPassMergeOptimizer optimizer;
    optimizer.Init();

    auto p0 = MakePass(0, "A", 1920, 1080);
    auto p1 = MakePass(1, "B", 1920, 1080);
    auto p2 = MakePass(2, "C", 960, 540); // Different res, won't merge

    optimizer.DeclarePass(p0);
    optimizer.DeclarePass(p1);
    optimizer.DeclarePass(p2);
    optimizer.Optimize();

    auto stats = optimizer.GetStats();
    EXPECT_EQ(stats.totalPassesDeclared, 3u);
    EXPECT_GE(stats.mergeAttempts, 1u);
    EXPECT_GE(stats.mergeSuccesses, 1u);
    EXPECT_GE(stats.mergeRejections, 1u);

    optimizer.Shutdown();
}

TEST(RenderPassMergeOptimizer, MaxPassesLimit) {
    RenderPassMergeOptimizer optimizer;
    RenderPassMergeConfig config;
    config.maxPasses = 3;
    optimizer.Init(config);

    for (u32 i = 0; i < 5; ++i) {
        optimizer.DeclarePass(MakePass(i, "P" + std::to_string(i), 1920, 1080));
    }

    EXPECT_EQ(optimizer.GetStats().totalPassesDeclared, 3u);

    optimizer.Shutdown();
}

TEST(RenderPassMergeOptimizer, MergedPassDebugName) {
    RenderPassMergeOptimizer optimizer;
    optimizer.Init();

    optimizer.DeclarePass(MakePass(0, "GBuffer", 1920, 1080));
    optimizer.DeclarePass(MakePass(1, "Lighting", 1920, 1080));

    auto result = optimizer.Optimize();
    EXPECT_EQ(result.size(), 1u);
    EXPECT_NE(result[0].debugName.find("GBuffer"), std::string::npos);
    EXPECT_NE(result[0].debugName.find("Lighting"), std::string::npos);

    optimizer.Shutdown();
}
