#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_desc_set_layout_optimizer.h"
#include <cstdio>

using namespace nge;
using namespace nge::rhi;

// Verify ABI consistency
static_assert(sizeof(DescriptorBinding) == 64, "DescriptorBinding size mismatch");
static_assert(sizeof(DescriptorSetLayout) == 96, "DescriptorSetLayout size mismatch");
static_assert(sizeof(DescriptorSetLayoutOptimizer) == 224, "DescriptorSetLayoutOptimizer size mismatch");

TEST(DescSetLayoutOptimizer, InitAndShutdown) {
    // Test 1: bare std::string
    {
        std::fprintf(stderr, "[TEST] Creating std::string...\n");
        std::string s = "DirectTest";
        std::fprintf(stderr, "[TEST] string OK: '%s'\n", s.c_str());
    }
    // Test 2: bare std::vector<DescriptorBinding>
    {
        std::fprintf(stderr, "[TEST] Creating std::vector<DescriptorBinding>...\n");
        std::vector<DescriptorBinding> v;
        std::fprintf(stderr, "[TEST] vector OK: size=%zu\n", v.size());
    }
    // Test 3b: Simple struct with vector + string
    {
        struct SimpleStruct {
            u32 a;
            u32 b;
            std::vector<int> v;
            u64 c;
            u32 d;
            std::string s;
        };
        std::fprintf(stderr, "[TEST] sizeof(SimpleStruct)=%zu\n", sizeof(SimpleStruct));
        SimpleStruct ss;
        ss.s = "SimpleTest";
        std::fprintf(stderr, "[TEST] simple struct OK: '%s'\n", ss.s.c_str());
    }
    // Test 3c: Same layout as DescriptorSetLayout but with vector<int>
    {
        struct SimilarStruct {
            u32 layoutId;
            u32 setIndex;
            std::vector<int> bindings;
            u64 layoutHash;
            u32 refCount;
            std::string debugName;
        };
        std::fprintf(stderr, "[TEST] sizeof(SimilarStruct)=%zu\n", sizeof(SimilarStruct));
        SimilarStruct ss;
        ss.debugName = "SimilarTest";
        std::fprintf(stderr, "[TEST] similar struct OK: '%s'\n", ss.debugName.c_str());
    }
    // Test 4: DescriptorSetLayout on heap - check offsets
    {
        std::fprintf(stderr, "[TEST] sizeof(vector<DescriptorBinding>)=%zu, sizeof(string)=%zu\n",
            sizeof(std::vector<DescriptorBinding>), sizeof(std::string));
        auto* layout = new DescriptorSetLayout();
        std::fprintf(stderr, "[TEST] offsets: layoutId=%td setIndex=%td bindings=%td layoutHash=%td refCount=%td debugName=%td\n",
            (char*)&layout->layoutId - (char*)layout,
            (char*)&layout->setIndex - (char*)layout,
            (char*)&layout->bindings - (char*)layout,
            (char*)&layout->layoutHash - (char*)layout,
            (char*)&layout->refCount - (char*)layout,
            (char*)&layout->debugName - (char*)layout);
        layout->debugName = "HeapTest";
        std::fprintf(stderr, "[TEST] heap layout OK: '%s'\n", layout->debugName.c_str());
        delete layout;
    }
    // Test 5: DescriptorSetLayout on stack - construct only
    {
        std::fprintf(stderr, "[TEST] Constructing DescriptorSetLayout on stack...\n");
        DescriptorSetLayout layout;
        std::fprintf(stderr, "[TEST] stack construct OK, sizeof=%zu\n", sizeof(layout));
    }
    // Test 6: DescriptorSetLayout on stack - assign debugName
    {
        std::fprintf(stderr, "[TEST] Assigning debugName...\n");
        DescriptorSetLayout layout;
        std::fprintf(stderr, "[TEST]  constructed\n");
        layout.debugName = "DirectTest";
        std::fprintf(stderr, "[TEST] layout OK: '%s'\n", layout.debugName.c_str());
    }

    DescriptorSetLayoutOptimizer opt;
    EXPECT_TRUE(opt.Init());
    EXPECT_EQ(opt.GetLayoutCount(), 0u);
    opt.Shutdown();
}

