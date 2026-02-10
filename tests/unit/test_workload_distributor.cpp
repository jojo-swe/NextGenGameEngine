#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_workload_distributor.h"

using namespace nge;
using namespace nge::rhi;

TEST(WorkloadDistributor, InitAndShutdown) {
    WorkloadDistributor dist;
    EXPECT_TRUE(dist.Init());
    EXPECT_EQ(dist.GetQueueCount(), 0u);
    EXPECT_EQ(dist.GetPendingWorkloadCount(), 0u);
    dist.Shutdown();
}

TEST(WorkloadDistributor, RegisterQueue) {
    WorkloadDistributor dist;
    dist.Init();

    u32 id = dist.RegisterQueue(QueueType::Graphics, 0, 1.0f, "MainGraphics");
    EXPECT_NE(id, UINT32_MAX);
    EXPECT_EQ(dist.GetQueueCount(), 1u);

    const auto* info = dist.GetQueueInfo(id);
    EXPECT_NE(info, nullptr);
    EXPECT_EQ(info->type, QueueType::Graphics);
    EXPECT_EQ(info->familyIndex, 0u);
    EXPECT_FLOAT_EQ(info->priority, 1.0f);
    EXPECT_EQ(info->debugName, "MainGraphics");

    dist.Shutdown();
}

TEST(WorkloadDistributor, RegisterMultipleQueues) {
    WorkloadDistributor dist;
    dist.Init();

    u32 gfx = dist.RegisterQueue(QueueType::Graphics, 0, 1.0f, "Graphics");
    u32 comp = dist.RegisterQueue(QueueType::Compute, 1, 0.8f, "AsyncCompute");
    u32 xfer = dist.RegisterQueue(QueueType::Transfer, 2, 0.5f, "DMA");

    EXPECT_EQ(dist.GetQueueCount(), 3u);
    EXPECT_NE(gfx, comp);
    EXPECT_NE(comp, xfer);

    auto gfxQueues = dist.GetQueuesOfType(QueueType::Graphics);
    EXPECT_EQ(gfxQueues.size(), 1u);

    auto compQueues = dist.GetQueuesOfType(QueueType::Compute);
    EXPECT_EQ(compQueues.size(), 1u);

    dist.Shutdown();
}

TEST(WorkloadDistributor, MaxQueuesLimit) {
    WorkloadDistributor dist;
    DistributorConfig config;
    config.maxQueues = 2;
    dist.Init(config);

    EXPECT_NE(dist.RegisterQueue(QueueType::Graphics, 0), UINT32_MAX);
    EXPECT_NE(dist.RegisterQueue(QueueType::Compute, 1), UINT32_MAX);
    EXPECT_EQ(dist.RegisterQueue(QueueType::Transfer, 2), UINT32_MAX);

    dist.Shutdown();
}

TEST(WorkloadDistributor, SubmitWorkload) {
    WorkloadDistributor dist;
    dist.Init();

    dist.RegisterQueue(QueueType::Graphics, 0);

    u32 wId = dist.SubmitWorkload(QueueType::Graphics, WorkloadPriority::Normal, 1000, "DrawPass");
    EXPECT_NE(wId, UINT32_MAX);
    EXPECT_EQ(dist.GetPendingWorkloadCount(), 1u);

    dist.Shutdown();
}

TEST(WorkloadDistributor, ScheduleAssignsToCorrectQueue) {
    WorkloadDistributor dist;
    dist.Init();

    u32 gfx = dist.RegisterQueue(QueueType::Graphics, 0, 1.0f, "Graphics");
    u32 comp = dist.RegisterQueue(QueueType::Compute, 1, 1.0f, "Compute");

    dist.SubmitWorkload(QueueType::Graphics, WorkloadPriority::Normal, 1000, "DrawPass");
    dist.SubmitWorkload(QueueType::Compute, WorkloadPriority::Normal, 500, "CullPass");

    auto results = dist.Schedule();
    EXPECT_EQ(results.size(), 2u);

    // Graphics workload should go to graphics queue
    bool gfxFound = false, compFound = false;
    for (const auto& r : results) {
        if (r.assignedQueueId == gfx) gfxFound = true;
        if (r.assignedQueueId == comp) compFound = true;
    }
    EXPECT_TRUE(gfxFound);
    EXPECT_TRUE(compFound);

    EXPECT_EQ(dist.GetPendingWorkloadCount(), 0u); // Cleared after schedule

    dist.Shutdown();
}

