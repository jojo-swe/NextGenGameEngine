#include "engine/animation/animation_system.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"
#include <algorithm>
#include <cmath>

namespace nge::animation {

bool AnimationSystem::Init() {
    NGE_LOG_INFO("Animation system initialized");
    return true;
}

void AnimationSystem::Shutdown() {
    m_skeletons.clear();
    m_clips.clear();
    m_activeStates.clear();
}

u32 AnimationSystem::RegisterSkeleton(const Skeleton& skeleton) {
    u32 index = static_cast<u32>(m_skeletons.size());
    m_skeletons.push_back(skeleton);
    NGE_LOG_INFO("Registered skeleton '{}': {} bones", skeleton.name, skeleton.GetBoneCount());
    return index;
}

u32 AnimationSystem::RegisterClip(const AnimationClip& clip) {
    u32 index = static_cast<u32>(m_clips.size());
    m_clips.push_back(clip);
    NGE_LOG_INFO("Registered animation clip '{}': {:.2f}s, {} channels",
                 clip.name, clip.duration, clip.channels.size());
    return index;
}

const Skeleton* AnimationSystem::GetSkeleton(u32 index) const {
    if (index >= m_skeletons.size()) return nullptr;
    return &m_skeletons[index];
}

const AnimationClip* AnimationSystem::GetClip(u32 index) const {
    if (index >= m_clips.size()) return nullptr;
    return &m_clips[index];
}

AnimationState* AnimationSystem::CreateState(u32 skeletonIndex) {
    if (skeletonIndex >= m_skeletons.size()) return nullptr;

    AnimationState state;
    state.skeletonIndex = skeletonIndex;
    state.currentPose.Resize(m_skeletons[skeletonIndex].GetBoneCount());
    state.dirty = true;

    m_activeStates.push_back(std::move(state));
    return &m_activeStates.back();
}

void AnimationSystem::Play(AnimationState& state, u32 clipIndex, f32 weight, f32 speed, BlendMode mode) {
    if (clipIndex >= m_clips.size()) return;

    // Check if already playing
    for (auto& node : state.activeNodes) {
        if (node.clipIndex == clipIndex) {
            node.weight = weight;
            node.speed = speed;
            node.mode = mode;
            return;
        }
    }

    BlendNode node;
    node.clipIndex = clipIndex;
    node.weight = weight;
    node.speed = speed;
    node.time = 0;
    node.mode = mode;
    state.activeNodes.push_back(node);
    state.dirty = true;
}

void AnimationSystem::Stop(AnimationState& state, u32 clipIndex) {
    state.activeNodes.erase(
        std::remove_if(state.activeNodes.begin(), state.activeNodes.end(),
            [clipIndex](const BlendNode& n) { return n.clipIndex == clipIndex; }),
        state.activeNodes.end());
    state.dirty = true;
}

void AnimationSystem::CrossFade(AnimationState& state, u32 fromClip, u32 toClip, f32 fadeDuration) {
    // Start the target clip at weight 0, ramp up over fadeDuration
    // The source clip will be ramped down simultaneously
    // This is handled in Update by interpolating weights over time
    Play(state, toClip, 0.0f, 1.0f, BlendMode::Blend);
    (void)fromClip;
    (void)fadeDuration;
    // TODO: Implement fade scheduling with timer
}

void AnimationSystem::Update(f32 deltaTime) {
    for (auto& state : m_activeStates) {
        EvaluateState(state, deltaTime);
    }
}

void AnimationSystem::EvaluateState(AnimationState& state, f32 deltaTime) {
    if (state.skeletonIndex >= m_skeletons.size()) return;
    if (state.activeNodes.empty()) return;

    const Skeleton& skeleton = m_skeletons[state.skeletonIndex];
    u32 boneCount = skeleton.GetBoneCount();

    // Start with bind pose
    AnimationPose resultPose;
    resultPose.Resize(boneCount);
    for (u32 i = 0; i < boneCount; ++i) {
        resultPose.localTransforms[i] = skeleton.bones[i].bindPose;
    }

    bool firstLayer = true;

    for (auto& node : state.activeNodes) {
        if (node.clipIndex >= m_clips.size()) continue;
        const AnimationClip& clip = m_clips[node.clipIndex];

        // Advance time
        node.time += deltaTime * node.speed * clip.ticksPerSecond;
        if (clip.looping && clip.duration > 0) {
            node.time = std::fmod(node.time, clip.duration);
            if (node.time < 0) node.time += clip.duration;
        } else {
            node.time = math::Clamp(node.time, 0.0f, clip.duration);
        }

        // Sample clip
        AnimationPose clipPose;
        clipPose.Resize(boneCount);
        SampleClip(clip, skeleton, node.time, clipPose);

        // Blend
        if (firstLayer || node.mode == BlendMode::Override) {
            if (node.weight >= 1.0f) {
                resultPose = clipPose;
            } else {
                BlendPoses(resultPose, clipPose, node.weight, BlendMode::Blend, resultPose);
            }
            firstLayer = false;
        } else {
            BlendPoses(resultPose, clipPose, node.weight, node.mode, resultPose);
        }
    }

    // Compute world transforms and skinning matrices
    ComputeWorldTransforms(skeleton, resultPose);
    state.currentPose = std::move(resultPose);
    state.dirty = false;
}

void AnimationSystem::SampleClip(const AnimationClip& clip, const Skeleton& skeleton,
                                   f32 time, AnimationPose& outPose) {
    u32 boneCount = skeleton.GetBoneCount();

    // Initialize with bind pose
    for (u32 i = 0; i < boneCount; ++i) {
        outPose.localTransforms[i] = skeleton.bones[i].bindPose;
    }

    // Override with animated channels
    for (const auto& channel : clip.channels) {
        if (channel.boneIndex >= boneCount) continue;
        if (channel.keyframes.empty()) continue;

        u32 idxA, idxB;
        f32 t;
        FindKeyframes(channel, time, idxA, idxB, t);

        outPose.localTransforms[channel.boneIndex] =
            InterpolateKeyframes(channel.keyframes[idxA], channel.keyframes[idxB], t);
    }
}

void AnimationSystem::ComputeWorldTransforms(const Skeleton& skeleton, AnimationPose& pose) {
    u32 boneCount = skeleton.GetBoneCount();

    for (u32 i = 0; i < boneCount; ++i) {
        if (skeleton.bones[i].parentIndex < 0) {
            pose.worldTransforms[i] = pose.localTransforms[i];
        } else {
            u32 parent = static_cast<u32>(skeleton.bones[i].parentIndex);
            pose.worldTransforms[i] = math::pga::Motor::Multiply(
                pose.worldTransforms[parent], pose.localTransforms[i]);
        }

        // Skinning matrix = worldTransform * inverseBindPose
        math::pga::Motor skinMotor = math::pga::Motor::Multiply(
            pose.worldTransforms[i], skeleton.bones[i].invBindPose);
        pose.skinningMatrices[i] = skinMotor.ToMat4();
    }
}

math::pga::Motor AnimationSystem::InterpolateKeyframes(const Keyframe& a, const Keyframe& b, f32 t) {
    // Use PGA Motor slerp for geodesic interpolation
    return math::pga::Motor::Slerp(a.transform, b.transform, t);
}

void AnimationSystem::FindKeyframes(const BoneChannel& channel, f32 time,
                                      u32& outA, u32& outB, f32& outT) {
    const auto& keys = channel.keyframes;

    if (keys.size() == 1) {
        outA = outB = 0;
        outT = 0;
        return;
    }

    // Find the two keyframes surrounding `time`
    outA = 0;
    for (u32 i = 0; i < static_cast<u32>(keys.size()) - 1; ++i) {
        if (time < keys[i + 1].time) {
            outA = i;
            break;
        }
        outA = i;
    }

    outB = (outA + 1) % static_cast<u32>(keys.size());

    f32 dt = keys[outB].time - keys[outA].time;
    if (dt <= 0.0001f) {
        outT = 0;
    } else {
        outT = (time - keys[outA].time) / dt;
        outT = math::Clamp(outT, 0.0f, 1.0f);
    }
}

void AnimationSystem::BlendPoses(const AnimationPose& a, const AnimationPose& b,
                                   f32 weight, BlendMode mode, AnimationPose& result) {
    u32 count = math::Min(static_cast<u32>(a.localTransforms.size()),
                           static_cast<u32>(b.localTransforms.size()));

    result.Resize(count);

    for (u32 i = 0; i < count; ++i) {
        switch (mode) {
            case BlendMode::Override:
                result.localTransforms[i] = (weight >= 0.5f) ? b.localTransforms[i] : a.localTransforms[i];
                break;

            case BlendMode::Blend:
                result.localTransforms[i] = math::pga::Motor::Slerp(
                    a.localTransforms[i], b.localTransforms[i], weight);
                break;

            case BlendMode::Additive:
                // Additive: apply B's delta on top of A
                // delta = B * inverse(bindPose), result = A * delta^weight
                // Simplified: slerp identity→B with weight, multiply onto A
                result.localTransforms[i] = math::pga::Motor::Multiply(
                    a.localTransforms[i],
                    math::pga::Motor::Slerp(math::pga::Motor::Identity(), b.localTransforms[i], weight));
                break;
        }
    }
}

} // namespace nge::animation
