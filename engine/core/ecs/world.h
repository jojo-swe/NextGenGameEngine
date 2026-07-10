#pragma once

#include "engine/core/types.h"
#include "engine/core/ecs/entity.h"
#include "engine/core/ecs/component.h"
#include "engine/core/ecs/archetype.h"
#include "engine/core/containers/array.h"
#include "engine/core/assert.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>

namespace nge::ecs {

// ─── Entity Record ───────────────────────────────────────────────────────
// Maps entity → archetype + row within that archetype.
struct EntityRecord {
    Archetype* archetype = nullptr;
    usize      row       = 0;
};

// ─── Component Registry ─────────────────────────────────────────────────
// Global registry mapping ComponentId → ComponentInfo.
class ComponentRegistry {
public:
    template <typename T>
    void Register(const char* name) {
        ComponentId id = ComponentType<T>::Id();
        if (m_infos.find(id) == m_infos.end()) {
            m_infos[id] = MakeComponentInfo<T>(name);
        }
    }

    const ComponentInfo* GetInfo(ComponentId id) const {
        auto it = m_infos.find(id);
        return it != m_infos.end() ? &it->second : nullptr;
    }

    bool IsRegistered(ComponentId id) const {
        return m_infos.find(id) != m_infos.end();
    }

private:
    std::unordered_map<ComponentId, ComponentInfo> m_infos;
};

// ─── World ───────────────────────────────────────────────────────────────
// The central ECS container. Owns all entities, archetypes, and component data.
class World {
public:
    World() = default;
    ~World() = default;

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // ─── Component registration ───────────────────────────────────────
    template <typename T>
    void RegisterComponent(const char* name = nullptr) {
        const char* n = name ? name : typeid(T).name();
        m_registry.Register<T>(n);
    }

    // ─── Entity lifecycle ─────────────────────────────────────────────
    Entity CreateEntity() {
        Entity entity;
        if (!m_freeEntities.IsEmpty()) {
            u32 index = m_freeEntities.Back();
            m_freeEntities.PopBack();
            u32 gen = m_generations[index] + 1;
            m_generations[index] = gen;
            entity = Entity(index, gen);
        } else {
            u32 index = static_cast<u32>(m_generations.size());
            m_generations.push_back(1);
            entity = Entity(index, 1);
        }

        // Start with empty archetype
        Archetype* emptyArch = GetOrCreateArchetype(ArchetypeSignature{});
        usize row = emptyArch->AddEntity(entity);
        m_entityRecords[entity.id] = {emptyArch, row};

        return entity;
    }

    void DestroyEntity(Entity entity) {
        if (!IsAlive(entity)) return;

        auto it = m_entityRecords.find(entity.id);
        if (it == m_entityRecords.end()) return;

        EntityRecord& record = it->second;
        Entity movedEntity = record.archetype->RemoveEntity(record.row);

        // Update the moved entity's record
        if (movedEntity.IsValid()) {
            m_entityRecords[movedEntity.id].row = record.row;
        }

        m_entityRecords.erase(it);

        // Increment generation so IsAlive returns false for destroyed entity
        m_generations[entity.Index()]++;

        // Recycle entity index
        m_freeEntities.PushBack(entity.Index());
    }

    bool IsAlive(Entity entity) const {
        u32 idx = entity.Index();
        if (idx >= m_generations.size()) return false;
        return m_generations[idx] == entity.Generation();
    }

    // ─── Component operations ─────────────────────────────────────────

    template <typename T>
    T& AddComponent(Entity entity, const T& value = T{}) {
        NGE_ASSERT(IsAlive(entity));
        m_registry.Register<T>(typeid(T).name());

        auto it = m_entityRecords.find(entity.id);
        NGE_ASSERT(it != m_entityRecords.end());

        EntityRecord& record = it->second;
        Archetype* oldArch = record.archetype;

        // Build new signature with added component
        ArchetypeSignature newSig = oldArch->GetSignature();
        ComponentId compId = ComponentType<T>::Id();

        if (newSig.Contains(compId)) {
            // Already has this component, just set value
            T* comp = oldArch->GetComponent<T>(record.row);
            *comp = value;
            return *comp;
        }

        newSig.Add(compId);

        // Migrate entity to new archetype
        Archetype* newArch = GetOrCreateArchetype(newSig);
        usize newRow = MigrateEntity(entity, record, oldArch, newArch);

        // Set the new component value
        T* comp = newArch->GetComponent<T>(newRow);
        *comp = value;

        record.archetype = newArch;
        record.row = newRow;

        return *comp;
    }

    template <typename T>
    void RemoveComponent(Entity entity) {
        NGE_ASSERT(IsAlive(entity));

        auto it = m_entityRecords.find(entity.id);
        NGE_ASSERT(it != m_entityRecords.end());

        EntityRecord& record = it->second;
        Archetype* oldArch = record.archetype;

        ArchetypeSignature newSig = oldArch->GetSignature();
        ComponentId compId = ComponentType<T>::Id();

        if (!newSig.Contains(compId)) return; // Doesn't have it

        newSig.Remove(compId);

        Archetype* newArch = GetOrCreateArchetype(newSig);
        usize newRow = MigrateEntity(entity, record, oldArch, newArch);

        record.archetype = newArch;
        record.row = newRow;
    }

