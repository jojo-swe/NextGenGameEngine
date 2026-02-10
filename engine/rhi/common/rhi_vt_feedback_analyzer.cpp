#include "engine/rhi/common/rhi_vt_feedback_analyzer.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool VTFeedbackAnalyzer::Init(const VTFeedbackConfig& config) {
    m_config = config;
    m_totalProcessed = 0;
    m_duplicatesRemoved = 0;
    m_pagesEvicted = 0;

    NGE_LOG_INFO("VT feedback analyzer initialized: maxPages={}, evictionThreshold={} frames",
                 config.maxPagesTracked, config.evictionFrameThreshold);
    return true;
}

void VTFeedbackAnalyzer::Shutdown() {
    m_requests.clear();
    m_residency.clear();
}

void VTFeedbackAnalyzer::SubmitFeedback(const VTPageId* pages, u32 count, u32 currentFrame) {
    std::lock_guard lock(m_mutex);

    u32 processCount = std::min(count, m_config.maxRequestsPerFrame);

    for (u32 i = 0; i < processCount; ++i) {
        m_totalProcessed++;

        const auto& page = pages[i];
        auto it = m_requests.find(page);

        if (it != m_requests.end()) {
            // Existing request: update
            it->second.requestCount++;
            it->second.lastRequestFrame = currentFrame;
            m_duplicatesRemoved++;
        } else {
            if (m_requests.size() >= m_config.maxPagesTracked) continue;

            VTPageRequest req;
            req.page = page;
            req.requestCount = 1;
            req.firstRequestFrame = currentFrame;
            req.lastRequestFrame = currentFrame;
            req.screenCoverage = 0.0f;
            req.priority = 0.0f;
            m_requests[page] = req;
        }
    }
}

void VTFeedbackAnalyzer::RequestPage(const VTPageId& page, float screenCoverage, u32 currentFrame) {
    std::lock_guard lock(m_mutex);
    m_totalProcessed++;

    auto it = m_requests.find(page);
    if (it != m_requests.end()) {
        it->second.requestCount++;
        it->second.lastRequestFrame = currentFrame;
        it->second.screenCoverage = std::max(it->second.screenCoverage, screenCoverage);
        m_duplicatesRemoved++;
    } else {
        if (m_requests.size() >= m_config.maxPagesTracked) return;

        VTPageRequest req;
        req.page = page;
        req.requestCount = 1;
        req.firstRequestFrame = currentFrame;
        req.lastRequestFrame = currentFrame;
        req.screenCoverage = screenCoverage;
        req.priority = 0.0f;
        m_requests[page] = req;
    }
}

std::vector<VTPageRequest> VTFeedbackAnalyzer::GetPriorityQueue(u32 maxCount) const {
    std::lock_guard lock(m_mutex);

    std::vector<VTPageRequest> result;
    result.reserve(std::min(static_cast<u32>(m_requests.size()), maxCount));

    for (const auto& [id, req] : m_requests) {
        // Only include non-resident pages
        auto resIt = m_residency.find(id);
        if (resIt != m_residency.end() && resIt->second == PageResidency::Resident) continue;
        if (resIt != m_residency.end() && resIt->second == PageResidency::Loading) continue;

        VTPageRequest scored = req;
        scored.priority = ComputePriority(req);
        result.push_back(scored);
    }

    // Sort by priority descending
    std::sort(result.begin(), result.end(), [](const VTPageRequest& a, const VTPageRequest& b) {
        return a.priority > b.priority;
    });

    if (result.size() > maxCount) {
        result.resize(maxCount);
    }

    return result;
}

void VTFeedbackAnalyzer::MarkResident(const VTPageId& page) {
    std::lock_guard lock(m_mutex);
    m_residency[page] = PageResidency::Resident;
}

void VTFeedbackAnalyzer::MarkLoading(const VTPageId& page) {
    std::lock_guard lock(m_mutex);
    m_residency[page] = PageResidency::Loading;
}

PageResidency VTFeedbackAnalyzer::GetResidency(const VTPageId& page) const {
    std::lock_guard lock(m_mutex);

    auto it = m_residency.find(page);
    if (it == m_residency.end()) return PageResidency::NotResident;
    return it->second;
}

