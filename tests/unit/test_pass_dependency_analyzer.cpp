#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_pass_dependency_analyzer.h"

using namespace nge;
using namespace nge::rhi;

TEST(PassDependencyAnalyzer, InitAndShutdown) {
    PassDependencyAnalyzer analyzer;
    EXPECT_TRUE(analyzer.Init());

    auto stats = analyzer.GetStats();
    EXPECT_EQ(stats.totalPasses, 0u);
    EXPECT_EQ(stats.totalDependencies, 0u);

    analyzer.Shutdown();
}

TEST(PassDependencyAnalyzer, SimpleRAWDependency) {
    PassDependencyAnalyzer analyzer;
    analyzer.Init();

    PassDeclaration pass0;
    pass0.passIndex = 0;
    pass0.passName = "GBuffer";
    pass0.queueFamily = 0;
    pass0.accesses = {
        {1, "DepthBuffer", AccessType::Write, PipelineStage::LateFragTest},
    };

    PassDeclaration pass1;
    pass1.passIndex = 1;
    pass1.passName = "Lighting";
    pass1.queueFamily = 0;
    pass1.accesses = {
        {1, "DepthBuffer", AccessType::Read, PipelineStage::FragmentShader},
    };

    analyzer.DeclarePass(pass0);
    analyzer.DeclarePass(pass1);
    analyzer.Analyze();

    auto& deps = analyzer.GetDependencies();
    EXPECT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0].srcPass, 0u);
    EXPECT_EQ(deps[0].dstPass, 1u);
    EXPECT_EQ(deps[0].hazardType, "RAW");
    EXPECT_EQ(deps[0].resourceName, "DepthBuffer");
    EXPECT_FALSE(deps[0].isCrossQueue);

    auto stats = analyzer.GetStats();
    EXPECT_EQ(stats.rawHazards, 1u);

    analyzer.Shutdown();
}

TEST(PassDependencyAnalyzer, WAWDependency) {
    PassDependencyAnalyzer analyzer;
    analyzer.Init();

    PassDeclaration pass0;
    pass0.passIndex = 0;
    pass0.passName = "ShadowPass";
    pass0.queueFamily = 0;
    pass0.accesses = {
        {10, "ShadowMap", AccessType::Write, PipelineStage::LateFragTest},
    };

    PassDeclaration pass1;
    pass1.passIndex = 1;
    pass1.passName = "ShadowPass2";
    pass1.queueFamily = 0;
    pass1.accesses = {
        {10, "ShadowMap", AccessType::Write, PipelineStage::LateFragTest},
    };

    analyzer.DeclarePass(pass0);
    analyzer.DeclarePass(pass1);
    analyzer.Analyze();

    auto& deps = analyzer.GetDependencies();
    EXPECT_GE(deps.size(), 1u);

    bool foundWAW = false;
    for (const auto& d : deps) {
        if (d.hazardType == "WAW") foundWAW = true;
    }
    EXPECT_TRUE(foundWAW);

    analyzer.Shutdown();
}

TEST(PassDependencyAnalyzer, WARDependency) {
    PassDependencyAnalyzer analyzer;
    analyzer.Init();

    PassDeclaration pass0;
    pass0.passIndex = 0;
    pass0.passName = "ReadPass";
    pass0.queueFamily = 0;
    pass0.accesses = {
        {5, "SharedBuf", AccessType::Read, PipelineStage::FragmentShader},
    };

    PassDeclaration pass1;
    pass1.passIndex = 1;
    pass1.passName = "WritePass";
    pass1.queueFamily = 0;
    pass1.accesses = {
        {5, "SharedBuf", AccessType::Write, PipelineStage::ComputeShader},
    };

    analyzer.DeclarePass(pass0);
    analyzer.DeclarePass(pass1);
    analyzer.Analyze();

    bool foundWAR = false;
    for (const auto& d : analyzer.GetDependencies()) {
        if (d.hazardType == "WAR") foundWAR = true;
    }
    EXPECT_TRUE(foundWAR);

    analyzer.Shutdown();
}

