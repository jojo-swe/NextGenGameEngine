#pragma once

#include "engine/core/types.h"

namespace nge::ecs {

// ─── Entity ID ───────────────────────────────────────────────────────────
// 64-bit: upper 32 bits = generation, lower 32 bits = index.
// Generation prevents use-after-free when entity indices are recycled.
struct Entity {
    u64 id = 0;

    constexpr Entity() = default;
    constexpr explicit Entity(u64 rawId) : id(rawId) {}
    constexpr Entity(u32 index, u32 generation)
        : id(static_cast<u64>(generation) << 32 | static_cast<u64>(index)) {}

    constexpr u32 Index() const { return static_cast<u32>(id & 0xFFFFFFFF); }
    constexpr u32 Generation() const { return static_cast<u32>(id >> 32); }
    constexpr bool IsValid() const { return id != 0; }

    constexpr bool operator==(Entity other) const { return id == other.id; }
    constexpr bool operator!=(Entity other) const { return id != other.id; }
    constexpr bool operator<(Entity other) const { return id < other.id; }

    static constexpr Entity Invalid() { return Entity(0); }
};

struct EntityHasher {
    u64 operator()(Entity e) const { return e.id * 0x9E3779B97F4A7C15ULL; }
};

} // namespace nge::ecs
