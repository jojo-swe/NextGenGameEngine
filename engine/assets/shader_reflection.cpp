#include "engine/assets/shader_reflection.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <cstring>

namespace nge::assets {

// ─── SPIR-V Constants ────────────────────────────────────────────────────
// Minimal SPIR-V parsing — enough to extract decorations and binding info.

namespace spirv {
    constexpr u32 MAGIC_NUMBER = 0x07230203;

    // Opcodes we care about
    constexpr u32 OpEntryPoint       = 15;
    constexpr u32 OpExecutionMode    = 16;
    constexpr u32 OpDecorate         = 71;
    constexpr u32 OpMemberDecorate   = 72;
    constexpr u32 OpName             = 5;
    constexpr u32 OpMemberName       = 6;
    constexpr u32 OpTypeVoid         = 19;
    constexpr u32 OpTypeBool         = 20;
    constexpr u32 OpTypeInt          = 21;
    constexpr u32 OpTypeFloat        = 22;
    constexpr u32 OpTypeVector       = 23;
    constexpr u32 OpTypeMatrix       = 24;
    constexpr u32 OpTypeImage        = 25;
    constexpr u32 OpTypeSampler      = 26;
    constexpr u32 OpTypeSampledImage = 27;
    constexpr u32 OpTypeArray        = 28;
    constexpr u32 OpTypeRuntimeArray = 29;
    constexpr u32 OpTypeStruct       = 30;
    constexpr u32 OpTypePointer      = 32;
    constexpr u32 OpVariable         = 59;
    constexpr u32 OpConstant         = 43;

    // Decorations
    constexpr u32 DecoBinding        = 33;
    constexpr u32 DecoDescriptorSet  = 34;
    constexpr u32 DecoLocation       = 30;
    constexpr u32 DecoNonWritable    = 24;
    constexpr u32 DecoBlock          = 2;
    constexpr u32 DecoBufferBlock    = 3;

    // Storage classes
    constexpr u32 StorageUniform          = 2;
    constexpr u32 StorageInput            = 1;
    constexpr u32 StorageOutput           = 3;
    constexpr u32 StorageUniformConstant  = 0;
    constexpr u32 StoragePushConstant     = 9;
    constexpr u32 StorageStorageBuffer    = 12;

    // Execution modes
    constexpr u32 ExecLocalSize      = 17;