TEST(PassDependencyAnalyzer, CrossQueueDetection) {
    PassDependencyAnalyzer analyzer;
    PassDependencyConfig config;
    config.detectCrossQueue = true;
    analyzer.Init(config);

    PassDeclaration pass0;
    pass0.passIndex = 0;
    pass0.passName = "ComputeCull";
    pass0.queueFamily = 1; // Compute queue
    pass0.accesses = {
        {20, "IndirectArgs", AccessType::Write, PipelineStage::ComputeShader},
    };

    PassDeclaration pass1;
    pass1.passIndex = 1;
    pass1.passName = "DrawIndirect";
    pass1.queueFamily = 0; // Graphics queue
    pass1.accesses = {
        {20, "IndirectArgs", AccessType::Read, PipelineStage::VertexInput},
    };

    analyzer.DeclarePass(pass0);
    analyzer.DeclarePass(pass1);
    analyzer.Analyze();

    EXPECT_EQ(analyzer.GetDependencies().size(), 1u);
    EXPECT_TRUE(analyzer.GetDependencies()[0].isCrossQueue);

    auto stats = analyzer.GetStats();
    EXPECT_EQ(stats.crossQueueDeps, 1u);

    analyzer.Shutdown();
}

TEST(PassDependencyAnalyzer, NoDependencyForIndependentPasses) {
    PassDependencyAnalyzer analyzer;
    analyzer.Init();

    PassDeclaration pass0;
    pass0.passIndex = 0;
    pass0.passName = "PassA";
    pass0.queueFamily = 0;
    pass0.accesses = {
        {1, "ResourceA", AccessType::Write, PipelineStage::ColorOutput},
    };

    PassDeclaration pass1;
    pass1.passIndex = 1;
    pass1.passName = "PassB";
    pass1.queueFamily = 0;
    pass1.accesses = {
        {2, "ResourceB", AccessType::Write, PipelineStage::ColorOutput},
    };

    analyzer.DeclarePass(pass0);
    analyzer.DeclarePass(pass1);
    analyzer.Analyze();

    EXPECT_TRUE(analyzer.GetDependencies().empty());

    analyzer.Shutdown();
}

TEST(PassDependencyAnalyzer, TopologicalOrder) {
    PassDependencyAnalyzer analyzer;
    analyzer.Init();

    // Chain: 0 -> 1 -> 2
    PassDeclaration p0, p1, p2;
    p0.passIndex = 0; p0.passName = "P0"; p0.queueFamily = 0;
    p0.accesses = {{1, "Res", AccessType::Write, PipelineStage::ColorOutput}};

    p1.passIndex = 1; p1.passName = "P1"; p1.queueFamily = 0;
    p1.accesses = {
        {1, "Res", AccessType::Read, PipelineStage::FragmentShader},
        {2, "Res2", AccessType::Write, PipelineStage::ColorOutput},
    };

    p2.passIndex = 2; p2.passName = "P2"; p2.queueFamily = 0;
    p2.accesses = {{2, "Res2", AccessType::Read, PipelineStage::FragmentShader}};

    analyzer.DeclarePass(p0);
    analyzer.DeclarePass(p1);
    analyzer.DeclarePass(p2);
    analyzer.Analyze();

    auto order = analyzer.GetTopologicalOrder();
    EXPECT_EQ(order.size(), 3u);

    // Verify ordering constraints
    auto indexOf = [&](u32 pass) -> int {
        for (int i = 0; i < (int)order.size(); ++i) {
            if (order[i] == pass) return i;
        }
        return -1;
    };

    EXPECT_LT(indexOf(0), indexOf(1));
    EXPECT_LT(indexOf(1), indexOf(2));

    analyzer.Shutdown();
}

TEST(PassDependencyAnalyzer, CycleDetection) {
    PassDependencyAnalyzer analyzer;
    analyzer.Init();

    PassDeclaration p0, p1;
    p0.passIndex = 0; p0.passName = "P0"; p0.queueFamily = 0;
    p0.accesses = {{1, "Res", AccessType::Write, PipelineStage::ColorOutput}};

    p1.passIndex = 1; p1.passName = "P1"; p1.queueFamily = 0;
    p1.accesses = {{1, "Res", AccessType::Read, PipelineStage::FragmentShader}};

    analyzer.DeclarePass(p0);
    analyzer.DeclarePass(p1);
    analyzer.Analyze();

    // Adding 1->0 would create cycle 0->1->0
    EXPECT_TRUE(analyzer.WouldCreateCycle(1, 0));
    // Adding 0->1 (already exists) is not a new cycle from scratch
    EXPECT_FALSE(analyzer.WouldCreateCycle(0, 1));

    analyzer.Shutdown();
}

