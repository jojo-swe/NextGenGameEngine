#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_descriptor_update_batcher.h"

using namespace nge::rhi;

TEST(DescriptorUpdateBatcher, InitAndShutdown) {
    DescriptorUpdateBatcher batcher;
    EXPECT_TRUE(batcher.Init());

    auto stats = batcher.GetStats();
    EXPECT_EQ(stats.totalWritesQueued, 0u);
    EXPECT_EQ(stats.pendingWrites, 0u);

    batcher.Shutdown();
}

TEST(DescriptorUpdateBatcher, WriteBufferQueued) {
    DescriptorUpdateBatcher batcher;
    batcher.Init();

    batcher.WriteBuffer(100, 0, 0, DescWriteType::UniformBuffer, 1, 0, 256);
    EXPECT_EQ(batcher.GetPendingWriteCount(), 1u);

    batcher.WriteBuffer(100, 1, 0, DescWriteType::StorageBuffer, 2, 0, 512);
    EXPECT_EQ(batcher.GetPendingWriteCount(), 2u);

    auto stats = batcher.GetStats();
    EXPECT_EQ(stats.totalWritesQueued, 2u);

    batcher.Shutdown();
}

TEST(DescriptorUpdateBatcher, WriteImageQueued) {
    DescriptorUpdateBatcher batcher;
    batcher.Init();

    batcher.WriteImage(100, 0, 0, DescWriteType::CombinedImageSampler, 10, 20, 1);
    EXPECT_EQ(batcher.GetPendingWriteCount(), 1u);

    batcher.WriteImage(100, 1, 0, DescWriteType::SampledImage, 11, 0, 1);
    EXPECT_EQ(batcher.GetPendingWriteCount(), 2u);

    batcher.Shutdown();
}

TEST(DescriptorUpdateBatcher, FlushClearsPending) {
    DescriptorUpdateBatcher batcher;
    batcher.Init();

    batcher.WriteBuffer(100, 0, 0, DescWriteType::UniformBuffer, 1, 0, 256);
    batcher.WriteBuffer(100, 1, 0, DescWriteType::StorageBuffer, 2, 0, 512);
    batcher.WriteImage(200, 0, 0, DescWriteType::CombinedImageSampler, 10, 20, 1);

    EXPECT_EQ(batcher.GetPendingWriteCount(), 3u);

    u32 flushed = batcher.Flush();
    EXPECT_EQ(flushed, 3u);
    EXPECT_EQ(batcher.GetPendingWriteCount(), 0u);

    auto stats = batcher.GetStats();
    EXPECT_EQ(stats.totalWritesFlushed, 3u);
    EXPECT_EQ(stats.totalFlushes, 1u);

    batcher.Shutdown();
}

TEST(DescriptorUpdateBatcher, FlushEmptyReturnsZero) {
    DescriptorUpdateBatcher batcher;
    batcher.Init();

    EXPECT_EQ(batcher.Flush(), 0u);

    batcher.Shutdown();
}

TEST(DescriptorUpdateBatcher, FlushSetFlushesOnlyTargetSet) {
    DescriptorUpdateBatcher batcher;
    batcher.Init();

    batcher.WriteBuffer(100, 0, 0, DescWriteType::UniformBuffer, 1, 0, 256);
    batcher.WriteBuffer(100, 1, 0, DescWriteType::UniformBuffer, 2, 0, 128);
    batcher.WriteBuffer(200, 0, 0, DescWriteType::StorageBuffer, 3, 0, 512);

    EXPECT_EQ(batcher.GetPendingWriteCount(), 3u);

    u32 flushed = batcher.FlushSet(100);
    EXPECT_EQ(flushed, 2u);
    EXPECT_EQ(batcher.GetPendingWriteCount(), 1u); // Set 200 still pending

    batcher.Shutdown();
}

TEST(DescriptorUpdateBatcher, CoalescingReplacesExistingWrite) {
    DescriptorUpdateBatcher batcher;
    DescUpdateBatcherConfig config;
    config.enableCoalescing = true;
    batcher.Init(config);

    // Write to set 100, binding 0
    batcher.WriteBuffer(100, 0, 0, DescWriteType::UniformBuffer, 1, 0, 256);
    EXPECT_EQ(batcher.GetPendingWriteCount(), 1u);

    // Write again to same set+binding+element - should coalesce
    batcher.WriteBuffer(100, 0, 0, DescWriteType::UniformBuffer, 2, 0, 512);
    EXPECT_EQ(batcher.GetPendingWriteCount(), 1u); // Still 1, coalesced

    auto stats = batcher.GetStats();
    EXPECT_EQ(stats.coalescedWrites, 1u);
    EXPECT_EQ(stats.totalWritesQueued, 2u); // Both counted as queued

    batcher.Shutdown();
}

