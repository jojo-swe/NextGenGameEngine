#include "engine/rhi/common/rhi_queue_capabilities.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool QueueCapabilityManager::Init(IDevice* device) {
    m_device = device;

    // TODO: Query VkPhysicalDevice for queue family properties
    // u32 familyCount = 0;
    // vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    // std::vector<VkQueueFamilyProperties> props(familyCount);
    // vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, props.data());
    //
    // for (u32 i = 0; i < familyCount; ++i) {
    //     QueueFamilyInfo info;
    //     info.familyIndex = i;
    //     info.capabilities = {};
    //     if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) info.capabilities = info.capabilities | QueueCapability::Graphics;
    //     if (props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) info.capabilities = info.capabilities | QueueCapability::Compute;
    //     if (props[i].queueFlags & VK_QUEUE_TRANSFER_BIT) info.capabilities = info.capabilities | QueueCapability::Transfer;
    //     if (props[i].queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) info.capabilities = info.capabilities | QueueCapability::SparseBinding;
    //     info.queueCount = props[i].queueCount;
    //     info.timestampValidBits = props[i].timestampValidBits;
    //     info.minImageTransferGranularityX = props[i].minImageTransferGranularity.width;
    //     info.minImageTransferGranularityY = props[i].minImageTransferGranularity.height;
    //     info.minImageTransferGranularityZ = props[i].minImageTransferGranularity.depth;
    //     // Check present support via vkGetPhysicalDeviceSurfaceSupportKHR
    //     m_families.push_back(info);
    // }

    // Stub: typical discrete GPU layout
    // Family 0: Graphics + Compute + Transfer (16 queues)
    QueueFamilyInfo graphicsFamily;
    graphicsFamily.familyIndex = 0;
    graphicsFamily.capabilities = QueueCapability::Graphics | QueueCapability::Compute | QueueCapability::Transfer;
    graphicsFamily.queueCount = 16;
    graphicsFamily.timestampValidBits = 64;
    graphicsFamily.minImageTransferGranularityX = 1;
    graphicsFamily.minImageTransferGranularityY = 1;
    graphicsFamily.minImageTransferGranularityZ = 1;
    graphicsFamily.supportsPresent = true;
    m_families.push_back(graphicsFamily);

    // Family 1: Compute only (8 queues) — async compute
    QueueFamilyInfo computeFamily;
    computeFamily.familyIndex = 1;
    computeFamily.capabilities = QueueCapability::Compute | QueueCapability::Transfer;
    computeFamily.queueCount = 8;
    computeFamily.timestampValidBits = 64;
    computeFamily.minImageTransferGranularityX = 1;
    computeFamily.minImageTransferGranularityY = 1;
    computeFamily.minImageTransferGranularityZ = 1;
    computeFamily.supportsPresent = false;
    m_families.push_back(computeFamily);

    // Family 2: Transfer only (2 queues) — DMA engine
    QueueFamilyInfo transferFamily;
    transferFamily.familyIndex = 2;
    transferFamily.capabilities = QueueCapability::Transfer;
    transferFamily.queueCount = 2;
    transferFamily.timestampValidBits = 64;
    transferFamily.minImageTransferGranularityX = 16;
    transferFamily.minImageTransferGranularityY = 16;
    transferFamily.minImageTransferGranularityZ = 16;
    transferFamily.supportsPresent = false;
    m_families.push_back(transferFamily);

    NGE_LOG_INFO("Queue capability manager initialized: {} families", m_families.size());
    for (const auto& family : m_families) {
        NGE_LOG_INFO("  Family {}: queues={}, graphics={}, compute={}, transfer={}, present={}",
                     family.familyIndex, family.queueCount,
                     HasCapability(family.capabilities, QueueCapability::Graphics),
                     HasCapability(family.capabilities, QueueCapability::Compute),
                     HasCapability(family.capabilities, QueueCapability::Transfer),
                     family.supportsPresent);
    }

    return true;
}

void QueueCapabilityManager::Shutdown() {
    m_families.clear();
}

