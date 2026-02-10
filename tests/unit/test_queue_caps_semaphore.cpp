#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_queue_capabilities.h"
#include "engine/rhi/common/rhi_timeline_semaphore_pool.h"

using namespace nge;
using namespace nge::rhi;

// ─── Queue Capability Manager Tests ──────────────────────────────────────

TEST(QueueCapabilities, InitEnumeratesFamilies) {
    QueueCapabilityManager mgr;
    EXPECT_TRUE(mgr.Init(nullptr));

    const auto& families = mgr.GetFamilies();
    EXPECT_GE(families.size(), 2u); // At least graphics + compute stubs

    auto stats = mgr.GetStats();
    EXPECT_GE(stats.totalFamilies, 2u);
    EXPECT_GT(stats.totalQueues, 0u);

    mgr.Shutdown();
}

TEST(QueueCapabilities, GraphicsQueueFound) {
    QueueCapabilityManager mgr;
    mgr.Init(nullptr);

    auto gfx = mgr.GetGraphicsQueue();
    EXPECT_TRUE(gfx.valid);
    EXPECT_TRUE(mgr.FamilySupports(gfx.familyIndex, QueueCapability::Graphics));

    mgr.Shutdown();
}

TEST(QueueCapabilities, AsyncComputeQueue) {
    QueueCapabilityManager mgr;
    mgr.Init(nullptr);

    auto asyncCompute = mgr.GetAsyncComputeQueue();
    EXPECT_TRUE(asyncCompute.valid);
    EXPECT_TRUE(mgr.FamilySupports(asyncCompute.familyIndex, QueueCapability::Compute));

    // Should prefer a dedicated compute queue (no graphics)
    if (asyncCompute.dedicated) {
        EXPECT_FALSE(mgr.FamilySupports(asyncCompute.familyIndex, QueueCapability::Graphics));
    }

    mgr.Shutdown();
}

TEST(QueueCapabilities, DedicatedTransferQueue) {
    QueueCapabilityManager mgr;
    mgr.Init(nullptr);

    auto transfer = mgr.GetTransferQueue();
    EXPECT_TRUE(transfer.valid);
    EXPECT_TRUE(mgr.FamilySupports(transfer.familyIndex, QueueCapability::Transfer));

    auto stats = mgr.GetStats();
    EXPECT_TRUE(stats.hasDedicatedTransfer);

    mgr.Shutdown();
}

TEST(QueueCapabilities, FamilySupportCheck) {
    QueueCapabilityManager mgr;
    mgr.Init(nullptr);

    // Family 0 (graphics) should support graphics
    EXPECT_TRUE(mgr.FamilySupports(0, QueueCapability::Graphics));

    // Invalid family index
    EXPECT_FALSE(mgr.FamilySupports(999, QueueCapability::Graphics));

    mgr.Shutdown();
}

TEST(QueueCapabilities, QueueCountAndTimestamp) {
    QueueCapabilityManager mgr;
    mgr.Init(nullptr);

    auto gfx = mgr.GetGraphicsQueue();
    EXPECT_GT(mgr.GetQueueCount(gfx.familyIndex), 0u);
    EXPECT_GT(mgr.GetTimestampBits(gfx.familyIndex), 0u);

    // Invalid family
    EXPECT_EQ(mgr.GetQueueCount(999), 0u);
    EXPECT_EQ(mgr.GetTimestampBits(999), 0u);

    mgr.Shutdown();
}

TEST(QueueCapabilities, FindBestQueuePrefersDedicated) {
    QueueCapabilityManager mgr;
    mgr.Init(nullptr);

    // Transfer-only queue should be preferred for transfer
    auto transfer = mgr.FindBestQueue(QueueCapability::Transfer);
    EXPECT_TRUE(transfer.valid);

    // The dedicated transfer queue should have the fewest extra capabilities
    auto dedicated = mgr.FindDedicatedQueue(QueueCapability::Transfer);
    if (dedicated.valid) {
        EXPECT_TRUE(dedicated.dedicated);
    }

    mgr.Shutdown();
}

TEST(QueueCapabilities, StatsReflectCapabilities) {
    QueueCapabilityManager mgr;
    mgr.Init(nullptr);

    auto stats = mgr.GetStats();
    EXPECT_TRUE(stats.hasAsyncCompute || stats.hasDedicatedCompute);
    EXPECT_TRUE(stats.hasDedicatedTransfer);

    mgr.Shutdown();
}

// ─── Timeline Semaphore Pool + Queue Integration Tests ───────────────────

TEST(SemaphoreQueueIntegration, AcquirePerQueue) {
    QueueCapabilityManager queueMgr;
    queueMgr.Init(nullptr);

    TimelineSemaphorePool semPool;
    semPool.Init(nullptr);

    auto gfx = queueMgr.GetGraphicsQueue();
    auto compute = queueMgr.GetAsyncComputeQueue();

    // Acquire semaphores for cross-queue sync
    u32 gfxToCompute = semPool.Acquire("GfxToCompute");
    u32 computeToGfx = semPool.Acquire("ComputeToGfx");
    EXPECT_NE(gfxToCompute, UINT32_MAX);
    EXPECT_NE(computeToGfx, UINT32_MAX);

    // Simulate signaling from graphics queue
    u64 signalVal = semPool.GetNextSignalValue(gfxToCompute);
    EXPECT_EQ(signalVal, 1u);

    // Compute queue waits for that value
    EXPECT_TRUE(semPool.HasReached(gfxToCompute, 0));
    // Value 1 was signaled via counter increment (stub)
    EXPECT_TRUE(semPool.HasReached(gfxToCompute, 1));

    // Signal back from compute
    u64 computeSignal = semPool.GetNextSignalValue(computeToGfx);
    EXPECT_EQ(computeSignal, 1u);

    auto stats = semPool.GetStats();
    EXPECT_EQ(stats.inUseSemaphores, 2u);
    EXPECT_EQ(stats.highestSignaledValue, 1u);

    semPool.Release(gfxToCompute);
    semPool.Release(computeToGfx);

    semPool.Shutdown();
    queueMgr.Shutdown();
}

TEST(SemaphoreQueueIntegration, MultiFrameSync) {
    TimelineSemaphorePool pool;
    pool.Init(nullptr);

    u32 sem = pool.Acquire("FrameSync");

    // Simulate 5 frames of signal/wait
    for (u64 frame = 1; frame <= 5; ++frame) {
        u64 val = pool.GetNextSignalValue(sem);
        EXPECT_EQ(val, frame);
        EXPECT_TRUE(pool.HasReached(sem, frame));
        EXPECT_FALSE(pool.HasReached(sem, frame + 1));
    }

    EXPECT_EQ(pool.GetCurrentValue(sem), 5u);

    pool.Release(sem);
    pool.Shutdown();
}
