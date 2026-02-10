#include "engine/rhi/common/rhi_shader_hot_reload.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool ShaderHotReloadManager::Init(const ShaderHotReloadConfig& config) {
    m_config = config;
    m_totalReloads = 0;
    m_totalFailures = 0;

    NGE_LOG_INFO("Shader hot reload initialized: maxShaders={}, pollInterval={}s, auto={}, log={}",
                 config.maxWatchedShaders, config.pollIntervalSeconds, config.autoRecompile, config.logReloads);
    return true;
}

void ShaderHotReloadManager::Shutdown() {
    m_shaders.clear();
    m_callbacks.clear();
}

bool ShaderHotReloadManager::WatchShader(u32 shaderId, const std::string& sourcePath, ShaderStage stage,
                                           const std::vector<std::string>& includes) {
    std::lock_guard lock(m_mutex);

    if (m_shaders.size() >= m_config.maxWatchedShaders && m_shaders.find(shaderId) == m_shaders.end()) {
        NGE_LOG_ERROR("Shader hot reload: max watched shaders reached ({})", m_config.maxWatchedShaders);
        return false;
    }

    WatchedShader shader;
    shader.shaderId = shaderId;
    shader.sourcePath = sourcePath;
    shader.stage = stage;
    shader.lastModifiedTime = 0;
    shader.compiledHash = 0;
    shader.includePaths = includes;
    shader.needsReload = false;
    shader.reloadCount = 0;
    shader.failCount = 0;

    m_shaders[shaderId] = std::move(shader);

    if (m_config.logReloads) {
        NGE_LOG_INFO("Watching shader {}: '{}'", shaderId, sourcePath);
    }

    return true;
}

void ShaderHotReloadManager::UnwatchShader(u32 shaderId) {
    std::lock_guard lock(m_mutex);
    m_shaders.erase(shaderId);
}

u32 ShaderHotReloadManager::PollChanges() {
    std::lock_guard lock(m_mutex);

    u32 changesDetected = 0;

    for (auto& [id, shader] : m_shaders) {
        // In a real implementation, we'd stat the file here.
        // For now, changes are detected via SimulateFileChange or MarkDirty.
        if (shader.needsReload) {
            changesDetected++;
        }
    }

    return changesDetected;
}

void ShaderHotReloadManager::MarkDirty(u32 shaderId) {
    std::lock_guard lock(m_mutex);

    auto it = m_shaders.find(shaderId);
    if (it == m_shaders.end()) return;

    it->second.needsReload = true;

    if (m_config.logReloads) {
        NGE_LOG_INFO("Shader {} marked dirty: '{}'", shaderId, it->second.sourcePath);
    }
}

void ShaderHotReloadManager::MarkReloaded(u32 shaderId, u64 newHash) {
    std::lock_guard lock(m_mutex);

    auto it = m_shaders.find(shaderId);
    if (it == m_shaders.end()) return;

    it->second.needsReload = false;
    it->second.compiledHash = newHash;
    it->second.reloadCount++;
    m_totalReloads++;

    if (m_config.logReloads) {
        NGE_LOG_INFO("Shader {} reloaded (hash={}): '{}'", shaderId, newHash, it->second.sourcePath);
    }

    // Notify callbacks
    for (const auto& cb : m_callbacks) {
        if (cb) cb(shaderId, it->second.sourcePath);
    }
}

void ShaderHotReloadManager::MarkFailed(u32 shaderId) {
    std::lock_guard lock(m_mutex);

    auto it = m_shaders.find(shaderId);
    if (it == m_shaders.end()) return;

    it->second.failCount++;
    m_totalFailures++;

    NGE_LOG_ERROR("Shader {} reload failed (attempt {}): '{}'",
                  shaderId, it->second.failCount, it->second.sourcePath);
}

std::vector<u32> ShaderHotReloadManager::GetPendingReloads() const {
    std::lock_guard lock(m_mutex);

    std::vector<u32> pending;
    for (const auto& [id, shader] : m_shaders) {
        if (shader.needsReload) {
            pending.push_back(id);
        }
    }
    return pending;
}

bool ShaderHotReloadManager::NeedsReload(u32 shaderId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_shaders.find(shaderId);
    if (it == m_shaders.end()) return false;

    return it->second.needsReload;
}

const WatchedShader* ShaderHotReloadManager::GetShaderInfo(u32 shaderId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_shaders.find(shaderId);
    if (it == m_shaders.end()) return nullptr;

    return &it->second;
}

void ShaderHotReloadManager::RegisterCallback(ShaderReloadCallback callback) {
    std::lock_guard lock(m_mutex);
    m_callbacks.push_back(std::move(callback));
}

void ShaderHotReloadManager::SimulateFileChange(u32 shaderId, u64 newTimestamp) {
    std::lock_guard lock(m_mutex);

    auto it = m_shaders.find(shaderId);
    if (it == m_shaders.end()) return;

    if (newTimestamp != it->second.lastModifiedTime) {
        it->second.lastModifiedTime = newTimestamp;
        it->second.needsReload = true;

        // Check if this shader's path is an include dependency of others
        CheckIncludeDependencies(it->second.sourcePath);
    }
}

u32 ShaderHotReloadManager::GetWatchedCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_shaders.size());
}

void ShaderHotReloadManager::Reset() {
    std::lock_guard lock(m_mutex);
    m_shaders.clear();
    m_callbacks.clear();
    m_totalReloads = 0;
    m_totalFailures = 0;
}

ShaderHotReloadStats ShaderHotReloadManager::GetStats() const {
    std::lock_guard lock(m_mutex);

    ShaderHotReloadStats stats{};
    stats.watchedShaders = static_cast<u32>(m_shaders.size());
    stats.totalReloads = m_totalReloads;
    stats.totalFailures = m_totalFailures;

    u32 pending = 0;
    u32 includes = 0;
    for (const auto& [id, shader] : m_shaders) {
        if (shader.needsReload) pending++;
        includes += static_cast<u32>(shader.includePaths.size());
    }
    stats.pendingReloads = pending;
    stats.includesTracked = includes;

    return stats;
}

void ShaderHotReloadManager::CheckIncludeDependencies(const std::string& changedInclude) {
    // If a file that changed is listed as an include of another shader,
    // mark that shader as dirty too
    for (auto& [id, shader] : m_shaders) {
        if (shader.needsReload) continue; // Already dirty

        for (const auto& inc : shader.includePaths) {
            if (inc == changedInclude) {
                shader.needsReload = true;
                if (m_config.logReloads) {
                    NGE_LOG_INFO("Shader {} marked dirty (include '{}' changed): '{}'",
                                 id, changedInclude, shader.sourcePath);
                }
                break;
            }
        }
    }
}

} // namespace nge::rhi
