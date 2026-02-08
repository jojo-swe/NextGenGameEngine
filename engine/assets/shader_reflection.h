#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace nge::assets {

// ─── Shader Reflection Utility ───────────────────────────────────────────
// Parses SPIR-V bytecode to extract binding layout information for
// automatic descriptor set layout generation. Replaces manual layout
// specification with reflection-driven pipeline creation.

enum class ShaderStage : u8 {
    Vertex,
    Fragment,
    Compute,
    Geometry,
    TessControl,
    TessEval,
    Mesh,
    Task,
    RayGen,
    RayMiss,
    RayClosestHit,
    RayAnyHit,
};

enum class BindingType : u8 {
    UniformBuffer,
    StorageBuffer,
    SampledImage,
    StorageImage,
    Sampler,
    CombinedImageSampler,
    InputAttachment,
    AccelerationStructure,
    PushConstant,
};

struct ReflectedBinding {
    std::string   name;
    BindingType   type;
    u32           set;
    u32           binding;
    u32           count;         // Array size (1 for non-array)
    u32           blockSize;     // For uniform/storage buffers
    ShaderStage   stage;
    bool          readonly;
};

struct ReflectedPushConstant {
    std::string name;
    u32         offset;
    u32         size;
    ShaderStage stage;
};

struct ReflectedInput {
    std::string name;
    u32         location;
    u32         componentCount;  // 1-4
    bool        isFloat;
};

struct ReflectedOutput {
    std::string name;
    u32         location;
    u32         componentCount;
    bool        isFloat;
};

struct ShaderReflectionData {
    ShaderStage                     stage;
    std::vector<ReflectedBinding>   bindings;
    std::vector<ReflectedPushConstant> pushConstants;
    std::vector<ReflectedInput>     inputs;
    std::vector<ReflectedOutput>    outputs;
    u32                             localSizeX = 0; // Compute only
    u32                             localSizeY = 0;
    u32                             localSizeZ = 0;
    std::string                     entryPoint;
};

class ShaderReflector {
public:
    // Reflect a single SPIR-V module
    static bool Reflect(const std::vector<u32>& spirvBytecode, ShaderReflectionData& outData);
    static bool Reflect(const u32* spirvData, u32 wordCount, ShaderReflectionData& outData);

    // Merge bindings from multiple stages into a unified layout
    static std::vector<ReflectedBinding> MergeBindings(
        const std::vector<ShaderReflectionData>& stages);

    // Generate descriptor set layout info from merged bindings
    struct DescriptorSetInfo {
        u32                             set;
        std::vector<ReflectedBinding>   bindings;
    };
    static std::vector<DescriptorSetInfo> BuildSetLayouts(
        const std::vector<ReflectedBinding>& mergedBindings);

    // Utility: get binding type name
    static const char* BindingTypeName(BindingType type);
    static const char* StageName(ShaderStage stage);
};

} // namespace nge::assets
