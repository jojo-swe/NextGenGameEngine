#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_aliasing_optimizer.h"

using namespace nge;
using namespace nge::rhi;

TEST(AliasingOptimizer, EmptyProducesEmptyPlan) {
    AliasingOptimizer opt;
    auto plan = opt.Optimize();
    EXPECT_EQ(plan.bins.size(), 0u);
    EXPECT_EQ(plan.totalWithoutAliasing, 0u);
    EXPECT_EQ(plan.totalWithAliasing, 0u);
    EXPECT_FLOAT_EQ(plan.reductionPercent, 0.0f);
}

TEST(AliasingOptimizer, SingleResourceOneBin) {
    AliasingOptimizer opt;
    opt.AddResource({0, 0, 3, 1024, 256, true});
    auto plan = opt.Optimize();

    EXPECT_EQ(plan.bins.size(), 1u);
    EXPECT_EQ(plan.totalWithoutAliasing, 1024u);
    EXPECT_EQ(plan.totalWithAliasing, 1024u);
    EXPECT_EQ(plan.memorySaved, 0u);
}

TEST(AliasingOptimizer, NonOverlappingResourcesAlias) {
    AliasingOptimizer opt;
    // Resource A: passes 0-2, Resource B: passes 3-5 → can alias
    opt.AddResource({0, 0, 2, 2048, 256, true});
    opt.AddResource({1, 3, 5, 1024, 256, true});

    EXPECT_TRUE(opt.CanAlias(0, 1));

    auto plan = opt.Optimize();
    EXPECT_EQ(plan.bins.size(), 1u); // Both in same bin
    EXPECT_EQ(plan.totalWithoutAliasing, 3072u);
    EXPECT_EQ(plan.totalWithAliasing, 2048u); // Max of the two
    EXPECT_EQ(plan.memorySaved, 1024u);
}

TEST(AliasingOptimizer, OverlappingResourcesSeparateBins) {
    AliasingOptimizer opt;
    // Resource A: passes 0-5, Resource B: passes 3-8 → overlap
    opt.AddResource({0, 0, 5, 2048, 256, true});
    opt.AddResource({1, 3, 8, 1024, 256, true});

    EXPECT_FALSE(opt.CanAlias(0, 1));

    auto plan = opt.Optimize();
    EXPECT_EQ(plan.bins.size(), 2u);
    EXPECT_EQ(plan.totalWithoutAliasing, 3072u);
    EXPECT_EQ(plan.totalWithAliasing, 3072u); // No savings
    EXPECT_EQ(plan.memorySaved, 0u);
}

TEST(AliasingOptimizer, ChainOfThreeResources) {
    AliasingOptimizer opt;
    // A: 0-1, B: 2-3, C: 4-5 → A can alias with B and C, B can alias with C
    opt.AddResource({0, 0, 1, 4096, 256, true});
    opt.AddResource({1, 2, 3, 2048, 256, true});
    opt.AddResource({2, 4, 5, 1024, 256, true});

    auto plan = opt.Optimize();
    EXPECT_EQ(plan.bins.size(), 1u); // All can share one bin
    EXPECT_EQ(plan.totalWithoutAliasing, 7168u);
    EXPECT_EQ(plan.totalWithAliasing, 4096u);
    EXPECT_EQ(plan.memorySaved, 3072u);
}

TEST(AliasingOptimizer, TwoOverlappingOneFree) {
    AliasingOptimizer opt;
    // A: 0-5, B: 3-8, C: 10-12
    // A overlaps B → separate bins. C doesn't overlap either → aliases with A or B
    opt.AddResource({0, 0, 5, 4096, 256, true});
    opt.AddResource({1, 3, 8, 2048, 256, true});
    opt.AddResource({2, 10, 12, 1024, 256, true});

    auto plan = opt.Optimize();
    EXPECT_EQ(plan.bins.size(), 2u); // A+C in one bin, B in another (or A alone, B+C)
    EXPECT_EQ(plan.totalWithoutAliasing, 7168u);
    EXPECT_EQ(plan.totalWithAliasing, 4096u + 2048u); // 6144
    EXPECT_EQ(plan.memorySaved, 1024u);
}

TEST(AliasingOptimizer, AllOverlapping) {
    AliasingOptimizer opt;
    // All resources overlap with each other
    opt.AddResource({0, 0, 10, 1024, 256, true});
    opt.AddResource({1, 0, 10, 2048, 256, true});
    opt.AddResource({2, 0, 10, 512, 256, true});

    auto plan = opt.Optimize();
    EXPECT_EQ(plan.bins.size(), 3u); // Each needs its own bin
    EXPECT_EQ(plan.memorySaved, 0u);
}

TEST(AliasingOptimizer, LargestFirstPacking) {
    AliasingOptimizer opt;
    // A(4K): 0-2, B(1K): 0-2, C(4K): 3-5, D(1K): 3-5
    // A overlaps B, C overlaps D, but A can alias C, B can alias D
    opt.AddResource({0, 0, 2, 4096, 256, true});
    opt.AddResource({1, 0, 2, 1024, 256, true});
    opt.AddResource({2, 3, 5, 4096, 256, true});
    opt.AddResource({3, 3, 5, 1024, 256, true});

    auto plan = opt.Optimize();
    EXPECT_EQ(plan.bins.size(), 2u);
    EXPECT_EQ(plan.totalWithoutAliasing, 10240u);
    EXPECT_EQ(plan.totalWithAliasing, 4096u + 1024u); // Two bins: 4K + 1K
    EXPECT_GT(plan.reductionPercent, 40.0f);
}

TEST(AliasingOptimizer, ReductionPercent) {
    AliasingOptimizer opt;
    opt.AddResource({0, 0, 1, 1000, 256, true});
    opt.AddResource({1, 2, 3, 1000, 256, true});

    auto plan = opt.Optimize();
    EXPECT_FLOAT_EQ(plan.reductionPercent, 50.0f);
}

TEST(AliasingOptimizer, Reset) {
    AliasingOptimizer opt;
    opt.AddResource({0, 0, 1, 1024, 256, true});
    EXPECT_EQ(opt.GetResourceCount(), 1u);

    opt.Reset();
    EXPECT_EQ(opt.GetResourceCount(), 0u);

    auto plan = opt.Optimize();
    EXPECT_EQ(plan.bins.size(), 0u);
}
