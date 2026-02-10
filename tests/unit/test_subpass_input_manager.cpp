#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_subpass_input_manager.h"

using namespace nge::rhi;

TEST(SubpassInputManager, InitAndShutdown) {
    SubpassInputManager mgr;
    EXPECT_TRUE(mgr.Init());
    EXPECT_EQ(mgr.GetSubpassCount(), 0u);
    EXPECT_EQ(mgr.GetAttachmentCount(), 0u);
    mgr.Shutdown();
}

TEST(SubpassInputManager, DeclareSubpass) {
    SubpassInputManager mgr;
    mgr.Init();

    EXPECT_TRUE(mgr.DeclareSubpass(0, "GBuffer"));
    EXPECT_TRUE(mgr.DeclareSubpass(1, "Lighting"));
    EXPECT_EQ(mgr.GetSubpassCount(), 2u);

    mgr.Shutdown();
}

TEST(SubpassInputManager, DeclareOutput) {
    SubpassInputManager mgr;
    mgr.Init();

    mgr.DeclareSubpass(0, "GBuffer");
    EXPECT_TRUE(mgr.DeclareOutput(0, 0, InputAttachmentType::Color, "Albedo"));
    EXPECT_TRUE(mgr.DeclareOutput(0, 1, InputAttachmentType::Color, "Normal"));
    EXPECT_TRUE(mgr.DeclareOutput(0, 2, InputAttachmentType::DepthStencil, "Depth"));

    EXPECT_EQ(mgr.GetAttachmentCount(), 3u);

    mgr.Shutdown();
}

TEST(SubpassInputManager, DeclareOutputUndeclaredSubpass) {
    SubpassInputManager mgr;
    mgr.Init();

    EXPECT_FALSE(mgr.DeclareOutput(99, 0, InputAttachmentType::Color));

    mgr.Shutdown();
}

TEST(SubpassInputManager, DeclareInput) {
    SubpassInputManager mgr;
    mgr.Init();

    mgr.DeclareSubpass(0, "GBuffer");
    mgr.DeclareSubpass(1, "Lighting");
    mgr.DeclareOutput(0, 0, InputAttachmentType::Color, "Albedo");

    EXPECT_TRUE(mgr.DeclareInput(1, 0)); // Lighting reads Albedo

    mgr.Shutdown();
}

TEST(SubpassInputManager, DeclareInputUndeclaredSubpass) {
    SubpassInputManager mgr;
    mgr.Init();

    EXPECT_FALSE(mgr.DeclareInput(99, 0));

    mgr.Shutdown();
}

TEST(SubpassInputManager, ValidateSuccess) {
    SubpassInputManager mgr;
    mgr.Init();

    mgr.DeclareSubpass(0, "GBuffer");
    mgr.DeclareSubpass(1, "Lighting");

    mgr.DeclareOutput(0, 0, InputAttachmentType::Color, "Albedo");
    mgr.DeclareOutput(0, 1, InputAttachmentType::Color, "Normal");
    mgr.DeclareInput(1, 0);
    mgr.DeclareInput(1, 1);

    EXPECT_TRUE(mgr.Validate());

    mgr.Shutdown();
}

TEST(SubpassInputManager, ValidateFailNoProducer) {
    SubpassInputManager mgr;
    mgr.Init();

    mgr.DeclareSubpass(0, "GBuffer");
    mgr.DeclareSubpass(1, "Lighting");

    // Subpass 1 reads attachment 5 which has no producer
    mgr.DeclareInput(1, 5);

    EXPECT_FALSE(mgr.Validate());

    auto errors = mgr.GetValidationErrors();
    EXPECT_GE(errors.size(), 1u);

    mgr.Shutdown();
}

TEST(SubpassInputManager, ValidateFailLaterProducer) {
    SubpassInputManager mgr;
    mgr.Init();

    mgr.DeclareSubpass(0, "First");
    mgr.DeclareSubpass(1, "Second");

    // Subpass 1 produces attachment 0, but subpass 0 tries to read it
    mgr.DeclareOutput(1, 0, InputAttachmentType::Color, "Late");
    mgr.DeclareInput(0, 0); // Reading from later subpass -> invalid

    EXPECT_FALSE(mgr.Validate());

    mgr.Shutdown();
}

