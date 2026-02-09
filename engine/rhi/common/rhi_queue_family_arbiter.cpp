#include "engine/rhi/common/rhi_queue_family_arbiter.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <limits>

namespace nge::rhi {

bool QueueFamilyArbiter::Init(const QueueFamilyArbiterConfig& config) {
    m_config = config;
    m_families.reserve(config.maxQueueFamilies);
    m_totalAssignments = 0;
    m_dedicatedAssignments = 0;

    NGE_LOG_INFO("Queue family arbiter initialized: loadBalancing={}, asyncCompute={}, dedicatedTransfer={}",
                 config.enableLoadBalancing, config.preferAsyncCompute, config.preferDedicatedTransfer);
    return true;
}

void QueueFamilyArbiter::Shutdown() {
    m_families.clear();
    m_queues.clear();
}

void QueueFamilyArbiter::RegisterFamily(const QueueFamilyInfo& family) {
    std::lock_guard lock(m_mutex);

    if (m_families.size() >= m_config.maxQueueFamilies) {
        NGE_LOG_WARN("Queue family arbiter: max families reached ({})", m_config.maxQueueFamilies);
        return;
    }

    m_families.push_back(family);

    NGE_LOG_INFO("Registered queue family {}: caps=0x{:X}, count={}, name='{}'",
                 family.familyIndex, static_cast<u32>(family.capabilities),
                 family.queueCount, family.debugName);
}

void QueueFamilyArbiter::RegisterQueue(u32 familyIndex, u32 queueIndex, const std::string& debugName) {
    std::lock_guard lock(m_mutex);

    QueueKey key{familyIndex, queueIndex};
    QueueInstance inst;
    inst.familyIndex = familyIndex;
    inst.queueIndex = queueIndex;
    inst.activeSubmissions = 0;
    inst.loadFactor = 0.0f;
    inst.dedicated = false;
    inst.debugName = debugName.empty() ? ("Q" + std::to_string(familyIndex) + "." + std::to_string(queueIndex)) : debugName;

    m_queues[key] = std::move(inst);
}

void QueueFamilyArbiter::SetDedicated(u32 familyIndex, u32 queueIndex, bool dedicated) {
    std::lock_guard lock(m_mutex);
    QueueKey key{familyIndex, queueIndex};
    auto it = m_queues.find(key);
    if (it != m_queues.end()) {
        it->second.dedicated = dedicated;
    }
}

QueueAssignment QueueFamilyArbiter::RequestQueue(const WorkRequest& request) const {
    std::lock_guard lock(m_mutex);

    QueueAssignment best;
    best.familyIndex = 0;
    best.queueIndex = 0;
    best.isDedicated = false;
    best.queueName = "";

    f32 bestScore = -1.0f;

    for (const auto& family : m_families) {
        // Check capability match
        if (!HasCapability(family.capabilities, request.requiredCaps)) continue;

        // Prefer dedicated async compute if requested
        bool isDedicatedCompute = HasCapability(family.capabilities, QueueCapability::Compute) &&
                                   !HasCapability(family.capabilities, QueueCapability::Graphics);
        bool isDedicatedTransfer = HasCapability(family.capabilities, QueueCapability::Transfer) &&
                                    !HasCapability(family.capabilities, QueueCapability::Graphics) &&
                                    !HasCapability(family.capabilities, QueueCapability::Compute);

        f32 familyScore = 1.0f;

        // Prefer narrower capability queues (more specialized = better)
        if (request.preferDedicated || m_config.preferAsyncCompute) {
            if (HasCapability(request.requiredCaps, QueueCapability::Compute) && isDedicatedCompute) {
                familyScore += 2.0f;
            }
        }
        if (m_config.preferDedicatedTransfer) {
            if (HasCapability(request.requiredCaps, QueueCapability::Transfer) && isDedicatedTransfer) {
                familyScore += 2.0f;
            }
        }

        // Priority bonus
        familyScore += static_cast<f32>(request.priority) * 0.5f;

        // Find best queue within this family
        for (u32 qi = 0; qi < family.queueCount; ++qi) {
            QueueKey key{family.familyIndex, qi};
            auto qIt = m_queues.find(key);

            f32 queueScore = familyScore;

            if (qIt != m_queues.end()) {
                const auto& inst = qIt->second;

                // Skip dedicated queues if not requesting dedicated
                if (inst.dedicated && !request.preferDedicated) continue;

                // Load balancing: prefer less loaded queues
                if (m_config.enableLoadBalancing) {
                    queueScore += (1.0f - inst.loadFactor) * 3.0f;
                }
            } else {
                // Queue not registered yet, assume idle
                queueScore += 3.0f;
            }

            if (queueScore > bestScore) {
                bestScore = queueScore;
                best.familyIndex = family.familyIndex;
                best.queueIndex = qi;
                best.isDedicated = (qIt != m_queues.end()) ? qIt->second.dedicated : false;
                best.queueName = (qIt != m_queues.end()) ? qIt->second.debugName : "";
            }
        }
    }

    m_totalAssignments++;
    if (best.isDedicated) m_dedicatedAssignments++;

    return best;
}

void QueueFamilyArbiter::RecordSubmission(u32 familyIndex, u32 queueIndex, u64 cost) {
    std::lock_guard lock(m_mutex);
    QueueKey key{familyIndex, queueIndex};
    auto it = m_queues.find(key);
    if (it != m_queues.end()) {
        it->second.activeSubmissions++;
        it->second.loadFactor = std::min(1.0f, it->second.loadFactor + static_cast<f32>(cost) / 1000.0f);
    }
}

void QueueFamilyArbiter::RecordCompletion(u32 familyIndex, u32 queueIndex, u64 cost) {
    std::lock_guard lock(m_mutex);
    QueueKey key{familyIndex, queueIndex};
    auto it = m_queues.find(key);
    if (it != m_queues.end()) {
        if (it->second.activeSubmissions > 0) it->second.activeSubmissions--;
        it->second.loadFactor = std::max(0.0f, it->second.loadFactor - static_cast<f32>(cost) / 1000.0f);
    }
}

std::vector<u32> QueueFamilyArbiter::GetFamiliesWithCapability(QueueCapability cap) const {
    std::lock_guard lock(m_mutex);
    std::vector<u32> result;
    for (const auto& f : m_families) {
        if (HasCapability(f.capabilities, cap)) {
            result.push_back(f.familyIndex);
        }
    }
    return result;
}

i32 QueueFamilyArbiter::GetAsyncComputeFamily() const {
    std::lock_guard lock(m_mutex);
    for (const auto& f : m_families) {
        if (HasCapability(f.capabilities, QueueCapability::Compute) &&
            !HasCapability(f.capabilities, QueueCapability::Graphics)) {
            return static_cast<i32>(f.familyIndex);
        }
    }
    return -1;
}

i32 QueueFamilyArbiter::GetDedicatedTransferFamily() const {
    std::lock_guard lock(m_mutex);
    for (const auto& f : m_families) {
        if (HasCapability(f.capabilities, QueueCapability::Transfer) &&
            !HasCapability(f.capabilities, QueueCapability::Graphics) &&
            !HasCapability(f.capabilities, QueueCapability::Compute)) {
            return static_cast<i32>(f.familyIndex);
        }
    }
    return -1;
}

f32 QueueFamilyArbiter::GetQueueLoad(u32 familyIndex, u32 queueIndex) const {
    std::lock_guard lock(m_mutex);
    QueueKey key{familyIndex, queueIndex};
    auto it = m_queues.find(key);
    if (it != m_queues.end()) return it->second.loadFactor;
    return 0.0f;
}

void QueueFamilyArbiter::ResetLoads() {
    std::lock_guard lock(m_mutex);
    for (auto& [key, inst] : m_queues) {
        inst.activeSubmissions = 0;
        inst.loadFactor = 0.0f;
    }
}

QueueFamilyArbiterStats QueueFamilyArbiter::GetStats() const {
    std::lock_guard lock(m_mutex);
    QueueFamilyArbiterStats stats{};
    stats.totalFamilies = static_cast<u32>(m_families.size());
    stats.totalQueues = static_cast<u32>(m_queues.size());
    stats.totalAssignments = m_totalAssignments;
    stats.dedicatedAssignments = m_dedicatedAssignments;

    f32 totalLoad = 0.0f;
    for (const auto& [key, inst] : m_queues) {
        totalLoad += inst.loadFactor;
    }
    stats.avgLoadFactor = m_queues.empty() ? 0.0f : totalLoad / static_cast<f32>(m_queues.size());

    for (const auto& f : m_families) {
        bool hasGraphics = HasCapability(f.capabilities, QueueCapability::Graphics);
        bool hasCompute = HasCapability(f.capabilities, QueueCapability::Compute);
        bool hasTransfer = HasCapability(f.capabilities, QueueCapability::Transfer);

        if (hasGraphics) stats.graphicsQueues += f.queueCount;
        if (hasCompute && !hasGraphics) stats.computeOnlyQueues += f.queueCount;
        if (hasTransfer && !hasGraphics && !hasCompute) stats.transferOnlyQueues += f.queueCount;
    }

    return stats;
}

} // namespace nge::rhi
