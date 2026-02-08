#pragma once

#include "engine/core/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <filesystem>
#include <thread>
#include <atomic>

namespace nge::assets {

// ─── Shader Hot-Reload File Watcher ──────────────────────────────────────
// Monitors shader source directories for file changes and triggers
// recompilation. Uses filesystem polling (portable) with optional
// platform-specific notification (ReadDirectoryChangesW / inotify).
//
// Flow:
//   1. Register watch directories (e.g., "shaders/")
//   2. Background thread polls for modifications
//   3. On change: invalidate shader variant cache + recompile
//   4. Callback notifies engine of updated shader

enum class FileChangeType : u8 {
    Modified,
    Created,
    Deleted,
    Renamed,
};

struct FileChangeEvent {
    std::filesystem::path path;
    FileChangeType        type;
    u64                   timestamp;
};

using FileChangeCallback = std::function<void(const FileChangeEvent&)>;

struct FileWatcherConfig {
    u32  pollIntervalMs = 500;         // Polling interval
    bool watchSubdirectories = true;
    std::vector<std::string> extensions = {".hlsl", ".glsl", ".h", ".hlsli"}; // File extensions to watch
    u32  debounceMs = 100;             // Ignore rapid repeated changes
};

struct FileWatcherStats {
    u32 watchedDirectories;
    u32 trackedFiles;
    u32 changesDetected;
    u32 recompilationsTriggered;
    bool running;
};

class ShaderFileWatcher {
public:
    bool Init(const FileWatcherConfig& config = {});
    void Shutdown();

    // Add a directory to watch
    void AddWatchDirectory(const std::filesystem::path& directory);

    // Remove a watch directory
    void RemoveWatchDirectory(const std::filesystem::path& directory);

    // Register callback for file changes
    void OnFileChanged(FileChangeCallback callback);

    // Start the background watcher thread
    void Start();

    // Stop the watcher thread
    void Stop();

    // Manual poll (call from main thread if not using background thread)
    std::vector<FileChangeEvent> Poll();

    // Process pending events (call from main thread to dispatch callbacks)
    void ProcessEvents();

    // Check if a specific file has been modified since last check
    bool HasChanged(const std::filesystem::path& filePath) const;

    FileWatcherStats GetStats() const;

private:
    struct TrackedFile {
        std::filesystem::path path;
        std::filesystem::file_time_type lastWriteTime;
        u64 lastChangeTimestamp = 0;
    };

    void WatcherThreadFunc();
    void ScanDirectory(const std::filesystem::path& dir);
    bool MatchesExtension(const std::filesystem::path& path) const;

    FileWatcherConfig m_config;
    std::vector<std::filesystem::path> m_watchDirs;
    std::unordered_map<std::string, TrackedFile> m_trackedFiles;
    std::vector<FileChangeCallback> m_callbacks;
    std::vector<FileChangeEvent> m_pendingEvents;

    std::thread m_watcherThread;
    std::atomic<bool> m_running{false};

    u32 m_changesDetected = 0;
    u32 m_recompilationsTriggered = 0;

    mutable std::mutex m_mutex;
    mutable std::mutex m_eventMutex;
};

} // namespace nge::assets
