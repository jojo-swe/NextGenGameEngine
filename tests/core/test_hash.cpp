#include <gtest/gtest.h>
#include "engine/core/hash.h"

using namespace nge;

TEST(Hash, FNV1a_Deterministic) {
    u64 h1 = fnv1a::HashString("hello");
    u64 h2 = fnv1a::HashString("hello");
    EXPECT_EQ(h1, h2);
}

TEST(Hash, FNV1a_Different) {
    u64 h1 = fnv1a::HashString("hello");
    u64 h2 = fnv1a::HashString("world");
    EXPECT_NE(h1, h2);
}

TEST(Hash, XXHash_Deterministic) {
    const char* data = "test data for hashing";
    u64 h1 = xxhash::Hash(data, 21);
    u64 h2 = xxhash::Hash(data, 21);
    EXPECT_EQ(h1, h2);
}

TEST(Hash, XXHash_DifferentSeeds) {
    const char* data = "test";
    u64 h1 = xxhash::Hash(data, 4, 0);
    u64 h2 = xxhash::Hash(data, 4, 42);
    EXPECT_NE(h1, h2);
}

TEST(Hash, CompileTimeLiteral) {
    constexpr u64 h = "test"_hash;
    EXPECT_NE(h, 0u);

    constexpr u64 h2 = "test"_hash;
    EXPECT_EQ(h, h2);

    constexpr u64 h3 = "different"_hash;
    EXPECT_NE(h, h3);
}