    template <typename T>
    T* GetComponent(Entity entity) {
        auto it = m_entityRecords.find(entity.id);
        if (it == m_entityRecords.end()) return nullptr;
        return it->second.archetype->GetComponent<T>(it->second.row);
    }

    template <typename T>
    const T* GetComponent(Entity entity) const {
        auto it = m_entityRecords.find(entity.id);
        if (it == m_entityRecords.end()) return nullptr;
        return it->second.archetype->GetComponent<T>(it->second.row);
    }

    template <typename T>
    bool HasComponent(Entity entity) const {
        auto it = m_entityRecords.find(entity.id);
        if (it == m_entityRecords.end()) return false;
        return it->second.archetype->HasComponent(ComponentType<T>::Id());
    }

    // ─── Query: iterate entities with specific components ─────────────
    // Usage: world.Each<Position, Velocity>([](Entity e, Position& p, Velocity& v) { ... });

    template <typename... Cs, typename Fn>
    void Each(Fn&& fn) {
        ArchetypeSignature querySig;
        (querySig.Add(ComponentType<Cs>::Id()), ...);

        for (auto& [hash, arch] : m_archetypes) {
            if (!arch->GetSignature().ContainsAll(querySig)) continue;

            usize count = arch->GetEntityCount();
            if (count == 0) continue;

            // Get typed column pointers
            auto columns = std::make_tuple(arch->GetColumnData<Cs>()...);

            for (usize i = 0; i < count; ++i) {
                Entity entity = arch->GetEntity(i);
                fn(entity, std::get<decltype(arch->GetColumnData<Cs>())>(columns)[i]...);
            }
        }
    }

    // ─── Entity metadata ──────────────────────────────────────────────
    std::string GetEntityName(Entity entity) const {
        auto it = m_entityNames.find(entity.id);
        return (it != m_entityNames.end()) ? it->second : "";
    }

    void SetEntityName(Entity entity, const std::string& name) {
        m_entityNames[entity.id] = name;
    }

    std::vector<Entity> GetAllEntities() const {
        std::vector<Entity> result;
        result.reserve(m_entityRecords.size());
        for (const auto& [id, record] : m_entityRecords) {
            u32 idx = static_cast<u32>(id & 0xFFFFFFFF);
            if (idx < m_generations.size()) {
                result.push_back(Entity(idx, m_generations[idx]));
            }
        }
        return result;
    }

    // ─── Stats ────────────────────────────────────────────────────────
    usize GetEntityCount() const { return m_entityRecords.size(); }
    usize GetArchetypeCount() const { return m_archetypes.size(); }

private:
    Archetype* GetOrCreateArchetype(const ArchetypeSignature& sig) {
        u64 hash = sig.Hash();

        auto it = m_archetypes.find(hash);
        if (it != m_archetypes.end()) return it->second.get();

        // Build component info list
        std::vector<ComponentInfo> infos;
        for (u32 i = 0; i < sig.count; ++i) {
            const ComponentInfo* info = m_registry.GetInfo(sig.ids[i]);
            if (info) infos.push_back(*info);
        }

        auto arch = std::make_unique<Archetype>(sig, infos);
        Archetype* ptr = arch.get();
        m_archetypes[hash] = std::move(arch);
        return ptr;
    }

    usize MigrateEntity(Entity entity, EntityRecord& record,
                        Archetype* oldArch, Archetype* newArch)
    {
        usize oldRow = record.row;
        usize newRow = newArch->AddEntity(entity);

        // Copy overlapping components
        const auto& oldSig = oldArch->GetSignature();
        const auto& newSig = newArch->GetSignature();

        for (u32 i = 0; i < oldSig.count; ++i) {
            ComponentId id = oldSig.ids[i];
            if (!newSig.Contains(id)) continue;

            i32 oldCol = oldArch->GetColumnIndex(id);
            i32 newCol = newArch->GetColumnIndex(id);
            if (oldCol < 0 || newCol < 0) continue;

            const ComponentInfo* info = m_registry.GetInfo(id);
            if (!info) continue;

            void* dst = newArch->GetComponentRaw(newRow, static_cast<u32>(newCol));
            void* src = oldArch->GetComponentRaw(oldRow, static_cast<u32>(oldCol));

            // Destruct default-constructed component in new archetype, then move from old
            info->destruct(dst);
            info->moveConstruct(dst, src);
        }

        // Remove from old archetype
        Entity movedEntity = oldArch->RemoveEntity(oldRow);
        if (movedEntity.IsValid()) {
            m_entityRecords[movedEntity.id].row = oldRow;
        }

        return newRow;
    }

    ComponentRegistry                                          m_registry;
    std::unordered_map<u64, std::unique_ptr<Archetype>>       m_archetypes;
    std::unordered_map<u64, EntityRecord>                      m_entityRecords;
    std::unordered_map<u64, std::string>                       m_entityNames;
    std::vector<u32>                                           m_generations;
    Array<u32>                                                 m_freeEntities;
};

} // namespace nge::ecs
