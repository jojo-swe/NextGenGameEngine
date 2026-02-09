#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_queue_family_arbiter.h"

using namespace nge::rhi;

TEST(QueueFamilyArbiter, InitAndShutdown) {
    QueueFamilyArbiter arbiter;
    EXPECT_TRUE(arbiter.Init());

    auto stats = arbiter.GetStats();
    EXPECT_EQ(stats.totalFamilies, 0u);
    EXPECT_EQ(stats.totalQueues, 0u);

    arbiter.Shutdown();
}

TEST(QueueFamilyArbiter, RegisterFamilyAndQueue) {
    QueueFamilyArbiter arbiter;
    arbiter.Init();

    QueueFamilyInfo gfxFamily;
    gfxFamily.familyIndex = 0;
    gfxFamily.capabilities = QueueCapability::Graphics | QueueCapability::Compute | QueueCapability::Transfer;
    gfxFamily.queueCount = 2;
    gfxFamily.debugName = "Graphics+Compute+Transfer";

    arbiter.RegisterFamily(gfxFamily);
    arbiter.RegisterQueue(0, 0, "GfxQueue0");
    arbiter.RegisterQueue(0, 1, "GfxQueue1");

    auto stats = arbiter.GetStats();
    EXPECT_EQ(stats.totalFamilies, 1u);
    EXPECT_EQ(stats.totalQueues, 2u);
    EXPECT_EQ(stats.graphicsQueues, 2u);

    arbiter.Shutdown();
}

TEST(QueueFamilyArbiter, RequestGraphicsQueue) {
    QueueFamilyArbiter arbiter;
    arbiter.Init();

    QueueFamilyInfo family;
    family.familyIndex = 0;
    family.capabilities = QueueCapability::Graphics | QueueCapability::Compute | QueueCapability::Transfer;
    family.queueCount = 1;
    family.debugName = "Universal";

    arbiter.RegisterFamily(family);
    arbiter.RegisterQueue(0, 0, "MainGfx");

    WorkRequest req;
    req.requiredCaps = QueueCapability::Graphics;
    req.priority = WorkPriority::Normal;
    req.estimatedCost = 100;
    req.preferDedicated = false;

    auto assignment = arbiter.RequestQueue(req);
    EXPECT_EQ(assignment.familyIndex, 0u);
    EXPECT_EQ(assignment.queueIndex, 0u);

    arbiter.Shutdown();
}

TEST(QueueFamilyArbiter, PreferAsyncComputeFamily) {
    QueueFamilyArbiter arbiter;
    QueueFamilyArbiterConfig config;
    config.preferAsyncCompute = true;
    arbiter.Init(config);

    // Family 0: Graphics + Compute + Transfer (universal)
    QueueFamilyInfo gfx;
    gfx.familyIndex = 0;
    gfx.capabilities = QueueCapability::Graphics | QueueCapability::Compute | QueueCapability::Transfer;
    gfx.queueCount = 1;
    gfx.debugName = "Universal";

    // Family 1: Compute-only (async compute)
    QueueFamilyInfo asyncCompute;
    asyncCompute.familyIndex = 1;
    asyncCompute.capabilities = QueueCapability::Compute | QueueCapability::Transfer;
    asyncCompute.queueCount = 1;
    asyncCompute.debugName = "AsyncCompute";

    arbiter.RegisterFamily(gfx);
    arbiter.RegisterFamily(asyncCompute);
    arbiter.RegisterQueue(0, 0, "GfxQ");
    arbiter.RegisterQueue(1, 0, "ComputeQ");

    WorkRequest req;
    req.requiredCaps = QueueCapability::Compute;
    req.priority = WorkPriority::Normal;
    req.estimatedCost = 50;
    req.preferDedicated = true;

    auto assignment = arbiter.RequestQueue(req);
    EXPECT_EQ(assignment.familyIndex, 1u); // Should prefer async compute

    arbiter.Shutdown();
}