TEST(SubpassInputManager, GenerateDependencies) {
    SubpassInputManager mgr;
    mgr.Init();

    mgr.DeclareSubpass(0, "GBuffer");
    mgr.DeclareSubpass(1, "Lighting");

    mgr.DeclareOutput(0, 0, InputAttachmentType::Color, "Albedo");
    mgr.DeclareOutput(0, 1, InputAttachmentType::DepthStencil, "Depth");
    mgr.DeclareInput(1, 0);
    mgr.DeclareInput(1, 1);

    auto deps = mgr.GenerateDependencies();
    EXPECT_EQ(deps.size(), 2u);

    // All should be by-region
    for (const auto& dep : deps) {
        EXPECT_TRUE(dep.byRegion);
        EXPECT_EQ(dep.srcSubpass, 0u);
        EXPECT_EQ(dep.dstSubpass, 1u);
    }

    mgr.Shutdown();
}

TEST(SubpassInputManager, GetInputsForSubpass) {
    SubpassInputManager mgr;
    mgr.Init();

    mgr.DeclareSubpass(0, "GBuffer");
    mgr.DeclareSubpass(1, "Lighting");

    mgr.DeclareOutput(0, 0, InputAttachmentType::Color, "Albedo");
    mgr.DeclareOutput(0, 1, InputAttachmentType::Color, "Normal");
    mgr.DeclareInput(1, 0);
    mgr.DeclareInput(1, 1);

    auto inputs = mgr.GetInputsForSubpass(1);
    EXPECT_EQ(inputs.size(), 2u);

    auto inputs0 = mgr.GetInputsForSubpass(0);
    EXPECT_EQ(inputs0.size(), 0u); // GBuffer has no inputs

    mgr.Shutdown();
}

TEST(SubpassInputManager, GetOutputsForSubpass) {
    SubpassInputManager mgr;
    mgr.Init();

    mgr.DeclareSubpass(0, "GBuffer");
    mgr.DeclareOutput(0, 0, InputAttachmentType::Color, "Albedo");
    mgr.DeclareOutput(0, 1, InputAttachmentType::Color, "Normal");

    auto outputs = mgr.GetOutputsForSubpass(0);
    EXPECT_EQ(outputs.size(), 2u);

    mgr.Shutdown();
}

TEST(SubpassInputManager, GetProducer) {
    SubpassInputManager mgr;
    mgr.Init();

    mgr.DeclareSubpass(0, "GBuffer");
    mgr.DeclareOutput(0, 5, InputAttachmentType::Color, "Emissive");

    EXPECT_EQ(mgr.GetProducer(5), 0u);
    EXPECT_EQ(mgr.GetProducer(999), UINT32_MAX); // Unknown

    mgr.Shutdown();
}

TEST(SubpassInputManager, GetConsumers) {
    SubpassInputManager mgr;
    mgr.Init();

    mgr.DeclareSubpass(0, "GBuffer");
    mgr.DeclareSubpass(1, "Lighting");
    mgr.DeclareSubpass(2, "Decals");

    mgr.DeclareOutput(0, 0, InputAttachmentType::Color, "Albedo");
    mgr.DeclareInput(1, 0);
    mgr.DeclareInput(2, 0);

    auto consumers = mgr.GetConsumers(0);
    EXPECT_EQ(consumers.size(), 2u);

    auto noConsumers = mgr.GetConsumers(999);
    EXPECT_EQ(noConsumers.size(), 0u);

    mgr.Shutdown();
}

TEST(SubpassInputManager, IsInputAttachment) {
    SubpassInputManager mgr;
    mgr.Init();

    mgr.DeclareSubpass(0, "GBuffer");
    mgr.DeclareSubpass(1, "Lighting");

    mgr.DeclareOutput(0, 0, InputAttachmentType::Color, "Albedo");
    mgr.DeclareOutput(0, 1, InputAttachmentType::Color, "Normal");
    mgr.DeclareInput(1, 0); // Only albedo is consumed

    EXPECT_TRUE(mgr.IsInputAttachment(0));
    EXPECT_FALSE(mgr.IsInputAttachment(1)); // Normal not consumed as input
    EXPECT_FALSE(mgr.IsInputAttachment(999));

    mgr.Shutdown();
}

