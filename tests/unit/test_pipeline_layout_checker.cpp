#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_pipeline_layout_checker.h"

using namespace nge;
using namespace nge::rhi;

static PipelineLayoutDesc MakeLayout(u64 id, const std::string& name,
                                      std::vector<DescriptorSetLayout> sets = {},
                                      std::vector<PushConstantRange> pc = {}) {
    PipelineLayoutDesc desc;
    desc.layoutId = id;
    desc.debugName = name;
    desc.setLayouts = std::move(sets);
    desc.pushConstants = std::move(pc);
    return desc;
}

static DescriptorSetLayout MakeSet(u32 setIndex, std::vector<LayoutBinding> bindings) {
    DescriptorSetLayout sl;
    sl.setIndex = setIndex;
    sl.bindings = std::move(bindings);
    return sl;
}

TEST(PipelineLayoutChecker, InitAndShutdown) {
    PipelineLayoutChecker checker;
    EXPECT_TRUE(checker.Init());

    auto stats = checker.GetStats();
    EXPECT_EQ(stats.totalLayouts, 0u);
    EXPECT_EQ(stats.totalChecks, 0u);

    checker.Shutdown();
}

TEST(PipelineLayoutChecker, IdenticalLayoutsCompatible) {
    PipelineLayoutChecker checker;
    checker.Init();

    LayoutBinding b0{0, DescBindingType::UniformBuffer, 1, ShaderStage::Vertex};
    LayoutBinding b1{1, DescBindingType::CombinedImageSampler, 1, ShaderStage::Fragment};

    auto layout = MakeLayout(1, "PBR", {MakeSet(0, {b0, b1})});
    checker.RegisterLayout(layout);

    auto layout2 = layout;
    layout2.layoutId = 2;
    layout2.debugName = "PBR_Clone";
    checker.RegisterLayout(layout2);

    auto issues = checker.CheckCompatibility(1, 2);
    EXPECT_TRUE(issues.empty());

    auto stats = checker.GetStats();
    EXPECT_EQ(stats.compatiblePairs, 1u);

    checker.Shutdown();
}

TEST(PipelineLayoutChecker, BindingTypeMismatch) {
    PipelineLayoutChecker checker;
    checker.Init();

    LayoutBinding bA{0, DescBindingType::UniformBuffer, 1, ShaderStage::Vertex};
    LayoutBinding bB{0, DescBindingType::StorageBuffer, 1, ShaderStage::Vertex};

    checker.RegisterLayout(MakeLayout(1, "A", {MakeSet(0, {bA})}));
    checker.RegisterLayout(MakeLayout(2, "B", {MakeSet(0, {bB})}));

    auto issues = checker.CheckCompatibility(1, 2);
    EXPECT_FALSE(issues.empty());

    bool foundTypeMismatch = false;
    for (const auto& i : issues) {
        if (i.type == IncompatibilityType::BindingTypeMismatch) foundTypeMismatch = true;
    }
    EXPECT_TRUE(foundTypeMismatch);

    checker.Shutdown();
}

TEST(PipelineLayoutChecker, BindingStageMismatch) {
    PipelineLayoutChecker checker;
    checker.Init();

    LayoutBinding bA{0, DescBindingType::UniformBuffer, 1, ShaderStage::Vertex};
    LayoutBinding bB{0, DescBindingType::UniformBuffer, 1, ShaderStage::Fragment};

    checker.RegisterLayout(MakeLayout(1, "A", {MakeSet(0, {bA})}));
    checker.RegisterLayout(MakeLayout(2, "B", {MakeSet(0, {bB})}));

    auto issues = checker.CheckCompatibility(1, 2);
    bool foundStageMismatch = false;
    for (const auto& i : issues) {
        if (i.type == IncompatibilityType::BindingStageMismatch) foundStageMismatch = true;
    }
    EXPECT_TRUE(foundStageMismatch);

    checker.Shutdown();
}

