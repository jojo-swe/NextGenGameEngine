#include "engine/assets/shader_file_watcher.h"
#include "engine/core/logging/log.h"
#include <chrono>
#include <algorithm>

namespace nge::assets {

bool ShaderFileWatcher::Init(const FileWatcherConfig& config) {
    m_config = config;
    m_changesDetected = 0;
    m_recompilationsTriggered = 0;

    NGE_LOG_INFO("Shader file watcher initialized: poll={}ms, debounce={}ms, extensions={}",
                 config.pollIntervalMs, config.debounceMs, config.extensions.size());
    return true;
}

void ShaderFileWatcher::Shutdown() {
    Stop();
    m_watchDirs.clear();
    m_trackedFiles.clear();
    m_callbacks.clear();
    m_pendingEvents.clear();
}

void ShaderFileWatcher::AddWatchDirectory(const std::filesystem::path& directory) {
    std::lock_guard lock(m_mutex);

    if (!std::filesystem::exists(directory)) {
        NGE_LOG_WARN("Watch directory does not exist: {}", directory.string());
        return;
    }

    m_watchDirs.push_back(directory);
    ScanDirectory(directory);

    NGE_LOG_INFO("Watching shader directory: {} ({} files tracked)",
                 directory.string(), m_trackedFiles.size());
}

void ShaderFileWatcher::RemoveWatchDirectory(const std::filesystem::path& directory) {
    std::lock_guard lock(m_mutex);
    m_watchDirs.erase(
        std::remove(m_watchDirs.begin(), m_watchDirs.end(), directory),
        m_watchDirs.end());
}

void ShaderFileWatcher::OnFileChanged(FileChangeCallback callback) {
    std::lock_guard lock(m_mutex);
    m_callbacks.push_back(std::move(callback));
}

void ShaderFileWatcher::Start() {
    if (m_running.load()) return;
    m_running.store(true);
    m_watcherThread = std::thread(&ShaderFileWatcher::WatcherThreadFunc, this);
    NGE_LOG_INFO("Shader file watcher started");
}

void ShaderFileWatcher::Stop() {
    m_running.store(false);
    if (m_watcherThread.joinable()) {
        m_watcherThread.join();
    }
}

std::vector<FileChangeEvent> ShaderFileWatcher::Poll() {
    std::lock_guard lock(m_mutex);
    std::vector<FileChangeEvent> events;

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    for (const auto& dir : m_watchDirs) {
        if (!std::filesystem::exists(dir)) continue;

        auto options = m_config.watchSubdirectories
            ? std::filesystem::directory_options::follow_directory_symlink
            : std::filesystem::directory_options::none;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, options)) {
            if (!entry.is_regular_file()) continue;
            if (!MatchesExtension(entry.path())) continue;

            auto pathStr = entry.path().string();
            auto lastWrite = entry.last_write_time();

            auto it = m_trackedFiles.find(pathStr);
            if (it == m_trackedFiles.end()) {
                // New file
                TrackedFile tracked;
                tracked.path = entry.path();
                tracked.lastWriteTime = lastWrite;
                tracked.lastChangeTimestamp = static_cast<u64>(now);
                m_trackedFiles[pathStr] = tracked;

                FileChangeEvent evt;
                evt.path = entry.path();
                evt.type = FileChangeType::Created;
                evt.timestamp = static_cast<u64>(now);
                events.push_back(evt);
            } else if (lastWrite != it->second.lastWriteTime) {
                // Debounce check
                u64 elapsed = static_cast<u64>(now) - it->second.lastChangeTimestamp;
                if (elapsed >= m_config.debounceMs) {
                    it->second.lastWriteTime = lastWrite;
                    it->second.lastChangeTimestamp = static_cast<u64>(now);

                    FileChangeEvent evt;
                    evt.path = entry.path();
                    evt.type = FileChangeType::Modified;
                    evt.timestamp = static_cast<u64>(now);
                    events.push_back(evt);
                }
            }
        }
    }

    // Check for deleted files
    for (auto it = m_trackedFiles.begin(); it != m_trackedFiles.end(); ) {
        if (!std::filesystem::exists(it->second.path)) {
            FileChangeEvent evt;
            evt.path = it->second.path;
            evt.type = FileChangeType::Deleted;
            evt.timestamp = static_cast<u64>(now);
            events.push_back(evt);
            it = m_trackedFiles.erase(it);
        } else {
            ++it;
        }
    }

    m_changesDetected += static_cast<u32>(events.size());

    // Queue events for callback processing
    {
        std::lock_guard eLock(m_eventMutex);
        m_pendingEvents.insert(m_pendingEvents.end(), events.begin(), events.end());
    }

    return events;
}

void ShaderFileWatcher::ProcessEvents() {
    std::vector<FileChangeEvent> events;
    {
        std::lock_guard lock(m_eventMutex);
        events.swap(m_pendingEvents);
    }

    std::lock_guard lock(m_mutex);
    for (const auto& evt : events) {
        for (const auto& cb : m_callbacks) {
            cb(evt);
        }
        if (evt.type == FileChangeType::Modified || evt.type == FileChangeType::Created) {
            m_recompilationsTriggered++;
        }
    }
}

bool ShaderFileWatcher::HasChanged(const std::filesystem::path& filePath) const {
    std::lock_guard lock(m_mutex);
    auto it = m_trackedFiles.find(filePath.string());
    if (it == m_trackedFiles.end()) return false;

    try {
        auto currentWrite = std::filesystem::last_write_time(filePath);
        return currentWrite != it->second.lastWriteTime;
    } catch (...) {
        return false;
    }
}

FileWatcherStats ShaderFileWatcher::GetStats() const {
    std::lock_guard lock(m_mutex);
    FileWatcherStats stats{};
    stats.watchedDirectories = static_cast<u32>(m_watchDirs.size());
    stats.trackedFiles = static_cast<u32>(m_trackedFiles.size());
    stats.changesDetected = m_changesDetected;
    stats.recompilationsTriggered = m_recompilationsTriggered;
    stats.running = m_running.load();
    return stats;
}

void ShaderFileWatcher::WatcherThreadFunc() {
    while (m_running.load()) {
        Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(m_config.pollIntervalMs));
    }
}

void ShaderFileWatcher::ScanDirectory(const std::filesystem::path& dir) {
    if (!std::filesystem::exists(dir)) return;

    auto options = m_config.watchSubdirectories
        ? std::filesystem::directory_options::follow_directory_symlink
        : std::filesystem::directory_options::none;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, options)) {
        if (!entry.is_regular_file()) continue;
        if (!MatchesExtension(entry.path())) continue;

        TrackedFile tracked;
        tracked.path = entry.path();
        tracked.lastWriteTime = entry.last_write_time();
        m_trackedFiles[entry.path().string()] = tracked;
    }
}

bool ShaderFileWatcher::MatchesExtension(const std::filesystem::path& path) const {
    auto ext = path.extension().string();
    for (const auto& watchExt : m_config.extensions) {
        if (ext == watchExt) return true;
    }
    return false;
}

} // namespace nge::assets
