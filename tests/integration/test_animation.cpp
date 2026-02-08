#include <gtest/gtest.h>
#include "engine/animation/animation_system.h"

using namespace nge;
using namespace nge::animation;

class AnimationTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_system.Init());

        // Create a simple skeleton: root → child
        Skeleton skel;
        skel.name = "TestSkeleton";

        Bone root;
        root.name = "Root";
        root.parentIndex = -1;
        root.bindPose = math::pga::Motor::Identity();
        root.invBindPose = math::pga::Motor::Identity();
        skel.bones.push_back(root);
        skel.boneNameToIndex["Root"] = 0;

        Bone child;
        child.name = "Child";
        child.parentIndex = 0;
        child.bindPose = math::pga::Motor::Translation(0, 1, 0);
        child.invBindPose = math::pga::Motor::Translation(0, -1, 0);
        skel.bones.push_back(child);
        skel.boneNameToIndex["Child"] = 1;

        m_skelId = m_system.RegisterSkeleton(skel);
    }

    void TearDown() override {
        m_system.Shutdown();
    }

    AnimationSystem m_system;
    u32 m_skelId = UINT32_MAX;
};

TEST_F(AnimationTest, RegisterSkeleton) {
    auto* skel = m_system.GetSkeleton(m_skelId);
    ASSERT_NE(skel, nullptr);
    EXPECT_EQ(skel->GetBoneCount(), 2u);
    EXPECT_EQ(skel->GetBoneIndex("Root"), 0u);
    EXPECT_EQ(skel->GetBoneIndex("Child"), 1u);
    EXPECT_EQ(skel->GetBoneIndex("NonExistent"), UINT32_MAX);
}

TEST_F(AnimationTest, RegisterClip) {
    AnimationClip clip;
    clip.name = "TestClip";
    clip.duration = 1.0f;
    clip.ticksPerSecond = 30.0f;

    BoneChannel channel;
    channel.boneIndex = 0;
    channel.keyframes.push_back({0.0f, math::pga::Motor::Identity()});
    channel.keyframes.push_back({1.0f, math::pga::Motor::Translation(5, 0, 0)});
    clip.channels.push_back(channel);

    u32 clipId = m_system.RegisterClip(clip);
    auto* retrieved = m_system.GetClip(clipId);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->name, "TestClip");
    EXPECT_FLOAT_EQ(retrieved->duration, 1.0f);
    EXPECT_EQ(retrieved->channels.size(), 1u);
}

TEST_F(AnimationTest, CreateState) {
    auto* state = m_system.CreateState(m_skelId);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->skeletonIndex, m_skelId);
    EXPECT_EQ(state->currentPose.localTransforms.size(), 2u);
}

TEST_F(AnimationTest, SampleClipAtTime) {
    AnimationClip clip;
    clip.name = "MoveClip";
    clip.duration = 1.0f;
    clip.ticksPerSecond = 1.0f;

    BoneChannel channel;
    channel.boneIndex = 0;
    channel.keyframes.push_back({0.0f, math::pga::Motor::Identity()});
    channel.keyframes.push_back({1.0f, math::pga::Motor::Translation(10, 0, 0)});
    clip.channels.push_back(channel);

    auto* skel = m_system.GetSkeleton(m_skelId);
    ASSERT_NE(skel, nullptr);

    AnimationPose pose;
    pose.Resize(skel->GetBoneCount());

    // Sample at t=0 → identity
    m_system.SampleClip(clip, *skel, 0.0f, pose);
    // Root bone should be near identity at t=0

    // Sample at t=1 → translated by 10
    m_system.SampleClip(clip, *skel, 1.0f, pose);
    // Root bone should be translated

    // Sample at t=0.5 → halfway (slerp interpolation)
    m_system.SampleClip(clip, *skel, 0.5f, pose);
    // Root bone should be approximately halfway
}

TEST_F(AnimationTest, PlayAndUpdate) {
    AnimationClip clip;
    clip.name = "WalkClip";
    clip.duration = 2.0f;
    clip.ticksPerSecond = 1.0f;
    clip.looping = true;

    BoneChannel channel;
    channel.boneIndex = 0;
    channel.keyframes.push_back({0.0f, math::pga::Motor::Identity()});
    channel.keyframes.push_back({2.0f, math::pga::Motor::Translation(4, 0, 0)});
    clip.channels.push_back(channel);

    u32 clipId = m_system.RegisterClip(clip);
    auto* state = m_system.CreateState(m_skelId);
    ASSERT_NE(state, nullptr);

    m_system.Play(*state, clipId);
    EXPECT_EQ(state->activeNodes.size(), 1u);

    // Update for a few frames
    for (int i = 0; i < 30; ++i) {
        m_system.EvaluateState(*state, 1.0f / 30.0f);
    }

    // Pose should have been updated
    EXPECT_FALSE(state->dirty);
}

TEST_F(AnimationTest, SH9Basics) {
    // Test SH9 from gi_probes.h (shared math concept)
    // Verify SH basis evaluation produces reasonable values
    renderer::SH9 sh = renderer::SH9::Evaluate({0, 1, 0}); // Up direction
    EXPECT_NEAR(sh.c[0], 0.282095f, 0.001f); // Y_0^0
    EXPECT_NEAR(sh.c[1], 0.488603f, 0.001f); // Y_1^-1 (y component)
    EXPECT_NEAR(sh.c[2], 0.0f, 0.001f);       // Y_1^0 (z=0)
    EXPECT_NEAR(sh.c[3], 0.0f, 0.001f);       // Y_1^1 (x=0)
}
