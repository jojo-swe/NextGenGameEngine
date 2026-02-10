#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_dynamic_state_tracker.h"

using namespace nge::rhi;

TEST(DynamicStateTracker, InitAndShutdown) {
    DynamicStateTracker tracker;
    EXPECT_TRUE(tracker.Init());
    EXPECT_FALSE(tracker.IsSet(DynamicState::Viewport));
    tracker.Shutdown();
}

TEST(DynamicStateTracker, SetState) {
    DynamicStateTracker tracker;
    tracker.Init();

    tracker.SetState(DynamicState::Viewport, 0xABCD);
    EXPECT_TRUE(tracker.IsSet(DynamicState::Viewport));
    EXPECT_FALSE(tracker.IsSet(DynamicState::Scissor));

    tracker.Shutdown();
}

TEST(DynamicStateTracker, SetMultipleStates) {
    DynamicStateTracker tracker;
    tracker.Init();

    tracker.SetState(DynamicState::Viewport, 1);
    tracker.SetState(DynamicState::Scissor, 2);
    tracker.SetState(DynamicState::DepthBias, 3);

    EXPECT_TRUE(tracker.IsSet(DynamicState::Viewport));
    EXPECT_TRUE(tracker.IsSet(DynamicState::Scissor));
    EXPECT_TRUE(tracker.IsSet(DynamicState::DepthBias));
    EXPECT_FALSE(tracker.IsSet(DynamicState::LineWidth));

    tracker.Shutdown();
}

TEST(DynamicStateTracker, RedundantSetDetection) {
    DynamicStateTracker tracker;
    DynamicStateTrackerConfig config;
    config.trackRedundant = true;
    tracker.Init(config);

    tracker.SetState(DynamicState::Viewport, 100);
    tracker.SetState(DynamicState::Viewport, 100); // Redundant
    tracker.SetState(DynamicState::Viewport, 200); // Different value

    EXPECT_EQ(tracker.GetSetCount(DynamicState::Viewport), 3u);
    EXPECT_EQ(tracker.GetRedundantCount(DynamicState::Viewport), 1u);

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.redundantSets, 1u);

    tracker.Shutdown();
}

TEST(DynamicStateTracker, RedundantTrackingDisabled) {
    DynamicStateTracker tracker;
    DynamicStateTrackerConfig config;
    config.trackRedundant = false;
    tracker.Init(config);

    tracker.SetState(DynamicState::Viewport, 100);
    tracker.SetState(DynamicState::Viewport, 100);

    EXPECT_EQ(tracker.GetRedundantCount(DynamicState::Viewport), 0u);

    tracker.Shutdown();
}

TEST(DynamicStateTracker, ValidateForDrawSuccess) {
    DynamicStateTracker tracker;
    tracker.Init();

    tracker.SetRequiredStates({DynamicState::Viewport, DynamicState::Scissor});
    tracker.SetState(DynamicState::Viewport, 1);
    tracker.SetState(DynamicState::Scissor, 2);

    EXPECT_TRUE(tracker.ValidateForDraw());

    tracker.Shutdown();
}

TEST(DynamicStateTracker, ValidateForDrawFailure) {
    DynamicStateTracker tracker;
    tracker.Init();

    tracker.SetRequiredStates({DynamicState::Viewport, DynamicState::Scissor});
    tracker.SetState(DynamicState::Viewport, 1);
    // Scissor not set

    EXPECT_FALSE(tracker.ValidateForDraw());

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.validationFailures, 1u);

    tracker.Shutdown();
}

TEST(DynamicStateTracker, ValidateDisabled) {
    DynamicStateTracker tracker;
    DynamicStateTrackerConfig config;
    config.validateBeforeDraw = false;
    tracker.Init(config);

    tracker.SetRequiredStates({DynamicState::Viewport});
    // Viewport not set, but validation disabled

    EXPECT_TRUE(tracker.ValidateForDraw());

    tracker.Shutdown();
}

TEST(DynamicStateTracker, GetMissingStates) {
    DynamicStateTracker tracker;
    tracker.Init();

    tracker.SetRequiredStates({DynamicState::Viewport, DynamicState::Scissor, DynamicState::DepthBias});
    tracker.SetState(DynamicState::Viewport, 1);

    auto missing = tracker.GetMissingStates();
    EXPECT_EQ(missing.size(), 2u); // Scissor and DepthBias

    tracker.Shutdown();
}

TEST(DynamicStateTracker, GetMissingStatesAllSet) {
    DynamicStateTracker tracker;
    tracker.Init();

    tracker.SetRequiredStates({DynamicState::Viewport});
    tracker.SetState(DynamicState::Viewport, 1);

    auto missing = tracker.GetMissingStates();
    EXPECT_EQ(missing.size(), 0u);

    tracker.Shutdown();
}

