#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Memory Aliasing Validator ───────────────────────────────────────
// Debug-mode tool that detects invalid overlapping usage of aliased
// transient resources within a frame. Catches bugs where two resources
// sharing the same memory region are both in use simultaneously.
//
// Use cases:
//   - Validate render graph aliasing correctness
//   - Detect use-after-alias hazards
//   - Verify transient resource lifetime non-overlap
//   - CI regression testing for aliasing optimizer

struct MemoryRegion {
    u64 offset;
    u64 size;
    u64 heapId;
};

struct ResourceAllocation {
    u64         resourceId;
    std::string name;
    MemoryRegion region;
    u32         firstUsePass;   // Pass index where resource is first used
    u32         lastUsePass;    // Pass index where resource is last used
    bool        isTransient;    // True if eligible for aliasing
};

struct AliasingViolation {
    u64         resourceA;
    u64         resourceB;
    std::string nameA;
    std::string nameB;
    u32         overlapPass;    // Pass where both are alive
    u64         overlapOffset;
    u64         overlapSize;
    std::string message;
};

struct AliasingValidatorConfig {
    bool enabled = true;           // Can disable in release
    bool breakOnViolation = false; // Debug break on first violation
    u32  maxResources = 4096;
};

struct AliasingValidatorStats {
    u32 totalResources;
    u32 transientResources;
    u32 aliasedPairs;
    u32 violationsDetected;
    u64 totalMemoryTracked;
    u64 aliasedMemorySaved;
};

class AliasingValidator {
public:
    bool Init(const AliasingValidatorConfig& config = {});
    void Shutdown();

    // Register a resource allocation for the current frame
    void RegisterResource(const ResourceAllocation& alloc);

    // Mark a resource as used in a specific pass
    void MarkUsedInPass(u64 resourceId, u32 passIndex);

    // Run validation: detect overlapping lifetimes on shared memory
    bool Validate();

    // Get all violations from last Validate() call
    const std::vector<AliasingViolation>& GetViolations() const;

    // Check if two specific resources overlap in memory AND lifetime
    bool CheckPairOverlap(u64 resourceA, u64 resourceB) const;

    // Clear for next frame
    void Reset();

    AliasingValidatorStats GetStats() const;

private:
    bool MemoryOverlaps(const MemoryRegion& a, const MemoryRegion& b) const;
    bool LifetimeOverlaps(const ResourceAllocation& a, const ResourceAllocation& b) const;

    AliasingValidatorConfig m_config;
    std::unordered_map<u64, ResourceAllocation> m_resources;
    std::vector<AliasingViolation> m_violations;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
