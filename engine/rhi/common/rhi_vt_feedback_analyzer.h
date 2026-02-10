#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace nge::rhi {

// ─── GPU Virtual Texture Feedback Analyzer ───────────────────────────────
// Aggregates and prioritizes virtual texture page requests from GPU
// feedback buffers. Deduplicates requests, tracks request frequency,
// and produces a prioritized list of pages to stream.
//
// Use cases:
//   - Aggregate VT feedback buffer readback into page requests
//   - Deduplicate redundant page requests across frames
//   - Prioritize pages by screen coverage and mip urgency
//   - Track page residency and eviction candidates
//   - Feed streaming system with ordered load requests

struct VTPageId {
    u32 textureId;
    u32 mipLevel;
    u32 pageX;
    u32 pageY;

    bool operator==(const VTPageId& other) const {
        return textureId == other.textureId && mipLevel == other.mipLevel &&
               pageX == other.pageX && pageY == other.pageY;
    }
};

struct VTPageIdHasher {
    size_t operator()(const VTPageId& id) const {
        size_t h = std::hash<u32>()(id.textureId);
        h ^= std::hash<u32>()(id.mipLevel) << 8;
        h ^= std::hash<u32>()(id.pageX) << 16;
        h ^= std::hash<u32>()(id.pageY) << 24;
        return h;
    }
};

struct VTPageRequest {
    VTPageId page;
    u32      requestCount;       // Times requested across frames
    u32      firstRequestFrame;
    u32      lastRequestFrame;
    float    screenCoverage;     // Estimated screen area (0..1)
    float    priority;           // Computed priority score
};

enum class PageResidency : u8 {
    NotResident,
    Loading,
    Resident,
    EvictionCandidate,
};

struct VTFeedbackConfig {
    u32  maxPagesTracked = 16384;
    u32  maxRequestsPerFrame = 4096;
    u32  evictionFrameThreshold = 120;  // Evict after N frames unused
    float mipUrgencyWeight = 2.0f;      // Lower mips = higher priority
    float frequencyWeight = 1.0f;       // More requests = higher priority
    float coverageWeight = 1.5f;        // Larger screen area = higher priority
};

struct VTFeedbackStats {
    u32 totalPagesTracked;
    u32 residentPages;
    u32 loadingPages;
    u32 pendingRequests;
    u32 evictionCandidates;
    u32 totalRequestsProcessed;
    u32 duplicatesRemoved;
    u32 pagesEvicted;
};

class VTFeedbackAnalyzer {
public:
    bool Init(const VTFeedbackConfig& config = {});
    void Shutdown();

    // Submit raw feedback data from GPU readback buffer
    void SubmitFeedback(const VTPageId* pages, u32 count, u32 currentFrame);

    // Submit a single page request
    void RequestPage(const VTPageId& page, float screenCoverage, u32 currentFrame);

    // Get prioritized list of pages to load (sorted by priority descending)
    std::vector<VTPageRequest> GetPriorityQueue(u32 maxCount = UINT32_MAX) const;

    // Mark a page as resident (loaded)
    void MarkResident(const VTPageId& page);

    // Mark a page as loading
    void MarkLoading(const VTPageId& page);

    // Check page residency
    PageResidency GetResidency(const VTPageId& page) const;

    // Check if a page is requested but not resident
    bool IsRequested(const VTPageId& page) const;

    // Process frame: update eviction candidates, age out old requests
    void ProcessFrame(u32 currentFrame);

    // Get pages that are candidates for eviction
    std::vector<VTPageId> GetEvictionCandidates(u32 maxCount = UINT32_MAX) const;

    // Evict a page (mark as not resident)
    void Evict(const VTPageId& page);

    u32 GetTrackedPageCount() const;

    void Reset();

    VTFeedbackStats GetStats() const;

private:
    float ComputePriority(const VTPageRequest& req) const;

    VTFeedbackConfig m_config;

    std::unordered_map<VTPageId, VTPageRequest, VTPageIdHasher> m_requests;
    std::unordered_map<VTPageId, PageResidency, VTPageIdHasher> m_residency;

    u32 m_totalProcessed = 0;
    u32 m_duplicatesRemoved = 0;
    u32 m_pagesEvicted = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
