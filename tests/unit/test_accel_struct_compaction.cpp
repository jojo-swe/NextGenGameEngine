#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_accel_struct_compaction.h"

using namespace nge;
using namespace nge::rhi;

TEST(AccelStructCompaction, InitAndShutdown) {
    AccelStructCompactionManager mgr;
    EXPECT_TRUE(mgr.Init());

    EXPECT_EQ(mgr.GetCount(), 0u);
    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalStructures, 0u);
    EXPECT_EQ(stats.totalBuilt, 0u);

    mgr.Shutdown();
}

TEST(AccelStructCompaction, RegisterBuilt) {
    AccelStructCompactionManager mgr;
    mgr.Init();

    u64 id = mgr.RegisterBuilt(AccelStructType::BLAS, 100, 200, 65536, 4, "MeshBLAS");
    EXPECT_NE(id, 0u);
    EXPECT_EQ(mgr.GetCount(), 1u);

    const auto* info = mgr.GetInfo(id);
    EXPECT_NE(info, nullptr);
    EXPECT_EQ(info->handle, 100u);
    EXPECT_EQ(info->bufferHandle, 200u);
    EXPECT_EQ(info->type, AccelStructType::BLAS);
    EXPECT_EQ(info->state, CompactionState::Built);
    EXPECT_EQ(info->originalSize, 65536u);
    EXPECT_EQ(info->geometryCount, 4u);
    EXPECT_EQ(info->debugName, "MeshBLAS");

    mgr.Shutdown();
}

TEST(AccelStructCompaction, RegisterTLAS) {
    AccelStructCompactionManager mgr;
    mgr.Init();

    u64 id = mgr.RegisterBuilt(AccelStructType::TLAS, 300, 400, 131072, 256, "SceneTLAS");

    const auto* info = mgr.GetInfo(id);
    EXPECT_EQ(info->type, AccelStructType::TLAS);

    mgr.Shutdown();
}

TEST(AccelStructCompaction, CompactionStateMachine) {
    AccelStructCompactionManager mgr;
    mgr.Init();

    u64 id = mgr.RegisterBuilt(AccelStructType::BLAS, 100, 200, 65536, 1);
    EXPECT_EQ(mgr.GetInfo(id)->state, CompactionState::Built);

    mgr.MarkQueryPending(id);
    EXPECT_EQ(mgr.GetInfo(id)->state, CompactionState::QueryPending);

    mgr.SetCompactedSize(id, 40000);
    EXPECT_EQ(mgr.GetInfo(id)->state, CompactionState::QueryReady);
    EXPECT_EQ(mgr.GetInfo(id)->compactedSize, 40000u);

    mgr.MarkCompacting(id);
    EXPECT_EQ(mgr.GetInfo(id)->state, CompactionState::Compacting);

    mgr.MarkCompacted(id, 500, 600, 10);
    EXPECT_EQ(mgr.GetInfo(id)->state, CompactionState::Compacted);
    EXPECT_EQ(mgr.GetInfo(id)->handle, 500u);
    EXPECT_EQ(mgr.GetInfo(id)->bufferHandle, 600u);
    EXPECT_EQ(mgr.GetInfo(id)->compactFrame, 10u);

    mgr.Shutdown();
}

TEST(AccelStructCompaction, ShouldCompactWorthwhile) {
    AccelStructCompactionManager mgr;
    AccelCompactionConfig config;
    config.minSavingsThreshold = 4096;
    config.minSavingsRatio = 0.1f;
    mgr.Init(config);

    u64 id = mgr.RegisterBuilt(AccelStructType::BLAS, 100, 200, 100000, 1);
    mgr.MarkQueryPending(id);
    mgr.SetCompactedSize(id, 50000); // 50% savings

    EXPECT_TRUE(mgr.ShouldCompact(id));

    mgr.Shutdown();
}

