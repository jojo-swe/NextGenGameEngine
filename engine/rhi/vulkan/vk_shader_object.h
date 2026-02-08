#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi::vulkan {

// ─── Vulkan Shader Object Manager (VK_EXT_shader_object) ────────────────
// Manages VkShaderEXT objects for runtime shader compilation without
// traditional VkPipeline objects. Shader objects can be independently
// compiled and bound, enabling faster iteration and dynamic combinations.
//
// Benefits over pipelines:
//   - No pipeline permutation explosion
//   - Dynamic state covers most render state
//   - Independent vertex/fragment/compute shader binding
//   - Binary shader caching for fast reload

enum class ShaderObjectStage : u8 {
    Vertex,
    Fragment,
    Compute,
    Geometry,
    TessControl,
    TessEvaluation,
    Mesh,
    Task,
};

struct ShaderObjectDesc {
    ShaderObjectStage           stage;
    std::string                 entryPoint = "main";
    std::string                 debugName;
    std::vector<u8>             spirvCode;
    std::vector<u32>            pushConstantRanges; // Packed: stageFlags|offset|size
    std::vector<u64>            descriptorSetLayouts;
};

struct ShaderObjectBinary {
    std::vector<u8> binaryData;
    u64             hash;
};

struct ShaderObjectStats {
    u32 totalObjects;
    u32 vertexShaders;
    u32 fragmentShaders;
    u32 computeShaders;
    u32 meshShaders;
    u32 cacheHits;
    u32 compilations;
    u64 totalBinaryBytes;
};

struct ShaderObjectConfig {
    bool enableBinaryCache = true;
    std::string cachePath = "shader_cache/objects/";
    u32 maxCachedObjects = 2048;
};

class ShaderObjectManager {
public:
    bool Init(IDevice* device, const ShaderObjectConfig& config = {});
    void Shutdown();

    // Create a shader object from SPIR-V
    u64 CreateShaderObject(const ShaderObjectDesc& desc);

    // Create from cached binary (fast path)
    u64 CreateFromBinary(const ShaderObjectBinary& binary, ShaderObjectStage stage);

    // Destroy a shader object
    void Destroy(u64 shaderObject);

    // Bind shader objects to a command list
    void BindVertexFragment(ICommandList* cmd, u64 vertexShader, u64 fragmentShader);
    void BindCompute(ICommandList* cmd, u64 computeShader);
    void BindMeshTask(ICommandList* cmd, u64 meshShader, u64 taskShader = 0);

    // Get binary for caching
    ShaderObjectBinary GetBinary(u64 shaderObject) const;

    // Save/load binary cache to disk
    bool SaveCacheToDisk() const;
    bool LoadCacheFromDisk();

    // Invalidate a specific shader (for hot-reload)
    void Invalidate(u64 shaderObject);

    // Get shader by debug name
    u64 FindByName(const std::string& name) const;

    ShaderObjectStats GetStats() const;

private:
    struct ShaderObject {
        u64               handle;     // VkShaderEXT
        ShaderObjectStage stage;
        std::string       debugName;
        u64               spirvHash;
        std::vector<u8>   binaryCache;
        bool              alive = true;
    };

    u64 HashSPIRV(const std::vector<u8>& spirv) const;

    IDevice* m_device = nullptr;
    ShaderObjectConfig m_config;
    std::unordered_map<u64, ShaderObject> m_objects; // handle -> object
    std::unordered_map<u64, u64> m_hashToHandle;     // spirvHash -> handle
    std::unordered_map<std::string, u64> m_nameToHandle;

    u64 m_nextHandle = 1;
    ShaderObjectStats m_stats{};
    mutable std::mutex m_mutex;
};

} // namespace nge::rhi::vulkan
