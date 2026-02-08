#include "engine/assets/async_loader.h"
#include "engine/core/logging/log.h"
#include <fstream>
#include <chrono>

namespace nge::assets {

bool AsyncAssetLoader::Init(const AsyncLoaderConfig& config) {
    m_config = config;
    m_running = true;
    m_totalBytesLoaded = 0;
    m_totalCompleted = 0;
    m_activeLoads = 0;

    // Spawn worker threads
    m_workers.reserve(config.workerThreads);
    for (u32 i = 0; i < config.workerThreads; ++i) {
        m_workers.emplace_back(&AsyncAssetLoader::WorkerThread, this);
    }

    NGE_LOG_INFO("Async asset loader initialized: {} worker threads", config.workerThreads);
    return true;
}

void AsyncAssetLoader::Shutdown() {
    m_running = false;
    m_queueCV.notify_all();

    for (auto& worker : m_workers) {
        if (worker.joinable()) worker.join();
    }
    m_workers.clear();

    // Clear queues
    {
        std::lock_guard lock(m_queueMutex);
        while (!m_pendingQueue.empty()) m_pendingQueue.pop();
    }
    {
        std::lock_guard lock(m_resultsMutex);
        m_completedResults.clear();
    }
    {
        std::lock_guard lock(m_callbackMutex);
        m_callbacks.clear();
    }
}

u64 AsyncAssetLoader::LoadAsync(const std::string& path, AssetPriority priority,
                                   AssetLoadCallback callback) {
    u64 requestId = m_nextRequestId.fetch_add(1);

    {
        std::lock_guard lock(m_statusMutex);
        m_requestStatus[requestId] = AssetStatus::Pending;
    }

    {
        std::lock_guard lock(m_callbackMutex);
        m_callbacks[requestId] = std::move(callback);
    }

    {
        std::lock_guard lock(m_queueMutex);
        AssetRequest req;
        req.path = path;
        req.priority = priority;
        req.requestId = requestId;
        m_pendingQueue.push(std::move(req));
    }

    m_queueCV.notify_one();
    return requestId;
}

bool AsyncAssetLoader::Cancel(u64 requestId) {
    std::lock_guard lock(m_statusMutex);
    auto it = m_requestStatus.find(requestId);
    if (it != m_requestStatus.end() && it->second == AssetStatus::Pending) {
        it->second = AssetStatus::Cancelled;
        return true;
    }
    return false;
}

void AsyncAssetLoader::ProcessCallbacks(u32 maxCallbacks) {
    std::vector<AssetLoadResult> results;

    {
        std::lock_guard lock(m_resultsMutex);
        u32 count = std::min(static_cast<u32>(m_completedResults.size()), maxCallbacks);
        results.assign(m_completedResults.begin(), m_completedResults.begin() + count);
        m_completedResults.erase(m_completedResults.begin(), m_completedResults.begin() + count);
    }

    m_completedThisFrame = 0;
    m_failedThisFrame = 0;

    for (const auto& result : results) {
        AssetLoadCallback callback;
        {
            std::lock_guard lock(m_callbackMutex);
            auto it = m_callbacks.find(result.requestId);
            if (it != m_callbacks.end()) {
                callback = std::move(it->second);
                m_callbacks.erase(it);
            }
        }

        if (callback) {
            bool success = (result.status == AssetStatus::Loaded);
            callback(result.path, result.data, success);

            if (success) m_completedThisFrame++;
            else m_failedThisFrame++;
        }
    }
}

AssetStatus AsyncAssetLoader::GetStatus(u64 requestId) const {
    std::lock_guard lock(m_statusMutex);
    auto it = m_requestStatus.find(requestId);
    return it != m_requestStatus.end() ? it->second : AssetStatus::Failed;
}

std::vector<u8> AsyncAssetLoader::LoadSync(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        NGE_LOG_ERROR("Sync load failed: {}", path);
        return {};
    }

    auto size = file.tellg();
    file.seekg(0);
    std::vector<u8> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

void AsyncAssetLoader::WorkerThread() {
    while (m_running) {
        AssetRequest req;

        {
            std::unique_lock lock(m_queueMutex);
            m_queueCV.wait(lock, [this] { return !m_pendingQueue.empty() || !m_running; });

            if (!m_running) return;
            if (m_pendingQueue.empty()) continue;

            req = m_pendingQueue.top();
            m_pendingQueue.pop();
        }

        // Check if cancelled
        {
            std::lock_guard lock(m_statusMutex);
            auto it = m_requestStatus.find(req.requestId);
            if (it != m_requestStatus.end() && it->second == AssetStatus::Cancelled) {
                continue;
            }
            it->second = AssetStatus::Loading;
        }

        m_activeLoads++;
        auto startTime = std::chrono::high_resolution_clock::now();

        // Perform file I/O
        AssetLoadResult result;
        result.path = req.path;
        result.requestId = req.requestId;

        std::ifstream file(req.path, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            auto size = file.tellg();
            file.seekg(0);
            result.data.resize(static_cast<size_t>(size));
            file.read(reinterpret_cast<char*>(result.data.data()), size);

            if (file.good()) {
                result.status = AssetStatus::Loaded;
                m_totalBytesLoaded += result.data.size();
            } else {
                result.status = AssetStatus::Failed;
                result.data.clear();
            }
        } else {
            result.status = AssetStatus::Failed;
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        result.loadTimeMs = std::chrono::duration<f64, std::milli>(endTime - startTime).count();

        m_activeLoads--;
        m_totalCompleted++;

        {
            std::lock_guard lock(m_statsMutex);
            m_totalLoadTimeMs += result.loadTimeMs;
        }

        // Update status
        {
            std::lock_guard lock(m_statusMutex);
            m_requestStatus[req.requestId] = result.status;
        }

        // Queue result for main-thread callback
        {
            std::lock_guard lock(m_resultsMutex);
            m_completedResults.push_back(std::move(result));
        }
    }
}

AsyncLoaderStats AsyncAssetLoader::GetStats() const {
    AsyncLoaderStats stats;
    {
        std::lock_guard lock(m_queueMutex);
        stats.pendingRequests = static_cast<u32>(m_pendingQueue.size());
    }
    stats.activeLoads = m_activeLoads.load();
    stats.completedThisFrame = m_completedThisFrame.load();
    stats.failedThisFrame = m_failedThisFrame.load();
    stats.totalBytesLoaded = m_totalBytesLoaded.load();
    stats.totalRequestsCompleted = m_totalCompleted.load();
    {
        std::lock_guard lock(m_statsMutex);
        stats.avgLoadTimeMs = m_totalCompleted > 0 ? m_totalLoadTimeMs / m_totalCompleted : 0;
    }
    return stats;
}

} // namespace nge::assets
