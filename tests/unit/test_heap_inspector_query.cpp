#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_heap_inspector.h"
#include "engine/rhi/common/rhi_query_readback.h"

using namespace nge::rhi;

// ─── Heap Inspector Tests ────────────────────────────────────────────────

TEST(HeapInspector, InitCreatesHeaps) {
    HeapInspector inspector;
    EXPECT_TRUE(inspector.Init(nullptr));

    auto heaps = inspector.GetHeapInfos();
    EXPECT_GE(heaps.size(), 2u); // device-local + host-visible stubs

    auto stats = inspector.GetStats();
    EXPECT_GE(stats.totalHeaps, 2u);
    EXPECT_EQ(stats.totalAllocations, 0u);

    inspector.Shutdown();
}

TEST(HeapInspector, TrackAllocationAndFree) {
    HeapInspector inspector;
    inspector.Init(nullptr);

    inspector.TrackAllocation(100, 0, 1024, 0, AllocationCategory::Texture, "albedo_tex");

    auto stats = inspector.GetStats();
    EXPECT_EQ(stats.totalAllocations, 1u);
    EXPECT_EQ(stats.bytesPerCategory.at(static_cast<u8>(AllocationCategory::Texture)), 1024u);

    inspector.TrackFree(100);
    stats = inspector.GetStats();
    EXPECT_EQ(stats.totalAllocations, 0u);

    inspector.Shutdown();
}

TEST(HeapInspector, MultipleAllocationsPerHeap) {
    HeapInspector inspector;
    inspector.Init(nullptr);

    inspector.TrackAllocation(1, 0, 4096, 0, AllocationCategory::RenderTarget, "rt0");
    inspector.TrackAllocation(2, 4096, 2048, 0, AllocationCategory::DepthStencil, "depth");
    inspector.TrackAllocation(3, 0, 8192, 1, AllocationCategory::StagingBuffer, "staging");

    auto heap0 = inspector.GetHeapInfo(0);
    EXPECT_EQ(heap0.allocationCount, 2u);
    EXPECT_EQ(heap0.usedSize, 4096u + 2048u);

    auto heap1 = inspector.GetHeapInfo(1);
    EXPECT_EQ(heap1.allocationCount, 1u);
    EXPECT_EQ(heap1.usedSize, 8192u);

    inspector.Shutdown();
}

TEST(HeapInspector, VisualizationBlocksWithGaps) {
    HeapInspector inspector;
    inspector.Init(nullptr);

    inspector.TrackAllocation(1, 0, 1024, 0, AllocationCategory::Texture, "tex0");
    inspector.TrackAllocation(2, 2048, 512, 0, AllocationCategory::Texture, "tex1");
    // Gap at offset 1024-2048

    auto blocks = inspector.GetVisualizationBlocks(0);
    EXPECT_GE(blocks.size(), 3u); // tex0, gap, tex1

    bool foundFree = false;
    for (const auto& block : blocks) {
        if (block.free) {
            foundFree = true;
            EXPECT_EQ(block.offset, 1024u);
            EXPECT_EQ(block.size, 1024u);
        }
    }
    EXPECT_TRUE(foundFree);

    inspector.Shutdown();
}

TEST(HeapInspector, LargestAllocations) {
    HeapInspector inspector;
    inspector.Init(nullptr);

    inspector.TrackAllocation(1, 0, 100, 0, AllocationCategory::UniformBuffer, "small");
    inspector.TrackAllocation(2, 100, 10000, 0, AllocationCategory::Texture, "large");
    inspector.TrackAllocation(3, 10100, 5000, 0, AllocationCategory::VertexBuffer, "medium");

    auto largest = inspector.GetLargestAllocations(2);
    EXPECT_EQ(largest.size(), 2u);
    EXPECT_EQ(largest[0].size, 10000u);
    EXPECT_EQ(largest[1].size, 5000u);

    inspector.Shutdown();
}

TEST(HeapInspector, CategoryFiltering) {
    HeapInspector inspector;
    inspector.Init(nullptr);

    inspector.TrackAllocation(1, 0, 1024, 0, AllocationCategory::Texture, "tex");
    inspector.TrackAllocation(2, 1024, 2048, 0, AllocationCategory::StorageBuffer, "ssbo");
    inspector.TrackAllocation(3, 3072, 512, 0, AllocationCategory::Texture, "tex2");

    auto textures = inspector.GetAllocationsByCategory(AllocationCategory::Texture);
    EXPECT_EQ(textures.size(), 2u);

    auto ssbos = inspector.GetAllocationsByCategory(AllocationCategory::StorageBuffer);
    EXPECT_EQ(ssbos.size(), 1u);

    inspector.Shutdown();
}