TEST(DescSetLayoutOptimizer, DeclareAndBuild) {
    DescriptorSetLayoutOptimizer opt;
    opt.Init();

    opt.DeclareBinding(0, 0, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerFrame, "CameraCB");
    opt.DeclareBinding(0, 1, DescriptorType::SampledImage, 1, 0x10, UpdateFrequency::PerFrame, "ShadowMap");

    auto layouts = opt.BuildOptimizedLayouts();
    EXPECT_GE(layouts.size(), 1u);

    // Should have a layout with 2 bindings
    bool foundPerFrame = false;
    for (const auto& l : layouts) {
        if (l.bindings.size() == 2) foundPerFrame = true;
    }
    EXPECT_TRUE(foundPerFrame);

    opt.Shutdown();
}

TEST(DescSetLayoutOptimizer, FrequencySorting) {
    DescriptorSetLayoutOptimizer opt;
    LayoutOptimizerConfig config;
    config.sortByFrequency = true;
    opt.Init(config);

    // Declare bindings with different frequencies in same set
    opt.DeclareBinding(0, 0, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerFrame, "Camera");
    opt.DeclareBinding(0, 1, DescriptorType::SampledImage, 1, 0x10, UpdateFrequency::PerMaterial, "Albedo");
    opt.DeclareBinding(0, 2, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerDraw, "Transform");

    auto layouts = opt.BuildOptimizedLayouts();

    // Should be split into separate sets by frequency
    EXPECT_GE(layouts.size(), 2u); // At least PerFrame + PerMaterial + PerDraw

    opt.Shutdown();
}

TEST(DescSetLayoutOptimizer, GetLayoutBySetIndex) {
    DescriptorSetLayoutOptimizer opt;
    LayoutOptimizerConfig config;
    config.sortByFrequency = false; // Keep original sets
    opt.Init(config);

    opt.DeclareBinding(0, 0, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerFrame);
    opt.DeclareBinding(1, 0, DescriptorType::SampledImage, 1, 0x10, UpdateFrequency::PerPass);

    opt.BuildOptimizedLayouts();

    const auto* set0 = opt.GetLayout(0);
    const auto* set1 = opt.GetLayout(1);

    EXPECT_NE(set0, nullptr);
    EXPECT_NE(set1, nullptr);
    EXPECT_EQ(set0->setIndex, 0u);
    EXPECT_EQ(set1->setIndex, 1u);

    EXPECT_EQ(opt.GetLayout(99), nullptr);

    opt.Shutdown();
}

TEST(DescSetLayoutOptimizer, Compaction) {
    DescriptorSetLayoutOptimizer opt;
    LayoutOptimizerConfig config;
    config.enableCompaction = true;
    config.sortByFrequency = false;
    opt.Init(config);

    // Duplicate binding indices -> should be compacted
    opt.DeclareBinding(0, 0, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerFrame);
    opt.DeclareBinding(0, 0, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerFrame); // Duplicate
    opt.DeclareBinding(0, 1, DescriptorType::SampledImage, 1, 0x10, UpdateFrequency::PerFrame);

    auto layouts = opt.BuildOptimizedLayouts();

    // Should have 2 bindings after compaction (not 3)
    EXPECT_EQ(layouts.size(), 1u);
    EXPECT_EQ(layouts[0].bindings.size(), 2u);

    auto stats = opt.GetStats();
    EXPECT_EQ(stats.bindingsRemoved, 1u);

    opt.Shutdown();
}

TEST(DescSetLayoutOptimizer, CompactionDisabled) {
    DescriptorSetLayoutOptimizer opt;
    LayoutOptimizerConfig config;
    config.enableCompaction = false;
    config.sortByFrequency = false;
    opt.Init(config);

    opt.DeclareBinding(0, 0, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerFrame);
    opt.DeclareBinding(0, 0, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerFrame);
    opt.DeclareBinding(0, 1, DescriptorType::SampledImage, 1, 0x10, UpdateFrequency::PerFrame);

    auto layouts = opt.BuildOptimizedLayouts();
    EXPECT_EQ(layouts[0].bindings.size(), 3u); // No compaction

    opt.Shutdown();
}