TEST(DescriptorUpdateBatcher, CoalescingDisabled) {
    DescriptorUpdateBatcher batcher;
    DescUpdateBatcherConfig config;
    config.enableCoalescing = false;
    batcher.Init(config);

    batcher.WriteBuffer(100, 0, 0, DescWriteType::UniformBuffer, 1, 0, 256);
    batcher.WriteBuffer(100, 0, 0, DescWriteType::UniformBuffer, 2, 0, 512);

    // Without coalescing, both writes remain
    EXPECT_EQ(batcher.GetPendingWriteCount(), 2u);

    auto stats = batcher.GetStats();
    EXPECT_EQ(stats.coalescedWrites, 0u);

    batcher.Shutdown();
}

TEST(DescriptorUpdateBatcher, DifferentBindingsNotCoalesced) {
    DescriptorUpdateBatcher batcher;
    DescUpdateBatcherConfig config;
    config.enableCoalescing = true;
    batcher.Init(config);

    batcher.WriteBuffer(100, 0, 0, DescWriteType::UniformBuffer, 1, 0, 256);
    batcher.WriteBuffer(100, 1, 0, DescWriteType::UniformBuffer, 2, 0, 256);

    // Different bindings, not coalesced
    EXPECT_EQ(batcher.GetPendingWriteCount(), 2u);
    EXPECT_EQ(batcher.GetStats().coalescedWrites, 0u);

    batcher.Shutdown();
}

TEST(DescriptorUpdateBatcher, CopyDescriptor) {
    DescriptorUpdateBatcher batcher;
    batcher.Init();

    batcher.CopyDescriptor(100, 0, 0, 200, 0, 0, 1);
    EXPECT_EQ(batcher.GetPendingCopyCount(), 1u);

    batcher.CopyDescriptor(100, 1, 0, 200, 1, 0, 4);
    EXPECT_EQ(batcher.GetPendingCopyCount(), 2u);

    u32 flushed = batcher.Flush();
    EXPECT_EQ(flushed, 2u); // 0 writes + 2 copies
    EXPECT_EQ(batcher.GetPendingCopyCount(), 0u);

    auto stats = batcher.GetStats();
    EXPECT_EQ(stats.totalCopiesQueued, 2u);
    EXPECT_EQ(stats.totalCopiesFlushed, 2u);

    batcher.Shutdown();
}

TEST(DescriptorUpdateBatcher, HasPendingWrites) {
    DescriptorUpdateBatcher batcher;
    batcher.Init();

    EXPECT_FALSE(batcher.HasPendingWrites(100));

    batcher.WriteBuffer(100, 0, 0, DescWriteType::UniformBuffer, 1, 0, 256);
    EXPECT_TRUE(batcher.HasPendingWrites(100));
    EXPECT_FALSE(batcher.HasPendingWrites(200));

    batcher.Shutdown();
}

TEST(DescriptorUpdateBatcher, DiscardAll) {
    DescriptorUpdateBatcher batcher;
    batcher.Init();

    batcher.WriteBuffer(100, 0, 0, DescWriteType::UniformBuffer, 1, 0, 256);
    batcher.WriteImage(200, 0, 0, DescWriteType::CombinedImageSampler, 10, 20, 1);
    batcher.CopyDescriptor(100, 0, 0, 300, 0, 0, 1);

    EXPECT_EQ(batcher.GetPendingWriteCount(), 2u);
    EXPECT_EQ(batcher.GetPendingCopyCount(), 1u);

    batcher.DiscardAll();

    EXPECT_EQ(batcher.GetPendingWriteCount(), 0u);
    EXPECT_EQ(batcher.GetPendingCopyCount(), 0u);

    // Stats should still show queued counts (not flushed)
    auto stats = batcher.GetStats();
    EXPECT_EQ(stats.totalWritesQueued, 2u);
    EXPECT_EQ(stats.totalWritesFlushed, 0u);

    batcher.Shutdown();
}