TEST(QueueFamilyArbiter, PreferDedicatedTransfer) {
    QueueFamilyArbiter arbiter;
    QueueFamilyArbiterConfig config;
    config.preferDedicatedTransfer = true;
    arbiter.Init(config);

    QueueFamilyInfo gfx;
    gfx.familyIndex = 0;
    gfx.capabilities = QueueCapability::Graphics | QueueCapability::Compute | QueueCapability::Transfer;
    gfx.queueCount = 1;

    QueueFamilyInfo transfer;
    transfer.familyIndex = 2;
    transfer.capabilities = QueueCapability::Transfer;
    transfer.queueCount = 1;
    transfer.debugName = "DMA";

    arbiter.RegisterFamily(gfx);
    arbiter.RegisterFamily(transfer);
    arbiter.RegisterQueue(0, 0);
    arbiter.RegisterQueue(2, 0, "DMAQueue");

    WorkRequest req;
    req.requiredCaps = QueueCapability::Transfer;
    req.priority = WorkPriority::Normal;
    req.estimatedCost = 200;
    req.preferDedicated = false;

    auto assignment = arbiter.RequestQueue(req);
    EXPECT_EQ(assignment.familyIndex, 2u); // Should prefer dedicated transfer

    arbiter.Shutdown();
}

TEST(QueueFamilyArbiter, LoadBalancing) {
    QueueFamilyArbiter arbiter;
    QueueFamilyArbiterConfig config;
    config.enableLoadBalancing = true;
    arbiter.Init(config);

    QueueFamilyInfo family;
    family.familyIndex = 0;
    family.capabilities = QueueCapability::Graphics | QueueCapability::Compute | QueueCapability::Transfer;
    family.queueCount = 2;

    arbiter.RegisterFamily(family);
    arbiter.RegisterQueue(0, 0, "Q0");
    arbiter.RegisterQueue(0, 1, "Q1");

    // Load up queue 0
    arbiter.RecordSubmission(0, 0, 800);

    WorkRequest req;
    req.requiredCaps = QueueCapability::Graphics;
    req.priority = WorkPriority::Normal;
    req.estimatedCost = 100;
    req.preferDedicated = false;

    auto assignment = arbiter.RequestQueue(req);
    EXPECT_EQ(assignment.queueIndex, 1u); // Should prefer less loaded queue

    arbiter.Shutdown();
}

TEST(QueueFamilyArbiter, GetAsyncComputeFamily) {
    QueueFamilyArbiter arbiter;
    arbiter.Init();

    QueueFamilyInfo gfx;
    gfx.familyIndex = 0;
    gfx.capabilities = QueueCapability::Graphics | QueueCapability::Compute;
    gfx.queueCount = 1;

    QueueFamilyInfo asyncComp;
    asyncComp.familyIndex = 1;
    asyncComp.capabilities = QueueCapability::Compute;
    asyncComp.queueCount = 1;

    arbiter.RegisterFamily(gfx);
    arbiter.RegisterFamily(asyncComp);

    EXPECT_EQ(arbiter.GetAsyncComputeFamily(), 1);

    arbiter.Shutdown();
}

TEST(QueueFamilyArbiter, GetDedicatedTransferFamily) {
    QueueFamilyArbiter arbiter;
    arbiter.Init();

    QueueFamilyInfo gfx;
    gfx.familyIndex = 0;
    gfx.capabilities = QueueCapability::Graphics | QueueCapability::Compute | QueueCapability::Transfer;
    gfx.queueCount = 1;

    QueueFamilyInfo dma;
    dma.familyIndex = 2;
    dma.capabilities = QueueCapability::Transfer;
    dma.queueCount = 1;

    arbiter.RegisterFamily(gfx);
    arbiter.RegisterFamily(dma);

    EXPECT_EQ(arbiter.GetDedicatedTransferFamily(), 2);

    arbiter.Shutdown();
}

TEST(QueueFamilyArbiter, NoAsyncComputeReturnsNegative) {
    QueueFamilyArbiter arbiter;
    arbiter.Init();

    QueueFamilyInfo gfx;
    gfx.familyIndex = 0;
    gfx.capabilities = QueueCapability::Graphics | QueueCapability::Compute;
    gfx.queueCount = 1;

    arbiter.RegisterFamily(gfx);

    EXPECT_EQ(arbiter.GetAsyncComputeFamily(), -1);
    EXPECT_EQ(arbiter.GetDedicatedTransferFamily(), -1);

    arbiter.Shutdown();
}

