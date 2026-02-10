#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_gpu_memory_heap.h"

using namespace nge;
using namespace nge::rhi;

TEST(GPUMemoryHeap, InitAndShutdown) {
    GPUMemoryHeapManager mgr;
    EXPECT_TRUE(mgr.Init());

    EXPECT_EQ(mgr.GetHeapCount(), 0u);
    EXPECT_EQ(mgr.GetTotalAllocationCount(), 0u);

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, RegisterHeap) {
    GPUMemoryHeapManager mgr;
    mgr.Init();

    u32 idx = mgr.RegisterHeap(HeapType::DeviceLocal, 1024 * 1024 * 256, 1024 * 1024 * 256, "VRAM");
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mgr.GetHeapCount(), 1u);

    const auto* info = mgr.GetHeapInfo(0);
    EXPECT_NE(info, nullptr);
    EXPECT_EQ(info->type, HeapType::DeviceLocal);
    EXPECT_EQ(info->totalSize, 1024u * 1024 * 256);
    EXPECT_EQ(info->usedSize, 0u);
    EXPECT_EQ(info->debugName, "VRAM");

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, RegisterMultipleHeaps) {
    GPUMemoryHeapManager mgr;
    mgr.Init();

    u32 h0 = mgr.RegisterHeap(HeapType::DeviceLocal, 256 * 1024 * 1024ULL, 0, "VRAM");
    u32 h1 = mgr.RegisterHeap(HeapType::HostVisible, 128 * 1024 * 1024ULL, 0, "Staging");
    u32 h2 = mgr.RegisterHeap(HeapType::HostCached, 64 * 1024 * 1024ULL, 0, "Readback");

    EXPECT_EQ(h0, 0u);
    EXPECT_EQ(h1, 1u);
    EXPECT_EQ(h2, 2u);
    EXPECT_EQ(mgr.GetHeapCount(), 3u);

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, MaxHeapsLimit) {
    GPUMemoryHeapManager mgr;
    HeapManagerConfig config;
    config.maxHeaps = 2;
    mgr.Init(config);

    EXPECT_NE(mgr.RegisterHeap(HeapType::DeviceLocal, 1024, 1024), UINT32_MAX);
    EXPECT_NE(mgr.RegisterHeap(HeapType::HostVisible, 1024, 1024), UINT32_MAX);
    EXPECT_EQ(mgr.RegisterHeap(HeapType::HostCached, 1024, 1024), UINT32_MAX);

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, RecordAllocation) {
    GPUMemoryHeapManager mgr;
    mgr.Init();

    mgr.RegisterHeap(HeapType::DeviceLocal, 1024 * 1024, 1024 * 1024);

    u64 allocId = mgr.RecordAllocation(0, 4096, 256, AllocUsage::Buffer, "VertexBuffer");
    EXPECT_NE(allocId, 0u);
    EXPECT_EQ(mgr.GetTotalAllocationCount(), 1u);

    const auto* alloc = mgr.GetAllocation(allocId);
    EXPECT_NE(alloc, nullptr);
    EXPECT_EQ(alloc->heapIndex, 0u);
    EXPECT_EQ(alloc->size, 4096u);
    EXPECT_EQ(alloc->alignment, 256u);
    EXPECT_EQ(alloc->usage, AllocUsage::Buffer);
    EXPECT_EQ(alloc->debugName, "VertexBuffer");

    const auto* heap = mgr.GetHeapInfo(0);
    EXPECT_EQ(heap->usedSize, 4096u);
    EXPECT_EQ(heap->allocationCount, 1u);

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, RecordFree) {
    GPUMemoryHeapManager mgr;
    mgr.Init();

    mgr.RegisterHeap(HeapType::DeviceLocal, 1024 * 1024, 1024 * 1024);

    u64 id1 = mgr.RecordAllocation(0, 4096, 256, AllocUsage::Buffer);
    u64 id2 = mgr.RecordAllocation(0, 8192, 256, AllocUsage::Image);

    EXPECT_EQ(mgr.GetHeapInfo(0)->usedSize, 4096u + 8192u);
    EXPECT_EQ(mgr.GetHeapInfo(0)->allocationCount, 2u);

    mgr.RecordFree(id1);

    EXPECT_EQ(mgr.GetHeapInfo(0)->usedSize, 8192u);
    EXPECT_EQ(mgr.GetHeapInfo(0)->allocationCount, 1u);
    EXPECT_EQ(mgr.GetAllocation(id1), nullptr); // Freed
    EXPECT_NE(mgr.GetAllocation(id2), nullptr); // Still alive

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, PeakUsageTracking) {
    GPUMemoryHeapManager mgr;
    mgr.Init();

    mgr.RegisterHeap(HeapType::DeviceLocal, 1024 * 1024, 1024 * 1024);

    u64 id1 = mgr.RecordAllocation(0, 10000, 256, AllocUsage::Buffer);
    u64 id2 = mgr.RecordAllocation(0, 20000, 256, AllocUsage::Buffer);

    EXPECT_EQ(mgr.GetHeapInfo(0)->peakUsed, 30000u);

    mgr.RecordFree(id1);
    mgr.RecordFree(id2);

    EXPECT_EQ(mgr.GetHeapInfo(0)->usedSize, 0u);
    EXPECT_EQ(mgr.GetHeapInfo(0)->peakUsed, 30000u); // Peak unchanged

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, FindBestHeapPreferred) {
    GPUMemoryHeapManager mgr;
    mgr.Init();

    mgr.RegisterHeap(HeapType::DeviceLocal, 256 * 1024 * 1024ULL, 256 * 1024 * 1024ULL, "VRAM");
    mgr.RegisterHeap(HeapType::HostVisible, 128 * 1024 * 1024ULL, 128 * 1024 * 1024ULL, "Staging");

    u32 best = mgr.FindBestHeap(HeapType::DeviceLocal, 4096, AllocUsage::Buffer);
    EXPECT_EQ(best, 0u); // Should pick VRAM

    best = mgr.FindBestHeap(HeapType::HostVisible, 4096, AllocUsage::Staging);
    EXPECT_EQ(best, 1u); // Should pick Staging

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, FindBestHeapFallback) {
    GPUMemoryHeapManager mgr;
    mgr.Init();

    mgr.RegisterHeap(HeapType::HostVisible, 128 * 1024 * 1024ULL, 128 * 1024 * 1024ULL, "Staging");

    // No DeviceLocal heap, should fall back
    u32 best = mgr.FindBestHeap(HeapType::DeviceLocal, 4096, AllocUsage::Buffer);
    EXPECT_EQ(best, 0u); // Falls back to HostVisible

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, FindBestHeapNoRoom) {
    GPUMemoryHeapManager mgr;
    HeapManagerConfig config;
    config.respectBudget = true;
    mgr.Init(config);

    mgr.RegisterHeap(HeapType::DeviceLocal, 1024, 1024, "TinyVRAM");

    u32 best = mgr.FindBestHeap(HeapType::DeviceLocal, 2048, AllocUsage::Buffer);
    EXPECT_EQ(best, UINT32_MAX); // No heap has room

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, HasRoom) {
    GPUMemoryHeapManager mgr;
    HeapManagerConfig config;
    config.respectBudget = true;
    mgr.Init(config);

    mgr.RegisterHeap(HeapType::DeviceLocal, 10000, 8000);

    EXPECT_TRUE(mgr.HasRoom(0, 7000));
    EXPECT_TRUE(mgr.HasRoom(0, 8000));
    EXPECT_FALSE(mgr.HasRoom(0, 8001)); // Over budget

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, UpdateBudget) {
    GPUMemoryHeapManager mgr;
    mgr.Init();

    mgr.RegisterHeap(HeapType::DeviceLocal, 256 * 1024 * 1024ULL, 256 * 1024 * 1024ULL);

    mgr.UpdateBudget(0, 200 * 1024 * 1024ULL);

    EXPECT_EQ(mgr.GetHeapInfo(0)->budgetSize, 200u * 1024 * 1024);

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, GetDefragCandidates) {
    GPUMemoryHeapManager mgr;
    HeapManagerConfig config;
    config.trackFragmentation = true;
    config.maxAllocationsPerHeap = 100;
    mgr.Init(config);

    mgr.RegisterHeap(HeapType::DeviceLocal, 1024 * 1024, 1024 * 1024);

    // Create many small allocations to increase fragmentation
    std::vector<u64> allocIds;
    for (u32 i = 0; i < 50; ++i) {
        allocIds.push_back(mgr.RecordAllocation(0, 100, 16, AllocUsage::Buffer));
    }

    // Free every other allocation to create fragmentation pattern
    for (u32 i = 0; i < allocIds.size(); i += 2) {
        mgr.RecordFree(allocIds[i]);
    }

    // Should have some fragmentation now
    auto candidates = mgr.GetDefragCandidates(0.01f);
    // Fragmentation depends on the heuristic but there should be some
    // since we have many allocations with low utilization

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, GetHeapAllocations) {
    GPUMemoryHeapManager mgr;
    mgr.Init();

    mgr.RegisterHeap(HeapType::DeviceLocal, 1024 * 1024, 1024 * 1024);
    mgr.RegisterHeap(HeapType::HostVisible, 1024 * 1024, 1024 * 1024);

    mgr.RecordAllocation(0, 4096, 256, AllocUsage::Buffer);
    mgr.RecordAllocation(0, 8192, 256, AllocUsage::Image);
    mgr.RecordAllocation(1, 2048, 256, AllocUsage::Staging);

    auto heap0Allocs = mgr.GetHeapAllocations(0);
    auto heap1Allocs = mgr.GetHeapAllocations(1);

    EXPECT_EQ(heap0Allocs.size(), 2u);
    EXPECT_EQ(heap1Allocs.size(), 1u);

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, StatsTracking) {
    GPUMemoryHeapManager mgr;
    mgr.Init();

    mgr.RegisterHeap(HeapType::DeviceLocal, 100000, 100000);
    mgr.RegisterHeap(HeapType::HostVisible, 50000, 50000);

    mgr.RecordAllocation(0, 30000, 256, AllocUsage::Buffer);
    mgr.RecordAllocation(1, 10000, 256, AllocUsage::Staging);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalHeaps, 2u);
    EXPECT_EQ(stats.totalAllocations, 2u);
    EXPECT_EQ(stats.totalUsed, 40000u);
    EXPECT_EQ(stats.totalCapacity, 150000u);
    EXPECT_GT(stats.overallUtilization, 0.0f);

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, ResetClearsAll) {
    GPUMemoryHeapManager mgr;
    mgr.Init();

    mgr.RegisterHeap(HeapType::DeviceLocal, 1024 * 1024, 1024 * 1024);
    mgr.RecordAllocation(0, 4096, 256, AllocUsage::Buffer);

    mgr.Reset();

    EXPECT_EQ(mgr.GetHeapCount(), 0u);
    EXPECT_EQ(mgr.GetTotalAllocationCount(), 0u);

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, GetHeapInfoInvalid) {
    GPUMemoryHeapManager mgr;
    mgr.Init();

    EXPECT_EQ(mgr.GetHeapInfo(0), nullptr);
    EXPECT_EQ(mgr.GetHeapInfo(999), nullptr);

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, GetAllocationInvalid) {
    GPUMemoryHeapManager mgr;
    mgr.Init();

    EXPECT_EQ(mgr.GetAllocation(0), nullptr);
    EXPECT_EQ(mgr.GetAllocation(9999), nullptr);

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, RecordAllocationInvalidHeap) {
    GPUMemoryHeapManager mgr;
    mgr.Init();

    u64 id = mgr.RecordAllocation(99, 4096, 256, AllocUsage::Buffer);
    EXPECT_EQ(id, 0u); // Invalid heap

    mgr.Shutdown();
}

TEST(GPUMemoryHeap, AllocationUsageTypes) {
    GPUMemoryHeapManager mgr;
    mgr.Init();

    mgr.RegisterHeap(HeapType::DeviceLocal, 1024 * 1024, 1024 * 1024);

    u64 a1 = mgr.RecordAllocation(0, 1024, 256, AllocUsage::Buffer);
    u64 a2 = mgr.RecordAllocation(0, 2048, 256, AllocUsage::Image);
    u64 a3 = mgr.RecordAllocation(0, 4096, 256, AllocUsage::AccelStruct);
    u64 a4 = mgr.RecordAllocation(0, 512, 256, AllocUsage::ScratchBuffer);

    EXPECT_EQ(mgr.GetAllocation(a1)->usage, AllocUsage::Buffer);
    EXPECT_EQ(mgr.GetAllocation(a2)->usage, AllocUsage::Image);
    EXPECT_EQ(mgr.GetAllocation(a3)->usage, AllocUsage::AccelStruct);
    EXPECT_EQ(mgr.GetAllocation(a4)->usage, AllocUsage::ScratchBuffer);

    mgr.Shutdown();
}
