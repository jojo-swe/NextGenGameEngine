#include "engine/rhi/common/rhi_sampler_feedback.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool SamplerFeedbackManager::Init(IDevice* device, const SamplerFeedbackConfig& config) {
    m_device = device;
    m_config = config;
    m_currentFrame = 0;
    m_frameNumber = 0;
    m_stats = {};

    // TODO: Create feedback buffer (GPU writable, structured buffer)
    // BufferDesc feedbackDesc{};
    // feedbackDesc.size = config.feedbackBufferSize * sizeof(FeedbackEntry);
    // feedbackDesc.usage = BufferUsage::StorageBuffer | BufferUsage::TransferSrc;
    // feedbackDesc.memoryType = MemoryType::DeviceLocal;
    // m_feedbackBuffer = device->CreateBuffer(feedbackDesc);

    // TODO: Create atomic counter buffer (4 bytes, GPU writable)
    // BufferDesc counterDesc{};
    // counterDesc.size = sizeof(u32);
    // counterDesc.usage = BufferUsage::StorageBuffer | BufferUsage::TransferDst;
    // m_counterBuffer = device->CreateBuffer(counterDesc);

    // TODO: Create compacted buffer
    // m_compactedBuffer = device->CreateBuffer(feedbackDesc);

    // Create per-frame readback buffers
    m_frames.resize(config.framesInFlight);
    for (auto& frame : m_frames) {
        // TODO: Create host-visible readback buffer
        // BufferDesc readbackDesc{};
        // readbackDesc.size = config.feedbackBufferSize * sizeof(FeedbackEntry);
        // readbackDesc.usage = BufferUsage::TransferDst;
        // readbackDesc.memoryType = MemoryType::HostVisible;
        // frame.readbackBuffer = device->CreateBuffer(readbackDesc);
        frame.entryCount = 0;
        frame.readbackReady = false;
    }

    NGE_LOG_INFO("Sampler feedback manager initialized: bufferSize={}, maxTextures={}, pageSize={}",
                 config.feedbackBufferSize, config.maxTexturesTracked, config.pageSize);
    return true;
}

void SamplerFeedbackManager::Shutdown() {
    // TODO: Destroy all buffers
    // device->DestroyBuffer(m_feedbackBuffer);
    // device->DestroyBuffer(m_counterBuffer);
    // device->DestroyBuffer(m_compactedBuffer);
    // for (auto& frame : m_frames) device->DestroyBuffer(frame.readbackBuffer);
    m_frames.clear();
    m_currentRequests.clear();
    m_requestMap.clear();
}

void SamplerFeedbackManager::BeginFrame(ICommandList* cmd, u64 frameNumber) {
    m_frameNumber = frameNumber;
    m_currentFrame = static_cast<u32>(frameNumber % m_config.framesInFlight);

    // Clear counter to zero
    // TODO: vkCmdFillBuffer(cmd, m_counterBuffer, 0, sizeof(u32), 0);

    // Clear feedback buffer
    // TODO: vkCmdFillBuffer(cmd, m_feedbackBuffer, 0, bufferSize, 0);

    (void)cmd;
}

void SamplerFeedbackManager::CompactFeedback(ICommandList* cmd) {
    std::lock_guard lock(m_mutex);

    // TODO: Dispatch compaction compute shader
    // 1. Read atomic counter to get entry count
    // 2. Sort feedback entries by (textureId, mipLevel, pageX, pageY)
    // 3. Deduplicate identical entries, accumulate priorities
    // 4. Write compacted result to m_compactedBuffer

    // TODO: Copy compacted buffer to readback buffer for this frame
    // vkCmdCopyBuffer(cmd, m_compactedBuffer, m_frames[m_currentFrame].readbackBuffer, ...);

    m_frames[m_currentFrame].readbackReady = true;
    (void)cmd;
}

