#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Dynamic State Tracker ───────────────────────────────────────────
// Tracks which Vulkan dynamic states are currently set and validates
// that all required dynamic states are configured before a draw/dispatch.
// Prevents missing dynamic state errors and tracks redundant sets.
//
// Use cases:
//   - Track viewport, scissor, blend constants, depth bias, etc.
//   - Validate all required dynamic states before draw
//   - Detect redundant dynamic state sets (same value)
//   - Reset tracking on pipeline bind (new required states)
//   - Stats: sets, redundant sets, validation failures

enum class DynamicState : u8 {
    Viewport,
    Scissor,
    LineWidth,
    DepthBias,
    BlendConstants,
    DepthBounds,
    StencilCompareMask,
    StencilWriteMask,
    StencilReference,
    CullMode,
    FrontFace,
    PrimitiveTopology,
    DepthTestEnable,
    DepthWriteEnable,
    DepthCompareOp,
    StencilTestEnable,
    RasterizerDiscardEnable,
    DepthBiasEnable,
    PrimitiveRestartEnable,
    Count,
};

struct DynamicStateValue {
    u64  hash;           // Hash of the state value
    bool isSet;          // Whether this state has been set
    u32  setCount;       // How many times set this frame
    u32  redundantCount; // How many times set redundantly
};

struct DynamicStateTrackerConfig {
    bool validateBeforeDraw = true;
    bool trackRedundant = true;
};

struct DynamicStateTrackerStats {
    u32 totalSets;
    u32 redundantSets;
    u32 validationFailures;
    u32 statesCurrentlySet;
    u32 drawsValidated;
    float redundancyRatio;
};

class DynamicStateTracker {
public:
    bool Init(const DynamicStateTrackerConfig& config = {});
    void Shutdown();

    // Mark a dynamic state as set with a value hash
    void SetState(DynamicState state, u64 valueHash);

    // Check if a state is currently set
    bool IsSet(DynamicState state) const;

    // Get how many times a state was set this frame
    u32 GetSetCount(DynamicState state) const;

    // Get how many redundant sets for a state
    u32 GetRedundantCount(DynamicState state) const;

    // Declare which dynamic states a pipeline requires
    void SetRequiredStates(const std::vector<DynamicState>& required);

    // Validate all required states are set before draw
    bool ValidateForDraw() const;

    // Get list of missing required states
    std::vector<DynamicState> GetMissingStates() const;

    // Invalidate all states (e.g., on pipeline bind)
    void InvalidateAll();

    // Invalidate a specific state
    void Invalidate(DynamicState state);

    // Reset per-frame counters
    void ResetFrameCounters();

    // Get state name for debugging
    static const char* GetStateName(DynamicState state);

    void Reset();

    DynamicStateTrackerStats GetStats() const;

private:
    DynamicStateTrackerConfig m_config;

    DynamicStateValue m_states[static_cast<u32>(DynamicState::Count)];
    std::vector<DynamicState> m_requiredStates;

    mutable u32 m_totalSets = 0;
    mutable u32 m_redundantSets = 0;
    mutable u32 m_validationFailures = 0;
    mutable u32 m_drawsValidated = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