TEST(SubpassInputManager, MaxSubpassesLimit) {
    SubpassInputManager mgr;
    SubpassInputConfig config;
    config.maxSubpasses = 2;
    mgr.Init(config);

    EXPECT_TRUE(mgr.DeclareSubpass(0));
    EXPECT_TRUE(mgr.DeclareSubpass(1));
    EXPECT_FALSE(mgr.DeclareSubpass(2)); // Exceeds

    mgr.Shutdown();
}

TEST(SubpassInputManager, MaxInputsPerSubpassLimit) {
    SubpassInputManager mgr;
    SubpassInputConfig config;
    config.maxInputsPerSubpass = 2;
    mgr.Init(config);

    mgr.DeclareSubpass(0, "Producer");
    mgr.DeclareSubpass(1, "Consumer");

    mgr.DeclareOutput(0, 0, InputAttachmentType::Color);
    mgr.DeclareOutput(0, 1, InputAttachmentType::Color);
    mgr.DeclareOutput(0, 2, InputAttachmentType::Color);

    EXPECT_TRUE(mgr.DeclareInput(1, 0));
    EXPECT_TRUE(mgr.DeclareInput(1, 1));
    EXPECT_FALSE(mgr.DeclareInput(1, 2)); // Exceeds max inputs

    mgr.Shutdown();
}

TEST(SubpassInputManager, DepthStencilDependency) {
    SubpassInputManager mgr;
    mgr.Init();

    mgr.DeclareSubpass(0, "DepthPrepass");
    mgr.DeclareSubpass(1, "Lighting");

    mgr.DeclareOutput(0, 0, InputAttachmentType::DepthStencil, "Depth");
    mgr.DeclareInput(1, 0);

    auto deps = mgr.GenerateDependencies();
    EXPECT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0].srcStageMask, 0x00000200u); // LATE_FRAGMENT_TESTS
    EXPECT_EQ(deps[0].srcAccessMask, 0x00000400u); // DEPTH_STENCIL_WRITE

    mgr.Shutdown();
}

TEST(SubpassInputManager, StatsTracking) {
    SubpassInputManager mgr;
    mgr.Init();

    mgr.DeclareSubpass(0, "GBuffer");
    mgr.DeclareSubpass(1, "Lighting");

    mgr.DeclareOutput(0, 0, InputAttachmentType::Color, "Albedo");
    mgr.DeclareOutput(0, 1, InputAttachmentType::Color, "Normal");
    mgr.DeclareInput(1, 0);
    mgr.DeclareInput(1, 1);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalSubpasses, 2u);
    EXPECT_EQ(stats.totalAttachments, 2u);
    EXPECT_EQ(stats.totalInputRefs, 2u);
    EXPECT_EQ(stats.totalDependencies, 2u);
    EXPECT_EQ(stats.byRegionDependencies, 2u);

    mgr.Shutdown();
}

TEST(SubpassInputManager, ResetClearsAll) {
    SubpassInputManager mgr;
    mgr.Init();

    mgr.DeclareSubpass(0);
    mgr.DeclareOutput(0, 0, InputAttachmentType::Color);

    mgr.Reset();

    EXPECT_EQ(mgr.GetSubpassCount(), 0u);
    EXPECT_EQ(mgr.GetAttachmentCount(), 0u);

    mgr.Shutdown();
}

TEST(SubpassInputManager, ThreeSubpassChain) {
    SubpassInputManager mgr;
    mgr.Init();

    mgr.DeclareSubpass(0, "GBuffer");
    mgr.DeclareSubpass(1, "Lighting");
    mgr.DeclareSubpass(2, "Composite");

    mgr.DeclareOutput(0, 0, InputAttachmentType::Color, "Albedo");
    mgr.DeclareOutput(0, 1, InputAttachmentType::DepthStencil, "Depth");
    mgr.DeclareOutput(1, 2, InputAttachmentType::Color, "LitColor");

    mgr.DeclareInput(1, 0); // Lighting reads Albedo
    mgr.DeclareInput(1, 1); // Lighting reads Depth
    mgr.DeclareInput(2, 2); // Composite reads LitColor

    EXPECT_TRUE(mgr.Validate());

    auto deps = mgr.GenerateDependencies();
    EXPECT_EQ(deps.size(), 3u); // 2 for subpass 0->1, 1 for subpass 1->2

    mgr.Shutdown();
}