void SamplerFeedbackManager::ReadbackResults() {
    std::lock_guard lock(m_mutex);

    // Read from the oldest frame's readback buffer
    u32 readbackFrame = (m_currentFrame + 1) % m_config.framesInFlight;
    if (readbackFrame >= m_frames.size()) return;
    if (!m_frames[readbackFrame].readbackReady) return;

    // TODO: Map readback buffer and parse FeedbackEntry array
    // void* mapped;
    // vkMapMemory(device, readbackMemory, offset, size, 0, &mapped);
    // FeedbackEntry* entries = static_cast<FeedbackEntry*>(mapped);
    // u32 count = m_frames[readbackFrame].entryCount;

    // Build residency requests
    m_currentRequests.clear();
    m_requestMap.clear();

    // Stub: in production, iterate over mapped entries
    // for (u32 i = 0; i < count; ++i) {
    //     FeedbackEntry& e = entries[i];
    //     u64 hash = HashRequest(e.textureId, e.mipLevel, e.pageX, e.pageY);
    //     auto it = m_requestMap.find(hash);
    //     if (it == m_requestMap.end()) {
    //         ResidencyRequest req;
    //         req.textureId = e.textureId;
    //         req.mipLevel = e.mipLevel;
    //         req.pageX = e.pageX;
    //         req.pageY = e.pageY;
    //         req.screenCoverage = static_cast<f32>(e.priority) / 65535.0f;
    //         req.frameRequested = static_cast<u32>(m_frameNumber);
    //         m_requestMap[hash] = req;
    //     } else {
    //         it->second.screenCoverage = std::max(it->second.screenCoverage,
    //             static_cast<f32>(e.priority) / 65535.0f);
    //     }
    // }

    for (auto& [hash, req] : m_requestMap) {
        m_currentRequests.push_back(req);
    }

    // Sort by screen coverage (highest priority first)
    std::sort(m_currentRequests.begin(), m_currentRequests.end(),
        [](const ResidencyRequest& a, const ResidencyRequest& b) {
            return a.screenCoverage > b.screenCoverage;
        });

    // Update stats
    m_stats.feedbackEntriesThisFrame = m_frames[readbackFrame].entryCount;
    m_stats.uniqueRequests = static_cast<u32>(m_currentRequests.size());

    std::unordered_map<u32, bool> uniqueTextures;
    u32 totalPages = 0;
    for (const auto& req : m_currentRequests) {
        uniqueTextures[req.textureId] = true;
        totalPages++;
    }
    m_stats.totalTexturesRequested = static_cast<u32>(uniqueTextures.size());
    m_stats.totalPagesRequested = totalPages;
    m_stats.compactionReductions = m_stats.feedbackEntriesThisFrame - m_stats.uniqueRequests;

    m_frames[readbackFrame].readbackReady = false;
    // vkUnmapMemory(device, readbackMemory);
}

std::vector<ResidencyRequest> SamplerFeedbackManager::GetResidencyRequests() const {
    std::lock_guard lock(m_mutex);
    return m_currentRequests;
}

std::vector<ResidencyRequest> SamplerFeedbackManager::GetRequestsForTexture(u32 textureId) const {
    std::lock_guard lock(m_mutex);
    std::vector<ResidencyRequest> filtered;
    for (const auto& req : m_currentRequests) {
        if (req.textureId == textureId) {
            filtered.push_back(req);
        }
    }
    return filtered;
}

bool SamplerFeedbackManager::WasPageRequested(u32 textureId, u8 mipLevel, u16 pageX, u16 pageY) const {
    std::lock_guard lock(m_mutex);
    u64 hash = HashRequest(textureId, mipLevel, pageX, pageY);
    return m_requestMap.count(hash) > 0;
}

SamplerFeedbackStats SamplerFeedbackManager::GetStats() const {
    std::lock_guard lock(m_mutex);
    return m_stats;
}

u64 SamplerFeedbackManager::HashRequest(u32 textureId, u8 mipLevel, u16 pageX, u16 pageY) const {
    u64 hash = 14695981039346656037ULL;
    hash ^= static_cast<u64>(textureId); hash *= 1099511628211ULL;
    hash ^= static_cast<u64>(mipLevel);  hash *= 1099511628211ULL;
    hash ^= static_cast<u64>(pageX);     hash *= 1099511628211ULL;
    hash ^= static_cast<u64>(pageY);     hash *= 1099511628211ULL;
    return hash;
}

} // namespace nge::rhi
