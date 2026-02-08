#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <variant>

namespace nge::rhi {

// ─── GPU Specialization Constant Manager ─────────────────────────────────
// Manages Vulkan specialization constants for shader permutations without
// recompiling shaders. Spec constants are baked into SPIR-V at pipeline
// creation time, allowing the driver to optimize branches away.
//
// Use cases:
//   - Toggle features (normal mapping, shadows, etc.)
//   - Set max light count, sample count
//   - Configure algorithm parameters at compile time

using SpecConstValue = std::variant<bool, i32, u32, f32>;

struct SpecConstantEntry {
    u32            constantId;
    std::string    name;
    SpecConstValue defaultValue;
};

struct SpecConstantMap {
    std::vector<SpecConstantEntry> entries;
    std::string debugName;
};

struct SpecConstantData {
    std::vector<u8> data;            // Raw bytes for VkSpecializationInfo
    std::vector<u32> mapEntryIds;    // VkSpecializationMapEntry constant IDs
    std::vector<u32> mapEntryOffsets;
    std::vector<u32> mapEntrySizes;
};

class SpecConstantManager {
public:
    // Register a specialization constant map for a shader
    u32 RegisterMap(const SpecConstantMap& map);

    // Set a boolean spec constant
    void SetBool(u32 mapId, u32 constantId, bool value);
    void SetBool(u32 mapId, const std::string& name, bool value);

    // Set an integer spec constant
    void SetInt(u32 mapId, u32 constantId, i32 value);
    void SetInt(u32 mapId, const std::string& name, i32 value);

    // Set an unsigned integer spec constant
    void SetUint(u32 mapId, u32 constantId, u32 value);
    void SetUint(u32 mapId, const std::string& name, u32 value);

    // Set a float spec constant
    void SetFloat(u32 mapId, u32 constantId, f32 value);
    void SetFloat(u32 mapId, const std::string& name, f32 value);

    // Reset all constants to defaults
    void ResetToDefaults(u32 mapId);

    // Build VkSpecializationInfo data for pipeline creation
    SpecConstantData BuildData(u32 mapId) const;

    // Get current value
    SpecConstValue GetValue(u32 mapId, u32 constantId) const;

    // Get registered map count
    u32 GetMapCount() const { return static_cast<u32>(m_maps.size()); }

    // Common presets
    static SpecConstantMap PBRMaterial();
    static SpecConstantMap PostProcess();

private:
    struct LiveMap {
        SpecConstantMap definition;
        std::unordered_map<u32, SpecConstValue> currentValues; // constantId -> value
    };

    u32 FindConstantId(const LiveMap& map, const std::string& name) const;

    std::vector<LiveMap> m_maps;
};

} // namespace nge::rhi
