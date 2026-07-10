#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_descriptor_pool_allocator.h"

using namespace nge;
using namespace nge::rhi;

static std::vector<PoolSizeEntry> SimpleReq(DescriptorType type = DescriptorType::UniformBuffer, u32 count = 1) {
    return {{type, count}};
}

TEST(DescriptorPoolAllocator, InitAndShutdown) {
    DescriptorPoolAllocator alloc;
    EXPECT_TRUE(alloc.Init());
    EXPECT_EQ(alloc.GetPoolCount(), 0u);
    EXPECT_EQ(alloc.GetTotalAllocated(), 0u);
    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, AllocateCreatesPool) {
    DescriptorPoolAllocator alloc;
    alloc.Init();

    u32 id = alloc.Allocate(SimpleReq());
    EXPECT_NE(id, UINT32_MAX);
    EXPECT_EQ(alloc.GetPoolCount(), 1u); // Auto-created first pool
    EXPECT_EQ(alloc.GetTotalAllocated(), 1u);

    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, MultipleAllocations) {
    DescriptorPoolAllocator alloc;
    alloc.Init();

    u32 id0 = alloc.Allocate(SimpleReq());
    u32 id1 = alloc.Allocate(SimpleReq());
    u32 id2 = alloc.Allocate(SimpleReq());

    EXPECT_NE(id0, UINT32_MAX);
    EXPECT_NE(id1, UINT32_MAX);
    EXPECT_NE(id2, UINT32_MAX);
    EXPECT_NE(id0, id1);
    EXPECT_NE(id1, id2);
    EXPECT_EQ(alloc.GetTotalAllocated(), 3u);

    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, FreeAllocation) {
    DescriptorPoolAllocator alloc;
    alloc.Init();

    u32 id = alloc.Allocate(SimpleReq());
    EXPECT_EQ(alloc.GetTotalAllocated(), 1u);

    alloc.Free(id);
    EXPECT_EQ(alloc.GetTotalAllocated(), 0u);

    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, FreeUnknownDoesNothing) {
    DescriptorPoolAllocator alloc;
    alloc.Init();

    alloc.Free(999); // Should not crash
    EXPECT_EQ(alloc.GetTotalAllocated(), 0u);

    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, AutoGrowOnExhaustion) {
    DescriptorPoolAllocator alloc;
    DescriptorPoolAllocConfig config;
    config.setsPerPool = 2;
    config.autoGrow = true;
    alloc.Init(config);

    alloc.Allocate(SimpleReq());
    alloc.Allocate(SimpleReq());
    EXPECT_EQ(alloc.GetPoolCount(), 1u);

    alloc.Allocate(SimpleReq()); // Should create new pool
    EXPECT_EQ(alloc.GetPoolCount(), 2u);
    EXPECT_EQ(alloc.GetTotalAllocated(), 3u);

    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, NoAutoGrowFails) {
    DescriptorPoolAllocator alloc;
    DescriptorPoolAllocConfig config;
    config.setsPerPool = 1;
    config.autoGrow = true;
    config.maxPools = 1;
    alloc.Init(config);

    alloc.Allocate(SimpleReq());
    u32 id = alloc.Allocate(SimpleReq()); // Pool full, can't create new
    EXPECT_EQ(id, UINT32_MAX);

    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, AutoGrowDisabledFails) {
    DescriptorPoolAllocator alloc;
    DescriptorPoolAllocConfig config;
    config.autoGrow = false;
    alloc.Init(config);

    // No pools exist and auto-grow disabled
    u32 id = alloc.Allocate(SimpleReq());
    EXPECT_EQ(id, UINT32_MAX);

    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, ResetAllPools) {
    DescriptorPoolAllocator alloc;
    alloc.Init();

    alloc.Allocate(SimpleReq());
    alloc.Allocate(SimpleReq());
    EXPECT_EQ(alloc.GetTotalAllocated(), 2u);

    alloc.ResetAllPools();
    EXPECT_EQ(alloc.GetTotalAllocated(), 0u);
    EXPECT_EQ(alloc.GetPoolCount(), 1u); // Pools still exist, just empty

    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, ResetSpecificPool) {
    DescriptorPoolAllocator alloc;
    DescriptorPoolAllocConfig config;
    config.setsPerPool = 2;
    alloc.Init(config);

    alloc.Allocate(SimpleReq());
    alloc.Allocate(SimpleReq());
    alloc.Allocate(SimpleReq()); // Creates pool 1

    alloc.ResetPool(0);

    // Pool 0 reset but pool 1 still has 1 allocation
    const auto* pool0 = alloc.GetPoolInfo(0);
    EXPECT_NE(pool0, nullptr);
    EXPECT_EQ(pool0->allocatedSets, 0u);

    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, GetPoolInfo) {
    DescriptorPoolAllocator alloc;
    alloc.Init();

    alloc.Allocate(SimpleReq());

    const auto* info = alloc.GetPoolInfo(0);
    EXPECT_NE(info, nullptr);
    EXPECT_EQ(info->poolId, 0u);
    EXPECT_EQ(info->allocatedSets, 1u);

    EXPECT_EQ(alloc.GetPoolInfo(999), nullptr);

    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, PoolExhaustedFlag) {
    DescriptorPoolAllocator alloc;
    DescriptorPoolAllocConfig config;
    config.setsPerPool = 2;
    alloc.Init(config);

    alloc.Allocate(SimpleReq());
    EXPECT_FALSE(alloc.GetPoolInfo(0)->exhausted);

    alloc.Allocate(SimpleReq());
    EXPECT_TRUE(alloc.GetPoolInfo(0)->exhausted);

    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, NeedsCompaction) {
    DescriptorPoolAllocator alloc;
    DescriptorPoolAllocConfig config;
    config.setsPerPool = 2;
    alloc.Init(config);

    // Create 4 pools with 1 allocation each, then free all
    u32 ids[8];
    for (int i = 0; i < 8; ++i) {
        ids[i] = alloc.Allocate(SimpleReq());
    }
    EXPECT_EQ(alloc.GetPoolCount(), 4u);

    for (int i = 0; i < 8; ++i) {
        alloc.Free(ids[i]);
    }

    // All pools are empty -> underutilized
    EXPECT_TRUE(alloc.NeedsCompaction(0.3f));

    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, GetUnderutilizedPools) {
    DescriptorPoolAllocator alloc;
    DescriptorPoolAllocConfig config;
    config.setsPerPool = 4;
    alloc.Init(config);

    // Create allocations then free some
    u32 id0 = alloc.Allocate(SimpleReq());
    alloc.Allocate(SimpleReq());
    alloc.Allocate(SimpleReq());
    alloc.Allocate(SimpleReq()); // Pool 0 full

    alloc.Allocate(SimpleReq()); // Pool 1 with 1 alloc

    alloc.Free(id0); // Pool 0 now 3/4

    auto underutilized = alloc.GetUnderutilizedPools(0.5f);
    // Pool 1 has 1/4 = 0.25 < 0.5 threshold
    bool pool1Found = false;
    for (u32 pid : underutilized) {
        if (pid == 1) pool1Found = true;
    }
    EXPECT_TRUE(pool1Found);

    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, MultipleDescriptorTypes) {
    DescriptorPoolAllocator alloc;
    alloc.Init();

    // Allocate with multiple types
    std::vector<PoolSizeEntry> req = {
        {DescriptorType::UniformBuffer, 2},
        {DescriptorType::CombinedImageSampler, 3},
    };

    u32 id = alloc.Allocate(req);
    EXPECT_NE(id, UINT32_MAX);

    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, StatsTracking) {
    DescriptorPoolAllocator alloc;
    DescriptorPoolAllocConfig config;
    config.setsPerPool = 4;
    alloc.Init(config);

    alloc.Allocate(SimpleReq());
    alloc.Allocate(SimpleReq());

    auto stats = alloc.GetStats();
    EXPECT_EQ(stats.totalPools, 1u);
    EXPECT_EQ(stats.totalSetsAllocated, 2u);
    EXPECT_EQ(stats.totalSetsCapacity, 4u);
    EXPECT_EQ(stats.exhaustedPools, 0u);
    EXPECT_GT(stats.utilizationRatio, 0.0f);
    EXPECT_GE(stats.poolGrowthCount, 1u);
    EXPECT_EQ(stats.allocationFailures, 0u);

    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, AllocationFailureTracked) {
    DescriptorPoolAllocator alloc;
    DescriptorPoolAllocConfig config;
    config.setsPerPool = 1;
    config.maxPools = 1;
    alloc.Init(config);

    alloc.Allocate(SimpleReq());
    alloc.Allocate(SimpleReq()); // Fails

    auto stats = alloc.GetStats();
    EXPECT_EQ(stats.allocationFailures, 1u);

    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, ResetClearsAll) {
    DescriptorPoolAllocator alloc;
    alloc.Init();

    alloc.Allocate(SimpleReq());
    alloc.Allocate(SimpleReq());

    alloc.Reset();

    EXPECT_EQ(alloc.GetPoolCount(), 0u);
    EXPECT_EQ(alloc.GetTotalAllocated(), 0u);

    auto stats = alloc.GetStats();
    EXPECT_EQ(stats.poolGrowthCount, 0u);
    EXPECT_EQ(stats.allocationFailures, 0u);

    alloc.Shutdown();
}

TEST(DescriptorPoolAllocator, ReuseAfterFree) {
    DescriptorPoolAllocator alloc;
    DescriptorPoolAllocConfig config;
    config.setsPerPool = 2;
    alloc.Init(config);

    u32 id0 = alloc.Allocate(SimpleReq());
    [[maybe_unused]] u32 id1 = alloc.Allocate(SimpleReq());
    // Pool 0 is full

    alloc.Free(id0);
    // Pool 0 now has space

    u32 id2 = alloc.Allocate(SimpleReq());
    EXPECT_NE(id2, UINT32_MAX);
    EXPECT_EQ(alloc.GetPoolCount(), 1u); // No new pool needed

    alloc.Shutdown();
}
