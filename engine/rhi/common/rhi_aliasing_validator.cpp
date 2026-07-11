#include "engine/rhi/common/rhi_aliasing_validator.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool AliasingValidator::Init(const AliasingValidatorConfig& config) {
    m_config = config;
    m_resources.reserve(config.maxResources);

    NGE_LOG_INFO("Aliasing validator initialized: enabled={}, breakOnViolation={}, maxResources={}",
                 config.enabled, config.breakOnViolation, config.maxResources);
    return true;
}

void AliasingValidator::Shutdown() {
    m_resources.clear();
    m_violations.clear();
}

void AliasingValidator::RegisterResource(const ResourceAllocation& alloc) {
    std::lock_guard lock(m_mutex);
    m_resources[alloc.resourceId] = alloc;
}

void AliasingValidator::MarkUsedInPass(u64 resourceId, u32 passIndex) {
    std::lock_guard lock(m_mutex);

    auto it = m_resources.find(resourceId);
    if (it == m_resources.end()) return;

    auto& res = it->second;
    if (passIndex < res.firstUsePass || res.firstUsePass == UINT32_MAX) {
        res.firstUsePass = passIndex;
    }
    if (passIndex > res.lastUsePass) {
        res.lastUsePass = passIndex;
    }
}

bool AliasingValidator::Validate() {
    std::lock_guard lock(m_mutex);

    m_violations.clear();

    if (!m_config.enabled) return true;

    // Collect all transient resources
    std::vector<const ResourceAllocation*> transients;
    for (const auto& [id, alloc] : m_resources) {
        if (alloc.isTransient) {
            transients.push_back(&alloc);
        }
    }

    // Sort by id so violations are reported deterministically (unordered_map
    // iteration order differs between standard library implementations).
    std::sort(transients.begin(), transients.end(),
              [](const ResourceAllocation* x, const ResourceAllocation* y) {
                  return x->resourceId < y->resourceId;
              });

    // O(n^2) pairwise check — acceptable for debug mode
    for (size_t i = 0; i < transients.size(); ++i) {
        for (size_t j = i + 1; j < transients.size(); ++j) {
            const auto* a = transients[i];
            const auto* b = transients[j];

            if (!MemoryOverlaps(a->region, b->region)) continue;
            if (!LifetimeOverlaps(*a, *b)) continue;

            // Violation: memory overlap + lifetime overlap
            AliasingViolation v;
            v.resourceA = a->resourceId;
            v.resourceB = b->resourceId;
            v.nameA = a->name;
            v.nameB = b->name;

            // Compute overlap pass (first pass where both are alive)
            v.overlapPass = std::max(a->firstUsePass, b->firstUsePass);

            // Compute memory overlap region
            u64 overlapStart = std::max(a->region.offset, b->region.offset);
            u64 overlapEnd = std::min(a->region.offset + a->region.size,
                                       b->region.offset + b->region.size);
            v.overlapOffset = overlapStart;
            v.overlapSize = overlapEnd - overlapStart;

            v.message = "Aliasing violation: '" + a->name + "' [pass " +
                        std::to_string(a->firstUsePass) + "-" + std::to_string(a->lastUsePass) +
                        "] overlaps with '" + b->name + "' [pass " +
                        std::to_string(b->firstUsePass) + "-" + std::to_string(b->lastUsePass) +
                        "] at offset " + std::to_string(overlapStart) +
                        " size " + std::to_string(v.overlapSize);

            NGE_LOG_ERROR("{}", v.message);
            m_violations.push_back(std::move(v));

            if (m_config.breakOnViolation) {
                // In a real engine: __debugbreak() or raise(SIGTRAP)
                NGE_LOG_ERROR("Break on aliasing violation triggered");
                return false;
            }
        }
    }

    return m_violations.empty();
}

const std::vector<AliasingViolation>& AliasingValidator::GetViolations() const {
    return m_violations;
}

bool AliasingValidator::CheckPairOverlap(u64 resourceA, u64 resourceB) const {
    std::lock_guard lock(m_mutex);

    auto itA = m_resources.find(resourceA);
    auto itB = m_resources.find(resourceB);
    if (itA == m_resources.end() || itB == m_resources.end()) return false;

    return MemoryOverlaps(itA->second.region, itB->second.region) &&
           LifetimeOverlaps(itA->second, itB->second);
}

void AliasingValidator::Reset() {
    std::lock_guard lock(m_mutex);
    m_resources.clear();
    m_violations.clear();
}

AliasingValidatorStats AliasingValidator::GetStats() const {
    std::lock_guard lock(m_mutex);
    AliasingValidatorStats stats{};
    stats.totalResources = static_cast<u32>(m_resources.size());

    u64 totalMem = 0;
    u32 transientCount = 0;

    for (const auto& [id, alloc] : m_resources) {
        totalMem += alloc.region.size;
        if (alloc.isTransient) transientCount++;
    }

    stats.transientResources = transientCount;
    stats.violationsDetected = static_cast<u32>(m_violations.size());
    stats.totalMemoryTracked = totalMem;

    // Count aliased pairs (memory overlap but valid — no lifetime overlap)
    u32 aliasedPairs = 0;
    u64 savedMem = 0;
    std::vector<const ResourceAllocation*> transients;
    for (const auto& [id, alloc] : m_resources) {
        if (alloc.isTransient) transients.push_back(&alloc);
    }

    for (size_t i = 0; i < transients.size(); ++i) {
        for (size_t j = i + 1; j < transients.size(); ++j) {
            if (MemoryOverlaps(transients[i]->region, transients[j]->region) &&
                !LifetimeOverlaps(*transients[i], *transients[j])) {
                aliasedPairs++;
                savedMem += std::min(transients[i]->region.size, transients[j]->region.size);
            }
        }
    }

    stats.aliasedPairs = aliasedPairs;
    stats.aliasedMemorySaved = savedMem;

    return stats;
}

bool AliasingValidator::MemoryOverlaps(const MemoryRegion& a, const MemoryRegion& b) const {
    if (a.heapId != b.heapId) return false;

    u64 aEnd = a.offset + a.size;
    u64 bEnd = b.offset + b.size;

    return a.offset < bEnd && b.offset < aEnd;
}

bool AliasingValidator::LifetimeOverlaps(const ResourceAllocation& a, const ResourceAllocation& b) const {
    return a.firstUsePass <= b.lastUsePass && b.firstUsePass <= a.lastUsePass;
}

} // namespace nge::rhi