TEST(AccelStructCompaction, ShouldCompactTooSmallSavings) {
    AccelStructCompactionManager mgr;
    AccelCompactionConfig config;
    config.minSavingsThreshold = 4096;
    config.minSavingsRatio = 0.1f;
    mgr.Init(config);

    u64 id = mgr.RegisterBuilt(AccelStructType::BLAS, 100, 200, 5000, 1);
    mgr.MarkQueryPending(id);
    mgr.SetCompactedSize(id, 4500); // Only 500 bytes savings < 4096

    EXPECT_FALSE(mgr.ShouldCompact(id));

    mgr.Shutdown();
}

TEST(AccelStructCompaction, ShouldCompactTooSmallRatio) {
    AccelStructCompactionManager mgr;
    AccelCompactionConfig config;
    config.minSavingsThreshold = 100;
    config.minSavingsRatio = 0.2f;
    mgr.Init(config);

    u64 id = mgr.RegisterBuilt(AccelStructType::BLAS, 100, 200, 100000, 1);
    mgr.MarkQueryPending(id);
    mgr.SetCompactedSize(id, 95000); // Only 5% savings < 20%

    EXPECT_FALSE(mgr.ShouldCompact(id));

    mgr.Shutdown();
}

TEST(AccelStructCompaction, ShouldCompactNotReady) {
    AccelStructCompactionManager mgr;
    mgr.Init();

    u64 id = mgr.RegisterBuilt(AccelStructType::BLAS, 100, 200, 65536, 1);

    // Still in Built state, not QueryReady
    EXPECT_FALSE(mgr.ShouldCompact(id));

    mgr.Shutdown();
}

TEST(AccelStructCompaction, GetBuiltStructures) {
    AccelStructCompactionManager mgr;
    mgr.Init();

    [[maybe_unused]] u64 id1 = mgr.RegisterBuilt(AccelStructType::BLAS, 100, 200, 65536, 1);
    u64 id2 = mgr.RegisterBuilt(AccelStructType::BLAS, 101, 201, 32768, 1);
    [[maybe_unused]] u64 id3 = mgr.RegisterBuilt(AccelStructType::BLAS, 102, 202, 16384, 1);

    mgr.MarkQueryPending(id2); // Move id2 out of Built state

    auto built = mgr.GetBuiltStructures();
    EXPECT_EQ(built.size(), 2u); // id1 and id3

    mgr.Shutdown();
}

TEST(AccelStructCompaction, GetReadyToCompact) {
    AccelStructCompactionManager mgr;
    AccelCompactionConfig config;
    config.minSavingsThreshold = 100;
    config.minSavingsRatio = 0.05f;
    mgr.Init(config);

    u64 id1 = mgr.RegisterBuilt(AccelStructType::BLAS, 100, 200, 65536, 1);
    u64 id2 = mgr.RegisterBuilt(AccelStructType::BLAS, 101, 201, 32768, 1);

    mgr.MarkQueryPending(id1);
    mgr.SetCompactedSize(id1, 40000); // Ready, worthwhile

    mgr.MarkQueryPending(id2);
    mgr.SetCompactedSize(id2, 20000); // Ready, worthwhile

    auto ready = mgr.GetReadyToCompact();
    EXPECT_EQ(ready.size(), 2u);

    // Limit count
    auto limited = mgr.GetReadyToCompact(1);
    EXPECT_EQ(limited.size(), 1u);

    mgr.Shutdown();
}

TEST(AccelStructCompaction, MarkFailed) {
    AccelStructCompactionManager mgr;
    mgr.Init();

    u64 id = mgr.RegisterBuilt(AccelStructType::BLAS, 100, 200, 65536, 1);
    mgr.MarkFailed(id);

    EXPECT_EQ(mgr.GetInfo(id)->state, CompactionState::Failed);
    EXPECT_EQ(mgr.GetStats().totalFailed, 1u);

    mgr.Shutdown();
}

TEST(AccelStructCompaction, Unregister) {
    AccelStructCompactionManager mgr;
    mgr.Init();

    u64 id = mgr.RegisterBuilt(AccelStructType::BLAS, 100, 200, 65536, 1);
    EXPECT_EQ(mgr.GetCount(), 1u);

    mgr.Unregister(id);
    EXPECT_EQ(mgr.GetCount(), 0u);
    EXPECT_EQ(mgr.GetInfo(id), nullptr);

    mgr.Shutdown();
}

