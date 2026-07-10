#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/core/math/pga.h"

#include <vector>
#include <string>
#include <unordered_map>

namespace nge::animation {

// ─── Skeleton ────────────────────────────────────────────────────────────
// Hierarchical bone structure. Each bone stores its bind pose as a PGA Motor.

struct Bone {
    std::string name;
    i32         parentIndex = -1; // -1 = root bone
    pga::Motor bindPose;    // Local bind-pose transform
    pga::Motor invBindPose; // Inverse bind-pose (for skinning)
};

struct Skeleton {
    std::string        name;
    std::vector<Bone>  bones;
    std::unordered_map<std::string, u32> boneNameToIndex;

    u32 GetBoneIndex(const std::string& boneName) const {
        auto it = boneNameToIndex.find(boneName);
        return (it != boneNameToIndex.end()) ? it->second : UINT32_MAX;
    }

    u32 GetBoneCount() const { return static_cast<u32>(bones.size()); }
};

// ─── Animation Clip ──────────────────────────────────────────────────────
// Stores keyframed bone transforms over time.

struct Keyframe {
    f32              time;
    pga::Motor transform; // Local bone transform at this keyframe
};

struct BoneChannel {
    u32                   boneIndex;
    std::vector<Keyframe> keyframes;
};

struct AnimationClip {
    std::string              name;
    f32                      duration = 0;
    f32                      ticksPerSecond = 30.0f;
    bool                     looping = true;
    std::vector<BoneChannel> channels;
};

// ─── Animation Pose ──────────────────────────────────────────────────────
// Result of evaluating an animation at a specific time.

struct AnimationPose {
    std::vector<pga::Motor> localTransforms;  // Per-bone local
    std::vector<pga::Motor> worldTransforms;  // Per-bone world (computed)
    std::vector<math::Mat4>       skinningMatrices;  // Final matrices for GPU upload

    void Resize(u32 boneCount) {
        localTransforms.resize(boneCount, pga::Motor::Identity());
        worldTransforms.resize(boneCount, pga::Motor::Identity());
        skinningMatrices.resize(boneCount, math::Mat4::Identity());
    }
};

// ─── Blend Tree Node ─────────────────────────────────────────────────────
// Simple blend tree for combining multiple animations.

enum class BlendMode : u8 {
    Override,   // Replace completely
    Additive,   // Add on top
    Blend,      // Linear blend by weight
};

struct BlendNode {
    u32       clipIndex = UINT32_MAX;
    f32       weight = 1.0f;
    f32       speed = 1.0f;
    f32       time = 0;
    BlendMode mode = BlendMode::Override;
};

struct CrossFadeState {
    u32       fromClip = UINT32_MAX;
    u32       toClip = UINT32_MAX;
    f32       duration = 0;
    f32       elapsed = 0;
    bool      active = false;
};

// ─── Animation State ─────────────────────────────────────────────────────
// Per-entity animation state.

struct AnimationState {
    u32                    skeletonIndex = UINT32_MAX;
    std::vector<BlendNode> activeNodes;  // Currently playing animations
    AnimationPose          currentPose;
    CrossFadeState         crossFade;
    bool                   dirty = true;
};

// ─── Animation System ────────────────────────────────────────────────────
// Manages skeletons, clips, and per-entity animation evaluation.

class AnimationSystem {
public:
    bool Init();
    void Shutdown();

    // Resource management
    u32 RegisterSkeleton(const Skeleton& skeleton);
    u32 RegisterClip(const AnimationClip& clip);
    const Skeleton* GetSkeleton(u32 index) const;
    const AnimationClip* GetClip(u32 index) const;

    // Per-entity state
    AnimationState* CreateState(u32 skeletonIndex);

    // Playback control
    void Play(AnimationState& state, u32 clipIndex, f32 weight = 1.0f, f32 speed = 1.0f, BlendMode mode = BlendMode::Override);
    void Stop(AnimationState& state, u32 clipIndex);
    void CrossFade(AnimationState& state, u32 fromClip, u32 toClip, f32 fadeDuration);

    // Evaluation
    void Update(f32 deltaTime);
    void EvaluateState(AnimationState& state, f32 deltaTime);

    // Sample a clip at a given time into a pose
    void SampleClip(const AnimationClip& clip, const Skeleton& skeleton,
                     f32 time, AnimationPose& outPose);

    // Compute world transforms and skinning matrices from local transforms
    void ComputeWorldTransforms(const Skeleton& skeleton, AnimationPose& pose);

private:
    // Interpolate between two keyframes
    pga::Motor InterpolateKeyframes(const Keyframe& a, const Keyframe& b, f32 t);

    // Find keyframes surrounding a given time
    void FindKeyframes(const BoneChannel& channel, f32 time,
                        u32& outA, u32& outB, f32& outT);

    // Blend two poses
    void BlendPoses(const AnimationPose& a, const AnimationPose& b,
                     f32 weight, BlendMode mode, AnimationPose& result);

    std::vector<Skeleton>       m_skeletons;
    std::vector<AnimationClip>  m_clips;
    std::vector<AnimationState> m_activeStates; // All active states for batch update
};

} // namespace nge::animation
