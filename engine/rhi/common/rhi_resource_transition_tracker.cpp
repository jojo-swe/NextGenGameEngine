#include "engine/rhi/common/rhi_resource_transition_tracker.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool ResourceTransitionTracker::Init(const TransitionTrackerConfig& config) {
    m_config = config;
    m_nextId = 0;
    m_totalTransitions = 0;
    m_redundantTransitions = 0;
    m_hazardsDetected = 0;
    m_errorsDetected = 0;

    NGE_LOG_INFO("Resource transition tracker initialized: maxResources={}, validate={}, hazards={}, autoBarriers={}",
                 config.maxResources, config.validateTransitions, config.detectHazards, config.autoInsertBarriers);
    return true;
}

void ResourceTransitionTracker::Shutdown() {
    m_resources.clear();
    m_pendingTransitions.clear();
}

u32 ResourceTransitionTracker::RegisterResource(bool isImage, u32 subresourceCount,
                                                   ResourceLayout initialLayout,
                                                   const std::string& name) {
    std::lock_guard lock(m_mutex);

    if (m_resources.size() >= m_config.maxResources) {
        NGE_LOG_ERROR("Resource transition tracker: max resources reached ({})", m_config.maxResources);
        return UINT32_MAX;
    }

    u32 id = m_nextId++;

    TrackedResource res;
    res.resourceId = id;
    res.currentLayout = initialLayout;
    res.lastAccess = ResourceAccessType::None;
    res.lastAccessPass = 0;
    res.subresourceCount = subresourceCount;
    res.isImage = isImage;
    res.debugName = name;

    m_resources[id] = std::move(res);
    return id;
}

bool ResourceTransitionTracker::RequestTransition(u32 resourceId, ResourceLayout newLayout,
                                                     ResourceAccessType access, u32 passIndex,
                                                     u32 subresource) {
    std::lock_guard lock(m_mutex);

    auto it = m_resources.find(resourceId);
    if (it == m_resources.end()) return false;

    auto& res = it->second;

    // Check for redundant transition
    if (res.currentLayout == newLayout &&
        (res.lastAccess == access || res.lastAccess == ResourceAccessType::None)) {
        m_redundantTransitions++;
        return true; // No-op but not an error
    }

    // Validate
    if (m_config.validateTransitions) {
        if (!IsValidTransition(res.currentLayout, newLayout)) {
            NGE_LOG_ERROR("Invalid transition for '{}': {} -> {}",
                          res.debugName, static_cast<u8>(res.currentLayout), static_cast<u8>(newLayout));
            m_errorsDetected++;
            return false;
        }
    }

    // Detect hazards
    if (m_config.detectHazards) {
        if (HasHazard(resourceId, access)) {
            NGE_LOG_WARN("Hazard detected for '{}': lastAccess={}, newAccess={}, pass={}",
                         res.debugName, static_cast<u8>(res.lastAccess), static_cast<u8>(access), passIndex);
            m_hazardsDetected++;
        }
    }

    // Record transition
    TransitionRequest req;
    req.resourceId = resourceId;
    req.fromLayout = res.currentLayout;
    req.toLayout = newLayout;
    req.accessType = access;
    req.passIndex = passIndex;
    req.subresource = subresource;

    m_pendingTransitions.push_back(req);

    // Update tracked state
    res.currentLayout = newLayout;
    res.lastAccess = access;
    res.lastAccessPass = passIndex;

    m_totalTransitions++;
    return true;
}

std::vector<TransitionIssue> ResourceTransitionTracker::Validate(u32 resourceId, ResourceLayout newLayout) const {
    std::lock_guard lock(m_mutex);

    std::vector<TransitionIssue> issues;

    auto it = m_resources.find(resourceId);
    if (it == m_resources.end()) {
        TransitionIssue issue;
        issue.resourceId = resourceId;
        issue.description = "Resource not registered";
        issue.isError = true;
        issues.push_back(std::move(issue));
        return issues;
    }

    const auto& res = it->second;

    // Transition from Undefined to read-only without initialization
    if (res.currentLayout == ResourceLayout::Undefined &&
        (newLayout == ResourceLayout::ShaderReadOnly || newLayout == ResourceLayout::TransferSrc)) {
        TransitionIssue issue;
        issue.resourceId = resourceId;
        issue.description = "Transition from Undefined to read layout without initialization for '" + res.debugName + "'";
        issue.isError = true;
        issues.push_back(std::move(issue));
    }

    // Transition to same layout (warning)
    if (res.currentLayout == newLayout) {
        TransitionIssue issue;
        issue.resourceId = resourceId;
        issue.description = "Redundant transition (already in target layout) for '" + res.debugName + "'";
        issue.isError = false;
        issues.push_back(std::move(issue));
    }

    if (!IsValidTransition(res.currentLayout, newLayout)) {
        TransitionIssue issue;
        issue.resourceId = resourceId;
        issue.description = "Invalid layout transition for '" + res.debugName + "'";
        issue.isError = true;
        issues.push_back(std::move(issue));
    }

    return issues;
}

