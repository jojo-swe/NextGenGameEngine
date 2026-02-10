#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_resource_transition_tracker.h"

using namespace nge;
using namespace nge::rhi;

TEST(ResourceTransitionTracker, InitAndShutdown) {
    ResourceTransitionTracker tracker;
    EXPECT_TRUE(tracker.Init());
    EXPECT_EQ(tracker.GetResourceCount(), 0u);
    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, RegisterResource) {
    ResourceTransitionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterResource(true, 1, ResourceLayout::Undefined, "GBuffer0");
    EXPECT_NE(id, UINT32_MAX);
    EXPECT_EQ(tracker.GetResourceCount(), 1u);

    const auto* res = tracker.GetResource(id);
    EXPECT_NE(res, nullptr);
    EXPECT_TRUE(res->isImage);
    EXPECT_EQ(res->currentLayout, ResourceLayout::Undefined);
    EXPECT_EQ(res->debugName, "GBuffer0");

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, BasicTransition) {
    ResourceTransitionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterResource(true, 1, ResourceLayout::Undefined, "Texture");

    EXPECT_TRUE(tracker.RequestTransition(id, ResourceLayout::TransferDst, ResourceAccessType::Write, 0));
    EXPECT_EQ(tracker.GetCurrentLayout(id), ResourceLayout::TransferDst);

    EXPECT_TRUE(tracker.RequestTransition(id, ResourceLayout::ShaderReadOnly, ResourceAccessType::Read, 1));
    EXPECT_EQ(tracker.GetCurrentLayout(id), ResourceLayout::ShaderReadOnly);

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, RedundantTransition) {
    ResourceTransitionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterResource(true, 1, ResourceLayout::ShaderReadOnly, "Tex");

    // Transition to same layout + same access -> redundant
    EXPECT_TRUE(tracker.RequestTransition(id, ResourceLayout::ShaderReadOnly, ResourceAccessType::Read, 0));

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.redundantTransitions, 1u);
    EXPECT_EQ(stats.totalTransitions, 0u); // Not counted as real transition

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, GetCurrentLayoutUnknown) {
    ResourceTransitionTracker tracker;
    tracker.Init();

    EXPECT_EQ(tracker.GetCurrentLayout(999), ResourceLayout::Undefined);

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, GetLastAccess) {
    ResourceTransitionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterResource(true, 1, ResourceLayout::Undefined, "Buf");
    EXPECT_EQ(tracker.GetLastAccess(id), ResourceAccessType::None);

    tracker.RequestTransition(id, ResourceLayout::StorageReadWrite, ResourceAccessType::ReadWrite, 0);
    EXPECT_EQ(tracker.GetLastAccess(id), ResourceAccessType::ReadWrite);

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, HazardWriteAfterRead) {
    ResourceTransitionTracker tracker;
    TransitionTrackerConfig config;
    config.detectHazards = true;
    tracker.Init(config);

    u32 id = tracker.RegisterResource(true, 1, ResourceLayout::ShaderReadOnly, "Tex");
    // Set last access to Read
    tracker.RequestTransition(id, ResourceLayout::ShaderReadOnly, ResourceAccessType::Read, 0);

    // Write after read -> hazard
    EXPECT_TRUE(tracker.HasHazard(id, ResourceAccessType::Write));

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, HazardReadAfterWrite) {
    ResourceTransitionTracker tracker;
    TransitionTrackerConfig config;
    config.detectHazards = true;
    tracker.Init(config);

    u32 id = tracker.RegisterResource(true, 1, ResourceLayout::Undefined, "Tex");
    tracker.RequestTransition(id, ResourceLayout::ColorAttachment, ResourceAccessType::Write, 0);

    // Read after write -> hazard
    EXPECT_TRUE(tracker.HasHazard(id, ResourceAccessType::Read));

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, HazardWriteAfterWrite) {
    ResourceTransitionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterResource(true, 1, ResourceLayout::Undefined, "Tex");
    tracker.RequestTransition(id, ResourceLayout::ColorAttachment, ResourceAccessType::Write, 0);

    EXPECT_TRUE(tracker.HasHazard(id, ResourceAccessType::Write));

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, NoHazardReadAfterRead) {
    ResourceTransitionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterResource(true, 1, ResourceLayout::ShaderReadOnly, "Tex");
    tracker.RequestTransition(id, ResourceLayout::ShaderReadOnly, ResourceAccessType::Read, 0);

    // Read after read -> no hazard
    EXPECT_FALSE(tracker.HasHazard(id, ResourceAccessType::Read));

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, ValidateUndefinedToRead) {
    ResourceTransitionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterResource(true, 1, ResourceLayout::Undefined, "UninitTex");

    auto issues = tracker.Validate(id, ResourceLayout::ShaderReadOnly);
    bool hasError = false;
    for (const auto& issue : issues) {
        if (issue.isError) hasError = true;
    }
    EXPECT_TRUE(hasError); // Undefined -> ShaderReadOnly without init

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, ValidateRedundant) {
    ResourceTransitionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterResource(true, 1, ResourceLayout::ShaderReadOnly, "Tex");

    auto issues = tracker.Validate(id, ResourceLayout::ShaderReadOnly);
    bool hasWarning = false;
    for (const auto& issue : issues) {
        if (!issue.isError) hasWarning = true;
    }
    EXPECT_TRUE(hasWarning); // Redundant transition warning

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, ValidateUnregistered) {
    ResourceTransitionTracker tracker;
    tracker.Init();

    auto issues = tracker.Validate(999, ResourceLayout::General);
    EXPECT_FALSE(issues.empty());
    EXPECT_TRUE(issues[0].isError);

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, FlushPendingTransitions) {
    ResourceTransitionTracker tracker;
    tracker.Init();

    u32 id1 = tracker.RegisterResource(true, 1, ResourceLayout::Undefined, "Tex1");
    u32 id2 = tracker.RegisterResource(true, 1, ResourceLayout::Undefined, "Tex2");

    tracker.RequestTransition(id1, ResourceLayout::ColorAttachment, ResourceAccessType::Write, 0);
    tracker.RequestTransition(id2, ResourceLayout::TransferDst, ResourceAccessType::Write, 0);

    auto pending = tracker.FlushPendingTransitions();
    EXPECT_EQ(pending.size(), 2u);

    // After flush, pending should be empty
    auto empty = tracker.FlushPendingTransitions();
    EXPECT_TRUE(empty.empty());

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, TransitionRequestDetails) {
    ResourceTransitionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterResource(true, 1, ResourceLayout::Undefined, "Tex");
    tracker.RequestTransition(id, ResourceLayout::ColorAttachment, ResourceAccessType::Write, 5);

    auto pending = tracker.FlushPendingTransitions();
    EXPECT_EQ(pending.size(), 1u);
    EXPECT_EQ(pending[0].resourceId, id);
    EXPECT_EQ(pending[0].fromLayout, ResourceLayout::Undefined);
    EXPECT_EQ(pending[0].toLayout, ResourceLayout::ColorAttachment);
    EXPECT_EQ(pending[0].accessType, ResourceAccessType::Write);
    EXPECT_EQ(pending[0].passIndex, 5u);

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, Unregister) {
    ResourceTransitionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterResource(true, 1, ResourceLayout::Undefined, "Temp");
    EXPECT_EQ(tracker.GetResourceCount(), 1u);

    tracker.Unregister(id);
    EXPECT_EQ(tracker.GetResourceCount(), 0u);
    EXPECT_EQ(tracker.GetResource(id), nullptr);

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, MaxResourcesLimit) {
    ResourceTransitionTracker tracker;
    TransitionTrackerConfig config;
    config.maxResources = 3;
    tracker.Init(config);

    for (u32 i = 0; i < 3; ++i) {
        EXPECT_NE(tracker.RegisterResource(true), UINT32_MAX);
    }
    EXPECT_EQ(tracker.RegisterResource(true), UINT32_MAX);

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, BufferResource) {
    ResourceTransitionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterResource(false, 1, ResourceLayout::Undefined, "VertexBuffer");

    const auto* res = tracker.GetResource(id);
    EXPECT_FALSE(res->isImage);

    tracker.RequestTransition(id, ResourceLayout::General, ResourceAccessType::ReadWrite, 0);
    EXPECT_EQ(tracker.GetCurrentLayout(id), ResourceLayout::General);

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, StatsTracking) {
    ResourceTransitionTracker tracker;
    TransitionTrackerConfig config;
    config.detectHazards = true;
    tracker.Init(config);

    u32 id = tracker.RegisterResource(true, 1, ResourceLayout::Undefined, "Tex");
    tracker.RequestTransition(id, ResourceLayout::ColorAttachment, ResourceAccessType::Write, 0);
    tracker.RequestTransition(id, ResourceLayout::ShaderReadOnly, ResourceAccessType::Read, 1); // RAW hazard

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.totalResources, 1u);
    EXPECT_EQ(stats.totalTransitions, 2u);
    EXPECT_GE(stats.hazardsDetected, 1u);

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, ResetClearsAll) {
    ResourceTransitionTracker tracker;
    tracker.Init();

    tracker.RegisterResource(true, 1, ResourceLayout::Undefined, "Tex");
    tracker.RequestTransition(0, ResourceLayout::General, ResourceAccessType::Write, 0);

    tracker.Reset();

    EXPECT_EQ(tracker.GetResourceCount(), 0u);
    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.totalTransitions, 0u);
    EXPECT_EQ(stats.hazardsDetected, 0u);

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, RequestTransitionInvalidResource) {
    ResourceTransitionTracker tracker;
    tracker.Init();

    EXPECT_FALSE(tracker.RequestTransition(999, ResourceLayout::General, ResourceAccessType::Read, 0));

    tracker.Shutdown();
}

TEST(ResourceTransitionTracker, PresentTransitionFromColorAttachment) {
    ResourceTransitionTracker tracker;
    tracker.Init();

    u32 id = tracker.RegisterResource(true, 1, ResourceLayout::ColorAttachment, "Swapchain");

    // ColorAttachment -> Present is valid
    EXPECT_TRUE(tracker.RequestTransition(id, ResourceLayout::Present, ResourceAccessType::Read, 0));
    EXPECT_EQ(tracker.GetCurrentLayout(id), ResourceLayout::Present);

    tracker.Shutdown();
}
