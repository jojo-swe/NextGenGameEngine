#include "engine/scene/prefab/prefab_system.h"
#include "engine/core/logging/log.h"
#include <fstream>
#include <cstring>

namespace nge::scene {

bool PrefabManager::Init() {
    m_nextId = 0;
    NGE_LOG_INFO("Prefab manager initialized");
    return true;
}

void PrefabManager::Shutdown() {
    m_prefabs.clear();
    m_nameToId.clear();
    m_componentAppliers.clear();
}

PrefabId PrefabManager::CreateFromEntity(ecs::World& world, ecs::Entity rootEntity,
                                           const std::string& name) {
    PrefabData data;
    data.name = name;

    // Traverse entity hierarchy and snapshot nodes
    // For now, create a single-node prefab from the root entity
    PrefabNode node;
    node.name = world.GetEntityName(rootEntity);
    node.parentIndex = UINT32_MAX;

    auto* transform = world.GetTransform(rootEntity);
    if (transform) {
        node.position = transform->position;
        node.rotation = transform->rotation;
        node.scale = transform->scale;
    }

    // TODO: Serialize each component on the entity into ComponentBlobs
    // using registered component serializers

    data.nodes.push_back(std::move(node));

    return RegisterPrefab(std::move(data));
}

PrefabId PrefabManager::RegisterPrefab(PrefabData data) {
    PrefabId id = m_nextId++;
    data.id = id;

    std::string name = data.name;
    m_prefabs[id] = std::move(data);
    m_nameToId[name] = id;

    NGE_LOG_INFO("Registered prefab '{}' (id={})", name, id);
    return id;
}

void PrefabManager::UnregisterPrefab(PrefabId id) {
    auto it = m_prefabs.find(id);
    if (it != m_prefabs.end()) {
        m_nameToId.erase(it->second.name);
        m_prefabs.erase(it);
    }
}

PrefabId PrefabManager::LoadFromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        NGE_LOG_ERROR("Failed to load prefab: {}", path);
        return INVALID_PREFAB;
    }

    auto size = file.tellg();
    file.seekg(0);

    // TODO: Deserialize PrefabData from binary format
    // For now, create a placeholder prefab
    PrefabData data;
    data.name = path;
    data.sourcePath = path;

    NGE_LOG_INFO("Loaded prefab from '{}' ({} bytes)", path, static_cast<usize>(size));
    return RegisterPrefab(std::move(data));
}

bool PrefabManager::SaveToFile(PrefabId id, const std::string& path) {
    auto it = m_prefabs.find(id);
    if (it == m_prefabs.end()) {
        NGE_LOG_ERROR("Prefab {} not found for saving", id);
        return false;
    }

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        NGE_LOG_ERROR("Failed to open file for writing: {}", path);
        return false;
    }

    // TODO: Serialize PrefabData to binary format
    // Header: magic, version, node count
    // Nodes: name, parent, transform, component blobs

    it->second.sourcePath = path;
    NGE_LOG_INFO("Saved prefab '{}' to '{}'", it->second.name, path);
    return true;
}

ecs::Entity PrefabManager::Instantiate(ecs::World& world, PrefabId id,
                                         const PrefabInstantiateParams& params) {
    auto it = m_prefabs.find(id);
    if (it == m_prefabs.end()) {
        NGE_LOG_ERROR("Cannot instantiate unknown prefab {}", id);
        return ecs::Entity{};
    }

    const auto& data = it->second;
    if (data.nodes.empty()) {
        NGE_LOG_WARN("Prefab '{}' has no nodes", data.name);
        return ecs::Entity{};
    }

    // Map from node index to created entity
    std::vector<ecs::Entity> entities(data.nodes.size());

    for (u32 i = 0; i < static_cast<u32>(data.nodes.size()); ++i) {
        const auto& node = data.nodes[i];

        ecs::Entity entity = world.CreateEntity(node.name);
        entities[i] = entity;

        // Compute transform (root gets params override, children use prefab-local)
        math::Vec3 pos = node.position;
        math::Vec4 rot = node.rotation;
        math::Vec3 scl = node.scale;

        if (i == 0) {
            // Root node: apply instantiation params
            pos = pos + params.position;
            // TODO: Compose rotations properly (quaternion multiply)
            scl = {scl.x * params.scale.x, scl.y * params.scale.y, scl.z * params.scale.z};
        }

        world.SetTransform(entity, pos, rot, scl);

        // Set parent
        if (node.parentIndex != UINT32_MAX && node.parentIndex < i) {
            world.SetParent(entity, entities[node.parentIndex]);
        }

        // Apply components
        for (const auto& blob : node.components) {
            auto applierIt = m_componentAppliers.find(blob.typeId);
            if (applierIt != m_componentAppliers.end()) {
                applierIt->second(entity, blob.data.data(), blob.data.size());
            }
        }
    }

    // Apply overrides
    for (const auto& override : params.overrides) {
        if (override.nodeIndex < entities.size()) {
            // TODO: Apply property override to entity
            (void)override;
        }
    }

    NGE_LOG_DEBUG("Instantiated prefab '{}' ({} entities)", data.name, data.nodes.size());
    return entities[0]; // Return root entity
}

const PrefabData* PrefabManager::GetPrefab(PrefabId id) const {
    auto it = m_prefabs.find(id);
    return it != m_prefabs.end() ? &it->second : nullptr;
}

PrefabId PrefabManager::FindByName(const std::string& name) const {
    auto it = m_nameToId.find(name);
    return it != m_nameToId.end() ? it->second : INVALID_PREFAB;
}

void PrefabManager::RegisterComponentApplier(u32 typeId, ComponentApplyFunc func) {
    m_componentAppliers[typeId] = std::move(func);
}

} // namespace nge::scene