TEST(AccelStructCompaction, ProcessFrame) {
    AccelStructCompactionManager mgr;
    AccelCompactionConfig config;
    config.maxCompactionsPerFrame = 2;
    config.autoCompact = true;
    config.minSavingsThreshold = 100;
    config.minSavingsRatio = 0.05f;
    mgr.Init(config);

    // Register 3 structures, all ready for compaction
    for (u32 i = 0; i < 3; ++i) {
        u64 id = mgr.RegisterBuilt(AccelStructType::BLAS, 100 + i, 200 + i, 65536, 1);
        mgr.MarkQueryPending(id);
        mgr.SetCompactedSize(id, 40000);
    }

    auto toCompact = mgr.ProcessFrame(1);
    // Should return at most maxCompactionsPerFrame (2)
    EXPECT_LE(toCompact.size(), 2u);
    EXPECT_GE(toCompact.size(), 1u);

    mgr.Shutdown();
}

TEST(AccelStructCompaction, ProcessFrameAutoDisabled) {
    AccelStructCompactionManager mgr;
    AccelCompactionConfig config;
    config.autoCompact = false;
    mgr.Init(config);

    u64 id = mgr.RegisterBuilt(AccelStructType::BLAS, 100, 200, 65536, 1);
    mgr.MarkQueryPending(id);
    mgr.SetCompactedSize(id, 40000);

    auto toCompact = mgr.ProcessFrame(1);
    EXPECT_TRUE(toCompact.empty()); // Auto-compact disabled

    mgr.Shutdown();
}

TEST(AccelStructCompaction, MaxStructuresLimit) {
    AccelStructCompactionManager mgr;
    AccelCompactionConfig config;
    config.maxStructures = 3;
    mgr.Init(config);

    for (u32 i = 0; i < 3; ++i) {
        EXPECT_NE(mgr.RegisterBuilt(AccelStructType::BLAS, i, i, 1024, 1), 0u);
    }

    // 4th should fail
    EXPECT_EQ(mgr.RegisterBuilt(AccelStructType::BLAS, 99, 99, 1024, 1), 0u);

    mgr.Shutdown();
}

TEST(AccelStructCompaction, StatsTracking) {
    AccelStructCompactionManager mgr;
    AccelCompactionConfig config;
    config.minSavingsThreshold = 100;
    config.minSavingsRatio = 0.05f;
    mgr.Init(config);

    u64 id1 = mgr.RegisterBuilt(AccelStructType::BLAS, 100, 200, 100000, 1);
    [[maybe_unused]] u64 id2 = mgr.RegisterBuilt(AccelStructType::BLAS, 101, 201, 50000, 1);

    mgr.MarkQueryPending(id1);
    mgr.SetCompactedSize(id1, 60000);
    mgr.MarkCompacting(id1);
    mgr.MarkCompacted(id1, 500, 600, 1);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalStructures, 2u);
    EXPECT_EQ(stats.totalBuilt, 2u);
    EXPECT_EQ(stats.totalCompacted, 1u);
    EXPECT_GT(stats.totalMemorySaved, 0u);
    EXPECT_GT(stats.averageSavingsRatio, 0.0f);

    mgr.Shutdown();
}

TEST(AccelStructCompaction, ResetClearsAll) {
    AccelStructCompactionManager mgr;
    mgr.Init();

    mgr.RegisterBuilt(AccelStructType::BLAS, 100, 200, 65536, 1);
    mgr.RegisterBuilt(AccelStructType::TLAS, 101, 201, 32768, 1);

    mgr.Reset();

    EXPECT_EQ(mgr.GetCount(), 0u);
    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalBuilt, 0u);
    EXPECT_EQ(stats.totalCompacted, 0u);

    mgr.Shutdown();
}

TEST(AccelStructCompaction, GetInfoNonExistent) {
    AccelStructCompactionManager mgr;
    mgr.Init();

    EXPECT_EQ(mgr.GetInfo(9999), nullptr);

    mgr.Shutdown();
}
