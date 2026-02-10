#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Descriptor Set Versioning Tracker ───────────────────────────────
// Tracks descriptor set content versions to detect stale bindings and
// avoid redundant descriptor updates. Each descriptor set has a generation
// counter that increments on write; consumers compare their cached version
// to decide if a rebind is needed.
//
// Use cases:
//   - Skip redundant vkUpdateDescriptorSets when content unchanged
//   - Detect stale descriptor sets (consumer version < current version)
//   - Per-binding dirty tracking within a set
//   - Frame-based version snapshots for multi-buffered sets
//   - Stats: updates avoided, stale detections

struct DescSetVersionInfo {
    u32         setId;
    u64         currentVersion;
    u64         lastUpdateFrame;
    u32         bindingCount;
    std::string debugName;
};

struct BindingVersion {
    u64 version;
    u64 contentHash;     // Hash of binding content (buffer handle, offset, etc.)
};

struct DescSetVersionConfig {
    u32  maxSets = 4096;
    u32  maxBindingsPerSet = 32;
    bool trackPerBinding = true;
};

struct DescSetVersionStats {
    u32 totalSets;
    u32 totalUpdates;
    u32 updatesAvoided;
    u32 staleDetections;
    u32 totalBindingUpdates;
    u32 bindingUpdatesAvoided;
    float avoidanceRatio;
};

class DescriptorSetVersionTracker {
public:
    bool Init(const DescSetVersionConfig& config = {});
    void Shutdown();

    // Register a descriptor set for version tracking
    u32 RegisterSet(u32 bindingCount, const std::string& name = "");

    // Mark a set as updated (increments version)
    void MarkUpdated(u32 setId, u32 frameIndex);

    // Mark a specific binding within a set as updated
    void MarkBindingUpdated(u32 setId, u32 bindingIndex, u64 contentHash);

    // Check if a set needs rebinding (consumer version < current)
    bool NeedsRebind(u32 setId, u64 consumerVersion) const;

    // Check if a specific binding has changed since a given version
    bool BindingChanged(u32 setId, u32 bindingIndex, u64 consumerVersion) const;

    // Get current version of a set
    u64 GetVersion(u32 setId) const;

    // Get current version of a specific binding
    u64 GetBindingVersion(u32 setId, u32 bindingIndex) const;

    // Get content hash of a binding
    u64 GetBindingContentHash(u32 setId, u32 bindingIndex) const;

    // Record that a consumer has consumed the current version (for stats)
    void RecordConsume(u32 setId, u64 consumerVersion);

    // Get set info
    const DescSetVersionInfo* GetSetInfo(u32 setId) const;

    // Unregister
    void Unregister(u32 setId);

    u32 GetSetCount() const;

    void Reset();

    DescSetVersionStats GetStats() const;

private:
    DescSetVersionConfig m_config;

    struct TrackedSet {
        DescSetVersionInfo info;
        std::vector<BindingVersion> bindings;
    };

    std::unordered_map<u32, TrackedSet> m_sets;
    u32 m_nextId = 0;

    u32 m_totalUpdates = 0;
    u32 m_updatesAvoided = 0;
    u32 m_staleDetections = 0;
    u32 m_totalBindingUpdates = 0;
    u32 m_bindingUpdatesAvoided = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
