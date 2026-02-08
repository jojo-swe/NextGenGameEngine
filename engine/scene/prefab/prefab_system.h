#pragma once

#include "engine/core/types.h"
#include "engine/core/ecs/entity.h"
#include "engine/core/ecs/world.h"
#include "engine/core/math/math_types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

namespace nge::scene {

// ─── Prefab System ───────────────────────────────────────────────────────
// Templates for entity hierarchies that can be instantiated at runtime.
// Prefabs store a snapshot of entities + components that can be stamped
// into the world multiple times with overrides.

using PrefabId = u32;
inline constexpr PrefabId INVALID_PREFAB = UINT32_MAX;

// ─── Prefab Node ─────────────────────────────────────────────────────────
// One node per entity in the prefab hierarchy.

struct PrefabNode {
    std::string name;
    u32         parentIndex = UINT32_MAX; // Index into PrefabData::nodes (UINT32_MAX = root)

    // Transform
    math::Vec3  position = {0, 0, 0};
    math::Vec4  rotation = {0, 0, 0, 1}; // Quaternion
    math::Vec3  scale    = {1, 1, 1};

    // Serialized component data (type ID → blob)
    struct ComponentBlob {
        u32              typeId;
        std::vector<u8>  data;
    };
    std::vector<ComponentBlob> components;
};

// ─── Prefab Data ─────────────────────────────────────────────────────────

struct PrefabData {
    PrefabId    id = INVALID_PREFAB;
    std::string name;
    std::string sourcePath; // File path if loaded from disk

    std::vector<PrefabNode> nodes;
};

// ─── Instantiation Override ──────────────────────────────────────────────
// Allows overriding specific properties when instantiating a prefab.

struct PrefabOverride {
    u32         nodeIndex;
    std::string propertyName;
    std::vector<u8> value;
};

struct PrefabInstantiateParams {
    math::Vec3  position = {0, 0, 0};
    math::Vec4  rotation = {0, 0, 0, 1};
    math::Vec3  scale    = {1, 1, 1};
    std::vector<PrefabOverride> overrides;
};

// ─── Prefab Manager ──────────────────────────────────────────────────────

class PrefabManager {
public:
    bool Init();
    void Shutdown();

    // Create prefab from existing entities in the world
    PrefabId CreateFromEntity(ecs::World& world, ecs::Entity rootEntity, const std::string& name);

    // Create prefab from data
    PrefabId RegisterPrefab(PrefabData data);

    // Remove prefab
    void UnregisterPrefab(PrefabId id);

    // Load prefab from file
    PrefabId LoadFromFile(const std::string& path);

    // Save prefab to file
    bool SaveToFile(PrefabId id, const std::string& path);

    // Instantiate prefab into the world
    // Returns the root entity of the instantiated hierarchy
    ecs::Entity Instantiate(ecs::World& world, PrefabId id,
                             const PrefabInstantiateParams& params = {});

    // Query
    const PrefabData* GetPrefab(PrefabId id) const;
    PrefabId FindByName(const std::string& name) const;
    u32 GetPrefabCount() const { return static_cast<u32>(m_prefabs.size()); }

    // Component deserializer registration
    using ComponentApplyFunc = std::function<void(ecs::Entity, const u8*, usize)>;
    void RegisterComponentApplier(u32 typeId, ComponentApplyFunc func);

private:
    PrefabId m_nextId = 0;
    std::unordered_map<PrefabId, PrefabData> m_prefabs;
    std::unordered_map<std::string, PrefabId> m_nameToId;
    std::unordered_map<u32, ComponentApplyFunc> m_componentAppliers;
};

} // namespace nge::scene
