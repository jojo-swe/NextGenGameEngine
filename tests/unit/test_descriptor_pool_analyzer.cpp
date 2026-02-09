#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_descriptor_pool_analyzer.h"

using namespace nge::rhi;

TEST(DescriptorPoolAnalyzer, InitAndShutdown) {
    DescriptorPoolAnalyzer analyzer;
    EXPECT_TRUE(analyzer.Init());

    auto stats = analyzer.GetStats();
    EXPECT_EQ(stats.totalPools, 0u);
    EXPECT_EQ(stats.totalDescriptorsUsed, 0u);

    analyzer.Shutdown();
}

TEST(DescriptorPoolAnalyzer, RegisterAndAnalyze) {
    DescriptorPoolAnalyzer analyzer;
    analyzer.Init();

    std::unordered_map<DescriptorCategory, u32> maxPerType = {
        {DescriptorCategory::UniformBuffer, 100},
        {DescriptorCategory::SampledImage, 200},
    };

    analyzer.RegisterPool(1, 50, maxPerType, "MainPool");

    std::unordered_map<DescriptorCategory, u32> usedPerType = {
        {DescriptorCategory::UniformBuffer, 80},
        {DescriptorCategory::SampledImage, 150},
    };

    analyzer.UpdatePoolUsage(1, 40, usedPerType);

    auto report = analyzer.AnalyzePool(1);
    EXPECT_EQ(report.poolId, 1u);
    EXPECT_NEAR(report.utilizationPercent, 80.0f, 0.1f); // 40/50
    EXPECT_EQ(report.wastedSets, 10u); // 50 - 40
    EXPECT_GT(report.wastedDescriptors, 0u); // 20 + 50 = 70

    analyzer.Shutdown();
}

TEST(DescriptorPoolAnalyzer, UnderutilizedDetection) {
    DescriptorPoolAnalyzer analyzer;
    DescriptorPoolAnalyzerConfig config;
    config.underutilizationThreshold = 0.25f;
    analyzer.Init(config);

    std::unordered_map<DescriptorCategory, u32> maxPerType = {
        {DescriptorCategory::UniformBuffer, 1000},
    };

    analyzer.RegisterPool(1, 1000, maxPerType, "HugePool");

    std::unordered_map<DescriptorCategory, u32> usedPerType = {
        {DescriptorCategory::UniformBuffer, 10},
    };

    analyzer.UpdatePoolUsage(1, 5, usedPerType); // 0.5% utilized

    auto report = analyzer.AnalyzePool(1);
    EXPECT_TRUE(report.isUnderutilized);
    EXPECT_LT(report.utilizationPercent, 25.0f);

    auto stats = analyzer.GetStats();
    EXPECT_EQ(stats.underutilizedPools, 1u);

    analyzer.Shutdown();
}

TEST(DescriptorPoolAnalyzer, WellUtilizedPool) {
    DescriptorPoolAnalyzer analyzer;
    analyzer.Init();

    std::unordered_map<DescriptorCategory, u32> maxPerType = {
        {DescriptorCategory::UniformBuffer, 100},
        {DescriptorCategory::SampledImage, 100},
    };

    analyzer.RegisterPool(1, 50, maxPerType, "GoodPool");

    std::unordered_map<DescriptorCategory, u32> usedPerType = {
        {DescriptorCategory::UniformBuffer, 90},
        {DescriptorCategory::SampledImage, 85},
    };

    analyzer.UpdatePoolUsage(1, 45, usedPerType);

    auto report = analyzer.AnalyzePool(1);
    EXPECT_FALSE(report.isUnderutilized);
    EXPECT_GT(report.utilizationPercent, 80.0f);
    EXPECT_GT(report.typeBalanceScore, 0.5f); // Well-balanced

    analyzer.Shutdown();
}

TEST(DescriptorPoolAnalyzer, ImbalancedTypes) {
    DescriptorPoolAnalyzer analyzer;
    analyzer.Init();

    std::unordered_map<DescriptorCategory, u32> maxPerType = {
        {DescriptorCategory::UniformBuffer, 100},
        {DescriptorCategory::SampledImage, 100},
        {DescriptorCategory::StorageBuffer, 100},
    };

    analyzer.RegisterPool(1, 50, maxPerType, "Imbalanced");

    // Only using UBOs, nothing else
    std::unordered_map<DescriptorCategory, u32> usedPerType = {
        {DescriptorCategory::UniformBuffer, 95},
        {DescriptorCategory::SampledImage, 0},
        {DescriptorCategory::StorageBuffer, 0},
    };

    analyzer.UpdatePoolUsage(1, 30, usedPerType);

    auto report = analyzer.AnalyzePool(1);
    EXPECT_LT(report.typeBalanceScore, 0.8f); // Imbalanced

    analyzer.Shutdown();
}

TEST(DescriptorPoolAnalyzer, RemovePool) {
    DescriptorPoolAnalyzer analyzer;
    analyzer.Init();

    std::unordered_map<DescriptorCategory, u32> maxPerType = {
        {DescriptorCategory::UniformBuffer, 100},
    };

    analyzer.RegisterPool(1, 50, maxPerType, "Temp");
    EXPECT_EQ(analyzer.GetStats().totalPools, 1u);

    analyzer.RemovePool(1);
    EXPECT_EQ(analyzer.GetStats().totalPools, 0u);

    analyzer.Shutdown();
}