TEST(DescriptorUpdateBatcher, AutoFlushAtThreshold) {
    DescriptorUpdateBatcher batcher;
    DescUpdateBatcherConfig config;
    config.autoFlushThreshold = 3;
    config.enableCoalescing = false;
    batcher.Init(config);

    batcher.WriteBuffer(100, 0, 0, DescWriteType::UniformBuffer, 1, 0, 256);
    batcher.WriteBuffer(100, 1, 0, DescWriteType::UniformBuffer, 2, 0, 256);

    // Third write should trigger auto-flush
    batcher.WriteBuffer(100, 2, 0, DescWriteType::UniformBuffer, 3, 0, 256);

    // After auto-flush, pending should be 0
    EXPECT_EQ(batcher.GetPendingWriteCount(), 0u);

    auto stats = batcher.GetStats();
    EXPECT_GE(stats.totalFlushes, 1u);

    batcher.Shutdown();
}

TEST(DescriptorUpdateBatcher, ResetClearsAll) {
    DescriptorUpdateBatcher batcher;
    batcher.Init();

    batcher.WriteBuffer(100, 0, 0, DescWriteType::UniformBuffer, 1, 0, 256);
    batcher.Flush();

    batcher.Reset();

    auto stats = batcher.GetStats();
    EXPECT_EQ(stats.totalWritesQueued, 0u);
    EXPECT_EQ(stats.totalFlushes, 0u);
    EXPECT_EQ(stats.totalWritesFlushed, 0u);
    EXPECT_EQ(stats.coalescedWrites, 0u);
    EXPECT_EQ(stats.pendingWrites, 0u);

    batcher.Shutdown();
}

TEST(DescriptorUpdateBatcher, UniqueSetsTracking) {
    DescriptorUpdateBatcher batcher;
    DescUpdateBatcherConfig config;
    config.trackPerSetStats = true;
    batcher.Init(config);

    batcher.WriteBuffer(100, 0, 0, DescWriteType::UniformBuffer, 1, 0, 256);
    batcher.WriteBuffer(100, 1, 0, DescWriteType::UniformBuffer, 2, 0, 128);
    batcher.WriteBuffer(200, 0, 0, DescWriteType::StorageBuffer, 3, 0, 512);
    batcher.WriteImage(300, 0, 0, DescWriteType::CombinedImageSampler, 10, 20, 1);

    auto stats = batcher.GetStats();
    EXPECT_EQ(stats.uniqueSetsUpdated, 3u); // Sets 100, 200, 300

    batcher.Shutdown();
}

TEST(DescriptorUpdateBatcher, MixedWritesAndCopies) {
    DescriptorUpdateBatcher batcher;
    batcher.Init();

    batcher.WriteBuffer(100, 0, 0, DescWriteType::UniformBuffer, 1, 0, 256);
    batcher.WriteImage(100, 1, 0, DescWriteType::CombinedImageSampler, 10, 20, 1);
    batcher.CopyDescriptor(100, 0, 0, 200, 0, 0, 1);

    u32 flushed = batcher.Flush();
    EXPECT_EQ(flushed, 3u); // 2 writes + 1 copy

    auto stats = batcher.GetStats();
    EXPECT_EQ(stats.totalWritesFlushed, 2u);
    EXPECT_EQ(stats.totalCopiesFlushed, 1u);

    batcher.Shutdown();
}

TEST(DescriptorUpdateBatcher, MultipleFlushes) {
    DescriptorUpdateBatcher batcher;
    batcher.Init();

    batcher.WriteBuffer(100, 0, 0, DescWriteType::UniformBuffer, 1, 0, 256);
    batcher.Flush();

    batcher.WriteBuffer(200, 0, 0, DescWriteType::StorageBuffer, 2, 0, 512);
    batcher.WriteBuffer(200, 1, 0, DescWriteType::StorageBuffer, 3, 0, 128);
    batcher.Flush();

    auto stats = batcher.GetStats();
    EXPECT_EQ(stats.totalFlushes, 2u);
    EXPECT_EQ(stats.totalWritesFlushed, 3u);
    EXPECT_EQ(stats.totalWritesQueued, 3u);

    batcher.Shutdown();
}
