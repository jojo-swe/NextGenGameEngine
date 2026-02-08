#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::rhi {

// ─── GPU Queue Capability Manager ────────────────────────────────────────
// Queries and tracks per-queue-family capabilities from the Vulkan
// physical device. Used by the render graph, submission batcher, and
// async compute scheduler to make optimal queue selection decisions.
//
// Provides:
//   - Queue family enumeration with capability flags
//   - Best queue selection for a given workload type
//   - Dedicated transfer/compute queue detection
//   - Queue priority and count information

enum class QueueCapability : u32 {
    Graphics       = 0x01,
    Compute        = 0x02,
    Transfer       = 0x04,
    SparseBinding  = 0x08,
    Present        = 0x10,
    VideoDecoder   = 0x20,
    VideoEncoder   = 0x40,
};

inline QueueCapability operator|(QueueCapability a, QueueCapability b) {
    return static_cast<QueueCapability>(static_cast<u32>(a) | static_cast<u32>(b));
}
inline QueueCapability operator&(QueueCapability a, QueueCapability b) {
    return static_cast<QueueCapability>(static_cast<u32>(a) & static_cast<u32>(b));
}
inline bool HasCapability(QueueCapability flags, QueueCapability test) {
    return (static_cast<u32>(flags) & static_cast<u32>(test)) != 0;
}

struct QueueFamilyInfo {
    u32              familyIndex;
    QueueCapability  capabilities;
    u32              queueCount;
    u32              timestampValidBits;
    u32              minImageTransferGranularityX;
    u32              minImageTransferGranularityY;
    u32              minImageTransferGranularityZ;
    bool             supportsPresent;
};

struct QueueSelection {
    u32  familyIndex;
    u32  queueIndex;
    bool dedicated;   // True if this is a dedicated (non-shared) queue for the capability
    bool valid;
};

struct QueueCapabilityStats {
    u32 totalFamilies;
    u32 totalQueues;
    bool hasDedicatedCompute;
    bool hasDedicatedTransfer;
    bool hasAsyncCompute;
    bool hasSparseBinding;
};

class QueueCapabilityManager {
public:
    bool Init(IDevice* device);
    void Shutdown();

    // Get all queue family information
    const std::vector<QueueFamilyInfo>& GetFamilies() const { return m_families; }

    // Find the best queue family for a given capability
    QueueSelection FindBestQueue(QueueCapability required) const;

    // Find a dedicated queue (one that ONLY has the requested capability)
    QueueSelection FindDedicatedQueue(QueueCapability capability) const;

    // Get the graphics queue family
    QueueSelection GetGraphicsQueue() const;

    // Get async compute queue (compute-only, no graphics)
    QueueSelection GetAsyncComputeQueue() const;

    // Get dedicated transfer queue
    QueueSelection GetTransferQueue() const;

    // Check if a family supports a capability
    bool FamilySupports(u32 familyIndex, QueueCapability capability) const;

    // Get the number of queues in a family
    u32 GetQueueCount(u32 familyIndex) const;

    // Get timestamp valid bits for a family
    u32 GetTimestampBits(u32 familyIndex) const;

    QueueCapabilityStats GetStats() const;

private:
    IDevice* m_device = nullptr;
    std::vector<QueueFamilyInfo> m_families;
};

} // namespace nge::rhi
