#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <filesystem>

namespace nge::rhi {

// ─── GPU Shader Module Cache ─────────────────────────────────────────────
// Deduplicates VkShaderModule objects by SPIR-V content hash.
// Lazy-loads shader bytecode from disk on first request and caches
// the resulting module handle. Supports invalidation for hot-reload.

struct ShaderModuleDesc {
    std::filesystem::path filePath;
    std::string           entryPoint = "main";
    u64                   contentHash = 0; // 0 = compute from bytecode
};

struct CachedShaderModule {
    u64                   handle;          // VkShaderModule
    u64                   contentHash;
    std::filesystem::path filePath;
    std::string           entryPoint;
    u64                   lastUsedFrame;
    u32                   refCount;
    std::vector<u8>       spirvBytes;      // Cached bytecode
};

struct ShaderModuleCacheConfig {
    u32 maxCachedModules = 512;
    u64 evictionAgeFrames = 600;
    bool keepBytecodeInMemory = false; // Keep SPIR-V after module creation
};

struct ShaderModuleCacheStats {
    u32 cachedModules;
    u32 hits;
    u32 misses;
    u32 evictions;
    u64 totalBytecodeBytes;
};

class ShaderModuleCache {
public:
    bool Init(IDevice* device, const ShaderModuleCacheConfig& config = {});
    void Shutdown();

    // Get or load a shader module (lazy load from disk)
    u64 GetOrLoad(const ShaderModuleDesc& desc, u64 frameNumber);

    // Get or create from in-memory SPIR-V bytecode
    u64 GetOrCreate(const std::vector<u8>& spirv, const std::string& debugName, u64 frameNumber);

    // Add a reference to a module
    void AddRef(u64 handle);

    // Release a reference
    void Release(u64 handle);

    // Invalidate a module (for hot-reload)
    void Invalidate(const std::filesystem::path& filePath);

    // Invalidate all modules
    void InvalidateAll();

    // Evict unused modules
    u32 EvictUnused(u64 currentFrame);

    // Check if a module exists
    bool HasModule(u64 contentHash) const;

    ShaderModuleCacheStats GetStats() const;

    // Hash SPIR-V bytecode
    static u64 HashSpirv(const std::vector<u8>& spirv);

private:
    std::vector<u8> LoadFile(const std::filesystem::path& path) const;

    IDevice* m_device = nullptr;
    ShaderModuleCacheConfig m_config;

    std::unordered_map<u64, CachedShaderModule> m_modules; // hash -> module
    std::unordered_map<std::string, u64> m_pathToHash;     // filepath -> hash

    u32 m_hits = 0;
    u32 m_misses = 0;
    u32 m_evictions = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
