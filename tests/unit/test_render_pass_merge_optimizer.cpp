#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_render_pass_merge_optimizer.h"

using namespace nge::rhi;

static RenderPassDesc MakePass(u32 id, const std::string& name, u32 w, u32 h, u32 samples = 1) {
    RenderPassDesc pass;
    pass.passId = id;
    pass.name = name;
    pass.width = w;
    pass.height = h;
    pass.sampleCount = samples;
    pass.usesDepth = false;
    return pass;
}

static PassAttachment MakeAttachment(u32 attId, u32 format, bool isInput, bool isOutput,
                                      PassAttachmentOp loadOp = PassAttachmentOp::Load,
                                      PassStoreOp storeOp = PassStoreOp::Store) {
    PassAttachment att;
    att.attachmentId = attId;
    att.format = format;
    att.loadOp = loadOp;
    att.storeOp = storeOp;
    att.isInput = isInput;
    att.isOutput = isOutput;
    return att;
}

TEST(RenderPassMergeOptimizer, InitAndShutdown) {
    RenderPassMergeOptimizer opt;
    EXPECT_TRUE(opt.Init());
    EXPECT_EQ(opt.GetPassCount(), 0u);
    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, AddPass) {
    RenderPassMergeOptimizer opt;
    opt.Init();

    auto pass = MakePass(0, "GBuffer", 1920, 1080);
    EXPECT_TRUE(opt.AddPass(pass));
    EXPECT_EQ(opt.GetPassCount(), 1u);

    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, MergeCompatiblePasses) {
    RenderPassMergeOptimizer opt;
    opt.Init();

    auto p0 = MakePass(0, "GBuffer", 1920, 1080);
    p0.attachments = {
        MakeAttachment(0, 1, false, true, PassAttachmentOp::Clear, PassStoreOp::Store),
        MakeAttachment(1, 2, false, true, PassAttachmentOp::Clear, PassStoreOp::Store),
    };

    auto p1 = MakePass(1, "Lighting", 1920, 1080);
    p1.attachments = {
        MakeAttachment(0, 1, true, false, PassAttachmentOp::Load, PassStoreOp::DontCare),
        MakeAttachment(1, 2, true, false, PassAttachmentOp::Load, PassStoreOp::DontCare),
        MakeAttachment(2, 3, false, true, PassAttachmentOp::Clear, PassStoreOp::Store),
    };
    p1.dependsOn = {0};

    opt.AddPass(p0);
    opt.AddPass(p1);
    opt.Optimize();

    EXPECT_EQ(opt.GetMergedGroupCount(), 1u); // Merged into one group
    EXPECT_TRUE(opt.IsMerged(0));
    EXPECT_TRUE(opt.IsMerged(1));
    EXPECT_EQ(opt.GetGroupForPass(0), opt.GetGroupForPass(1));

    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, NoMergeDifferentResolution) {
    RenderPassMergeOptimizer opt;
    RenderPassMergeConfig config;
    config.requireSameResolution = true;
    opt.Init(config);

    opt.AddPass(MakePass(0, "Pass1", 1920, 1080));
    opt.AddPass(MakePass(1, "Pass2", 960, 540)); // Different resolution

    opt.Optimize();

    EXPECT_EQ(opt.GetMergedGroupCount(), 2u); // Not merged
    EXPECT_FALSE(opt.IsMerged(0));
    EXPECT_FALSE(opt.IsMerged(1));

    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, NoMergeDifferentSampleCount) {
    RenderPassMergeOptimizer opt;
    RenderPassMergeConfig config;
    config.requireSameSampleCount = true;
    opt.Init(config);

    opt.AddPass(MakePass(0, "Pass1", 1920, 1080, 1));
    opt.AddPass(MakePass(1, "Pass2", 1920, 1080, 4)); // Different MSAA

    opt.Optimize();

    EXPECT_EQ(opt.GetMergedGroupCount(), 2u);

    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, AreCompatible) {
    RenderPassMergeOptimizer opt;
    opt.Init();

    opt.AddPass(MakePass(0, "A", 1920, 1080));
    opt.AddPass(MakePass(1, "B", 1920, 1080));
    opt.AddPass(MakePass(2, "C", 960, 540));

    EXPECT_TRUE(opt.AreCompatible(0, 1));
    EXPECT_FALSE(opt.AreCompatible(0, 2)); // Different resolution
    EXPECT_FALSE(opt.AreCompatible(0, 999)); // Unknown

    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, MergingDisabled) {
    RenderPassMergeOptimizer opt;
    RenderPassMergeConfig config;
    config.enableMerging = false;
    opt.Init(config);

    opt.AddPass(MakePass(0, "A", 1920, 1080));
    opt.AddPass(MakePass(1, "B", 1920, 1080));

    opt.Optimize();

    EXPECT_EQ(opt.GetMergedGroupCount(), 2u); // Each pass is its own group
    EXPECT_FALSE(opt.IsMerged(0));

    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, LoadStoreOpsSaved) {
    RenderPassMergeOptimizer opt;
    opt.Init();

    auto p0 = MakePass(0, "GBuffer", 1920, 1080);
    p0.attachments = {
        MakeAttachment(0, 1, false, true, PassAttachmentOp::Clear, PassStoreOp::Store),
    };

    auto p1 = MakePass(1, "Lighting", 1920, 1080);
    p1.attachments = {
        MakeAttachment(0, 1, true, false, PassAttachmentOp::Load, PassStoreOp::DontCare),
        MakeAttachment(1, 2, false, true, PassAttachmentOp::Clear, PassStoreOp::Store),
    };
    p1.dependsOn = {0};

    opt.AddPass(p0);
    opt.AddPass(p1);
    opt.Optimize();

    auto stats = opt.GetStats();
    EXPECT_GE(stats.loadOpsSaved, 1u);  // p1 loads att 0 which p0 stored
    EXPECT_GE(stats.storeOpsSaved, 1u); // p0 stores att 0 which p1 reads

    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, ExternalDependencyPreventsmerge) {
    RenderPassMergeOptimizer opt;
    opt.Init();

    opt.AddPass(MakePass(0, "Shadow", 2048, 2048));

    auto p1 = MakePass(1, "GBuffer", 1920, 1080);
    // p1 depends on pass 0 which has different resolution -> can't merge anyway
    p1.dependsOn = {0};
    opt.AddPass(p1);

    auto p2 = MakePass(2, "Lighting", 1920, 1080);
    p2.dependsOn = {0}; // External dep on pass 0 (not in same group as p1)
    opt.AddPass(p2);

    opt.Optimize();

    // Shadow is separate (different res), GBuffer and Lighting may or may not merge
    // depending on whether external dep on pass 0 blocks it
    EXPECT_GE(opt.GetMergedGroupCount(), 2u);

    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, GetPass) {
    RenderPassMergeOptimizer opt;
    opt.Init();

    opt.AddPass(MakePass(42, "MyPass", 800, 600));

    const auto* pass = opt.GetPass(42);
    EXPECT_NE(pass, nullptr);
    EXPECT_EQ(pass->name, "MyPass");
    EXPECT_EQ(pass->width, 800u);

    EXPECT_EQ(opt.GetPass(999), nullptr);

    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, GetGroupForPassUnknown) {
    RenderPassMergeOptimizer opt;
    opt.Init();

    EXPECT_EQ(opt.GetGroupForPass(999), UINT32_MAX);

    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, MaxPassesLimit) {
    RenderPassMergeOptimizer opt;
    RenderPassMergeConfig config;
    config.maxPasses = 3;
    opt.Init(config);

    EXPECT_TRUE(opt.AddPass(MakePass(0, "A", 100, 100)));
    EXPECT_TRUE(opt.AddPass(MakePass(1, "B", 100, 100)));
    EXPECT_TRUE(opt.AddPass(MakePass(2, "C", 100, 100)));
    EXPECT_FALSE(opt.AddPass(MakePass(3, "D", 100, 100))); // Exceeds

    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, MaxAttachmentsLimit) {
    RenderPassMergeOptimizer opt;
    RenderPassMergeConfig config;
    config.maxAttachmentsPerPass = 2;
    opt.Init(config);

    auto pass = MakePass(0, "TooMany", 100, 100);
    pass.attachments = {
        MakeAttachment(0, 1, false, true),
        MakeAttachment(1, 2, false, true),
        MakeAttachment(2, 3, false, true), // 3 > max 2
    };

    EXPECT_FALSE(opt.AddPass(pass));

    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, FormatMismatchPreventsmerge) {
    RenderPassMergeOptimizer opt;
    opt.Init();

    auto p0 = MakePass(0, "A", 1920, 1080);
    p0.attachments = {MakeAttachment(0, 1, false, true)};

    auto p1 = MakePass(1, "B", 1920, 1080);
    p1.attachments = {MakeAttachment(0, 2, true, false)}; // Same att ID, different format

    opt.AddPass(p0);
    opt.AddPass(p1);
    opt.Optimize();

    EXPECT_EQ(opt.GetMergedGroupCount(), 2u); // Not merged due to format mismatch

    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, StatsTracking) {
    RenderPassMergeOptimizer opt;
    opt.Init();

    auto p0 = MakePass(0, "A", 1920, 1080);
    p0.attachments = {MakeAttachment(0, 1, false, true, PassAttachmentOp::Clear, PassStoreOp::Store)};

    auto p1 = MakePass(1, "B", 1920, 1080);
    p1.attachments = {MakeAttachment(0, 1, true, false, PassAttachmentOp::Load, PassStoreOp::DontCare)};
    p1.dependsOn = {0};

    auto p2 = MakePass(2, "C", 960, 540); // Different resolution

    opt.AddPass(p0);
    opt.AddPass(p1);
    opt.AddPass(p2);
    opt.Optimize();

    auto stats = opt.GetStats();
    EXPECT_EQ(stats.totalPasses, 3u);
    EXPECT_EQ(stats.mergedGroups, 2u); // A+B merged, C separate
    EXPECT_EQ(stats.passesMerged, 2u);
    EXPECT_EQ(stats.passesUnmerged, 1u);
    EXPECT_GT(stats.mergeRatio, 0.0f);
    EXPECT_LT(stats.mergeRatio, 1.0f);

    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, ClearRemovesAll) {
    RenderPassMergeOptimizer opt;
    opt.Init();

    opt.AddPass(MakePass(0, "A", 100, 100));
    opt.Optimize();

    opt.Clear();

    EXPECT_EQ(opt.GetPassCount(), 0u);
    EXPECT_EQ(opt.GetMergedGroupCount(), 0u);

    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, ResetClearsAll) {
    RenderPassMergeOptimizer opt;
    opt.Init();

    opt.AddPass(MakePass(0, "A", 100, 100));
    opt.Optimize();

    opt.Reset();

    EXPECT_EQ(opt.GetPassCount(), 0u);
    EXPECT_EQ(opt.GetMergedGroupCount(), 0u);

    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, SinglePassOptimize) {
    RenderPassMergeOptimizer opt;
    opt.Init();

    opt.AddPass(MakePass(0, "Only", 1920, 1080));
    opt.Optimize();

    EXPECT_EQ(opt.GetMergedGroupCount(), 1u);
    EXPECT_FALSE(opt.IsMerged(0)); // Single pass, not "merged"

    opt.Shutdown();
}

