#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Resource Version Tracker ────────────────────────────────────────
// Assigns a monotonically increasing generation counter to each GPU
// resource. Consumers hold a (handle, generation) pair; if the current
// generation doesn't match, the reference is stale (resource was
// destroyed and handle recycled).
//
// Use cases:
//   - Detect stale descriptor set references after resource destruction
//   - Validate render graph resource bindings
//   - Debug dangling GPU handle access
//   - Safe handle recycling with ABA-problem prevention

struct VersionedHandle {
    u64 handle;
    u32 generation;
};

struct ResourceVersionEntry {
    u64         handle;
    u32         currentGeneration;
    std::string debugName;
    bool        alive;
};

struct ResourceVersionConfig {
    u32 maxResources = 65536;
    bool trackDestroyedHistory = true; // Keep history of destroyed resources
    u32  destroyedHistorySize = 256;   // Rolling history
};

struct ResourceVersionStats {
    u32 aliveResources;
    u32 totalCreated;
    u32 totalDestroyed;
    u32 staleAccessesDetected;
    u32 maxGeneration;
};

class ResourceVersionTracker {
public:
    bool Init(const ResourceVersionConfig& config = {});
    void Shutdown();

    // Register a new resource, returns versioned handle
    VersionedHandle Register(u64 handle, const std::string& debugName = "");

    // Destroy a resource (increments generation, marks dead)
    void Destroy(u64 handle);

    // Re-register a recycled handle (bumps generation)
    VersionedHandle Reregister(u64 handle, const std::string& debugName = "");

    // Validate a versioned handle — returns true if alive and generation matches
    bool IsValid(const VersionedHandle& vh) const;

    // Get current generation for a handle
    u32 GetGeneration(u64 handle) const;

    // Check if a handle is currently alive
    bool IsAlive(u64 handle) const;

    // Get debug name
    std::string GetDebugName(u64 handle) const;

    // Record a stale access attempt (for diagnostics)
    void RecordStaleAccess(const VersionedHandle& vh);

    // Get recently destroyed resource names (for debugging)
    std::vector<std::string> GetDestroyedHistory() const;

    // Clear all tracking
    void Reset();

    ResourceVersionStats GetStats() const;

private:
    ResourceVersionConfig m_config;
    std::unordered_map<u64, ResourceVersionEntry> m_entries;
    std::vector<std::string> m_destroyedHistory;

    u32 m_totalCreated = 0;
    u32 m_totalDestroyed = 0;
    u32 m_staleAccesses = 0;
    u32 m_maxGeneration = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