TEST(DescSetLayoutOptimizer, AreCompatible) {
    DescriptorSetLayoutOptimizer opt;
    LayoutOptimizerConfig config;
    config.sortByFrequency = false;
    config.enableMerging = false; // Keep all layouts separate
    opt.Init(config);

    // Two sets with identical bindings
    opt.DeclareBinding(0, 0, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerFrame);
    opt.DeclareBinding(1, 0, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerFrame);

    // Different set
    opt.DeclareBinding(2, 0, DescriptorType::SampledImage, 4, 0x10, UpdateFrequency::PerMaterial);

    auto layouts = opt.BuildOptimizedLayouts();
    EXPECT_EQ(layouts.size(), 3u);

    // Sets 0 and 1 should be compatible (same hash)
    EXPECT_TRUE(opt.AreCompatible(layouts[0].layoutId, layouts[1].layoutId));
    // Set 2 is different
    EXPECT_FALSE(opt.AreCompatible(layouts[0].layoutId, layouts[2].layoutId));

    opt.Shutdown();
}

TEST(DescSetLayoutOptimizer, LayoutHashDeterministic) {
    DescriptorSetLayoutOptimizer opt;
    LayoutOptimizerConfig config;
    config.sortByFrequency = false;
    config.enableMerging = false;
    opt.Init(config);

    opt.DeclareBinding(0, 0, DescriptorType::UniformBuffer, 1, 0xFF, UpdateFrequency::PerFrame);
    opt.DeclareBinding(0, 1, DescriptorType::SampledImage, 4, 0xFF, UpdateFrequency::PerFrame);

    auto layouts1 = opt.BuildOptimizedLayouts();
    u64 hash1 = layouts1[0].layoutHash;

    opt.Clear();

    opt.DeclareBinding(0, 0, DescriptorType::UniformBuffer, 1, 0xFF, UpdateFrequency::PerFrame);
    opt.DeclareBinding(0, 1, DescriptorType::SampledImage, 4, 0xFF, UpdateFrequency::PerFrame);

    auto layouts2 = opt.BuildOptimizedLayouts();
    u64 hash2 = layouts2[0].layoutHash;

    EXPECT_EQ(hash1, hash2); // Same inputs -> same hash

    opt.Shutdown();
}

TEST(DescSetLayoutOptimizer, GetBindingCount) {
    DescriptorSetLayoutOptimizer opt;
    LayoutOptimizerConfig config;
    config.sortByFrequency = false;
    opt.Init(config);

    opt.DeclareBinding(0, 0, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerFrame);
    opt.DeclareBinding(0, 1, DescriptorType::SampledImage, 1, 0x10, UpdateFrequency::PerFrame);
    opt.DeclareBinding(0, 2, DescriptorType::StorageBuffer, 1, 0x1, UpdateFrequency::PerFrame);

    // Before build: check declared
    EXPECT_EQ(opt.GetBindingCount(0), 3u);

    opt.BuildOptimizedLayouts();

    // After build: check optimized
    EXPECT_EQ(opt.GetBindingCount(0), 3u);
    EXPECT_EQ(opt.GetBindingCount(99), 0u);

    opt.Shutdown();
}

TEST(DescSetLayoutOptimizer, RecordBind) {
    DescriptorSetLayoutOptimizer opt;
    opt.Init();

    opt.RecordBind(0);
    opt.RecordBind(0);
    opt.RecordBind(1);

    // Stats should show total binds (no direct getter but stats has it implicitly)
    // Just ensure it doesn't crash
    opt.Shutdown();
}