QueueSelection QueueCapabilityManager::FindBestQueue(QueueCapability required) const {
    QueueSelection best;
    best.valid = false;
    best.dedicated = false;
    u32 bestScore = UINT32_MAX; // Lower is better (fewer extra capabilities)

    for (const auto& family : m_families) {
        if (!HasCapability(family.capabilities, required)) continue;
        if (family.queueCount == 0) continue;

        // Score: count extra capabilities (prefer dedicated)
        u32 score = 0;
        if (HasCapability(family.capabilities, QueueCapability::Graphics) &&
            !HasCapability(required, QueueCapability::Graphics)) score++;
        if (HasCapability(family.capabilities, QueueCapability::Compute) &&
            !HasCapability(required, QueueCapability::Compute)) score++;
        if (HasCapability(family.capabilities, QueueCapability::Transfer) &&
            !HasCapability(required, QueueCapability::Transfer)) score++;

        if (score < bestScore) {
            bestScore = score;
            best.familyIndex = family.familyIndex;
            best.queueIndex = 0;
            best.dedicated = (score == 0);
            best.valid = true;
        }
    }

    return best;
}

QueueSelection QueueCapabilityManager::FindDedicatedQueue(QueueCapability capability) const {
    QueueSelection result;
    result.valid = false;

    for (const auto& family : m_families) {
        if (!HasCapability(family.capabilities, capability)) continue;

        // Check if this family ONLY has the requested capability (+ transfer which is always present)
        bool isDedicated = true;
        if (capability != QueueCapability::Graphics && HasCapability(family.capabilities, QueueCapability::Graphics))
            isDedicated = false;
        if (capability != QueueCapability::Compute && HasCapability(family.capabilities, QueueCapability::Compute) &&
            capability != QueueCapability::Transfer)
            isDedicated = false;

        if (isDedicated) {
            result.familyIndex = family.familyIndex;
            result.queueIndex = 0;
            result.dedicated = true;
            result.valid = true;
            return result;
        }
    }

    return result;
}

QueueSelection QueueCapabilityManager::GetGraphicsQueue() const {
    return FindBestQueue(QueueCapability::Graphics);
}

QueueSelection QueueCapabilityManager::GetAsyncComputeQueue() const {
    // Prefer a compute queue that does NOT have graphics
    auto dedicated = FindDedicatedQueue(QueueCapability::Compute);
    if (dedicated.valid) return dedicated;

    // Fallback: any compute queue
    return FindBestQueue(QueueCapability::Compute);
}

QueueSelection QueueCapabilityManager::GetTransferQueue() const {
    // Prefer a transfer-only queue
    auto dedicated = FindDedicatedQueue(QueueCapability::Transfer);
    if (dedicated.valid) return dedicated;

    // Fallback: any transfer queue
    return FindBestQueue(QueueCapability::Transfer);
}

bool QueueCapabilityManager::FamilySupports(u32 familyIndex, QueueCapability capability) const {
    if (familyIndex >= m_families.size()) return false;
    return HasCapability(m_families[familyIndex].capabilities, capability);
}

u32 QueueCapabilityManager::GetQueueCount(u32 familyIndex) const {
    if (familyIndex >= m_families.size()) return 0;
    return m_families[familyIndex].queueCount;
}

u32 QueueCapabilityManager::GetTimestampBits(u32 familyIndex) const {
    if (familyIndex >= m_families.size()) return 0;
    return m_families[familyIndex].timestampValidBits;
}

QueueCapabilityStats QueueCapabilityManager::GetStats() const {
    QueueCapabilityStats stats{};
    stats.totalFamilies = static_cast<u32>(m_families.size());

    for (const auto& family : m_families) {
        stats.totalQueues += family.queueCount;
        if (HasCapability(family.capabilities, QueueCapability::SparseBinding))
            stats.hasSparseBinding = true;
    }

    auto asyncCompute = GetAsyncComputeQueue();
    stats.hasAsyncCompute = asyncCompute.valid && asyncCompute.dedicated;
    stats.hasDedicatedCompute = FindDedicatedQueue(QueueCapability::Compute).valid;
    stats.hasDedicatedTransfer = FindDedicatedQueue(QueueCapability::Transfer).valid;

    return stats;
}

} // namespace nge::rhi