TEST(PipelineLayoutChecker, BindingArraySizeMismatch) {
    PipelineLayoutChecker checker;
    checker.Init();

    LayoutBinding bA{0, DescBindingType::SampledImage, 4, ShaderStage::Fragment};
    LayoutBinding bB{0, DescBindingType::SampledImage, 8, ShaderStage::Fragment};

    checker.RegisterLayout(MakeLayout(1, "A", {MakeSet(0, {bA})}));
    checker.RegisterLayout(MakeLayout(2, "B", {MakeSet(0, {bB})}));

    auto issues = checker.CheckCompatibility(1, 2);
    bool foundArrayMismatch = false;
    for (const auto& i : issues) {
        if (i.type == IncompatibilityType::BindingArraySizeMismatch) foundArrayMismatch = true;
    }
    EXPECT_TRUE(foundArrayMismatch);

    checker.Shutdown();
}

TEST(PipelineLayoutChecker, MissingSet) {
    PipelineLayoutChecker checker;
    checker.Init();

    LayoutBinding b0{0, DescBindingType::UniformBuffer, 1, ShaderStage::Vertex};

    checker.RegisterLayout(MakeLayout(1, "A", {MakeSet(0, {b0}), MakeSet(1, {b0})}));
    checker.RegisterLayout(MakeLayout(2, "B", {MakeSet(0, {b0})}));

    auto issues = checker.CheckCompatibility(1, 2);
    bool foundMissing = false;
    for (const auto& i : issues) {
        if (i.type == IncompatibilityType::MissingSet) foundMissing = true;
    }
    EXPECT_TRUE(foundMissing);

    checker.Shutdown();
}

TEST(PipelineLayoutChecker, BindingCountMismatch) {
    PipelineLayoutChecker checker;
    checker.Init();

    LayoutBinding b0{0, DescBindingType::UniformBuffer, 1, ShaderStage::Vertex};
    LayoutBinding b1{1, DescBindingType::SampledImage, 1, ShaderStage::Fragment};

    checker.RegisterLayout(MakeLayout(1, "A", {MakeSet(0, {b0, b1})}));
    checker.RegisterLayout(MakeLayout(2, "B", {MakeSet(0, {b0})}));

    auto issues = checker.CheckCompatibility(1, 2);
    bool foundCountMismatch = false;
    for (const auto& i : issues) {
        if (i.type == IncompatibilityType::BindingCountMismatch) foundCountMismatch = true;
    }
    EXPECT_TRUE(foundCountMismatch);

    checker.Shutdown();
}

TEST(PipelineLayoutChecker, PushConstantOverlap) {
    PipelineLayoutChecker checker;
    checker.Init();

    PushConstantRange pcA{ShaderStage::Vertex, 0, 64};
    PushConstantRange pcB{ShaderStage::Fragment, 32, 64}; // Overlaps [32, 64)

    checker.RegisterLayout(MakeLayout(1, "A", {}, {pcA}));
    checker.RegisterLayout(MakeLayout(2, "B", {}, {pcB}));

    auto issues = checker.CheckCompatibility(1, 2);
    bool foundOverlap = false;
    for (const auto& i : issues) {
        if (i.type == IncompatibilityType::PushConstantOverlap) foundOverlap = true;
    }
    EXPECT_TRUE(foundOverlap);

    checker.Shutdown();
}

TEST(PipelineLayoutChecker, PushConstantGap) {
    PipelineLayoutChecker checker;
    PipelineLayoutCheckerConfig config;
    config.checkPushConstantGaps = true;
    checker.Init(config);

    PushConstantRange pc0{ShaderStage::Vertex, 0, 32};
    PushConstantRange pc1{ShaderStage::Fragment, 64, 32}; // Gap at [32, 64)

    checker.RegisterLayout(MakeLayout(1, "A", {}, {pc0, pc1}));
    checker.RegisterLayout(MakeLayout(2, "B", {}, {}));

    auto issues = checker.CheckCompatibility(1, 2);
    bool foundGap = false;
    for (const auto& i : issues) {
        if (i.type == IncompatibilityType::PushConstantGap) foundGap = true;
    }
    EXPECT_TRUE(foundGap);

    checker.Shutdown();
}

TEST(PipelineLayoutChecker, IdenticalPushConstantsCompatible) {
    PipelineLayoutChecker checker;
    checker.Init();

    PushConstantRange pc{ShaderStage::Vertex, 0, 64};

    checker.RegisterLayout(MakeLayout(1, "A", {}, {pc}));
    checker.RegisterLayout(MakeLayout(2, "B", {}, {pc}));

    auto issues = checker.CheckCompatibility(1, 2);
    // No push constant issues (identical ranges)
    bool hasPCIssue = false;
    for (const auto& i : issues) {
        if (i.type == IncompatibilityType::PushConstantOverlap ||
            i.type == IncompatibilityType::PushConstantGap) {
            hasPCIssue = true;
        }
    }
    EXPECT_FALSE(hasPCIssue);

    checker.Shutdown();
}

