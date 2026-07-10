#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_subresource_state_tracker.h"

using namespace nge;
using namespace nge::rhi;

TEST(SubresourceStateTracker, InitAndShutdown) {
    SubresourceStateTracker tracker;
    EXPECT_TRUE(tracker.Init());

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.totalImages, 0u);
    EXPECT_EQ(stats.totalSubresources, 0u);

    tracker.Shutdown();
}

TEST(SubresourceStateTracker, RegisterImage) {
    SubresourceStateTracker tracker;
    tracker.Init();

    tracker.RegisterImage(1, "Albedo", 4, 1, ImageLayout::Undefined);
    EXPECT_TRUE(tracker.IsTracked(1));
    EXPECT_FALSE(tracker.IsTracked(2));

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.totalImages, 1u);
    EXPECT_EQ(stats.totalSubresources, 4u); // 4 mips * 1 layer

    tracker.Shutdown();
}

TEST(SubresourceStateTracker, RegisterImageArrayLayers) {
    SubresourceStateTracker tracker;
    tracker.Init();

    // Cubemap: 1 mip, 6 layers
    tracker.RegisterImage(1, "Cubemap", 1, 6, ImageLayout::Undefined);

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.totalSubresources, 6u);

    // 3 mips, 4 layers
    tracker.RegisterImage(2, "TexArray", 3, 4, ImageLayout::ShaderReadOnly);

    stats = tracker.GetStats();
    EXPECT_EQ(stats.totalSubresources, 6u + 12u);

    tracker.Shutdown();
}

TEST(SubresourceStateTracker, UnregisterImage) {
    SubresourceStateTracker tracker;
    tracker.Init();

    tracker.RegisterImage(1, "Tex", 2, 1);
    EXPECT_TRUE(tracker.IsTracked(1));

    tracker.UnregisterImage(1);
    EXPECT_FALSE(tracker.IsTracked(1));
    EXPECT_EQ(tracker.GetStats().totalImages, 0u);

    tracker.Shutdown();
}

TEST(SubresourceStateTracker, TransitionSubresource) {
    SubresourceStateTracker tracker;
    tracker.Init();

    tracker.RegisterImage(1, "RT", 1, 1, ImageLayout::Undefined);

    auto barrier = tracker.TransitionSubresource(1, 0, 0,
        ImageLayout::ColorAttachment, static_cast<u32>(AccessFlags::ColorAttachmentWrite));

    EXPECT_EQ(barrier.oldLayout, ImageLayout::Undefined);
    EXPECT_EQ(barrier.newLayout, ImageLayout::ColorAttachment);
    EXPECT_EQ(barrier.imageHandle, 1u);
    EXPECT_EQ(barrier.mipLevel, 0u);
    EXPECT_EQ(barrier.arrayLayer, 0u);

    // Verify state updated
    EXPECT_EQ(tracker.GetLayout(1, 0, 0), ImageLayout::ColorAttachment);

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.totalTransitions, 1u);

    tracker.Shutdown();
}

TEST(SubresourceStateTracker, TransitionWholeImage) {
    SubresourceStateTracker tracker;
    tracker.Init();

    tracker.RegisterImage(1, "Tex", 3, 2, ImageLayout::Undefined);

    auto barriers = tracker.TransitionWholeImage(1,
        ImageLayout::ShaderReadOnly, static_cast<u32>(AccessFlags::ShaderRead));

    EXPECT_EQ(barriers.size(), 6u); // 3 mips * 2 layers

    // All should be from Undefined -> ShaderReadOnly
    for (const auto& b : barriers) {
        EXPECT_EQ(b.oldLayout, ImageLayout::Undefined);
        EXPECT_EQ(b.newLayout, ImageLayout::ShaderReadOnly);
    }

    EXPECT_TRUE(tracker.IsWholeImageInLayout(1, ImageLayout::ShaderReadOnly));

    tracker.Shutdown();
}