TEST(HeapInspector, CategoryNames) {
    EXPECT_STREQ(HeapInspector::CategoryName(AllocationCategory::RenderTarget), "RenderTarget");
    EXPECT_STREQ(HeapInspector::CategoryName(AllocationCategory::Texture), "Texture");
    EXPECT_STREQ(HeapInspector::CategoryName(AllocationCategory::StorageBuffer), "StorageBuffer");
    EXPECT_STREQ(HeapInspector::CategoryName(AllocationCategory::Transient), "Transient");
}

TEST(HeapInspector, FragmentationCalculation) {
    HeapInspector inspector;
    inspector.Init(nullptr);

    // Two allocations with a gap between them = fragmentation
    inspector.TrackAllocation(1, 0, 1000, 0, AllocationCategory::Texture, "a");
    inspector.TrackAllocation(2, 5000, 1000, 0, AllocationCategory::Texture, "b");

    auto heap = inspector.GetHeapInfo(0);
    EXPECT_GT(heap.fragmentationPercent, 0.0f);

    inspector.Shutdown();
}

// ─── Query Readback Manager Tests ────────────────────────────────────────

TEST(QueryReadback, InitAndRegisterGroup) {
    QueryReadbackManager mgr;
    QueryReadbackConfig config;
    config.framesInFlight = 3;
    config.timestampPeriodNs = 1.0;
    EXPECT_TRUE(mgr.Init(nullptr, config));

    QueryGroupDesc desc;
    desc.name = "ShadowPass";
    desc.type = QueryType::Timestamp;
    desc.queryCount = 64;

    u32 id = mgr.RegisterGroup(desc);
    EXPECT_EQ(id, 0u);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.activeGroups, 1u);
    EXPECT_EQ(stats.totalQueries, 64u);

    mgr.Shutdown();
}

TEST(QueryReadback, MultipleGroups) {
    QueryReadbackManager mgr;
    mgr.Init(nullptr);

    QueryGroupDesc tsDesc;
    tsDesc.name = "Timestamps";
    tsDesc.type = QueryType::Timestamp;
    tsDesc.queryCount = 128;

    QueryGroupDesc occDesc;
    occDesc.name = "Occlusion";
    occDesc.type = QueryType::Occlusion;
    occDesc.queryCount = 256;

    u32 id0 = mgr.RegisterGroup(tsDesc);
    u32 id1 = mgr.RegisterGroup(occDesc);
    EXPECT_NE(id0, id1);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.activeGroups, 2u);
    EXPECT_EQ(stats.totalQueries, 128u + 256u);

    mgr.Shutdown();
}

TEST(QueryReadback, AggregationEmpty) {
    QueryReadbackManager mgr;
    mgr.Init(nullptr);

    QueryGroupDesc desc;
    desc.name = "Test";
    desc.type = QueryType::Timestamp;
    desc.queryCount = 4;
    u32 id = mgr.RegisterGroup(desc);

    auto agg = mgr.GetAggregation(id, 0);
    EXPECT_EQ(agg.sampleCount, 0u);
    EXPECT_EQ(agg.avgMs, 0.0);

    mgr.Shutdown();
}

TEST(QueryReadback, InvalidGroupReturnsZero) {
    QueryReadbackManager mgr;
    mgr.Init(nullptr);

    auto ts = mgr.GetTimestampMs(999, 0, 1);
    EXPECT_EQ(ts, 0.0);

    auto occ = mgr.GetOcclusionResult(999, 0);
    EXPECT_FALSE(occ.visible);

    mgr.Shutdown();
}

TEST(QueryReadback, CallbackRegistration) {
    QueryReadbackManager mgr;
    mgr.Init(nullptr);

    bool called = false;
    mgr.OnResult([&](const std::string&, u32, f64) { called = true; });

    // Callback won't fire without actual GPU results, but registration should work
    EXPECT_FALSE(called);

    mgr.Shutdown();
}
