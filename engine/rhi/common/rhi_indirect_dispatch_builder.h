#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Indirect Dispatch Builder ───────────────────────────────────────
// Constructs VkDispatchIndirectCommand from GPU-side data, enabling
// fully GPU-driven compute dispatches without CPU readback.
//
// Use cases:
//   - Dispatch compute work based on GPU culling results
//   - Variable workgroup count from stream compaction output
//   - Cascaded compute passes (output count drives next dispatch)
//   - Particle simulation dispatch from live count
//
// Compatible with vkCmdDispatchIndirect.

struct DispatchIndirectCommand {
    u32 groupCountX;
    u32 groupCountY;
    u32 groupCountZ;
};

struct IndirectDispatchRequest {
    u64         countBufferHandle;   // GPU buffer containing element count
    u64         countBufferOffset;   // Byte offset to the count value
    u32         workgroupSize;       // Elements per workgroup (e.g., 64)
    u32         maxGroupCount;       // Safety clamp (default 65535)
    bool        is2D;                // 2D dispatch (count = width*height)
    u32         dispatchWidth;       // Only for 2D: fixed width dimension
    std::string debugName;
};

struct IndirectDispatchSlot {
    u32         slotId;
    u64         outputBufferHandle;  // Buffer containing DispatchIndirectCommand
    u64         outputOffset;
    IndirectDispatchRequest request;
    bool        ready;
};

struct IndirectDispatchConfig {
    u32  maxSlots = 64;
    u64  commandBufferSize = 4096;   // Bytes for all indirect commands
    bool clampToMaxGroups = true;
};

struct IndirectDispatchStats {
    u32 activeSlots;
    u32 totalDispatches;
    u32 clampedDispatches;
    u64 totalWorkgroups;
};

class IndirectDispatchBuilder {
public:
    bool Init(const IndirectDispatchConfig& config = {});
    void Shutdown();

    // Register an indirect dispatch slot
    u32 CreateSlot(const IndirectDispatchRequest& request);

    // Remove a slot
    void DestroySlot(u32 slotId);

    // Build indirect commands (CPU-side fallback for testing)
    DispatchIndirectCommand BuildCommand(u32 elementCount, const IndirectDispatchRequest& request) const;

    // Get the output buffer offset for a slot (for vkCmdDispatchIndirect)
    u64 GetCommandOffset(u32 slotId) const;

    // Get slot info
    const IndirectDispatchSlot* GetSlot(u32 slotId) const;

    // Get all active slots
    std::vector<u32> GetActiveSlotIds() const;

    // Clear all slots
    void Clear();

    // Record a dispatch event (for stats)
    void RecordDispatch(u32 slotId, u32 actualGroupCount);

    IndirectDispatchStats GetStats() const;

private:
    IndirectDispatchConfig m_config;
    std::unordered_map<u32, IndirectDispatchSlot> m_slots;

    u32 m_nextSlotId = 1;
    u64 m_nextOutputOffset = 0;

    u32 m_totalDispatches = 0;
    u32 m_clampedDispatches = 0;
    u64 m_totalWorkgroups = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
