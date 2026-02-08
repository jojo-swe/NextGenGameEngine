#pragma once

#include "engine/core/types.h"
#include <random>
#include <atomic>

namespace nge {

struct UUID {
    u64 low  = 0;
    u64 high = 0;

    constexpr UUID() = default;
    constexpr UUID(u64 lo, u64 hi) : low(lo), high(hi) {}

    static UUID Generate() {
        thread_local std::mt19937_64 rng{std::random_device{}()};
        UUID id;
        id.low  = rng();
        id.high = rng();
        // Set version 4 (random) bits
        id.high = (id.high & ~(0xFULL << 12)) | (0x4ULL << 12);
        // Set variant bits (10xx)
        id.low = (id.low & ~(0x3ULL << 62)) | (0x2ULL << 62);
        return id;
    }

    constexpr bool operator==(const UUID& o) const { return low == o.low && high == o.high; }
    constexpr bool operator!=(const UUID& o) const { return !(*this == o); }
    constexpr bool operator<(const UUID& o) const {
        return high < o.high || (high == o.high && low < o.low);
    }

    constexpr bool IsValid() const { return low != 0 || high != 0; }
    constexpr explicit operator bool() const { return IsValid(); }
};

struct UUIDHasher {
    u64 operator()(const UUID& id) const {
        return id.low ^ (id.high * 0x9E3779B97F4A7C15ULL);
    }
};

} // namespace nge