ResourceLayout ResourceTransitionTracker::GetCurrentLayout(u32 resourceId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_resources.find(resourceId);
    if (it == m_resources.end()) return ResourceLayout::Undefined;
    return it->second.currentLayout;
}

ResourceAccessType ResourceTransitionTracker::GetLastAccess(u32 resourceId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_resources.find(resourceId);
    if (it == m_resources.end()) return ResourceAccessType::None;
    return it->second.lastAccess;
}

bool ResourceTransitionTracker::HasHazard(u32 resourceId, ResourceAccessType newAccess) const {
    // Note: caller must hold m_mutex if calling from internal method,
    // or this is called from public API which locks

    auto it = m_resources.find(resourceId);
    if (it == m_resources.end()) return false;

    const auto& res = it->second;

    // Write-after-read hazard
    if (res.lastAccess == ResourceAccessType::Read &&
        (newAccess == ResourceAccessType::Write || newAccess == ResourceAccessType::ReadWrite)) {
        return true;
    }

    // Read-after-write hazard
    if ((res.lastAccess == ResourceAccessType::Write || res.lastAccess == ResourceAccessType::ReadWrite) &&
        newAccess == ResourceAccessType::Read) {
        return true;
    }

    // Write-after-write hazard
    if ((res.lastAccess == ResourceAccessType::Write || res.lastAccess == ResourceAccessType::ReadWrite) &&
        (newAccess == ResourceAccessType::Write || newAccess == ResourceAccessType::ReadWrite)) {
        return true;
    }

    return false;
}

std::vector<TransitionRequest> ResourceTransitionTracker::FlushPendingTransitions() {
    std::lock_guard lock(m_mutex);

    std::vector<TransitionRequest> result = std::move(m_pendingTransitions);
    m_pendingTransitions.clear();
    return result;
}

const TrackedResource* ResourceTransitionTracker::GetResource(u32 resourceId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_resources.find(resourceId);
    if (it == m_resources.end()) return nullptr;
    return &it->second;
}

void ResourceTransitionTracker::Unregister(u32 resourceId) {
    std::lock_guard lock(m_mutex);
    m_resources.erase(resourceId);
}

u32 ResourceTransitionTracker::GetResourceCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_resources.size());
}

void ResourceTransitionTracker::Reset() {
    std::lock_guard lock(m_mutex);
    m_resources.clear();
    m_pendingTransitions.clear();
    m_nextId = 0;
    m_totalTransitions = 0;
    m_redundantTransitions = 0;
    m_hazardsDetected = 0;
    m_errorsDetected = 0;
}

TransitionTrackerStats ResourceTransitionTracker::GetStats() const {
    std::lock_guard lock(m_mutex);

    TransitionTrackerStats stats{};
    stats.totalResources = static_cast<u32>(m_resources.size());
    stats.totalTransitions = m_totalTransitions;
    stats.redundantTransitions = m_redundantTransitions;
    stats.hazardsDetected = m_hazardsDetected;
    stats.errorsDetected = m_errorsDetected;
    stats.batchedTransitions = 0; // Updated by FlushPendingTransitions consumer

    return stats;
}

bool ResourceTransitionTracker::IsValidTransition(ResourceLayout from, ResourceLayout to) const {
    // Undefined -> anything is valid (initial transition)
    if (from == ResourceLayout::Undefined) return true;

    // Same layout is valid (redundant but not invalid)
    if (from == to) return true;

    // General can go to/from anything
    if (from == ResourceLayout::General || to == ResourceLayout::General) return true;

    // Present can only come from ColorAttachment or General
    if (to == ResourceLayout::Present) {
        return from == ResourceLayout::ColorAttachment || from == ResourceLayout::General;
    }

    // All other transitions are valid in general
    return true;
}

} // namespace nge::rhi
