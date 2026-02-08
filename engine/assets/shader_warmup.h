#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/rhi/vulkan/vk_pipeline_cache.h"
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>

namespace nge::assets {

// ─── Shader Variant Warm-Up System ───────────────────────────────────────
// Pre-compiles pipeline state objects (PSOs) at load time to avoid
// runtime hitching. Processes permutations in parallel using the job
// system and reports progress for loading screens.
//
// Usage:
//   warmup.Init(device, pipelineCache);
//   warmup.AddVariant("PBR_Opaque", { "HAS_NORMAL_MAP", "HAS_AO" });
//   warmup.AddVariant("PBR_Opaque", { "HAS_NORMAL_MAP" });
//   warmup.Execute(); // Blocks until all PSOs compiled
//   // Or: warmup.ExecuteAsync(); // Non-blocking

struct ShaderVariant {
    std::string shaderName;
    std::vector<std::string> defines;
    rhi::PipelineHandle cachedPipeline;
    bool compiled = false;
    bool failed = false;
};

struct WarmupConfig {
    u32  maxParallelCompiles = 4;     // Worker threads for compilation
    bool enableDiskCache = true;      // Load/save from pipeline cache
    bool logProgress = true;
    bool abortOnError = false;
};

struct WarmupProgress {
    u32 total;
    u32 completed;
    u32 failed;
    f32 percent;
    bool done;
};

class ShaderWarmupSystem {
public:
    using CompileCallback = std::function<rhi::PipelineHandle(
        const std::string& shaderName,
        const std::vector<std::string>& defines)>;

    bool Init(rhi::IDevice* device, const WarmupConfig& config = {});
    void Shutdown();

    // Set the compilation function (bridges to your pipeline creation)
    void SetCompileCallback(CompileCallback callback);

    // Queue variants for compilation
    void AddVariant(const std::string& shaderName, const std::vector<std::string>& defines = {});

    // Add all permutations of a shader (2^N variants for N flags)
    void AddAllPermutations(const std::string& shaderName,
                              const std::vector<std::string>& possibleDefines,
                              u32 maxPermutations = 256);

    // Execute synchronously (blocks until done)
    void Execute();

    // Execute asynchronously (returns immediately, poll progress)
    void ExecuteAsync();

    // Poll progress
    WarmupProgress GetProgress() const;
    bool IsDone() const { return m_done.load(); }

    // Wait for async execution to finish
    void Wait();

    // Get compiled pipeline for a variant
    rhi::PipelineHandle GetPipeline(const std::string& shaderName,
                                      const std::vector<std::string>& defines) const;

    // Stats
    u32 GetVariantCount() const { return static_cast<u32>(m_variants.size()); }
    u32 GetCompiledCount() const { return m_compiledCount.load(); }
    u32 GetFailedCount() const { return m_failedCount.load(); }
    f64 GetTotalCompileTimeMs() const { return m_totalCompileTimeMs; }

private:
    void CompileVariant(u32 index);
    std::string MakeKey(const std::string& shaderName,
                          const std::vector<std::string>& defines) const;

    rhi::IDevice* m_device = nullptr;
    WarmupConfig m_config;
    CompileCallback m_compileCallback;

    std::vector<ShaderVariant> m_variants;
    mutable std::mutex m_mutex;

    std::atomic<u32> m_compiledCount{0};
    std::atomic<u32> m_failedCount{0};
    std::atomic<bool> m_done{false};
    f64 m_totalCompileTimeMs = 0;
};

} // namespace nge::assets