TEST(DescSetLayoutOptimizer, MultipleSets) {
    DescriptorSetLayoutOptimizer opt;
    LayoutOptimizerConfig config;
    config.sortByFrequency = false;
    opt.Init(config);

    opt.DeclareBinding(0, 0, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerFrame);
    opt.DeclareBinding(1, 0, DescriptorType::SampledImage, 1, 0x10, UpdateFrequency::PerPass);
    opt.DeclareBinding(2, 0, DescriptorType::CombinedImageSampler, 8, 0x10, UpdateFrequency::PerMaterial);
    opt.DeclareBinding(3, 0, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerDraw);

    auto layouts = opt.BuildOptimizedLayouts();
    EXPECT_GE(layouts.size(), 4u);

    opt.Shutdown();
}

TEST(DescSetLayoutOptimizer, AllDescriptorTypes) {
    DescriptorSetLayoutOptimizer opt;
    LayoutOptimizerConfig config;
    config.sortByFrequency = false;
    opt.Init(config);

    opt.DeclareBinding(0, 0, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerFrame);
    opt.DeclareBinding(0, 1, DescriptorType::StorageBuffer, 1, 0x1, UpdateFrequency::PerFrame);
    opt.DeclareBinding(0, 2, DescriptorType::SampledImage, 1, 0x10, UpdateFrequency::PerFrame);
    opt.DeclareBinding(0, 3, DescriptorType::StorageImage, 1, 0x100, UpdateFrequency::PerFrame);
    opt.DeclareBinding(0, 4, DescriptorType::Sampler, 1, 0x10, UpdateFrequency::PerFrame);
    opt.DeclareBinding(0, 5, DescriptorType::CombinedImageSampler, 1, 0x10, UpdateFrequency::PerFrame);
    opt.DeclareBinding(0, 6, DescriptorType::InputAttachment, 1, 0x10, UpdateFrequency::PerFrame);
    opt.DeclareBinding(0, 7, DescriptorType::AccelerationStructure, 1, 0x1, UpdateFrequency::PerFrame);

    auto layouts = opt.BuildOptimizedLayouts();
    EXPECT_EQ(layouts.size(), 1u);
    EXPECT_EQ(layouts[0].bindings.size(), 8u);

    opt.Shutdown();
}

TEST(DescSetLayoutOptimizer, ClearAndRebuild) {
    DescriptorSetLayoutOptimizer opt;
    opt.Init();

    opt.DeclareBinding(0, 0, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerFrame);
    opt.BuildOptimizedLayouts();

    opt.Clear();
    EXPECT_EQ(opt.GetLayoutCount(), 0u);

    // Rebuild with different bindings
    opt.DeclareBinding(0, 0, DescriptorType::SampledImage, 4, 0x10, UpdateFrequency::PerMaterial);
    auto layouts = opt.BuildOptimizedLayouts();
    EXPECT_GE(layouts.size(), 1u);

    opt.Shutdown();
}

TEST(DescSetLayoutOptimizer, ResetClearsAll) {
    DescriptorSetLayoutOptimizer opt;
    opt.Init();

    opt.DeclareBinding(0, 0, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerFrame);
    opt.BuildOptimizedLayouts();

    opt.Reset();

    EXPECT_EQ(opt.GetLayoutCount(), 0u);
    auto stats = opt.GetStats();
    EXPECT_EQ(stats.layoutsMerged, 0u);
    EXPECT_EQ(stats.bindingsRemoved, 0u);

    opt.Shutdown();
}

TEST(DescSetLayoutOptimizer, StatsTracking) {
    DescriptorSetLayoutOptimizer opt;
    LayoutOptimizerConfig config;
    config.enableCompaction = true;
    config.sortByFrequency = false;
    opt.Init(config);

    opt.DeclareBinding(0, 0, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerFrame);
    opt.DeclareBinding(0, 0, DescriptorType::UniformBuffer, 1, 0x1, UpdateFrequency::PerFrame); // Dup
    opt.DeclareBinding(0, 1, DescriptorType::SampledImage, 1, 0x10, UpdateFrequency::PerFrame);

    opt.BuildOptimizedLayouts();

    auto stats = opt.GetStats();
    EXPECT_EQ(stats.totalLayouts, 1u);
    EXPECT_EQ(stats.totalBindings, 2u);
    EXPECT_EQ(stats.bindingsRemoved, 1u);

    opt.Shutdown();
}
