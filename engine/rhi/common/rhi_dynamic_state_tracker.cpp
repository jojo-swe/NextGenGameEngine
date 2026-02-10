#include "engine/rhi/common/rhi_dynamic_state_tracker.h"
#include "engine/core/logging/log.h"
#include <cstring>

namespace nge::rhi {

bool DynamicStateTracker::Init(const DynamicStateTrackerConfig& config) {
    m_config = config;
    m_totalSets = 0;
    m_redundantSets = 0;
    m_validationFailures = 0;
    m_drawsValidated = 0;

    std::memset(m_states, 0, sizeof(m_states));

    NGE_LOG_INFO("Dynamic state tracker initialized: validate={}, trackRedundant={}",
                 config.validateBeforeDraw, config.trackRedundant);
    return true;
}

void DynamicStateTracker::Shutdown() {
    m_requiredStates.clear();
}

void DynamicStateTracker::SetState(DynamicState state, u64 valueHash) {
    std::lock_guard lock(m_mutex);

    u32 idx = static_cast<u32>(state);
    if (idx >= static_cast<u32>(DynamicState::Count)) return;

    auto& s = m_states[idx];

    if (m_config.trackRedundant && s.isSet && s.hash == valueHash) {
        s.redundantCount++;
        m_redundantSets++;
    }

    s.hash = valueHash;
    s.isSet = true;
    s.setCount++;
    m_totalSets++;
}

bool DynamicStateTracker::IsSet(DynamicState state) const {
    std::lock_guard lock(m_mutex);

    u32 idx = static_cast<u32>(state);
    if (idx >= static_cast<u32>(DynamicState::Count)) return false;

    return m_states[idx].isSet;
}

u32 DynamicStateTracker::GetSetCount(DynamicState state) const {
    std::lock_guard lock(m_mutex);

    u32 idx = static_cast<u32>(state);
    if (idx >= static_cast<u32>(DynamicState::Count)) return 0;

    return m_states[idx].setCount;
}

u32 DynamicStateTracker::GetRedundantCount(DynamicState state) const {
    std::lock_guard lock(m_mutex);

    u32 idx = static_cast<u32>(state);
    if (idx >= static_cast<u32>(DynamicState::Count)) return 0;

    return m_states[idx].redundantCount;
}

void DynamicStateTracker::SetRequiredStates(const std::vector<DynamicState>& required) {
    std::lock_guard lock(m_mutex);
    m_requiredStates = required;
}

bool DynamicStateTracker::ValidateForDraw() const {
    std::lock_guard lock(m_mutex);

    m_drawsValidated++;

    if (!m_config.validateBeforeDraw) return true;

    for (auto state : m_requiredStates) {
        u32 idx = static_cast<u32>(state);
        if (idx >= static_cast<u32>(DynamicState::Count)) continue;

        if (!m_states[idx].isSet) {
            m_validationFailures++;
            NGE_LOG_WARN("Dynamic state validation failed: {} not set", GetStateName(state));
            return false;
        }
    }

    return true;
}

std::vector<DynamicState> DynamicStateTracker::GetMissingStates() const {
    std::lock_guard lock(m_mutex);

    std::vector<DynamicState> missing;

    for (auto state : m_requiredStates) {
        u32 idx = static_cast<u32>(state);
        if (idx >= static_cast<u32>(DynamicState::Count)) continue;

        if (!m_states[idx].isSet) {
            missing.push_back(state);
        }
    }

    return missing;
}

void DynamicStateTracker::InvalidateAll() {
    std::lock_guard lock(m_mutex);

    for (u32 i = 0; i < static_cast<u32>(DynamicState::Count); ++i) {
        m_states[i].isSet = false;
    }
}

void DynamicStateTracker::Invalidate(DynamicState state) {
    std::lock_guard lock(m_mutex);

    u32 idx = static_cast<u32>(state);
    if (idx >= static_cast<u32>(DynamicState::Count)) return;

    m_states[idx].isSet = false;
}

void DynamicStateTracker::ResetFrameCounters() {
    std::lock_guard lock(m_mutex);

    for (u32 i = 0; i < static_cast<u32>(DynamicState::Count); ++i) {
        m_states[i].setCount = 0;
        m_states[i].redundantCount = 0;
    }
}

const char* DynamicStateTracker::GetStateName(DynamicState state) {
    switch (state) {
        case DynamicState::Viewport:                return "Viewport";
        case DynamicState::Scissor:                 return "Scissor";
        case DynamicState::LineWidth:               return "LineWidth";
        case DynamicState::DepthBias:               return "DepthBias";
        case DynamicState::BlendConstants:          return "BlendConstants";
        case DynamicState::DepthBounds:             return "DepthBounds";
        case DynamicState::StencilCompareMask:      return "StencilCompareMask";
        case DynamicState::StencilWriteMask:        return "StencilWriteMask";
        case DynamicState::StencilReference:        return "StencilReference";
        case DynamicState::CullMode:                return "CullMode";
        case DynamicState::FrontFace:               return "FrontFace";
        case DynamicState::PrimitiveTopology:       return "PrimitiveTopology";
        case DynamicState::DepthTestEnable:         return "DepthTestEnable";
        case DynamicState::DepthWriteEnable:        return "DepthWriteEnable";
        case DynamicState::DepthCompareOp:          return "DepthCompareOp";
        case DynamicState::StencilTestEnable:       return "StencilTestEnable";
        case DynamicState::RasterizerDiscardEnable: return "RasterizerDiscardEnable";
        case DynamicState::DepthBiasEnable:         return "DepthBiasEnable";
        case DynamicState::PrimitiveRestartEnable:  return "PrimitiveRestartEnable";
        default:                                    return "Unknown";
    }
}

void DynamicStateTracker::Reset() {
    std::lock_guard lock(m_mutex);

    std::memset(m_states, 0, sizeof(m_states));
    m_requiredStates.clear();
    m_totalSets = 0;
    m_redundantSets = 0;
    m_validationFailures = 0;
    m_drawsValidated = 0;
}

DynamicStateTrackerStats DynamicStateTracker::GetStats() const {
    std::lock_guard lock(m_mutex);

    DynamicStateTrackerStats stats{};
    stats.totalSets = m_totalSets;
    stats.redundantSets = m_redundantSets;
    stats.validationFailures = m_validationFailures;
    stats.drawsValidated = m_drawsValidated;

    u32 setCount = 0;
    for (u32 i = 0; i < static_cast<u32>(DynamicState::Count); ++i) {
        if (m_states[i].isSet) setCount++;
    }
    stats.statesCurrentlySet = setCount;

    stats.redundancyRatio = m_totalSets > 0
        ? static_cast<float>(m_redundantSets) / static_cast<float>(m_totalSets)
        : 0.0f;

    return stats;
}

} // namespace nge::rhi
