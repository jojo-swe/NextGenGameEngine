#pragma once

#include "engine/core/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <bitset>
#include <functional>

namespace nge::assets {

// ─── Shader Permutation System ───────────────────────────────────────────
// Manages shader feature flags and compiles/caches variant permutations.
// Each shader can have up to 32 boolean feature flags that produce
// unique compiled variants via preprocessor defines.
//
// Example:
//   RegisterFeature("HAS_NORMAL_MAP", 0);
//   RegisterFeature("HAS_EMISSIVE", 1);
//   RegisterFeature("ENABLE_SHADOWS", 2);
//   auto key = MakeKey({0, 2}); // HAS_NORMAL_MAP + ENABLE_SHADOWS
//   auto* variant = GetOrCompile("pbr_forward", key);

using PermutationKey = u32; // Bitmask of enabled features

struct ShaderFeature {
    std::string name;       // Preprocessor define name
    u32         bit;        // Bit index (0-31)
    std::string description;
};

struct ShaderPermutation {
    std::string        shaderName;
    PermutationKey     key;
    std::vector<u8>    compiledSpirV; // Cached SPIR-V bytecode
    bool               compiled = false;
    u64                compileTimeNs = 0;
};

struct ShaderPermutationDesc {
    std::string              sourcePath;   // HLSL source file
    std::string              entryPoint;   // Entry point function name
    std::string              profile;      // e.g., "cs_6_6", "vs_6_6", "ps_6_6"
    std::vector<ShaderFeature> features;   // Available features for this shader
    std::vector<std::string> staticDefines; // Always-on defines
};

class ShaderPermutationManager {
public:
    bool Init();
    void Shutdown();

    // Register a shader with its available features
    void RegisterShader(const std::string& name, const ShaderPermutationDesc& desc);
    void UnregisterShader(const std::string& name);

    // Build a permutation key from feature bit indices
    static PermutationKey MakeKey(std::initializer_list<u32> bits);
    static PermutationKey MakeKey(const std::vector<u32>& bits);

    // Get or compile a specific permutation
    const ShaderPermutation* GetOrCompile(const std::string& shaderName, PermutationKey key);

    // Pre-compile all permutations for a shader (2^N variants)
    void PrecompileAll(const std::string& shaderName);

    // Pre-compile specific permutations
    void Precompile(const std::string& shaderName, const std::vector<PermutationKey>& keys);

    // Invalidate cached variants (e.g., on hot-reload)
    void InvalidateShader(const std::string& shaderName);
    void InvalidateAll();

    // Query
    u32 GetVariantCount(const std::string& shaderName) const;
    u32 GetTotalVariantCount() const;
    std::vector<std::string> GetRegisteredShaders() const;

    // Get the defines for a permutation key
    std::vector<std::string> GetDefines(const std::string& shaderName, PermutationKey key) const;

    // Compile callback (set to integrate with ShaderCompiler)
    using CompileFunc = std::function<std::vector<u8>(
        const std::string& sourcePath,
        const std::string& entryPoint,
        const std::string& profile,
        const std::vector<std::string>& defines
    )>;
    void SetCompileCallback(CompileFunc func) { m_compileFunc = std::move(func); }

private:
    struct ShaderEntry {
        ShaderPermutationDesc desc;
        std::unordered_map<PermutationKey, ShaderPermutation> variants;
    };

    std::unordered_map<std::string, ShaderEntry> m_shaders;
    CompileFunc m_compileFunc;
};

} // namespace nge::assets