TEST(SubresourceStateTracker, TransitionMipRange) {
    SubresourceStateTracker tracker;
    tracker.Init();

    tracker.RegisterImage(1, "Tex", 4, 1, ImageLayout::ShaderReadOnly);

    // Transition mips 1-2 to TransferDst
    auto barriers = tracker.TransitionMipRange(1, 1, 2, 0, 1,
        ImageLayout::TransferDst, static_cast<u32>(AccessFlags::TransferWrite));

    EXPECT_EQ(barriers.size(), 2u);

    // Mip 0 should still be ShaderReadOnly
    EXPECT_EQ(tracker.GetLayout(1, 0, 0), ImageLayout::ShaderReadOnly);
    // Mips 1-2 should be TransferDst
    EXPECT_EQ(tracker.GetLayout(1, 1, 0), ImageLayout::TransferDst);
    EXPECT_EQ(tracker.GetLayout(1, 2, 0), ImageLayout::TransferDst);
    // Mip 3 untouched
    EXPECT_EQ(tracker.GetLayout(1, 3, 0), ImageLayout::ShaderReadOnly);

    EXPECT_FALSE(tracker.IsWholeImageInLayout(1, ImageLayout::ShaderReadOnly));

    tracker.Shutdown();
}

TEST(SubresourceStateTracker, RedundantTransitionDetection) {
    SubresourceStateTracker tracker;
    SubresourceTrackerConfig config;
    config.detectRedundantBarriers = true;
    tracker.Init(config);

    tracker.RegisterImage(1, "Tex", 1, 1, ImageLayout::ShaderReadOnly);

    // First transition: Undefined -> ShaderReadOnly already done at registration
    // Transition to same layout should be redundant
    [[maybe_unused]] auto barrier = tracker.TransitionSubresource(1, 0, 0,
        ImageLayout::ShaderReadOnly, static_cast<u32>(AccessFlags::ShaderRead));

    // The state was already ShaderReadOnly with access None, but access mask differs
    // so it shouldn't count as fully redundant unless both match
    // Let's do an exact duplicate:
    tracker.TransitionSubresource(1, 0, 0,
        ImageLayout::ShaderReadOnly, static_cast<u32>(AccessFlags::ShaderRead));

    // Now this one should be redundant (same layout AND same access)
    auto stats = tracker.GetStats();
    EXPECT_GT(stats.redundantTransitions, 0u);

    tracker.Shutdown();
}

TEST(SubresourceStateTracker, WholeImageRedundantSkipped) {
    SubresourceStateTracker tracker;
    SubresourceTrackerConfig config;
    config.detectRedundantBarriers = true;
    tracker.Init(config);

    tracker.RegisterImage(1, "Tex", 2, 1, ImageLayout::Undefined);

    // First: transition all to ShaderReadOnly
    auto barriers1 = tracker.TransitionWholeImage(1,
        ImageLayout::ShaderReadOnly, static_cast<u32>(AccessFlags::ShaderRead));
    EXPECT_EQ(barriers1.size(), 2u);

    // Second: same transition should produce 0 barriers (all redundant)
    auto barriers2 = tracker.TransitionWholeImage(1,
        ImageLayout::ShaderReadOnly, static_cast<u32>(AccessFlags::ShaderRead));
    EXPECT_EQ(barriers2.size(), 0u);

    tracker.Shutdown();
}

TEST(SubresourceStateTracker, QueueOwnershipTransfer) {
    SubresourceStateTracker tracker;
    SubresourceTrackerConfig config;
    config.trackQueueOwnership = true;
    tracker.Init(config);

    tracker.RegisterImage(1, "Tex", 1, 1, ImageLayout::ShaderReadOnly);

    auto barrier = tracker.TransferQueueOwnership(1, 0, 0, 0, 1);

    EXPECT_EQ(barrier.srcQueueFamily, 0u);
    EXPECT_EQ(barrier.dstQueueFamily, 1u);
    // Layout should remain the same
    EXPECT_EQ(barrier.oldLayout, ImageLayout::ShaderReadOnly);
    EXPECT_EQ(barrier.newLayout, ImageLayout::ShaderReadOnly);

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.queueOwnershipTransfers, 1u);

    tracker.Shutdown();
}

