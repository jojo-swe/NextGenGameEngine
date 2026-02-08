#pragma once

#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace nge {

// ─── Fixed-width integer aliases ──────────────────────────────────────────
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

using usize = std::size_t;
using isize = std::ptrdiff_t;

using byte = std::uint8_t;

// ─── Compile-time type ID ─────────────────────────────────────────────────
namespace detail {
    inline u64 g_nextTypeId = 0;
}

template <typename T>
struct TypeId {
    static u64 Value() {
        static const u64 id = detail::g_nextTypeId++;
        return id;
    }
};

// ─── Utility type traits ──────────────────────────────────────────────────
template <typename T>
constexpr bool IsPOD = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

// ─── Constants ────────────────────────────────────────────────────────────
inline constexpr usize CACHE_LINE_SIZE = 64;
inline constexpr usize PAGE_SIZE       = 4096;

// ─── Alignment helpers ───────────────────────────────────────────────────
constexpr usize AlignUp(usize value, usize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

constexpr usize AlignDown(usize value, usize alignment) {
    return value & ~(alignment - 1);
}

constexpr bool IsAligned(usize value, usize alignment) {
    return (value & (alignment - 1)) == 0;
}

// ─── Bit manipulation ────────────────────────────────────────────────────
constexpr bool IsPowerOfTwo(u64 value) {
    return value != 0 && (value & (value - 1)) == 0;
}

constexpr u32 NextPowerOfTwo(u32 v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

constexpr u32 Log2(u32 v) {
    u32 r = 0;
    while (v >>= 1) r++;
    return r;
}

} // namespace nge
