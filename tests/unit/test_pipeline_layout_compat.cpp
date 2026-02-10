#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_pipeline_layout_compat.h"

using namespace nge;
using namespace nge::rhi;

TEST(PipelineLayoutCompat, InitAndShutdown) {
    PipelineLayoutCompatChecker checker;
    EXPECT_TRUE(checker.Init());
    EXPECT_EQ(checker.GetLayoutCount(), 0u);
    checker.Shutdown();
}

TEST(PipelineLayoutCompat, RegisterLayout) {
    PipelineLayoutCompatChecker checker;
    checker.Init();

    std::vector<u64> sets = {0xABCD, 0x1234};
    std::vector<PushConstantRange> pcs = {{0x1, 0, 64, "MVP"}};

    u32 id = checker.RegisterLayout(sets, pcs, "ForwardPass");
    EXPECT_NE(id, UINT32_MAX);
    EXPECT_EQ(checker.GetLayoutCount(), 1u);

    const auto* layout = checker.GetLayout(id);
    EXPECT_NE(layout, nullptr);
    EXPECT_EQ(layout->setLayoutHashes.size(), 2u);
    EXPECT_EQ(layout->pushConstantRanges.size(), 1u);
    EXPECT_EQ(layout->debugName, "ForwardPass");

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, CompatibleLayouts) {
    PipelineLayoutCompatChecker checker;
    checker.Init();

    std::vector<u64> setsA = {0xABCD, 0x1234};
    std::vector<u64> setsB = {0xABCD, 0x1234}; // Same

    u32 a = checker.RegisterLayout(setsA, {}, "LayoutA");
    u32 b = checker.RegisterLayout(setsB, {}, "LayoutB");

    EXPECT_TRUE(checker.AreCompatible(a, b));

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, IncompatibleLayouts) {
    PipelineLayoutCompatChecker checker;
    checker.Init();

    std::vector<u64> setsA = {0xABCD, 0x1234};
    std::vector<u64> setsB = {0xABCD, 0x5678}; // Set 1 differs

    u32 a = checker.RegisterLayout(setsA, {}, "LayoutA");
    u32 b = checker.RegisterLayout(setsB, {}, "LayoutB");

    EXPECT_FALSE(checker.AreCompatible(a, b));

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, CompatibleUpToSet) {
    PipelineLayoutCompatChecker checker;
    checker.Init();

    std::vector<u64> setsA = {0xABCD, 0x1234, 0x9999};
    std::vector<u64> setsB = {0xABCD, 0x1234, 0x7777}; // Set 2 differs

    u32 a = checker.RegisterLayout(setsA, {}, "LayoutA");
    u32 b = checker.RegisterLayout(setsB, {}, "LayoutB");

    // Compatible up to set 1 (sets 0 and 1 match)
    EXPECT_TRUE(checker.AreCompatibleUpToSet(a, b, 1));
    // Not compatible up to set 2
    EXPECT_FALSE(checker.AreCompatibleUpToSet(a, b, 2));

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, DifferentSetCounts) {
    PipelineLayoutCompatChecker checker;
    checker.Init();

    std::vector<u64> setsA = {0xABCD, 0x1234};
    std::vector<u64> setsB = {0xABCD};          // Fewer sets

    u32 a = checker.RegisterLayout(setsA, {}, "LayoutA");
    u32 b = checker.RegisterLayout(setsB, {}, "LayoutB");

    // Compatible on shared sets (set 0 matches)
    EXPECT_TRUE(checker.AreCompatible(a, b));

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, ValidateTooManySets) {
    PipelineLayoutCompatChecker checker;
    PipelineLayoutCompatConfig config;
    config.maxDescriptorSets = 4;
    checker.Init(config);

    std::vector<u64> sets = {1, 2, 3, 4, 5}; // 5 > max of 4
    u32 id = checker.RegisterLayout(sets, {}, "TooManySets");

    auto issues = checker.ValidateLayout(id);
    bool found = false;
    for (const auto& issue : issues) {
        if (issue.type == CompatIssueType::TooManyDescriptorSets) found = true;
    }
    EXPECT_TRUE(found);

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, ValidatePushConstantExceedsLimit) {
    PipelineLayoutCompatChecker checker;
    PipelineLayoutCompatConfig config;
    config.maxPushConstantSize = 128;
    checker.Init(config);

    std::vector<PushConstantRange> pcs = {{0x1, 0, 256, "BigPush"}}; // 256 > 128
    u32 id = checker.RegisterLayout({0x1}, pcs, "BigPushLayout");

    auto issues = checker.ValidateLayout(id);
    bool found = false;
    for (const auto& issue : issues) {
        if (issue.type == CompatIssueType::PushConstantExceedsLimit) found = true;
    }
    EXPECT_TRUE(found);

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, ValidatePushConstantStageConflict) {
    PipelineLayoutCompatChecker checker;
    checker.Init();

    // Two ranges overlapping in bytes AND sharing stage flags
    std::vector<PushConstantRange> pcs = {
        {0x1, 0, 64, "RangeA"},   // Offset 0, size 64, vertex stage
        {0x1, 32, 64, "RangeB"},  // Offset 32, size 64, vertex stage (overlap at 32-64)
    };
    u32 id = checker.RegisterLayout({0x1}, pcs, "ConflictLayout");

    auto issues = checker.ValidateLayout(id);
    bool found = false;
    for (const auto& issue : issues) {
        if (issue.type == CompatIssueType::PushConstantStageConflict) found = true;
    }
    EXPECT_TRUE(found);

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, ValidateNoOverlapDifferentStages) {
    PipelineLayoutCompatChecker checker;
    checker.Init();

    // Overlapping ranges but different stage flags -> no conflict
    std::vector<PushConstantRange> pcs = {
        {0x1, 0, 64, "Vertex"},    // Vertex stage
        {0x10, 0, 64, "Fragment"}, // Fragment stage (different flag)
    };
    u32 id = checker.RegisterLayout({0x1}, pcs, "NoConflict");

    auto issues = checker.ValidateLayout(id);
    bool hasStageConflict = false;
    for (const auto& issue : issues) {
        if (issue.type == CompatIssueType::PushConstantStageConflict) hasStageConflict = true;
    }
    EXPECT_FALSE(hasStageConflict);

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, ValidateSparseSet) {
    PipelineLayoutCompatChecker checker;
    PipelineLayoutCompatConfig config;
    config.warnOnSparseSet = true;
    checker.Init(config);

    std::vector<u64> sets = {0xABCD, 0, 0x1234}; // Gap at set 1
    u32 id = checker.RegisterLayout(sets, {}, "SparseLayout");

    auto issues = checker.ValidateLayout(id);
    bool found = false;
    for (const auto& issue : issues) {
        if (issue.type == CompatIssueType::MissingSetLayout) found = true;
    }
    EXPECT_TRUE(found);

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, FindIssues) {
    PipelineLayoutCompatChecker checker;
    checker.Init();

    std::vector<u64> setsA = {0xABCD, 0x1111, 0x2222};
    std::vector<u64> setsB = {0xABCD, 0x3333, 0x2222}; // Set 1 differs

    u32 a = checker.RegisterLayout(setsA, {}, "A");
    u32 b = checker.RegisterLayout(setsB, {}, "B");

    auto issues = checker.FindIssues(a, b);
    EXPECT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].type, CompatIssueType::SetLayoutMismatch);
    EXPECT_EQ(issues[0].setIndex, 1u);

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, GetPushConstantSize) {
    PipelineLayoutCompatChecker checker;
    checker.Init();

    std::vector<PushConstantRange> pcs = {
        {0x1, 0, 64, "MVP"},
        {0x10, 64, 32, "Material"},
    };
    u32 id = checker.RegisterLayout({0x1}, pcs, "Layout");

    EXPECT_EQ(checker.GetPushConstantSize(id), 96u); // 64 + 32

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, GetPushConstantSizeNone) {
    PipelineLayoutCompatChecker checker;
    checker.Init();

    u32 id = checker.RegisterLayout({0x1}, {}, "NoPush");
    EXPECT_EQ(checker.GetPushConstantSize(id), 0u);

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, Unregister) {
    PipelineLayoutCompatChecker checker;
    checker.Init();

    u32 id = checker.RegisterLayout({0x1}, {}, "Temp");
    EXPECT_EQ(checker.GetLayoutCount(), 1u);

    checker.Unregister(id);
    EXPECT_EQ(checker.GetLayoutCount(), 0u);
    EXPECT_EQ(checker.GetLayout(id), nullptr);

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, MaxLayoutsLimit) {
    PipelineLayoutCompatChecker checker;
    PipelineLayoutCompatConfig config;
    config.maxLayouts = 3;
    checker.Init(config);

    for (u32 i = 0; i < 3; ++i) {
        EXPECT_NE(checker.RegisterLayout({static_cast<u64>(i)}, {}), UINT32_MAX);
    }
    EXPECT_EQ(checker.RegisterLayout({99}, {}), UINT32_MAX);

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, StatsTracking) {
    PipelineLayoutCompatChecker checker;
    checker.Init();

    u32 a = checker.RegisterLayout({0xABCD}, {}, "A");
    u32 b = checker.RegisterLayout({0xABCD}, {}, "B");
    u32 c = checker.RegisterLayout({0x1234}, {}, "C");

    checker.AreCompatible(a, b); // Compatible
    checker.AreCompatible(a, c); // Incompatible

    auto stats = checker.GetStats();
    EXPECT_EQ(stats.totalLayouts, 3u);
    EXPECT_EQ(stats.totalValidations, 2u);
    EXPECT_EQ(stats.compatiblePairs, 1u);
    EXPECT_EQ(stats.incompatiblePairs, 1u);

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, ResetClearsAll) {
    PipelineLayoutCompatChecker checker;
    checker.Init();

    checker.RegisterLayout({0x1}, {}, "A");
    checker.RegisterLayout({0x2}, {}, "B");

    checker.Reset();

    EXPECT_EQ(checker.GetLayoutCount(), 0u);
    auto stats = checker.GetStats();
    EXPECT_EQ(stats.totalValidations, 0u);
    EXPECT_EQ(stats.compatiblePairs, 0u);

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, GetLayoutInvalid) {
    PipelineLayoutCompatChecker checker;
    checker.Init();

    EXPECT_EQ(checker.GetLayout(999), nullptr);
    EXPECT_EQ(checker.GetPushConstantSize(999), 0u);

    checker.Shutdown();
}

TEST(PipelineLayoutCompat, ValidateCleanLayout) {
    PipelineLayoutCompatChecker checker;
    checker.Init();

    std::vector<PushConstantRange> pcs = {{0x1, 0, 64, "MVP"}};
    u32 id = checker.RegisterLayout({0xABCD, 0x1234}, pcs, "CleanLayout");

    auto issues = checker.ValidateLayout(id);
    EXPECT_TRUE(issues.empty());

    checker.Shutdown();
}