TEST(SubresourceStateTracker, GetSubresourceState) {
    SubresourceStateTracker tracker;
    tracker.Init();

    tracker.RegisterImage(1, "Tex", 2, 2, ImageLayout::General);

    const auto* state = tracker.GetSubresourceState(1, 0, 0);
    EXPECT_NE(state, nullptr);
    EXPECT_EQ(state->layout, ImageLayout::General);

    const auto* state11 = tracker.GetSubresourceState(1, 1, 1);
    EXPECT_NE(state11, nullptr);
    EXPECT_EQ(state11->layout, ImageLayout::General);

    // Out of range
    EXPECT_EQ(tracker.GetSubresourceState(1, 5, 0), nullptr);

    // Non-existent image
    EXPECT_EQ(tracker.GetSubresourceState(999, 0, 0), nullptr);

    tracker.Shutdown();
}

TEST(SubresourceStateTracker, GetLayoutUnregistered) {
    SubresourceStateTracker tracker;
    tracker.Init();

    EXPECT_EQ(tracker.GetLayout(999, 0, 0), ImageLayout::Undefined);

    tracker.Shutdown();
}

TEST(SubresourceStateTracker, MixedSubresourceLayouts) {
    SubresourceStateTracker tracker;
    tracker.Init();

    tracker.RegisterImage(1, "DepthRT", 1, 1, ImageLayout::DepthStencilAttachment);
    tracker.RegisterImage(2, "ColorRT", 1, 1, ImageLayout::ColorAttachment);

    // Transition depth to read-only for sampling
    tracker.TransitionSubresource(1, 0, 0,
        ImageLayout::DepthStencilReadOnly, static_cast<u32>(AccessFlags::DepthStencilRead));

    EXPECT_EQ(tracker.GetLayout(1, 0, 0), ImageLayout::DepthStencilReadOnly);
    EXPECT_EQ(tracker.GetLayout(2, 0, 0), ImageLayout::ColorAttachment);

    tracker.Shutdown();
}

TEST(SubresourceStateTracker, ResetClearsAll) {
    SubresourceStateTracker tracker;
    tracker.Init();

    tracker.RegisterImage(1, "A", 2, 1);
    tracker.RegisterImage(2, "B", 3, 2);
    tracker.TransitionSubresource(1, 0, 0, ImageLayout::General, 0);

    tracker.Reset();

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.totalImages, 0u);
    EXPECT_EQ(stats.totalSubresources, 0u);
    EXPECT_EQ(stats.totalTransitions, 0u);
    EXPECT_EQ(stats.redundantTransitions, 0u);
    EXPECT_FALSE(tracker.IsTracked(1));

    tracker.Shutdown();
}

TEST(SubresourceStateTracker, MaxImagesLimit) {
    SubresourceStateTracker tracker;
    SubresourceTrackerConfig config;
    config.maxImages = 2;
    tracker.Init(config);

    tracker.RegisterImage(1, "A", 1, 1);
    tracker.RegisterImage(2, "B", 1, 1);
    tracker.RegisterImage(3, "C", 1, 1); // Exceeds limit

    EXPECT_EQ(tracker.GetStats().totalImages, 2u);
    EXPECT_FALSE(tracker.IsTracked(3));

    tracker.Shutdown();
}

TEST(SubresourceStateTracker, UnregisteredImageTransition) {
    SubresourceStateTracker tracker;
    tracker.Init();

    // Transition an unregistered image should return safe defaults
    auto barrier = tracker.TransitionSubresource(999, 0, 0,
        ImageLayout::ShaderReadOnly, static_cast<u32>(AccessFlags::ShaderRead));

    EXPECT_EQ(barrier.oldLayout, ImageLayout::Undefined);
    EXPECT_EQ(barrier.newLayout, ImageLayout::ShaderReadOnly);

    tracker.Shutdown();
}

TEST(SubresourceStateTracker, StatsTracking) {
    SubresourceStateTracker tracker;
    tracker.Init();

    tracker.RegisterImage(1, "Tex", 2, 2);

    tracker.TransitionSubresource(1, 0, 0, ImageLayout::General, 0);
    tracker.TransitionSubresource(1, 1, 0, ImageLayout::TransferDst,
        static_cast<u32>(AccessFlags::TransferWrite));
    tracker.TransferQueueOwnership(1, 0, 0, 0, 1);

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.totalImages, 1u);
    EXPECT_EQ(stats.totalSubresources, 4u);
    EXPECT_EQ(stats.totalTransitions, 2u);
    EXPECT_EQ(stats.queueOwnershipTransfers, 1u);

    tracker.Shutdown();
}