    // Execution models
    constexpr u32 ExecVertex         = 0;
    constexpr u32 ExecTessControl    = 1;
    constexpr u32 ExecTessEval       = 2;
    constexpr u32 ExecGeometry       = 3;
    constexpr u32 ExecFragment       = 4;
    constexpr u32 ExecGLCompute      = 5;
    constexpr u32 ExecMeshNV         = 5320;
    constexpr u32 ExecTaskNV         = 5267;
    constexpr u32 ExecRayGenKHR      = 5313;
    constexpr u32 ExecMissKHR        = 5317;
    constexpr u32 ExecClosestHitKHR  = 5314;
    constexpr u32 ExecAnyHitKHR      = 5315;
}

static ShaderStage ExecutionModelToStage(u32 model) {
    switch (model) {
        case spirv::ExecVertex:        return ShaderStage::Vertex;
        case spirv::ExecFragment:      return ShaderStage::Fragment;
        case spirv::ExecGLCompute:     return ShaderStage::Compute;
        case spirv::ExecGeometry:      return ShaderStage::Geometry;
        case spirv::ExecTessControl:   return ShaderStage::TessControl;
        case spirv::ExecTessEval:      return ShaderStage::TessEval;
        case spirv::ExecMeshNV:        return ShaderStage::Mesh;
        case spirv::ExecTaskNV:        return ShaderStage::Task;
        case spirv::ExecRayGenKHR:     return ShaderStage::RayGen;
        case spirv::ExecMissKHR:       return ShaderStage::RayMiss;
        case spirv::ExecClosestHitKHR: return ShaderStage::RayClosestHit;
        case spirv::ExecAnyHitKHR:     return ShaderStage::RayAnyHit;
        default:                       return ShaderStage::Compute;
    }
}

static std::string ReadString(const u32* data, u32 wordOffset, u32 maxWords) {
    std::string result;
    const char* str = reinterpret_cast<const char*>(data + wordOffset);
    u32 maxChars = (maxWords - wordOffset) * 4;
    for (u32 i = 0; i < maxChars && str[i] != '\0'; ++i) {
        result += str[i];
    }
    return result;
}

bool ShaderReflector::Reflect(const std::vector<u32>& spirvBytecode, ShaderReflectionData& outData) {
    return Reflect(spirvBytecode.data(), static_cast<u32>(spirvBytecode.size()), outData);
}

bool ShaderReflector::Reflect(const u32* spirvData, u32 wordCount, ShaderReflectionData& outData) {
    if (!spirvData || wordCount < 5) return false;
    if (spirvData[0] != spirv::MAGIC_NUMBER) {
        NGE_LOG_ERROR("Shader reflection: invalid SPIR-V magic number");
        return false;
    }

    // Header: magic, version, generator, bound, reserved
    u32 bound = spirvData[3];

    // Per-ID tracking
    struct IdInfo {
        std::string name;
        u32 set = UINT32_MAX;
        u32 binding = UINT32_MAX;
        u32 location = UINT32_MAX;
        u32 storageClass = UINT32_MAX;
        u32 typeId = 0;
        u32 pointedTypeId = 0;
        bool nonWritable = false;
        bool isBlock = false;
        bool isBufferBlock = false;
    };
    std::vector<IdInfo> ids(bound);

    // Per-type tracking
    struct TypeInfo {
        u32 opcode = 0;
        u32 elementTypeId = 0;
        u32 componentCount = 0;
        u32 arrayLength = 0;
        bool isFloat = false;
        std::vector<u32> memberTypeIds;
    };
    std::vector<TypeInfo> types(bound);

    // Constants
    std::unordered_map<u32, u32> constants;

    // First pass: collect names, decorations, types
    u32 offset = 5; // Skip header
    while (offset < wordCount) {
        u32 word = spirvData[offset];
        u32 opcode = word & 0xFFFF;
        u32 instrLen = word >> 16;
        if (instrLen == 0) break;

        switch (opcode) {
            case spirv::OpName: {
                u32 id = spirvData[offset + 1];
                if (id < bound) ids[id].name = ReadString(spirvData, offset + 2, offset + instrLen);
                break;
            }
            case spirv::OpDecorate: {
                u32 id = spirvData[offset + 1];
                u32 decoration = spirvData[offset + 2];
                if (id < bound) {
                    if (decoration == spirv::DecoBinding && instrLen > 3)
                        ids[id].binding = spirvData[offset + 3];
                    else if (decoration == spirv::DecoDescriptorSet && instrLen > 3)
                        ids[id].set = spirvData[offset + 3];
                    else if (decoration == spirv::DecoLocation && instrLen > 3)
                        ids[id].location = spirvData[offset + 3];
                    else if (decoration == spirv::DecoNonWritable)
                        ids[id].nonWritable = true;
                    else if (decoration == spirv::DecoBlock)
                        ids[id].isBlock = true;
                    else if (decoration == spirv::DecoBufferBlock)
                        ids[id].isBufferBlock = true;
                }
                break;
            }
            case spirv::OpEntryPoint: {
                u32 execModel = spirvData[offset + 1];
                outData.stage = ExecutionModelToStage(execModel);
                outData.entryPoint = ReadString(spirvData, offset + 3, offset + instrLen);
                break;
            }
            case spirv::OpExecutionMode: {
                u32 mode = spirvData[offset + 2];
                if (mode == spirv::ExecLocalSize && instrLen >= 6) {
                    outData.localSizeX = spirvData[offset + 3];
                    outData.localSizeY = spirvData[offset + 4];
                    outData.localSizeZ = spirvData[offset + 5];
                }
                break;
            }
            case spirv::OpConstant: {
                u32 resultType = spirvData[offset + 1];
                u32 resultId = spirvData[offset + 2];
                if (instrLen > 3) constants[resultId] = spirvData[offset + 3];
                (void)resultType;
                break;
            }
            case spirv::OpTypeFloat: {
                u32 id = spirvData[offset + 1];
                if (id < bound) { types[id].opcode = opcode; types[id].isFloat = true; }
                break;
            }
            case spirv::OpTypeInt: {
                u32 id = spirvData[offset + 1];
                if (id < bound) types[id].opcode = opcode;
                break;
            }
            case spirv::OpTypeVector: {
                u32 id = spirvData[offset + 1];
                if (id < bound) {
                    types[id].opcode = opcode;
                    types[id].elementTypeId = spirvData[offset + 2];
                    types[id].componentCount = spirvData[offset + 3];
                    types[id].isFloat = (types[spirvData[offset + 2]].isFloat);
                }
                break;
            }
            case spirv::OpTypeImage:
            case spirv::OpTypeSampler:
            case spirv::OpTypeSampledImage: {
                u32 id = spirvData[offset + 1];
                if (id < bound) types[id].opcode = opcode;
                break;
            }
            case spirv::OpTypeStruct: {
                u32 id = spirvData[offset + 1];
                if (id < bound) {
                    types[id].opcode = opcode;
                    for (u32 m = 2; m < instrLen; ++m)
                        types[id].memberTypeIds.push_back(spirvData[offset + m]);
                }
                break;
            }
            case spirv::OpTypeArray: {
                u32 id = spirvData[offset + 1];
                if (id < bound) {
                    types[id].opcode = opcode;
                    types[id].elementTypeId = spirvData[offset + 2];
                    u32 lengthId = spirvData[offset + 3];
                    auto cit = constants.find(lengthId);
                    types[id].arrayLength = (cit != constants.end()) ? cit->second : 0;
                }
                break;
            }
            case spirv::OpTypeRuntimeArray: {
                u32 id = spirvData[offset + 1];
                if (id < bound) {
                    types[id].opcode = opcode;
                    types[id].elementTypeId = spirvData[offset + 2];
                    types[id].arrayLength = 0; // Runtime
                }
                break;
            }
            case spirv::OpTypePointer: {
                u32 id = spirvData[offset + 1];
                if (id < bound) {
                    types[id].opcode = opcode;
                    ids[id].storageClass = spirvData[offset + 2];
                    ids[id].pointedTypeId = spirvData[offset + 3];
                }
                break;
            }
            case spirv::OpVariable: {
                u32 typeId = spirvData[offset + 1];
                u32 resultId = spirvData[offset + 2];
                u32 storageClass = spirvData[offset + 3];
                if (resultId < bound) {
                    ids[resultId].typeId = typeId;
                    ids[resultId].storageClass = storageClass;
                    ids[resultId].pointedTypeId = ids[typeId].pointedTypeId;
                }
                break;
            }
        }

        offset += instrLen;
    }

    // Second pass: build reflected bindings from variables
    for (u32 id = 0; id < bound; ++id) {
        const auto& info = ids[id];

        // Inputs
        if (info.storageClass == spirv::StorageInput && info.location != UINT32_MAX) {
            ReflectedInput input;
            input.name = info.name;
            input.location = info.location;
            input.componentCount = 4; // Default
            input.isFloat = true;

            u32 pointedType = info.pointedTypeId;
            if (pointedType < bound) {
                if (types[pointedType].opcode == spirv::OpTypeVector) {
                    input.componentCount = types[pointedType].componentCount;
                    input.isFloat = types[pointedType].isFloat;
                } else if (types[pointedType].opcode == spirv::OpTypeFloat) {
                    input.componentCount = 1;
                    input.isFloat = true;
                } else if (types[pointedType].opcode == spirv::OpTypeInt) {
                    input.componentCount = 1;
                    input.isFloat = false;
                }
            }
            outData.inputs.push_back(std::move(input));
        }

        // Outputs
        if (info.storageClass == spirv::StorageOutput && info.location != UINT32_MAX) {
            ReflectedOutput output;
            output.name = info.name;
            output.location = info.location;
            output.componentCount = 4;
            output.isFloat = true;
            outData.outputs.push_back(std::move(output));
        }

        // Push constants
        if (info.storageClass == spirv::StoragePushConstant) {
            ReflectedPushConstant pc;
            pc.name = info.name.empty() ? "push_constants" : info.name;
            pc.offset = 0;
            pc.size = 128; // Default estimate; real size comes from struct reflection
            pc.stage = outData.stage;
            outData.pushConstants.push_back(std::move(pc));
        }

        // Descriptor bindings
        if (info.binding == UINT32_MAX) continue;
        if (info.storageClass != spirv::StorageUniform &&
            info.storageClass != spirv::StorageUniformConstant &&
            info.storageClass != spirv::StorageStorageBuffer) continue;

        ReflectedBinding rb;
        rb.name = info.name;
        rb.set = (info.set != UINT32_MAX) ? info.set : 0;
        rb.binding = info.binding;
        rb.count = 1;
        rb.blockSize = 0;
        rb.stage = outData.stage;
        rb.readonly = info.nonWritable;

        // Determine binding type from pointed type
        u32 pointedType = info.pointedTypeId;
        if (pointedType < bound) {
            u32 typeOp = types[pointedType].opcode;
            if (typeOp == spirv::OpTypeSampledImage) {
                rb.type = BindingType::CombinedImageSampler;
            } else if (typeOp == spirv::OpTypeImage) {
                rb.type = info.nonWritable ? BindingType::SampledImage : BindingType::StorageImage;
            } else if (typeOp == spirv::OpTypeSampler) {
                rb.type = BindingType::Sampler;
            } else if (typeOp == spirv::OpTypeStruct) {
                if (info.storageClass == spirv::StorageStorageBuffer || ids[pointedType].isBufferBlock) {
                    rb.type = BindingType::StorageBuffer;
                } else {
                    rb.type = BindingType::UniformBuffer;
                }
            } else if (typeOp == spirv::OpTypeArray || typeOp == spirv::OpTypeRuntimeArray) {
                rb.count = types[pointedType].arrayLength;
                rb.type = BindingType::SampledImage; // Assume texture array
            }
        }

        outData.bindings.push_back(std::move(rb));
    }

    // Sort by set then binding
    std::sort(outData.bindings.begin(), outData.bindings.end(),
        [](const ReflectedBinding& a, const ReflectedBinding& b) {
            return a.set < b.set || (a.set == b.set && a.binding < b.binding);
        });

    std::sort(outData.inputs.begin(), outData.inputs.end(),
        [](const ReflectedInput& a, const ReflectedInput& b) { return a.location < b.location; });

    std::sort(outData.outputs.begin(), outData.outputs.end(),
        [](const ReflectedOutput& a, const ReflectedOutput& b) { return a.location < b.location; });

    return true;
}

std::vector<ReflectedBinding> ShaderReflector::MergeBindings(
    const std::vector<ShaderReflectionData>& stages) {
    std::unordered_map<std::string, ReflectedBinding> merged;

    for (const auto& stage : stages) {
        for (const auto& binding : stage.bindings) {
            std::string key = std::to_string(binding.set) + ":" + std::to_string(binding.binding);
            auto it = merged.find(key);
            if (it == merged.end()) {
                merged[key] = binding;
            }
            // Multiple stages can reference the same binding — that's fine
        }
    }

    std::vector<ReflectedBinding> result;
    result.reserve(merged.size());
    for (auto& [key, binding] : merged) {
        result.push_back(std::move(binding));
    }

    std::sort(result.begin(), result.end(),
        [](const ReflectedBinding& a, const ReflectedBinding& b) {
            return a.set < b.set || (a.set == b.set && a.binding < b.binding);
        });

    return result;
}

std::vector<ShaderReflector::DescriptorSetInfo> ShaderReflector::BuildSetLayouts(
    const std::vector<ReflectedBinding>& mergedBindings) {
    std::unordered_map<u32, std::vector<ReflectedBinding>> setMap;
    for (const auto& b : mergedBindings) {
        setMap[b.set].push_back(b);
    }

    std::vector<DescriptorSetInfo> layouts;
    for (auto& [setIndex, bindings] : setMap) {
        DescriptorSetInfo info;
        info.set = setIndex;
        info.bindings = std::move(bindings);
        layouts.push_back(std::move(info));
    }

    std::sort(layouts.begin(), layouts.end(),
        [](const DescriptorSetInfo& a, const DescriptorSetInfo& b) { return a.set < b.set; });

    return layouts;
}

const char* ShaderReflector::BindingTypeName(BindingType type) {
    switch (type) {
        case BindingType::UniformBuffer:        return "UniformBuffer";
        case BindingType::StorageBuffer:        return "StorageBuffer";
        case BindingType::SampledImage:         return "SampledImage";
        case BindingType::StorageImage:         return "StorageImage";
        case BindingType::Sampler:              return "Sampler";
        case BindingType::CombinedImageSampler: return "CombinedImageSampler";
        case BindingType::InputAttachment:      return "InputAttachment";
        case BindingType::AccelerationStructure:return "AccelerationStructure";
        case BindingType::PushConstant:         return "PushConstant";
    }
    return "Unknown";
}

const char* ShaderReflector::StageName(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:       return "Vertex";
        case ShaderStage::Fragment:     return "Fragment";
        case ShaderStage::Compute:      return "Compute";
        case ShaderStage::Geometry:     return "Geometry";
        case ShaderStage::TessControl:  return "TessControl";
        case ShaderStage::TessEval:     return "TessEval";
        case ShaderStage::Mesh:         return "Mesh";
        case ShaderStage::Task:         return "Task";
        case ShaderStage::RayGen:       return "RayGen";
        case ShaderStage::RayMiss:      return "RayMiss";
        case ShaderStage::RayClosestHit:return "RayClosestHit";
        case ShaderStage::RayAnyHit:    return "RayAnyHit";
    }
    return "Unknown";
}

} // namespace nge::assets
