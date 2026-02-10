#pragma once

#include "engine/core/types.h"
#include <string>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <unordered_map>

namespace nge::assets {

// ─── Async Asset Loader ──────────────────────────────────────────────────
// Background-thread I/O system with priority queue. Loads assets from
// disk without blocking the main thread. Supports callbacks on completion
// and cancellation of pending requests.

enum class AssetPriority : u8 {
    Critical = 0,   // Needed this frame (blocking if not ready)
    High     = 1,   // Needed soon (visible geometry/textures)
    Medium   = 2,   // Prefetch (near camera but not yet visible)
    Low      = 3,   // Background preload
};

enum class AssetStatus : u8 {
    Pending,
    Loading,
    Loaded,
    Failed,
    Cancelled,
};

using AssetLoadCallback = std::function<void(const std::string& path, const std::vector<u8>& data, bool success)>;

struct AssetRequest {
    std::string       path;
    AssetPriority     priority;
    AssetLoadCallback callback;
    u64               requestId;
};

struct AssetLoadResult {
    std::string     path;
    std::vector<u8> data;
    AssetStatus     status;
    u64             requestId;
    f64             loadTimeMs;
};

struct AsyncLoaderConfig {
    u32 workerThreads = 2;
    u64 maxPendingRequests = 256;
    u64 readBufferSize = 4 * 1024 * 1024; // 4 MB read buffer
};

struct AsyncLoaderStats {
    u32 pendingRequests;
    u32 activeLoads;
    u32 completedThisFrame;
    u32 failedThisFrame;
    u64 totalBytesLoaded;
    u64 totalRequestsCompleted;
    f64 avgLoadTimeMs;
};

class AsyncAssetLoader {
public:
    bool Init(const AsyncLoaderConfig& config = {});
    void Shutdown();

    // Submit a load request (returns request ID)
    u64 LoadAsync(const std::string& path, AssetPriority priority, AssetLoadCallback callback);

    // Cancel a pending request
    bool Cancel(u64 requestId);

    // Process completed callbacks on the main thread (call once per frame)
    void ProcessCallbacks(u32 maxCallbacks = 32);

    // Check status of a request
    AssetStatus GetStatus(u64 requestId) const;

    // Synchronous load (blocks until complete)
    std::vector<u8> LoadSync(const std::string& path);

    // Stats
    AsyncLoaderStats GetStats() const;

private:
    struct CompareRequests {
        bool operator()(const AssetRequest& a, const AssetRequest& b) const {
            return static_cast<u8>(a.priority) > static_cast<u8>(b.priority);
        }
    };

    void WorkerThread();

    AsyncLoaderConfig m_config;

    // Priority queue of pending requests
    std::priority_queue<AssetRequest, std::vector<AssetRequest>, CompareRequests> m_pendingQueue;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCV;

    // Completed results waiting for main-thread callback dispatch
    std::vector<AssetLoadResult> m_completedResults;
    std::mutex m_resultsMutex;

    // Status tracking
    mutable std::mutex m_statusMutex;
    std::unordered_map<u64, AssetStatus> m_requestStatus;

    // Worker threads
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_running{false};
    std::atomic<u64> m_nextRequestId{1};

    // Callback map
    std::unordered_map<u64, AssetLoadCallback> m_callbacks;
    std::mutex m_callbackMutex;

    // Stats
    std::atomic<u64> m_totalBytesLoaded{0};
    std::atomic<u64> m_totalCompleted{0};
    std::atomic<u32> m_activeLoads{0};
    std::atomic<u32> m_completedThisFrame{0};
    std::atomic<u32> m_failedThisFrame{0};
    f64 m_totalLoadTimeMs = 0;
    mutable std::mutex m_statsMutex;
};

} // namespace nge::assets