TEST(DescriptorPoolAnalyzer, MostUnderutilizedPool) {
    DescriptorPoolAnalyzer analyzer;
    analyzer.Init();

    std::unordered_map<DescriptorCategory, u32> maxPerType = {
        {DescriptorCategory::UniformBuffer, 100},
    };

    analyzer.RegisterPool(1, 100, maxPerType, "Pool1");
    analyzer.RegisterPool(2, 100, maxPerType, "Pool2");
    analyzer.RegisterPool(3, 100, maxPerType, "Pool3");

    std::unordered_map<DescriptorCategory, u32> used1 = {{DescriptorCategory::UniformBuffer, 80}};
    std::unordered_map<DescriptorCategory, u32> used2 = {{DescriptorCategory::UniformBuffer, 5}};
    std::unordered_map<DescriptorCategory, u32> used3 = {{DescriptorCategory::UniformBuffer, 50}};

    analyzer.UpdatePoolUsage(1, 80, used1);
    analyzer.UpdatePoolUsage(2, 5, used2);   // Worst
    analyzer.UpdatePoolUsage(3, 50, used3);

    EXPECT_EQ(analyzer.GetMostUnderutilizedPool(), 2u);

    analyzer.Shutdown();
}

TEST(DescriptorPoolAnalyzer, TotalWaste) {
    DescriptorPoolAnalyzer analyzer;
    analyzer.Init();

    std::unordered_map<DescriptorCategory, u32> maxPerType = {
        {DescriptorCategory::UniformBuffer, 100},
        {DescriptorCategory::SampledImage, 50},
    };

    analyzer.RegisterPool(1, 50, maxPerType, "Pool1");

    std::unordered_map<DescriptorCategory, u32> usedPerType = {
        {DescriptorCategory::UniformBuffer, 60},
        {DescriptorCategory::SampledImage, 30},
    };

    analyzer.UpdatePoolUsage(1, 40, usedPerType);

    // Waste: (100-60) + (50-30) = 40 + 20 = 60
    EXPECT_EQ(analyzer.GetTotalWaste(), 60u);

    analyzer.Shutdown();
}

TEST(DescriptorPoolAnalyzer, AnalyzeMultiplePools) {
    DescriptorPoolAnalyzer analyzer;
    analyzer.Init();

    std::unordered_map<DescriptorCategory, u32> maxPerType = {
        {DescriptorCategory::UniformBuffer, 100},
    };

    analyzer.RegisterPool(1, 100, maxPerType, "A");
    analyzer.RegisterPool(2, 100, maxPerType, "B");

    std::unordered_map<DescriptorCategory, u32> used = {{DescriptorCategory::UniformBuffer, 50}};
    analyzer.UpdatePoolUsage(1, 90, used);
    analyzer.UpdatePoolUsage(2, 10, used);

    auto reports = analyzer.Analyze();
    EXPECT_EQ(reports.size(), 2u);

    // Sorted by utilization (worst first)
    EXPECT_EQ(reports[0].poolId, 2u); // 10% utilized
    EXPECT_EQ(reports[1].poolId, 1u); // 90% utilized

    analyzer.Shutdown();
}

TEST(DescriptorPoolAnalyzer, ResetClearsAll) {
    DescriptorPoolAnalyzer analyzer;
    analyzer.Init();

    std::unordered_map<DescriptorCategory, u32> maxPerType = {
        {DescriptorCategory::UniformBuffer, 100},
    };

    analyzer.RegisterPool(1, 50, maxPerType, "Pool");
    EXPECT_EQ(analyzer.GetStats().totalPools, 1u);

    analyzer.Reset();
    EXPECT_EQ(analyzer.GetStats().totalPools, 0u);

    analyzer.Shutdown();
}

TEST(DescriptorPoolAnalyzer, Recommendations) {
    DescriptorPoolAnalyzer analyzer;
    DescriptorPoolAnalyzerConfig config;
    config.enableRecommendations = true;
    analyzer.Init(config);

    std::unordered_map<DescriptorCategory, u32> maxPerType = {
        {DescriptorCategory::UniformBuffer, 1000},
    };

    analyzer.RegisterPool(1, 1000, maxPerType, "WastefulPool");

    std::unordered_map<DescriptorCategory, u32> usedPerType = {
        {DescriptorCategory::UniformBuffer, 5},
    };

    analyzer.UpdatePoolUsage(1, 2, usedPerType);

    auto report = analyzer.AnalyzePool(1);
    EXPECT_FALSE(report.recommendation.empty());
    EXPECT_NE(report.recommendation.find("underutilized"), std::string::npos);

    analyzer.Shutdown();
}

TEST(DescriptorPoolAnalyzer, MaxPoolsLimit) {
    DescriptorPoolAnalyzer analyzer;
    DescriptorPoolAnalyzerConfig config;
    config.maxTrackedPools = 2;
    analyzer.Init(config);

    std::unordered_map<DescriptorCategory, u32> maxPerType = {
        {DescriptorCategory::UniformBuffer, 10},
    };

    analyzer.RegisterPool(1, 10, maxPerType, "A");
    analyzer.RegisterPool(2, 10, maxPerType, "B");
    analyzer.RegisterPool(3, 10, maxPerType, "C"); // Exceeds limit

    EXPECT_EQ(analyzer.GetStats().totalPools, 2u);

    analyzer.Shutdown();
}

TEST(DescriptorPoolAnalyzer, UnknownPoolReport) {
    DescriptorPoolAnalyzer analyzer;
    analyzer.Init();

    auto report = analyzer.AnalyzePool(999);
    EXPECT_EQ(report.poolId, 999u);
    EXPECT_NE(report.recommendation.find("not found"), std::string::npos);

    analyzer.Shutdown();
}
