#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_queue_capabilities.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Queue Family Arbiter ────────────────────────────────────────────
// Manages multi-queue workload distribution across Vulkan queue families.
// Assigns work to optimal queues based on capability, load, and priority.
//
// Use cases:
//   - Route graphics/compute/transfer work to appropriate queues
//   - Load-balance across multiple queues of the same family
//   - Priority-based scheduling (high-priority gets dedicated queue)
//   - Async compute overlap with graphics
//   - Dedicated transfer queue for DMA uploads

enum class WorkPriority : u8 {
    Low,
    Normal,
    High,
    Critical,
};

struct ArbiterQueueFamilyInfo {
    u32              familyIndex;
    QueueCapability  capabilities;
    u32              queueCount;        // Number of queues in this family
    u32              timestampValidBits;
    f32              minImageTransferGranularityX;
    f32              minImageTransferGranularityY;
    f32              minImageTransferGranularityZ;
    std::string      debugName;
};

struct QueueInstance {
    u32  familyIndex;
    u32  queueIndex;       // Index within the family
    u32  activeSubmissions; // Current in-flight submissions
    f32  loadFactor;        // 0=idle, 1=fully loaded
    bool dedicated;         // Reserved for specific work type
    std::string debugName;
};

struct WorkRequest {
    QueueCapability requiredCaps;
    WorkPriority    priority;
    u64             estimatedCost;    // Relative cost for load balancing
    bool            preferDedicated;  // Prefer a dedicated async queue
    std::string     debugName;
};

struct QueueAssignment {
    u32  familyIndex;
    u32  queueIndex;
    bool isDedicated;
    std::string queueName;
};

struct QueueFamilyArbiterConfig {
    bool enableLoadBalancing = true;
    bool preferAsyncCompute = true;      // Use dedicated compute queue when available
    bool preferDedicatedTransfer = true;  // Use dedicated transfer queue when available
    u32  maxQueueFamilies = 8;
};

struct QueueFamilyArbiterStats {
    u32 totalFamilies;
    u32 totalQueues;
    u32 graphicsQueues;
    u32 computeOnlyQueues;
    u32 transferOnlyQueues;
    u32 totalAssignments;
    u32 dedicatedAssignments;
    f32 avgLoadFactor;
};

class QueueFamilyArbiter {
public:
    bool Init(const QueueFamilyArbiterConfig& config = {});
    void Shutdown();

    // Register available queue families from device
    void RegisterFamily(const ArbiterQueueFamilyInfo& family);

    // Register individual queue instances
    void RegisterQueue(u32 familyIndex, u32 queueIndex, const std::string& debugName = "");

    // Mark a queue as dedicated (reserved for specific work)
    void SetDedicated(u32 familyIndex, u32 queueIndex, bool dedicated);

    // Request optimal queue for a workload
    QueueAssignment RequestQueue(const WorkRequest& request);

    // Update queue load after submission
    void RecordSubmission(u32 familyIndex, u32 queueIndex, u64 cost);

    // Update queue load after completion
    void RecordCompletion(u32 familyIndex, u32 queueIndex, u64 cost);

    // Get all families with a specific capability
    std::vector<u32> GetFamiliesWithCapability(QueueCapability cap) const;

    // Get dedicated compute queue family (if exists, separate from graphics)
    i32 GetAsyncComputeFamily() const;

    // Get dedicated transfer queue family (if exists)
    i32 GetDedicatedTransferFamily() const;

    // Get queue load factor
    f32 GetQueueLoad(u32 familyIndex, u32 queueIndex) const;

    // Reset load counters
    void ResetLoads();

    QueueFamilyArbiterStats GetStats() const;

private:
    struct QueueKey {
        u32 familyIndex;
        u32 queueIndex;
        bool operator==(const QueueKey& o) const { return familyIndex == o.familyIndex && queueIndex == o.queueIndex; }
    };

    struct QueueKeyHash {
        size_t operator()(const QueueKey& k) const {
            return std::hash<u64>()(static_cast<u64>(k.familyIndex) << 32 | k.queueIndex);
        }
    };

    QueueFamilyArbiterConfig m_config;
    std::vector<ArbiterQueueFamilyInfo> m_families;
    std::unordered_map<QueueKey, QueueInstance, QueueKeyHash> m_queues;

    u32 m_totalAssignments = 0;
    u32 m_dedicatedAssignments = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