bool VTFeedbackAnalyzer::IsRequested(const VTPageId& page) const {
    std::lock_guard lock(m_mutex);

    auto reqIt = m_requests.find(page);
    if (reqIt == m_requests.end()) return false;

    auto resIt = m_residency.find(page);
    if (resIt != m_residency.end() && resIt->second == PageResidency::Resident) return false;

    return true;
}

void VTFeedbackAnalyzer::ProcessFrame(u32 currentFrame) {
    std::lock_guard lock(m_mutex);

    // Mark resident pages as eviction candidates if not requested recently
    for (auto& [page, residency] : m_residency) {
        if (residency != PageResidency::Resident) continue;

        auto reqIt = m_requests.find(page);
        if (reqIt == m_requests.end()) {
            residency = PageResidency::EvictionCandidate;
            continue;
        }

        u32 framesSinceLastRequest = currentFrame - reqIt->second.lastRequestFrame;
        if (framesSinceLastRequest > m_config.evictionFrameThreshold) {
            residency = PageResidency::EvictionCandidate;
        }
    }

    // Age out very old requests that were never loaded
    std::vector<VTPageId> toRemove;
    for (const auto& [page, req] : m_requests) {
        u32 age = currentFrame - req.lastRequestFrame;
        if (age > m_config.evictionFrameThreshold * 2) {
            auto resIt = m_residency.find(page);
            if (resIt == m_residency.end() || resIt->second == PageResidency::NotResident) {
                toRemove.push_back(page);
            }
        }
    }

    for (const auto& page : toRemove) {
        m_requests.erase(page);
    }
}

std::vector<VTPageId> VTFeedbackAnalyzer::GetEvictionCandidates(u32 maxCount) const {
    std::lock_guard lock(m_mutex);

    std::vector<VTPageId> candidates;
    for (const auto& [page, residency] : m_residency) {
        if (residency == PageResidency::EvictionCandidate) {
            candidates.push_back(page);
            if (candidates.size() >= maxCount) break;
        }
    }
    return candidates;
}

void VTFeedbackAnalyzer::Evict(const VTPageId& page) {
    std::lock_guard lock(m_mutex);

    auto it = m_residency.find(page);
    if (it != m_residency.end()) {
        it->second = PageResidency::NotResident;
        m_pagesEvicted++;
    }
}

u32 VTFeedbackAnalyzer::GetTrackedPageCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_requests.size());
}

void VTFeedbackAnalyzer::Reset() {
    std::lock_guard lock(m_mutex);
    m_requests.clear();
    m_residency.clear();
    m_totalProcessed = 0;
    m_duplicatesRemoved = 0;
    m_pagesEvicted = 0;
}

VTFeedbackStats VTFeedbackAnalyzer::GetStats() const {
    std::lock_guard lock(m_mutex);

    VTFeedbackStats stats{};
    stats.totalPagesTracked = static_cast<u32>(m_requests.size());
    stats.totalRequestsProcessed = m_totalProcessed;
    stats.duplicatesRemoved = m_duplicatesRemoved;
    stats.pagesEvicted = m_pagesEvicted;

    u32 resident = 0, loading = 0, pending = 0, eviction = 0;
    for (const auto& [page, residency] : m_residency) {
        switch (residency) {
            case PageResidency::Resident:           resident++; break;
            case PageResidency::Loading:            loading++; break;
            case PageResidency::EvictionCandidate:  eviction++; break;
            default: break;
        }
    }

    // Pending = requested but not resident/loading
    for (const auto& [page, req] : m_requests) {
        auto resIt = m_residency.find(page);
        if (resIt == m_residency.end() || resIt->second == PageResidency::NotResident) {
            pending++;
        }
    }

    stats.residentPages = resident;
    stats.loadingPages = loading;
    stats.pendingRequests = pending;
    stats.evictionCandidates = eviction;

    return stats;
}

float VTFeedbackAnalyzer::ComputePriority(const VTPageRequest& req) const {
    // Lower mip levels (more detailed) get higher priority
    float mipFactor = 1.0f / (1.0f + static_cast<float>(req.page.mipLevel));
    mipFactor *= m_config.mipUrgencyWeight;

    // More frequent requests get higher priority
    float freqFactor = static_cast<float>(req.requestCount) * m_config.frequencyWeight;

    // Larger screen coverage gets higher priority
    float coverageFactor = req.screenCoverage * m_config.coverageWeight;

    return mipFactor + freqFactor + coverageFactor;
}

} // namespace nge::rhi