TEST(WorkloadDistributor, PriorityOrdering) {
    WorkloadDistributor dist;
    dist.Init();

    dist.RegisterQueue(QueueType::Graphics, 0);

    dist.SubmitWorkload(QueueType::Graphics, WorkloadPriority::Low, 1000, "LowPri");
    dist.SubmitWorkload(QueueType::Graphics, WorkloadPriority::Critical, 500, "Critical");
    dist.SubmitWorkload(QueueType::Graphics, WorkloadPriority::Normal, 800, "NormalPri");

    auto results = dist.Schedule();
    EXPECT_EQ(results.size(), 3u);

    // Critical should be scheduled first (lowest estimatedStartNs)
    EXPECT_EQ(results[0].estimatedStartNs, 0u);

    dist.Shutdown();
}

TEST(WorkloadDistributor, LoadBalancing) {
    WorkloadDistributor dist;
    DistributorConfig config;
    config.enableLoadBalancing = true;
    dist.Init(config);

    u32 comp0 = dist.RegisterQueue(QueueType::Compute, 1, 1.0f, "Compute0");
    u32 comp1 = dist.RegisterQueue(QueueType::Compute, 1, 1.0f, "Compute1");

    // Submit 4 compute workloads
    for (u32 i = 0; i < 4; ++i) {
        dist.SubmitWorkload(QueueType::Compute, WorkloadPriority::Normal, 1000, "ComputeWork");
    }

    auto results = dist.Schedule();
    EXPECT_EQ(results.size(), 4u);

    // Count per queue
    u32 count0 = 0, count1 = 0;
    for (const auto& r : results) {
        if (r.assignedQueueId == comp0) count0++;
        if (r.assignedQueueId == comp1) count1++;
    }

    // Should be roughly balanced
    EXPECT_GE(count0, 1u);
    EXPECT_GE(count1, 1u);

    dist.Shutdown();
}

TEST(WorkloadDistributor, ComputeFallbackToGraphics) {
    WorkloadDistributor dist;
    DistributorConfig config;
    config.preferAsyncCompute = true;
    dist.Init(config);

    // Only graphics queue available
    u32 gfx = dist.RegisterQueue(QueueType::Graphics, 0);

    dist.SubmitWorkload(QueueType::Compute, WorkloadPriority::Normal, 1000, "ComputeWork");

    auto results = dist.Schedule();
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].assignedQueueId, gfx); // Fell back to graphics

    dist.Shutdown();
}

TEST(WorkloadDistributor, TransferPrefersDedicated) {
    WorkloadDistributor dist;
    DistributorConfig config;
    config.preferDedicatedTransfer = true;
    dist.Init(config);

    u32 gfx = dist.RegisterQueue(QueueType::Graphics, 0);
    u32 xfer = dist.RegisterQueue(QueueType::Transfer, 2);

    WorkloadDesc desc;
    desc.preferredQueue = QueueType::Transfer;
    desc.priority = WorkloadPriority::Normal;
    desc.estimatedDurationNs = 500;
    desc.requiresGraphics = false;
    desc.requiresTransfer = true;
    desc.debugName = "Upload";
    dist.SubmitWorkload(desc);

    auto results = dist.Schedule();
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].assignedQueueId, xfer); // Used dedicated transfer

    dist.Shutdown();
}

TEST(WorkloadDistributor, MarkCompleted) {
    WorkloadDistributor dist;
    dist.Init();

    u32 gfx = dist.RegisterQueue(QueueType::Graphics, 0);
    u32 wId = dist.SubmitWorkload(QueueType::Graphics, WorkloadPriority::Normal, 1000, "Draw");

    dist.Schedule();
    EXPECT_EQ(dist.GetQueueInfo(gfx)->pendingSubmissions, 1u);

    dist.MarkCompleted(wId);
    EXPECT_EQ(dist.GetQueueInfo(gfx)->pendingSubmissions, 0u);

    dist.Shutdown();
}

TEST(WorkloadDistributor, GetQueueLoad) {
    WorkloadDistributor dist;
    dist.Init();

    u32 gfx = dist.RegisterQueue(QueueType::Graphics, 0);

    EXPECT_EQ(dist.GetQueueLoad(gfx), 0u);

    dist.SubmitWorkload(QueueType::Graphics, WorkloadPriority::Normal, 5000);
    dist.SubmitWorkload(QueueType::Graphics, WorkloadPriority::Normal, 3000);
    dist.Schedule();

    EXPECT_EQ(dist.GetQueueLoad(gfx), 8000u);

    dist.Shutdown();
}

