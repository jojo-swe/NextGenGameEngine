#include "engine/rhi/common/rhi_mipmap_scheduler.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <cmath>

namespace nge::rhi {

bool MipmapScheduler::Init(const MipSchedulerConfig& config) {
    m_config = config;
    m_jobs.reserve(config.maxPendingJobs);
    m_totalSubmitted = 0;
    m_totalCompleted = 0;
    m_totalMips = 0;
    m_frameMips = 0;
    m_asyncDispatches = 0;
    m_graphicsDispatches = 0;
    m_batchesDispatched = 0;

    NGE_LOG_INFO("Mipmap scheduler initialized: maxPending={}, batch={}, maxMipsPerFrame={}, async={}",
                 config.maxPendingJobs, config.batchSize, config.maxMipsPerFrame, config.preferAsyncCompute);
    return true;
}

void MipmapScheduler::Shutdown() {
    m_jobs.clear();
    m_jobIndex.clear();
}

u64 MipmapScheduler::Submit(const MipGenRequest& request) {
    std::lock_guard lock(m_mutex);

    if (m_jobs.size() >= m_config.maxPendingJobs) {
        NGE_LOG_WARN("Mipmap scheduler: max pending jobs reached ({})", m_config.maxPendingJobs);
        return 0;
    }

    // Check for duplicate
    if (m_jobIndex.count(request.textureHandle)) {
        NGE_LOG_DEBUG("Mipmap scheduler: texture 0x{:X} already queued", request.textureHandle);
        return request.textureHandle;
    }

    MipGenJob job;
    job.textureHandle = request.textureHandle;
    job.currentMip = 0;
    job.totalMips = request.mipLevels > 0 ? request.mipLevels : ComputeMipLevels(request.width, request.height);
    job.width = request.width;
    job.height = request.height;
    job.arrayLayers = request.arrayLayers;
    job.format = request.format;
    job.filter = request.filter;
    job.completed = false;
    job.dispatched = false;

    u32 index = static_cast<u32>(m_jobs.size());
    m_jobs.push_back(std::move(job));
    m_jobIndex[request.textureHandle] = index;
    m_totalSubmitted++;

    NGE_LOG_DEBUG("Mipmap scheduler: queued '{}' ({}x{}, {} mips, {} layers)",
                  request.debugName, request.width, request.height, job.totalMips, request.arrayLayers);

    return request.textureHandle;
}

bool MipmapScheduler::Cancel(u64 textureHandle) {
    std::lock_guard lock(m_mutex);

    auto it = m_jobIndex.find(textureHandle);
    if (it == m_jobIndex.end()) return false;

    u32 idx = it->second;
    m_jobs[idx].completed = true; // Mark as done so it gets cleaned up
    m_jobIndex.erase(it);

    return true;
}

u32 MipmapScheduler::ProcessFrame() {
    std::lock_guard lock(m_mutex);
    m_frameMips = 0;

    // Collect active jobs
    std::vector<MipGenJob*> active;
    for (auto& job : m_jobs) {
        if (!job.completed && job.currentMip < job.totalMips) {
            active.push_back(&job);
        }
    }

    if (active.empty()) return 0;

    // Process in batches
    u32 processed = 0;
    for (size_t i = 0; i < active.size() && m_frameMips < m_config.maxMipsPerFrame; i += m_config.batchSize) {
        std::vector<MipGenJob*> batch;
        for (size_t j = i; j < std::min(i + m_config.batchSize, active.size()); ++j) {
            batch.push_back(active[j]);
        }
        processed += DispatchBatch(batch);
    }

    return processed;
}

void MipmapScheduler::MarkCompleted(u64 textureHandle) {
    std::lock_guard lock(m_mutex);

    auto it = m_jobIndex.find(textureHandle);
    if (it == m_jobIndex.end()) return;

    m_jobs[it->second].completed = true;
    m_totalCompleted++;
}

bool MipmapScheduler::IsComplete(u64 textureHandle) const {
    std::lock_guard lock(m_mutex);

    auto it = m_jobIndex.find(textureHandle);
    if (it == m_jobIndex.end()) return false;

    return m_jobs[it->second].completed;
}

f32 MipmapScheduler::GetProgress(u64 textureHandle) const {
    std::lock_guard lock(m_mutex);

    auto it = m_jobIndex.find(textureHandle);
    if (it == m_jobIndex.end()) return 0.0f;

    const auto& job = m_jobs[it->second];
    if (job.totalMips == 0) return 1.0f;
    if (job.completed) return 1.0f;

    return static_cast<f32>(job.currentMip) / static_cast<f32>(job.totalMips);
}

u32 MipmapScheduler::GetPendingCount() const {
    std::lock_guard lock(m_mutex);

    u32 count = 0;
    for (const auto& job : m_jobs) {
        if (!job.completed) count++;
    }
    return count;
}

u32 MipmapScheduler::ComputeMipLevels(u32 width, u32 height) {
    u32 maxDim = std::max(width, height);
    if (maxDim == 0) return 0;
    return static_cast<u32>(std::floor(std::log2(static_cast<f64>(maxDim)))) + 1;
}

void MipmapScheduler::ComputeDispatchSize(u32 width, u32 height, u32 mipLevel,
                                            u32& groupsX, u32& groupsY) {
    u32 mipWidth = std::max(1u, width >> mipLevel);
    u32 mipHeight = std::max(1u, height >> mipLevel);

    // 8x8 thread groups
    groupsX = (mipWidth + 7) / 8;
    groupsY = (mipHeight + 7) / 8;
}

void MipmapScheduler::Reset() {
    std::lock_guard lock(m_mutex);
    m_jobs.clear();
    m_jobIndex.clear();
    m_totalSubmitted = 0;
    m_totalCompleted = 0;
    m_totalMips = 0;
    m_frameMips = 0;
    m_asyncDispatches = 0;
    m_graphicsDispatches = 0;
    m_batchesDispatched = 0;
}

MipSchedulerStats MipmapScheduler::GetStats() const {
    std::lock_guard lock(m_mutex);
    MipSchedulerStats stats{};
    stats.totalJobsSubmitted = m_totalSubmitted;
    stats.totalJobsCompleted = m_totalCompleted;
    stats.mipsGeneratedThisFrame = m_frameMips;
    stats.totalMipsGenerated = m_totalMips;
    stats.asyncDispatches = m_asyncDispatches;
    stats.graphicsDispatches = m_graphicsDispatches;
    stats.batchesDispatched = m_batchesDispatched;

    u32 pending = 0;
    for (const auto& job : m_jobs) {
        if (!job.completed) pending++;
    }
    stats.pendingJobs = pending;

    return stats;
}

u32 MipmapScheduler::DispatchBatch(std::vector<MipGenJob*>& batch) {
    u32 mipsGenerated = 0;

    for (auto* job : batch) {
        if (m_frameMips >= m_config.maxMipsPerFrame) break;

        // Generate one mip level per job per batch pass
        u32 srcMip = job->currentMip;
        u32 dstMip = srcMip + 1;

        if (dstMip >= job->totalMips) {
            job->completed = true;
            m_totalCompleted++;
            continue;
        }

        u32 groupsX, groupsY;
        ComputeDispatchSize(job->width, job->height, dstMip, groupsX, groupsY);

        // TODO: Bind compute pipeline based on format + filter
        // TODO: Set source mip SRV and destination mip UAV
        // TODO: vkCmdDispatch(groupsX, groupsY, job->arrayLayers)

        // sRGB correction: linearize before filter, re-encode after
        if (m_config.enableSRGBCorrection &&
            (job->format == MipFormat::RGBA8_SRGB)) {
            // Use sRGB-aware mip generation shader variant
        }

        // Min/Max filter for depth mips
        if (job->filter == MipFilter::Min || job->filter == MipFilter::Max) {
            // Use min/max reduction shader variant
        }

        job->currentMip = dstMip;
        job->dispatched = true;
        mipsGenerated++;
        m_frameMips++;
        m_totalMips++;

        if (m_config.preferAsyncCompute) {
            m_asyncDispatches++;
        } else {
            m_graphicsDispatches++;
        }
    }

    if (mipsGenerated > 0) {
        m_batchesDispatched++;
    }

    return mipsGenerated;
}

} // namespace nge::rhi
