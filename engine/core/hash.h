#pragma once

#include "engine/core/types.h"
#include <cstring>

namespace nge {

// ─── FNV-1a Hash ─────────────────────────────────────────────────────────
namespace fnv1a {

inline constexpr u64 OFFSET_BASIS = 14695981039346656037ULL;
inline constexpr u64 PRIME        = 1099511628211ULL;

constexpr u64 Hash(const char* data, usize length, u64 hash = OFFSET_BASIS) {
    for (usize i = 0; i < length; ++i) {
        hash ^= static_cast<u64>(static_cast<u8>(data[i]));
        hash *= PRIME;
    }
    return hash;
}

constexpr u64 HashString(const char* str) {
    u64 hash = OFFSET_BASIS;
    while (*str) {
        hash ^= static_cast<u64>(static_cast<u8>(*str));
        hash *= PRIME;
        ++str;
    }
    return hash;
}

} // namespace fnv1a

// ─── xxHash64 (simplified inline version) ─────────────────────────────────
namespace xxhash {

inline constexpr u64 PRIME1 = 11400714785074694791ULL;
inline constexpr u64 PRIME2 = 14029467366897019727ULL;
inline constexpr u64 PRIME3 =  1609587929392839161ULL;
inline constexpr u64 PRIME4 =  9650029242287828579ULL;
inline constexpr u64 PRIME5 =  2870177450012600261ULL;

inline u64 RotL(u64 x, int r) { return (x << r) | (x >> (64 - r)); }

inline u64 Round(u64 acc, u64 input) {
    acc += input * PRIME2;
    acc  = RotL(acc, 31);
    acc *= PRIME1;
    return acc;
}

inline u64 MergeRound(u64 acc, u64 val) {
    val  = Round(0, val);
    acc ^= val;
    acc  = acc * PRIME1 + PRIME4;
    return acc;
}

inline u64 Hash(const void* data, usize len, u64 seed = 0) {
    const u8* p    = static_cast<const u8*>(data);
    const u8* end  = p + len;
    u64 h64;

    if (len >= 32) {
        const u8* limit = end - 32;
        u64 v1 = seed + PRIME1 + PRIME2;
        u64 v2 = seed + PRIME2;
        u64 v3 = seed;
        u64 v4 = seed - PRIME1;

        do {
            u64 k1; std::memcpy(&k1, p,      8);
            u64 k2; std::memcpy(&k2, p + 8,  8);
            u64 k3; std::memcpy(&k3, p + 16, 8);
            u64 k4; std::memcpy(&k4, p + 24, 8);
            v1 = Round(v1, k1);
            v2 = Round(v2, k2);
            v3 = Round(v3, k3);
            v4 = Round(v4, k4);
            p += 32;
        } while (p <= limit);

        h64 = RotL(v1, 1) + RotL(v2, 7) + RotL(v3, 12) + RotL(v4, 18);
        h64 = MergeRound(h64, v1);
        h64 = MergeRound(h64, v2);
        h64 = MergeRound(h64, v3);
        h64 = MergeRound(h64, v4);
    } else {
        h64 = seed + PRIME5;
    }

    h64 += static_cast<u64>(len);

    while (p + 8 <= end) {
        u64 k1; std::memcpy(&k1, p, 8);
        k1 *= PRIME2;
        k1  = RotL(k1, 31);
        k1 *= PRIME1;
        h64 ^= k1;
        h64  = RotL(h64, 27) * PRIME1 + PRIME4;
        p += 8;
    }

    while (p + 4 <= end) {
        u32 k1; std::memcpy(&k1, p, 4);
        h64 ^= static_cast<u64>(k1) * PRIME1;
        h64  = RotL(h64, 23) * PRIME2 + PRIME3;
        p += 4;
    }

    while (p < end) {
        h64 ^= static_cast<u64>(*p) * PRIME5;
        h64  = RotL(h64, 11) * PRIME1;
        p++;
    }

    h64 ^= h64 >> 33;
    h64 *= PRIME2;
    h64 ^= h64 >> 29;
    h64 *= PRIME3;
    h64 ^= h64 >> 32;

    return h64;
}

} // namespace xxhash

// ─── Convenience ─────────────────────────────────────────────────────────
inline u64 HashBytes(const void* data, usize len) { return xxhash::Hash(data, len); }

constexpr u64 HashString(const char* str) { return fnv1a::HashString(str); }

// Compile-time string hash via user-defined literal
consteval u64 operator""_hash(const char* str, usize len) {
    return fnv1a::Hash(str, len);
}

} // namespace nge