TEST(PassDependencyAnalyzer, GetDependenciesForPass) {
    PassDependencyAnalyzer analyzer;
    analyzer.Init();

    PassDeclaration p0, p1, p2;
    p0.passIndex = 0; p0.passName = "Shadow"; p0.queueFamily = 0;
    p0.accesses = {{1, "ShadowMap", AccessType::Write, PipelineStage::LateFragTest}};

    p1.passIndex = 1; p1.passName = "GBuffer"; p1.queueFamily = 0;
    p1.accesses = {{2, "GBuf", AccessType::Write, PipelineStage::ColorOutput}};

    p2.passIndex = 2; p2.passName = "Lighting"; p2.queueFamily = 0;
    p2.accesses = {
        {1, "ShadowMap", AccessType::Read, PipelineStage::FragmentShader},
        {2, "GBuf", AccessType::Read, PipelineStage::FragmentShader},
    };

    analyzer.DeclarePass(p0);
    analyzer.DeclarePass(p1);
    analyzer.DeclarePass(p2);
    analyzer.Analyze();

    auto depsForLighting = analyzer.GetDependenciesForPass(2);
    EXPECT_EQ(depsForLighting.size(), 2u); // Depends on both Shadow and GBuffer

    analyzer.Shutdown();
}

TEST(PassDependencyAnalyzer, RedundantDepsRemoved) {
    PassDependencyAnalyzer analyzer;
    PassDependencyConfig config;
    config.warnOnRedundant = true;
    analyzer.Init(config);

    // Chain: 0 writes Res, 1 reads Res & writes Res2, 2 reads Res & Res2
    // Dependency 0->2 via Res is redundant because 0->1->2 exists
    PassDeclaration p0, p1, p2;
    p0.passIndex = 0; p0.passName = "P0"; p0.queueFamily = 0;
    p0.accesses = {{1, "Res", AccessType::Write, PipelineStage::ColorOutput}};

    p1.passIndex = 1; p1.passName = "P1"; p1.queueFamily = 0;
    p1.accesses = {
        {1, "Res", AccessType::Read, PipelineStage::FragmentShader},
        {2, "Res2", AccessType::Write, PipelineStage::ColorOutput},
    };

    p2.passIndex = 2; p2.passName = "P2"; p2.queueFamily = 0;
    p2.accesses = {
        {1, "Res", AccessType::Read, PipelineStage::FragmentShader},
        {2, "Res2", AccessType::Read, PipelineStage::FragmentShader},
    };

    analyzer.DeclarePass(p0);
    analyzer.DeclarePass(p1);
    analyzer.DeclarePass(p2);
    analyzer.Analyze();

    auto stats = analyzer.GetStats();
    EXPECT_GT(stats.redundantDepsRemoved, 0u);

    analyzer.Shutdown();
}

TEST(PassDependencyAnalyzer, ClearResetsState) {
    PassDependencyAnalyzer analyzer;
    analyzer.Init();

    PassDeclaration p0;
    p0.passIndex = 0; p0.passName = "P0"; p0.queueFamily = 0;
    p0.accesses = {{1, "Res", AccessType::Write, PipelineStage::ColorOutput}};
    analyzer.DeclarePass(p0);
    analyzer.Analyze();

    EXPECT_EQ(analyzer.GetStats().totalPasses, 1u);

    analyzer.Clear();

    EXPECT_EQ(analyzer.GetStats().totalPasses, 0u);
    EXPECT_TRUE(analyzer.GetDependencies().empty());

    analyzer.Shutdown();
}

TEST(PassDependencyAnalyzer, MultipleResourceDeps) {
    PassDependencyAnalyzer analyzer;
    analyzer.Init();

    PassDeclaration p0;
    p0.passIndex = 0; p0.passName = "GBuffer"; p0.queueFamily = 0;
    p0.accesses = {
        {1, "Albedo", AccessType::Write, PipelineStage::ColorOutput},
        {2, "Normal", AccessType::Write, PipelineStage::ColorOutput},
        {3, "Depth", AccessType::Write, PipelineStage::LateFragTest},
    };

    PassDeclaration p1;
    p1.passIndex = 1; p1.passName = "SSAO"; p1.queueFamily = 0;
    p1.accesses = {
        {2, "Normal", AccessType::Read, PipelineStage::ComputeShader},
        {3, "Depth", AccessType::Read, PipelineStage::ComputeShader},
    };

    analyzer.DeclarePass(p0);
    analyzer.DeclarePass(p1);
    analyzer.Analyze();

    // Should have deps for Normal (RAW) and Depth (RAW)
    auto& deps = analyzer.GetDependencies();
    EXPECT_GE(deps.size(), 1u); // May be merged or separate

    // All should be RAW
    for (const auto& d : deps) {
        EXPECT_EQ(d.hazardType, "RAW");
        EXPECT_EQ(d.srcPass, 0u);
        EXPECT_EQ(d.dstPass, 1u);
    }

    analyzer.Shutdown();
}
