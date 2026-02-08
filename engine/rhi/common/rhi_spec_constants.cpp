#include "engine/rhi/common/rhi_spec_constants.h"
#include "engine/core/logging/log.h"
#include <cstring>

namespace nge::rhi {

u32 SpecConstantManager::RegisterMap(const SpecConstantMap& map) {
    LiveMap live;
    live.definition = map;

    // Initialize current values to defaults
    for (const auto& entry : map.entries) {
        live.currentValues[entry.constantId] = entry.defaultValue;
    }

    u32 id = static_cast<u32>(m_maps.size());
    m_maps.push_back(std::move(live));
    return id;
}

void SpecConstantManager::SetBool(u32 mapId, u32 constantId, bool value) {
    if (mapId >= m_maps.size()) return;
    m_maps[mapId].currentValues[constantId] = value;
}

void SpecConstantManager::SetBool(u32 mapId, const std::string& name, bool value) {
    if (mapId >= m_maps.size()) return;
    u32 id = FindConstantId(m_maps[mapId], name);
    if (id != UINT32_MAX) m_maps[mapId].currentValues[id] = value;
}

void SpecConstantManager::SetInt(u32 mapId, u32 constantId, i32 value) {
    if (mapId >= m_maps.size()) return;
    m_maps[mapId].currentValues[constantId] = value;
}

void SpecConstantManager::SetInt(u32 mapId, const std::string& name, i32 value) {
    if (mapId >= m_maps.size()) return;
    u32 id = FindConstantId(m_maps[mapId], name);
    if (id != UINT32_MAX) m_maps[mapId].currentValues[id] = value;
}

void SpecConstantManager::SetUint(u32 mapId, u32 constantId, u32 value) {
    if (mapId >= m_maps.size()) return;
    m_maps[mapId].currentValues[constantId] = value;
}

void SpecConstantManager::SetUint(u32 mapId, const std::string& name, u32 value) {
    if (mapId >= m_maps.size()) return;
    u32 id = FindConstantId(m_maps[mapId], name);
    if (id != UINT32_MAX) m_maps[mapId].currentValues[id] = value;
}

void SpecConstantManager::SetFloat(u32 mapId, u32 constantId, f32 value) {
    if (mapId >= m_maps.size()) return;
    m_maps[mapId].currentValues[constantId] = value;
}

void SpecConstantManager::SetFloat(u32 mapId, const std::string& name, f32 value) {
    if (mapId >= m_maps.size()) return;
    u32 id = FindConstantId(m_maps[mapId], name);
    if (id != UINT32_MAX) m_maps[mapId].currentValues[id] = value;
}

void SpecConstantManager::ResetToDefaults(u32 mapId) {
    if (mapId >= m_maps.size()) return;
    auto& live = m_maps[mapId];
    for (const auto& entry : live.definition.entries) {
        live.currentValues[entry.constantId] = entry.defaultValue;
    }
}

SpecConstantData SpecConstantManager::BuildData(u32 mapId) const {
    SpecConstantData result;
    if (mapId >= m_maps.size()) return result;

    const auto& live = m_maps[mapId];
    u32 offset = 0;

    for (const auto& entry : live.definition.entries) {
        auto it = live.currentValues.find(entry.constantId);
        if (it == live.currentValues.end()) continue;

        result.mapEntryIds.push_back(entry.constantId);
        result.mapEntryOffsets.push_back(offset);

        std::visit([&](auto&& val) {
            using T = std::decay_t<decltype(val)>;
            u32 size = sizeof(T);
            result.mapEntrySizes.push_back(size);

            size_t oldSize = result.data.size();
            result.data.resize(oldSize + size);
            std::memcpy(result.data.data() + oldSize, &val, size);
            offset += size;
        }, it->second);
    }

    return result;
}

SpecConstValue SpecConstantManager::GetValue(u32 mapId, u32 constantId) const {
    if (mapId >= m_maps.size()) return false;
    const auto& live = m_maps[mapId];
    auto it = live.currentValues.find(constantId);
    if (it != live.currentValues.end()) return it->second;

    // Return default
    for (const auto& entry : live.definition.entries) {
        if (entry.constantId == constantId) return entry.defaultValue;
    }
    return false;
}

SpecConstantMap SpecConstantManager::PBRMaterial() {
    SpecConstantMap map;
    map.debugName = "PBRMaterial";
    map.entries = {
        {0, "USE_NORMAL_MAP",     true},
        {1, "USE_EMISSIVE",       false},
        {2, "USE_AO_MAP",         true},
        {3, "USE_METALLIC_MAP",   true},
        {4, "USE_ROUGHNESS_MAP",  true},
        {5, "USE_ALPHA_CUTOUT",   false},
        {6, "MAX_LIGHTS",         u32(256)},
        {7, "SHADOW_CASCADE_COUNT", u32(4)},
    };
    return map;
}

SpecConstantMap SpecConstantManager::PostProcess() {
    SpecConstantMap map;
    map.debugName = "PostProcess";
    map.entries = {
        {0, "ENABLE_BLOOM",       true},
        {1, "ENABLE_VIGNETTE",    true},
        {2, "ENABLE_CHROMATIC_AB", false},
        {3, "ENABLE_FILM_GRAIN",  false},
        {4, "TONEMAP_OPERATOR",   u32(0)},    // 0=ACES, 1=Reinhard, 2=Uncharted2
        {5, "BLOOM_MIP_COUNT",    u32(5)},
        {6, "EXPOSURE_MODE",      u32(0)},     // 0=Auto, 1=Manual
    };
    return map;
}

u32 SpecConstantManager::FindConstantId(const LiveMap& map, const std::string& name) const {
    for (const auto& entry : map.definition.entries) {
        if (entry.name == name) return entry.constantId;
    }
    NGE_LOG_WARN("Spec constant '{}' not found in map '{}'", name, map.definition.debugName);
    return UINT32_MAX;
}

} // namespace nge::rhi
