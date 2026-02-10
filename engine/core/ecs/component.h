#pragma once

#include "engine/core/types.h"
#include <atomic>
#include <typeinfo>
#include <cstring>

namespace nge::ecs {

// ─── Component Type ID ───────────────────────────────────────────────────
using ComponentId = u64;

namespace detail {
    inline std::atomic<ComponentId> g_nextComponentId{1};
}

template <typename T>
struct ComponentType {
    static ComponentId Id() {
        static const ComponentId id = detail::g_nextComponentId.fetch_add(1, std::memory_order_relaxed);
        return id;
    }
};

// ─── Component Info ──────────────────────────────────────────────────────
// Runtime type metadata for a component.
struct ComponentInfo {
    ComponentId id       = 0;
    usize       size     = 0;
    usize       alignment = 0;
    const char* name     = nullptr;

    // Function pointers for type-erased operations
    void (*construct)(void* dst)                       = nullptr;
    void (*destruct)(void* ptr)                        = nullptr;
    void (*moveConstruct)(void* dst, void* src)        = nullptr;
    void (*copyConstruct)(void* dst, const void* src)  = nullptr;
};

template <typename T>
ComponentInfo MakeComponentInfo(const char* name) {
    ComponentInfo info;
    info.id        = ComponentType<T>::Id();
    info.size      = sizeof(T);
    info.alignment = alignof(T);
    info.name      = name;

    info.construct = [](void* dst) {
        new (dst) T{};
    };
    info.destruct = [](void* ptr) {
        static_cast<T*>(ptr)->~T();
    };
    info.moveConstruct = [](void* dst, void* src) {
        new (dst) T(static_cast<T&&>(*static_cast<T*>(src)));
    };
    info.copyConstruct = [](void* dst, const void* src) {
        new (dst) T(*static_cast<const T*>(src));
    };

    return info;
}

// ─── Archetype Signature ─────────────────────────────────────────────────
// A sorted list of ComponentIds that uniquely identifies an archetype.
// Entities with the same set of components share an archetype.
static constexpr usize MAX_COMPONENTS_PER_ARCHETYPE = 64;

struct ArchetypeSignature {
    ComponentId ids[MAX_COMPONENTS_PER_ARCHETYPE] = {};
    u32 count = 0;

    void Add(ComponentId id) {
        // Insert sorted
        u32 insertAt = count;
        for (u32 i = 0; i < count; ++i) {
            if (ids[i] == id) return; // Already present
            if (ids[i] > id) { insertAt = i; break; }
        }
        // Shift right
        for (u32 i = count; i > insertAt; --i) {
            ids[i] = ids[i - 1];
        }
        ids[insertAt] = id;
        count++;
    }

    void Remove(ComponentId id) {
        for (u32 i = 0; i < count; ++i) {
            if (ids[i] == id) {
                for (u32 j = i; j < count - 1; ++j) {
                    ids[j] = ids[j + 1];
                }
                count--;
                return;
            }
        }
    }

    bool Contains(ComponentId id) const {
        for (u32 i = 0; i < count; ++i) {
            if (ids[i] == id) return true;
            if (ids[i] > id) return false; // Sorted, no need to continue
        }
        return false;
    }

    bool ContainsAll(const ArchetypeSignature& other) const {
        u32 j = 0;
        for (u32 i = 0; i < other.count && j < count; ) {
            if (ids[j] == other.ids[i]) { ++i; ++j; }
            else if (ids[j] < other.ids[i]) { ++j; }
            else return false;
        }
        return true;
    }

    bool operator==(const ArchetypeSignature& other) const {
        if (count != other.count) return false;
        return std::memcmp(ids, other.ids, count * sizeof(ComponentId)) == 0;
    }

    u64 Hash() const {
        u64 h = 0;
        for (u32 i = 0; i < count; ++i) {
            h ^= ids[i] * 0x9E3779B97F4A7C15ULL;
            h = (h << 7) | (h >> 57);
        }
        return h;
    }
};

} // namespace nge::ecs
