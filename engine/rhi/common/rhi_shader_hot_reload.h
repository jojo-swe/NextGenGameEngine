#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <mutex>

namespace nge::rhi {

// ─── GPU Shader Hot Reload Manager ───────────────────────────────────────
// Watches shader source files for changes and triggers recompilation +
// pipeline recreation. Tracks file timestamps, manages reload callbacks,
// and provides dependency tracking for includes.
//
// Use cases:
//   - Live shader editing during development
//   - Track shader source file modification timestamps
//   - Trigger recompile on change detection
//   - Include dependency tracking (changing common.hlsl recompiles dependents)
//   - Notify listeners when a shader is reloaded
//   - Stats: reloads, failures, watched files

// ShaderStage is defined in rhi_types.h

struct WatchedShader {
    u32                       shaderId;
    std::string               sourcePath;
    ShaderStage               stage;
    u64                       lastModifiedTime;
    u64                       compiledHash;
    std::vector<std::string>  includePaths;
    std::unordered_map<std::string, u64> includeModifiedTimes;
    bool                      needsReload;
    u32                       reloadCount;
    u32                       failCount;
};

struct ShaderHotReloadConfig {
    u32    maxWatchedShaders = 512;
    float  pollIntervalSeconds = 0.5f;
    bool   autoRecompile = true;
    bool   logReloads = true;
};

struct ShaderHotReloadStats {
    u32 watchedShaders;
    u32 totalReloads;
    u32 totalFailures;
    u32 pendingReloads;
    u32 includesTracked;
};

using ShaderReloadCallback = std::function<void(u32 shaderId, const std::string& path)>;

class ShaderHotReloadManager {
public:
    bool Init(const ShaderHotReloadConfig& config = {});
    void Shutdown();

    // Register a shader source file to watch
    bool WatchShader(u32 shaderId, const std::string& sourcePath, ShaderStage stage,
                      const std::vector<std::string>& includes = {});

    // Unwatch a shader
    void UnwatchShader(u32 shaderId);

    // Check all watched files for changes (call periodically)
    u32 PollChanges();

    // Mark a shader as needing reload (manual trigger)
    void MarkDirty(u32 shaderId);

    // Mark a shader reload as complete
    void MarkReloaded(u32 shaderId, u64 newHash);

    // Mark a shader reload as failed
    void MarkFailed(u32 shaderId);

    // Get all shaders that need reloading
    std::vector<u32> GetPendingReloads() const;

    // Check if a shader needs reload
    bool NeedsReload(u32 shaderId) const;

    // Get watched shader info
    const WatchedShader* GetShaderInfo(u32 shaderId) const;

    // Register a callback for when any shader is reloaded
    void RegisterCallback(ShaderReloadCallback callback);

    // Simulate a file modification (for testing)
    void SimulateFileChange(u32 shaderId, u64 newTimestamp);

    u32 GetWatchedCount() const;

    void Reset();

    ShaderHotReloadStats GetStats() const;

private:
    static u64 GetFileTimestamp(const std::string& path);
    void CheckIncludeDependencies(const std::string& changedInclude);

    ShaderHotReloadConfig m_config;

    std::unordered_map<u32, WatchedShader> m_shaders;
    std::vector<ShaderReloadCallback> m_callbacks;

    u32 m_totalReloads = 0;
    u32 m_totalFailures = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
