#include "engine/assets/shader_warmup.h"
#include "engine/core/logging/log.h"
#include <chrono>
#include <thread>
#include <algorithm>
#include <sstream>

namespace nge::assets {

bool ShaderWarmupSystem::Init(rhi::IDevice* device, const WarmupConfig& config) {
    m_device = device;
    m_config = config;
    m_compiledCount = 0;
    m_failedCount = 0;
    m_done = false;
    m_totalCompileTimeMs = 0;
    return true;
}

void ShaderWarmupSystem::Shutdown() {
    Wait();
    m_variants.clear();
}

void ShaderWarmupSystem::SetCompileCallback(CompileCallback callback) {
    m_compileCallback = std::move(callback);
}

void ShaderWarmupSystem::AddVariant(const std::string& shaderName,
                                      const std::vector<std::string>& defines) {
    ShaderVariant variant;
    variant.shaderName = shaderName;
    variant.defines = defines;
    m_variants.push_back(std::move(variant));
}

void ShaderWarmupSystem::AddAllPermutations(const std::string& shaderName,
                                               const std::vector<std::string>& possibleDefines,
                                               u32 maxPermutations) {
    u32 count = std::min(1u << static_cast<u32>(possibleDefines.size()), maxPermutations);

    for (u32 mask = 0; mask < count; ++mask) {
        std::vector<std::string> defines;
        for (u32 bit = 0; bit < static_cast<u32>(possibleDefines.size()); ++bit) {
            if (mask & (1u << bit)) {
                defines.push_back(possibleDefines[bit]);
            }
        }
        AddVariant(shaderName, defines);
    }

    if (m_config.logProgress) {
        NGE_LOG_INFO("Shader warmup: queued {} permutations for '{}'", count, shaderName);
    }
}

void ShaderWarmupSystem::Execute() {
    if (!m_compileCallback) {
        NGE_LOG_ERROR("Shader warmup: no compile callback set");
        return;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    if (m_config.logProgress) {
        NGE_LOG_INFO("Shader warmup: compiling {} variants...", m_variants.size());
    }

    m_compiledCount = 0;
    m_failedCount = 0;
    m_done = false;

    // Simple parallel compilation using std::thread
    u32 numThreads = std::min(m_config.maxParallelCompiles,
                               static_cast<u32>(m_variants.size()));
    std::atomic<u32> nextIndex{0};

    auto workerFn = [this, &nextIndex]() {
        while (true) {
            u32 idx = nextIndex.fetch_add(1);
            if (idx >= m_variants.size()) break;
            CompileVariant(idx);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (u32 i = 0; i < numThreads; ++i) {
        threads.emplace_back(workerFn);
    }
    for (auto& t : threads) {
        t.join();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    m_totalCompileTimeMs = std::chrono::duration<f64, std::milli>(endTime - startTime).count();
    m_done = true;

    if (m_config.logProgress) {
        NGE_LOG_INFO("Shader warmup complete: {}/{} compiled, {} failed, {:.1f} ms",
                     m_compiledCount.load(), m_variants.size(),
                     m_failedCount.load(), m_totalCompileTimeMs);
    }
}

void ShaderWarmupSystem::ExecuteAsync() {
    std::thread([this]() { Execute(); }).detach();
}

WarmupProgress ShaderWarmupSystem::GetProgress() const {
    WarmupProgress progress;
    progress.total = static_cast<u32>(m_variants.size());
    progress.completed = m_compiledCount.load();
    progress.failed = m_failedCount.load();
    progress.percent = progress.total > 0
        ? static_cast<f32>(progress.completed + progress.failed) * 100.0f / static_cast<f32>(progress.total)
        : 100.0f;
    progress.done = m_done.load();
    return progress;
}

void ShaderWarmupSystem::Wait() {
    while (!m_done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

rhi::PipelineHandle ShaderWarmupSystem::GetPipeline(const std::string& shaderName,
                                                       const std::vector<std::string>& defines) const {
    std::string key = MakeKey(shaderName, defines);
    std::lock_guard lock(m_mutex);
    for (const auto& variant : m_variants) {
        if (MakeKey(variant.shaderName, variant.defines) == key && variant.compiled) {
            return variant.cachedPipeline;
        }
    }
    return rhi::PipelineHandle{};
}

void ShaderWarmupSystem::CompileVariant(u32 index) {
    auto& variant = m_variants[index];

    try {
        variant.cachedPipeline = m_compileCallback(variant.shaderName, variant.defines);
        variant.compiled = true;
        m_compiledCount.fetch_add(1);
    } catch (const std::exception& e) {
        variant.failed = true;
        m_failedCount.fetch_add(1);
        NGE_LOG_ERROR("Shader warmup failed for '{}': {}", variant.shaderName, e.what());

        if (m_config.abortOnError) {
            NGE_LOG_ERROR("Aborting shader warmup due to error");
        }
    }
}

std::string ShaderWarmupSystem::MakeKey(const std::string& shaderName,
                                          const std::vector<std::string>& defines) const {
    // Sort defines for consistent key generation
    auto sorted = defines;
    std::sort(sorted.begin(), sorted.end());

    std::ostringstream ss;
    ss << shaderName;
    for (const auto& d : sorted) {
        ss << "|" << d;
    }
    return ss.str();
}

} // namespace nge::assets
