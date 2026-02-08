#include "engine/assets/shader_permutations.h"
#include "engine/core/logging/log.h"
#include <chrono>

namespace nge::assets {

bool ShaderPermutationManager::Init() {
    NGE_LOG_INFO("Shader permutation manager initialized");
    return true;
}

void ShaderPermutationManager::Shutdown() {
    m_shaders.clear();
}

void ShaderPermutationManager::RegisterShader(const std::string& name,
                                                const ShaderPermutationDesc& desc) {
    ShaderEntry entry;
    entry.desc = desc;
    m_shaders[name] = std::move(entry);

    u32 maxVariants = 1u << static_cast<u32>(desc.features.size());
    NGE_LOG_INFO("Registered shader '{}' with {} features ({} max variants)",
                 name, desc.features.size(), maxVariants);
}

void ShaderPermutationManager::UnregisterShader(const std::string& name) {
    m_shaders.erase(name);
}

PermutationKey ShaderPermutationManager::MakeKey(std::initializer_list<u32> bits) {
    PermutationKey key = 0;
    for (u32 bit : bits) {
        key |= (1u << bit);
    }
    return key;
}

PermutationKey ShaderPermutationManager::MakeKey(const std::vector<u32>& bits) {
    PermutationKey key = 0;
    for (u32 bit : bits) {
        key |= (1u << bit);
    }
    return key;
}

const ShaderPermutation* ShaderPermutationManager::GetOrCompile(const std::string& shaderName,
                                                                  PermutationKey key) {
    auto shaderIt = m_shaders.find(shaderName);
    if (shaderIt == m_shaders.end()) {
        NGE_LOG_ERROR("Shader '{}' not registered", shaderName);
        return nullptr;
    }

    auto& entry = shaderIt->second;

    // Check cache
    auto variantIt = entry.variants.find(key);
    if (variantIt != entry.variants.end() && variantIt->second.compiled) {
        return &variantIt->second;
    }

    // Compile
    if (!m_compileFunc) {
        NGE_LOG_ERROR("No compile callback set for shader permutation manager");
        return nullptr;
    }

    auto defines = GetDefines(shaderName, key);

    auto startTime = std::chrono::high_resolution_clock::now();

    std::vector<u8> spirv = m_compileFunc(
        entry.desc.sourcePath,
        entry.desc.entryPoint,
        entry.desc.profile,
        defines
    );

    auto endTime = std::chrono::high_resolution_clock::now();
    u64 compileNs = static_cast<u64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count());

    if (spirv.empty()) {
        NGE_LOG_ERROR("Failed to compile shader '{}' variant 0x{:08X}", shaderName, key);
        return nullptr;
    }

    ShaderPermutation perm;
    perm.shaderName = shaderName;
    perm.key = key;
    perm.compiledSpirV = std::move(spirv);
    perm.compiled = true;
    perm.compileTimeNs = compileNs;

    entry.variants[key] = std::move(perm);

    NGE_LOG_DEBUG("Compiled shader '{}' variant 0x{:08X} ({} bytes, {:.2f} ms)",
                  shaderName, key, entry.variants[key].compiledSpirV.size(),
                  static_cast<f64>(compileNs) / 1e6);

    return &entry.variants[key];
}

void ShaderPermutationManager::PrecompileAll(const std::string& shaderName) {
    auto it = m_shaders.find(shaderName);
    if (it == m_shaders.end()) return;

    u32 featureCount = static_cast<u32>(it->second.desc.features.size());
    u32 totalVariants = 1u << featureCount;

    NGE_LOG_INFO("Pre-compiling {} variants for shader '{}'", totalVariants, shaderName);

    for (u32 key = 0; key < totalVariants; ++key) {
        GetOrCompile(shaderName, key);
    }
}

void ShaderPermutationManager::Precompile(const std::string& shaderName,
                                            const std::vector<PermutationKey>& keys) {
    for (PermutationKey key : keys) {
        GetOrCompile(shaderName, key);
    }
}

void ShaderPermutationManager::InvalidateShader(const std::string& shaderName) {
    auto it = m_shaders.find(shaderName);
    if (it != m_shaders.end()) {
        it->second.variants.clear();
        NGE_LOG_INFO("Invalidated all variants for shader '{}'", shaderName);
    }
}

void ShaderPermutationManager::InvalidateAll() {
    for (auto& [name, entry] : m_shaders) {
        entry.variants.clear();
    }
    NGE_LOG_INFO("Invalidated all shader variants");
}

u32 ShaderPermutationManager::GetVariantCount(const std::string& shaderName) const {
    auto it = m_shaders.find(shaderName);
    return it != m_shaders.end() ? static_cast<u32>(it->second.variants.size()) : 0;
}

u32 ShaderPermutationManager::GetTotalVariantCount() const {
    u32 total = 0;
    for (const auto& [name, entry] : m_shaders) {
        total += static_cast<u32>(entry.variants.size());
    }
    return total;
}

std::vector<std::string> ShaderPermutationManager::GetRegisteredShaders() const {
    std::vector<std::string> names;
    names.reserve(m_shaders.size());
    for (const auto& [name, entry] : m_shaders) {
        names.push_back(name);
    }
    return names;
}

std::vector<std::string> ShaderPermutationManager::GetDefines(const std::string& shaderName,
                                                                PermutationKey key) const {
    std::vector<std::string> defines;

    auto it = m_shaders.find(shaderName);
    if (it == m_shaders.end()) return defines;

    const auto& desc = it->second.desc;

    // Static defines (always on)
    for (const auto& d : desc.staticDefines) {
        defines.push_back(d);
    }

    // Feature-based defines
    for (const auto& feature : desc.features) {
        if (key & (1u << feature.bit)) {
            defines.push_back(feature.name + "=1");
        } else {
            defines.push_back(feature.name + "=0");
        }
    }

    return defines;
}

} // namespace nge::assets