TEST(QueueFamilyArbiter, GetFamiliesWithCapability) {
    QueueFamilyArbiter arbiter;
    arbiter.Init();

    QueueFamilyInfo f0, f1, f2;
    f0.familyIndex = 0; f0.capabilities = QueueCapability::Graphics | QueueCapability::Compute | QueueCapability::Transfer; f0.queueCount = 1;
    f1.familyIndex = 1; f1.capabilities = QueueCapability::Compute; f1.queueCount = 1;
    f2.familyIndex = 2; f2.capabilities = QueueCapability::Transfer; f2.queueCount = 1;

    arbiter.RegisterFamily(f0);
    arbiter.RegisterFamily(f1);
    arbiter.RegisterFamily(f2);

    auto computeFamilies = arbiter.GetFamiliesWithCapability(QueueCapability::Compute);
    EXPECT_EQ(computeFamilies.size(), 2u); // Family 0 and 1

    auto transferFamilies = arbiter.GetFamiliesWithCapability(QueueCapability::Transfer);
    EXPECT_EQ(transferFamilies.size(), 2u); // Family 0 and 2

    auto graphicsFamilies = arbiter.GetFamiliesWithCapability(QueueCapability::Graphics);
    EXPECT_EQ(graphicsFamilies.size(), 1u); // Only family 0

    arbiter.Shutdown();
}

TEST(QueueFamilyArbiter, RecordSubmissionAndCompletion) {
    QueueFamilyArbiter arbiter;
    arbiter.Init();

    QueueFamilyInfo family;
    family.familyIndex = 0;
    family.capabilities = QueueCapability::Graphics;
    family.queueCount = 1;

    arbiter.RegisterFamily(family);
    arbiter.RegisterQueue(0, 0);

    EXPECT_NEAR(arbiter.GetQueueLoad(0, 0), 0.0f, 0.01f);

    arbiter.RecordSubmission(0, 0, 500);
    EXPECT_GT(arbiter.GetQueueLoad(0, 0), 0.0f);

    arbiter.RecordCompletion(0, 0, 500);
    EXPECT_NEAR(arbiter.GetQueueLoad(0, 0), 0.0f, 0.01f);

    arbiter.Shutdown();
}

TEST(QueueFamilyArbiter, ResetLoads) {
    QueueFamilyArbiter arbiter;
    arbiter.Init();

    QueueFamilyInfo family;
    family.familyIndex = 0;
    family.capabilities = QueueCapability::Graphics;
    family.queueCount = 1;

    arbiter.RegisterFamily(family);
    arbiter.RegisterQueue(0, 0);

    arbiter.RecordSubmission(0, 0, 800);
    EXPECT_GT(arbiter.GetQueueLoad(0, 0), 0.0f);

    arbiter.ResetLoads();
    EXPECT_NEAR(arbiter.GetQueueLoad(0, 0), 0.0f, 0.01f);

    arbiter.Shutdown();
}

TEST(QueueFamilyArbiter, SetDedicated) {
    QueueFamilyArbiter arbiter;
    arbiter.Init();

    QueueFamilyInfo family;
    family.familyIndex = 0;
    family.capabilities = QueueCapability::Graphics | QueueCapability::Compute;
    family.queueCount = 2;

    arbiter.RegisterFamily(family);
    arbiter.RegisterQueue(0, 0, "General");
    arbiter.RegisterQueue(0, 1, "Dedicated");

    arbiter.SetDedicated(0, 1, true);

    // Non-dedicated request should avoid the dedicated queue
    WorkRequest req;
    req.requiredCaps = QueueCapability::Graphics;
    req.priority = WorkPriority::Normal;
    req.estimatedCost = 100;
    req.preferDedicated = false;

    auto assignment = arbiter.RequestQueue(req);
    EXPECT_EQ(assignment.queueIndex, 0u); // Should skip dedicated

    arbiter.Shutdown();
}

TEST(QueueFamilyArbiter, StatsTracking) {
    QueueFamilyArbiter arbiter;
    arbiter.Init();

    QueueFamilyInfo f0, f1;
    f0.familyIndex = 0; f0.capabilities = QueueCapability::Graphics | QueueCapability::Compute | QueueCapability::Transfer; f0.queueCount = 2;
    f1.familyIndex = 1; f1.capabilities = QueueCapability::Compute; f1.queueCount = 1;

    arbiter.RegisterFamily(f0);
    arbiter.RegisterFamily(f1);
    arbiter.RegisterQueue(0, 0);
    arbiter.RegisterQueue(0, 1);
    arbiter.RegisterQueue(1, 0);

    auto stats = arbiter.GetStats();
    EXPECT_EQ(stats.totalFamilies, 2u);
    EXPECT_EQ(stats.totalQueues, 3u);
    EXPECT_EQ(stats.graphicsQueues, 2u);
    EXPECT_EQ(stats.computeOnlyQueues, 1u);

    arbiter.Shutdown();
}