TEST(WorkloadDistributor, FindBestQueue) {
    WorkloadDistributor dist;
    dist.Init();

    u32 gfx = dist.RegisterQueue(QueueType::Graphics, 0);
    u32 comp = dist.RegisterQueue(QueueType::Compute, 1);

    EXPECT_EQ(dist.FindBestQueue(QueueType::Graphics), gfx);
    EXPECT_EQ(dist.FindBestQueue(QueueType::Compute), comp);
    EXPECT_EQ(dist.FindBestQueue(QueueType::Transfer), UINT32_MAX); // None registered

    dist.Shutdown();
}

TEST(WorkloadDistributor, MaxWorkloadsLimit) {
    WorkloadDistributor dist;
    DistributorConfig config;
    config.maxWorkloadsPerFrame = 3;
    dist.Init(config);

    dist.RegisterQueue(QueueType::Graphics, 0);

    for (u32 i = 0; i < 3; ++i) {
        EXPECT_NE(dist.SubmitWorkload(QueueType::Graphics, WorkloadPriority::Normal, 100), UINT32_MAX);
    }
    EXPECT_EQ(dist.SubmitWorkload(QueueType::Graphics, WorkloadPriority::Normal, 100), UINT32_MAX);

    dist.Shutdown();
}

TEST(WorkloadDistributor, ClearPending) {
    WorkloadDistributor dist;
    dist.Init();

    dist.RegisterQueue(QueueType::Graphics, 0);
    dist.SubmitWorkload(QueueType::Graphics, WorkloadPriority::Normal, 1000);
    dist.SubmitWorkload(QueueType::Graphics, WorkloadPriority::Normal, 2000);

    EXPECT_EQ(dist.GetPendingWorkloadCount(), 2u);
    dist.ClearPending();
    EXPECT_EQ(dist.GetPendingWorkloadCount(), 0u);

    dist.Shutdown();
}

TEST(WorkloadDistributor, StatsTracking) {
    WorkloadDistributor dist;
    dist.Init();

    dist.RegisterQueue(QueueType::Graphics, 0);
    dist.RegisterQueue(QueueType::Compute, 1);

    dist.SubmitWorkload(QueueType::Graphics, WorkloadPriority::Normal, 2000);
    dist.SubmitWorkload(QueueType::Compute, WorkloadPriority::Normal, 1000);
    dist.Schedule();

    auto stats = dist.GetStats();
    EXPECT_EQ(stats.totalQueues, 2u);
    EXPECT_EQ(stats.totalWorkloadsScheduled, 2u);
    EXPECT_EQ(stats.workloadsOnGraphics, 1u);
    EXPECT_EQ(stats.workloadsOnCompute, 1u);
    EXPECT_GT(stats.totalEstimatedGpuTimeNs, 0u);

    dist.Shutdown();
}

TEST(WorkloadDistributor, ResetClearsAll) {
    WorkloadDistributor dist;
    dist.Init();

    dist.RegisterQueue(QueueType::Graphics, 0);
    dist.SubmitWorkload(QueueType::Graphics, WorkloadPriority::Normal, 1000);
    dist.Schedule();

    dist.Reset();

    EXPECT_EQ(dist.GetQueueCount(), 0u);
    EXPECT_EQ(dist.GetPendingWorkloadCount(), 0u);
    auto stats = dist.GetStats();
    EXPECT_EQ(stats.totalWorkloadsScheduled, 0u);

    dist.Shutdown();
}

TEST(WorkloadDistributor, GetQueueInfoInvalid) {
    WorkloadDistributor dist;
    dist.Init();

    EXPECT_EQ(dist.GetQueueInfo(999), nullptr);
    EXPECT_EQ(dist.GetQueueLoad(999), 0u);

    dist.Shutdown();
}

TEST(WorkloadDistributor, EstimatedTiming) {
    WorkloadDistributor dist;
    dist.Init();

    dist.RegisterQueue(QueueType::Graphics, 0);

    dist.SubmitWorkload(QueueType::Graphics, WorkloadPriority::Critical, 1000, "First");
    dist.SubmitWorkload(QueueType::Graphics, WorkloadPriority::Normal, 2000, "Second");

    auto results = dist.Schedule();
    EXPECT_EQ(results.size(), 2u);

    // Critical scheduled first: start=0, end=1000
    // Normal scheduled second: start=1000, end=3000
    EXPECT_EQ(results[0].estimatedStartNs, 0u);
    EXPECT_EQ(results[0].estimatedEndNs, 1000u);
    EXPECT_EQ(results[1].estimatedStartNs, 1000u);
    EXPECT_EQ(results[1].estimatedEndNs, 3000u);

    dist.Shutdown();
}
