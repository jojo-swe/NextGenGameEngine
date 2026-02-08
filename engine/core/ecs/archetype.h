#pragma once

#include "engine/core/types.h"
#include "engine/core/ecs/entity.h"
#include "engine/core/ecs/component.h"
#include "engine/core/containers/array.h"
#include "engine/core/assert.h"
#include <cstring>
#include <vector>
#include <unordered_map>

namespace nge::ecs {

// ─── Archetype ───────────────────────────────────────────────────────────
// Stores all entities that share the same component set.
// Each component type is stored in a contiguous column (SoA layout).
// This gives excellent cache performance during iteration.
class Archetype {
public:
    static constexpr usize CHUNK_SIZE = 16384; // 16KB per chunk

    Archetype(const ArchetypeSignature& sig, const std::vector<ComponentInfo>& infos)
        : m_signature(sig)
        , m_componentInfos(infos)
    {
        // Build column layout
        m_columnCount = static_cast<u32>(infos.size());
        m_columnOffsets.resize(m_columnCount);
        m_columnSizes.resize(m_columnCount);

        // Build a map from ComponentId → column index
        for (u32 i = 0; i < m_columnCount; ++i) {
            m_componentToColumn[infos[i].id] = i;
            m_columnSizes[i] = infos[i].size;
        }

        // Calculate row size (sum of all component sizes, aligned)
        m_rowSize = 0;
        for (u32 i = 0; i < m_columnCount; ++i) {
            m_columnOffsets[i] = m_rowSize;
            m_rowSize += AlignUp(infos[i].size, infos[i].alignment);
        }

        if (m_rowSize == 0) m_rowSize = 1; // At least 1 byte per entity
    }

    ~Archetype() {
        // Destruct all live components
        for (usize i = 0; i < m_entityCount; ++i) {
            for (u32 col = 0; col < m_columnCount; ++col) {
                void* ptr = GetComponentRaw(i, col);
                m_componentInfos[col].destruct(ptr);
            }
        }
    }

    // Add an entity, default-constructing all components. Returns row index.
    usize AddEntity(Entity entity) {
        usize row = m_entityCount;
        EnsureCapacity(row + 1);

        // Default-construct each component
        for (u32 col = 0; col < m_columnCount; ++col) {
            void* ptr = GetComponentRaw(row, col);
            m_componentInfos[col].construct(ptr);
        }

        m_entities.PushBack(entity);
        m_entityCount++;
        return row;
    }

    // Remove entity at row by swap-removing with the last entity.
    // Returns the entity that was moved into the vacated slot (or Invalid if it was the last).
    Entity RemoveEntity(usize row) {
        NGE_ASSERT(row < m_entityCount);

        usize lastRow = m_entityCount - 1;
        Entity movedEntity = Entity::Invalid();

        if (row != lastRow) {
            // Move last entity's components into this row
            for (u32 col = 0; col < m_columnCount; ++col) {
                void* dst = GetComponentRaw(row, col);
                void* src = GetComponentRaw(lastRow, col);
                m_componentInfos[col].destruct(dst);
                m_componentInfos[col].moveConstruct(dst, src);
            }
            movedEntity = m_entities[lastRow];
            m_entities[row] = movedEntity;
        }

        // Destruct last row's components (they were moved or are the removed entity)
        if (row == lastRow) {
            for (u32 col = 0; col < m_columnCount; ++col) {
                void* ptr = GetComponentRaw(lastRow, col);
                m_componentInfos[col].destruct(ptr);
            }
        }

        m_entities.PopBack();
        m_entityCount--;
        return movedEntity;
    }

    // Get typed component pointer for an entity at a given row
    template <typename T>
    T* GetComponent(usize row) {
        auto it = m_componentToColumn.find(ComponentType<T>::Id());
        if (it == m_componentToColumn.end()) return nullptr;
        return static_cast<T*>(GetComponentRaw(row, it->second));
    }

    template <typename T>
    const T* GetComponent(usize row) const {
        auto it = m_componentToColumn.find(ComponentType<T>::Id());
        if (it == m_componentToColumn.end()) return nullptr;
        return static_cast<const T*>(GetComponentRaw(row, it->second));
    }

    // Get raw component pointer by column index
    void* GetComponentRaw(usize row, u32 column) {
        NGE_ASSERT(row < m_entityCount || row < m_capacity);
        NGE_ASSERT(column < m_columnCount);
        return m_columns[column].data() + row * m_columnSizes[column];
    }

    const void* GetComponentRaw(usize row, u32 column) const {
        NGE_ASSERT(row < m_entityCount || row < m_capacity);
        NGE_ASSERT(column < m_columnCount);
        return m_columns[column].data() + row * m_columnSizes[column];
    }

    // Get raw column data pointer for a component type
    template <typename T>
    T* GetColumnData() {
        auto it = m_componentToColumn.find(ComponentType<T>::Id());
        if (it == m_componentToColumn.end()) return nullptr;
        return reinterpret_cast<T*>(m_columns[it->second].data());
    }

    // Column index for a component type (-1 if not present)
    i32 GetColumnIndex(ComponentId id) const {
        auto it = m_componentToColumn.find(id);
        return it != m_componentToColumn.end() ? static_cast<i32>(it->second) : -1;
    }

    bool HasComponent(ComponentId id) const {
        return m_componentToColumn.find(id) != m_componentToColumn.end();
    }

    // Accessors
    const ArchetypeSignature& GetSignature() const { return m_signature; }
    usize GetEntityCount() const { return m_entityCount; }
    Entity GetEntity(usize row) const { return m_entities[row]; }
    u32 GetColumnCount() const { return m_columnCount; }
    const std::vector<ComponentInfo>& GetComponentInfos() const { return m_componentInfos; }

private:
    void EnsureCapacity(usize needed) {
        if (needed <= m_capacity) return;

        usize newCapacity = m_capacity == 0 ? 64 : m_capacity * 2;
        while (newCapacity < needed) newCapacity *= 2;

        if (m_columns.empty()) {
            m_columns.resize(m_columnCount);
        }

        for (u32 col = 0; col < m_columnCount; ++col) {
            std::vector<byte> newData(newCapacity * m_columnSizes[col]);

            if (m_entityCount > 0 && !m_columns[col].empty()) {
                // Move existing data
                std::memcpy(newData.data(), m_columns[col].data(),
                            m_entityCount * m_columnSizes[col]);
            }

            m_columns[col] = std::move(newData);
        }

        m_capacity = newCapacity;
    }

    ArchetypeSignature                           m_signature;
    std::vector<ComponentInfo>                   m_componentInfos;
    std::unordered_map<ComponentId, u32>         m_componentToColumn;
    std::vector<std::vector<byte>>               m_columns;
    Array<Entity>                                m_entities;

    u32   m_columnCount = 0;
    usize m_entityCount = 0;
    usize m_capacity    = 0;
    usize m_rowSize     = 0;
    std::vector<usize> m_columnOffsets;
    std::vector<usize> m_columnSizes;
};

} // namespace nge::ecs
