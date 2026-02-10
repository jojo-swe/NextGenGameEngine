#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Resource Transition Tracker ─────────────────────────────────────
// Tracks the current state of GPU resources (images and buffers) and
// validates/generates transitions. Prevents invalid transitions, detects
// missing barriers, and optimizes transition batching.
//
// Use cases:
//   - Track image layout transitions (Vulkan VkImageLayout)
//   - Track buffer access state for barrier insertion
//   - Validate transitions (no undefined -> shader read without init)
//   - Batch compatible transitions to minimize barrier calls
//   - Detect read-after-write and write-after-read hazards

enum class ResourceLayout : u8 {
    Undefined,
    General,
    ColorAttachment,
    DepthStencilAttachment,
    DepthStencilReadOnly,
    ShaderReadOnly,
    TransferSrc,
    TransferDst,
    Present,
    StorageReadWrite,
};

enum class ResourceAccessType : u8 {
    None,
    Read,
    Write,
    ReadWrite,
};

struct TrackedResource {
    u32              resourceId;
    ResourceLayout   currentLayout;
    ResourceAccessType lastAccess;
    u32              lastAccessPass;    // Pass index of last access
    u32              subresourceCount;  // For images: mip * layer
    bool             isImage;
    std::string      debugName;
};

struct TransitionRequest {
    u32             resourceId;
    ResourceLayout  fromLayout;
    ResourceLayout  toLayout;
    ResourceAccessType accessType;
    u32             passIndex;
    u32             subresource;       // UINT32_MAX = all subresources
};

struct TransitionIssue {
    u32         resourceId;
    std::string description;
    bool        isError;              // true = error, false = warning
};

struct TransitionTrackerConfig {
    u32  maxResources = 4096;
    bool validateTransitions = true;
    bool detectHazards = true;
    bool autoInsertBarriers = true;
};

struct TransitionTrackerStats {
    u32 totalResources;
    u32 totalTransitions;
    u32 redundantTransitions;
    u32 hazardsDetected;
    u32 errorsDetected;
    u32 batchedTransitions;
};

class ResourceTransitionTracker {
public:
    bool Init(const TransitionTrackerConfig& config = {});
    void Shutdown();

    // Register a resource for tracking
    u32 RegisterResource(bool isImage, u32 subresourceCount = 1,
                          ResourceLayout initialLayout = ResourceLayout::Undefined,
                          const std::string& name = "");

    // Request a transition
    bool RequestTransition(u32 resourceId, ResourceLayout newLayout,
                            ResourceAccessType access, u32 passIndex,
                            u32 subresource = UINT32_MAX);

    // Validate a transition (returns issues)
    std::vector<TransitionIssue> Validate(u32 resourceId, ResourceLayout newLayout) const;

    // Get current layout of a resource
    ResourceLayout GetCurrentLayout(u32 resourceId) const;

    // Get last access type
    ResourceAccessType GetLastAccess(u32 resourceId) const;

    // Check for read-after-write or write-after-read hazard
    bool HasHazard(u32 resourceId, ResourceAccessType newAccess) const;

    // Get all pending transitions (for barrier batching)
    std::vector<TransitionRequest> FlushPendingTransitions();

    // Get resource info
    const TrackedResource* GetResource(u32 resourceId) const;

    // Unregister
    void Unregister(u32 resourceId);

    u32 GetResourceCount() const;

    void Reset();

    TransitionTrackerStats GetStats() const;

private:
    bool IsValidTransition(ResourceLayout from, ResourceLayout to) const;

    TransitionTrackerConfig m_config;
    std::unordered_map<u32, TrackedResource> m_resources;
    std::vector<TransitionRequest> m_pendingTransitions;

    u32 m_nextId = 0;
    u32 m_totalTransitions = 0;
    u32 m_redundantTransitions = 0;
    u32 m_hazardsDetected = 0;
    u32 m_errorsDetected = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