TEST(DynamicStateTracker, InvalidateAll) {
    DynamicStateTracker tracker;
    tracker.Init();

    tracker.SetState(DynamicState::Viewport, 1);
    tracker.SetState(DynamicState::Scissor, 2);
    tracker.SetState(DynamicState::DepthBias, 3);

    tracker.InvalidateAll();

    EXPECT_FALSE(tracker.IsSet(DynamicState::Viewport));
    EXPECT_FALSE(tracker.IsSet(DynamicState::Scissor));
    EXPECT_FALSE(tracker.IsSet(DynamicState::DepthBias));

    tracker.Shutdown();
}

TEST(DynamicStateTracker, InvalidateSingle) {
    DynamicStateTracker tracker;
    tracker.Init();

    tracker.SetState(DynamicState::Viewport, 1);
    tracker.SetState(DynamicState::Scissor, 2);

    tracker.Invalidate(DynamicState::Viewport);

    EXPECT_FALSE(tracker.IsSet(DynamicState::Viewport));
    EXPECT_TRUE(tracker.IsSet(DynamicState::Scissor));

    tracker.Shutdown();
}

TEST(DynamicStateTracker, ResetFrameCounters) {
    DynamicStateTracker tracker;
    tracker.Init();

    tracker.SetState(DynamicState::Viewport, 1);
    tracker.SetState(DynamicState::Viewport, 1);

    EXPECT_EQ(tracker.GetSetCount(DynamicState::Viewport), 2u);

    tracker.ResetFrameCounters();

    EXPECT_EQ(tracker.GetSetCount(DynamicState::Viewport), 0u);
    EXPECT_EQ(tracker.GetRedundantCount(DynamicState::Viewport), 0u);
    EXPECT_TRUE(tracker.IsSet(DynamicState::Viewport)); // Still set, just counters reset

    tracker.Shutdown();
}

TEST(DynamicStateTracker, GetStateName) {
    EXPECT_STREQ(DynamicStateTracker::GetStateName(DynamicState::Viewport), "Viewport");
    EXPECT_STREQ(DynamicStateTracker::GetStateName(DynamicState::Scissor), "Scissor");
    EXPECT_STREQ(DynamicStateTracker::GetStateName(DynamicState::DepthBias), "DepthBias");
    EXPECT_STREQ(DynamicStateTracker::GetStateName(DynamicState::CullMode), "CullMode");
    EXPECT_STREQ(DynamicStateTracker::GetStateName(DynamicState::PrimitiveTopology), "PrimitiveTopology");
}

TEST(DynamicStateTracker, StatsTracking) {
    DynamicStateTracker tracker;
    tracker.Init();

    tracker.SetRequiredStates({DynamicState::Viewport, DynamicState::Scissor});

    tracker.SetState(DynamicState::Viewport, 1);
    tracker.SetState(DynamicState::Scissor, 2);
    tracker.SetState(DynamicState::Viewport, 1); // Redundant

    tracker.ValidateForDraw(); // Success
    tracker.Invalidate(DynamicState::Scissor);
    tracker.ValidateForDraw(); // Failure

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.totalSets, 3u);
    EXPECT_EQ(stats.redundantSets, 1u);
    EXPECT_EQ(stats.drawsValidated, 2u);
    EXPECT_EQ(stats.validationFailures, 1u);
    EXPECT_EQ(stats.statesCurrentlySet, 1u); // Only Viewport
    EXPECT_GT(stats.redundancyRatio, 0.0f);

    tracker.Shutdown();
}

TEST(DynamicStateTracker, ResetClearsAll) {
    DynamicStateTracker tracker;
    tracker.Init();

    tracker.SetState(DynamicState::Viewport, 1);
    tracker.SetRequiredStates({DynamicState::Viewport});
    tracker.ValidateForDraw();

    tracker.Reset();

    EXPECT_FALSE(tracker.IsSet(DynamicState::Viewport));
    auto missing = tracker.GetMissingStates();
    EXPECT_EQ(missing.size(), 0u); // No required states after reset

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.totalSets, 0u);
    EXPECT_EQ(stats.drawsValidated, 0u);

    tracker.Shutdown();
}

TEST(DynamicStateTracker, AllDynamicStates) {
    DynamicStateTracker tracker;
    tracker.Init();

    // Set all states
    for (u32 i = 0; i < static_cast<u32>(DynamicState::Count); ++i) {
        tracker.SetState(static_cast<DynamicState>(i), i + 1);
    }

    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.statesCurrentlySet, static_cast<u32>(DynamicState::Count));

    tracker.Shutdown();
}

TEST(DynamicStateTracker, ChangeRequiredStates) {
    DynamicStateTracker tracker;
    tracker.Init();

    tracker.SetRequiredStates({DynamicState::Viewport});
    tracker.SetState(DynamicState::Viewport, 1);
    EXPECT_TRUE(tracker.ValidateForDraw());

    // Change required states (simulating pipeline bind)
    tracker.SetRequiredStates({DynamicState::Viewport, DynamicState::Scissor});
    EXPECT_FALSE(tracker.ValidateForDraw()); // Scissor not set

    tracker.SetState(DynamicState::Scissor, 2);
    EXPECT_TRUE(tracker.ValidateForDraw());

    tracker.Shutdown();
}