TEST(PipelineLayoutChecker, PrefixSetCompatibility) {
    PipelineLayoutChecker checker;
    checker.Init();

    LayoutBinding b0{0, DescBindingType::UniformBuffer, 1, ShaderStage::Vertex};
    LayoutBinding b1{0, DescBindingType::SampledImage, 1, ShaderStage::Fragment};
    LayoutBinding b1_diff{0, DescBindingType::StorageImage, 1, ShaderStage::Fragment};

    // Same set 0, different set 1
    checker.RegisterLayout(MakeLayout(1, "A", {MakeSet(0, {b0}), MakeSet(1, {b1})}));
    checker.RegisterLayout(MakeLayout(2, "B", {MakeSet(0, {b0}), MakeSet(1, {b1_diff})}));

    // Sets prefix up to 0 should be compatible
    EXPECT_TRUE(checker.AreSetsPrefixCompatible(1, 2, 0));

    // Sets prefix up to 1 should NOT be compatible
    EXPECT_FALSE(checker.AreSetsPrefixCompatible(1, 2, 1));

    checker.Shutdown();
}

TEST(PipelineLayoutChecker, RemoveLayout) {
    PipelineLayoutChecker checker;
    checker.Init();

    checker.RegisterLayout(MakeLayout(1, "A"));
    EXPECT_NE(checker.GetLayout(1), nullptr);
    EXPECT_EQ(checker.GetStats().totalLayouts, 1u);

    checker.RemoveLayout(1);
    EXPECT_EQ(checker.GetLayout(1), nullptr);
    EXPECT_EQ(checker.GetStats().totalLayouts, 0u);

    checker.Shutdown();
}

TEST(PipelineLayoutChecker, ResetClearsAll) {
    PipelineLayoutChecker checker;
    checker.Init();

    checker.RegisterLayout(MakeLayout(1, "A"));
    checker.RegisterLayout(MakeLayout(2, "B"));
    checker.CheckCompatibility(1, 2);

    checker.Reset();

    auto stats = checker.GetStats();
    EXPECT_EQ(stats.totalLayouts, 0u);
    EXPECT_EQ(stats.totalChecks, 0u);
    EXPECT_EQ(stats.compatiblePairs, 0u);

    checker.Shutdown();
}

TEST(PipelineLayoutChecker, UnknownLayoutReturnsError) {
    PipelineLayoutChecker checker;
    checker.Init();

    auto issues = checker.CheckCompatibility(999, 888);
    EXPECT_FALSE(issues.empty());

    checker.Shutdown();
}

TEST(PipelineLayoutChecker, MaxLayoutsLimit) {
    PipelineLayoutChecker checker;
    PipelineLayoutCheckerConfig config;
    config.maxLayouts = 2;
    checker.Init(config);

    checker.RegisterLayout(MakeLayout(1, "A"));
    checker.RegisterLayout(MakeLayout(2, "B"));
    checker.RegisterLayout(MakeLayout(3, "C")); // Exceeds limit

    EXPECT_EQ(checker.GetStats().totalLayouts, 2u);
    EXPECT_EQ(checker.GetLayout(3), nullptr);

    checker.Shutdown();
}

TEST(PipelineLayoutChecker, GetLayoutInfo) {
    PipelineLayoutChecker checker;
    checker.Init();

    LayoutBinding b0{0, DescBindingType::UniformBuffer, 1, ShaderStage::Vertex};
    checker.RegisterLayout(MakeLayout(42, "TestLayout", {MakeSet(0, {b0})}));

    const auto* layout = checker.GetLayout(42);
    EXPECT_NE(layout, nullptr);
    EXPECT_EQ(layout->layoutId, 42u);
    EXPECT_EQ(layout->debugName, "TestLayout");
    EXPECT_EQ(layout->setLayouts.size(), 1u);
    EXPECT_EQ(layout->setLayouts[0].bindings.size(), 1u);

    checker.Shutdown();
}