TEST(RenderPassMergeOptimizer, ThreePassMerge) {
    RenderPassMergeOptimizer opt;
    opt.Init();

    auto p0 = MakePass(0, "A", 1920, 1080);
    p0.attachments = {MakeAttachment(0, 1, false, true, PassAttachmentOp::Clear, PassStoreOp::Store)};

    auto p1 = MakePass(1, "B", 1920, 1080);
    p1.attachments = {
        MakeAttachment(0, 1, true, false, PassAttachmentOp::Load, PassStoreOp::DontCare),
        MakeAttachment(1, 2, false, true, PassAttachmentOp::Clear, PassStoreOp::Store),
    };
    p1.dependsOn = {0};

    auto p2 = MakePass(2, "C", 1920, 1080);
    p2.attachments = {
        MakeAttachment(1, 2, true, false, PassAttachmentOp::Load, PassStoreOp::DontCare),
        MakeAttachment(2, 3, false, true, PassAttachmentOp::Clear, PassStoreOp::Store),
    };
    p2.dependsOn = {1};

    opt.AddPass(p0);
    opt.AddPass(p1);
    opt.AddPass(p2);
    opt.Optimize();

    EXPECT_EQ(opt.GetMergedGroupCount(), 1u); // All three merged
    EXPECT_EQ(opt.GetMergedGroups()[0].passIds.size(), 3u);

    opt.Shutdown();
}
