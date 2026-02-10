#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_permutation_key_builder.h"

using namespace nge::rhi;

TEST(PermutationKeyBuilder, InitAndShutdown) {
    PermutationKeyBuilder builder;
    EXPECT_TRUE(builder.Init());
    EXPECT_EQ(builder.GetFeatureCount(), 0u);
    builder.Shutdown();
}

TEST(PermutationKeyBuilder, RegisterFeature) {
    PermutationKeyBuilder builder;
    builder.Init();

    EXPECT_TRUE(builder.RegisterFeature("HAS_NORMAL_MAP"));
    EXPECT_TRUE(builder.RegisterFeature("USE_SKINNING"));
    EXPECT_EQ(builder.GetFeatureCount(), 2u);

    const auto* f = builder.GetFeature("HAS_NORMAL_MAP");
    EXPECT_NE(f, nullptr);
    EXPECT_EQ(f->name, "HAS_NORMAL_MAP");
    EXPECT_EQ(f->bitIndex, 0u);

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, RegisterDuplicate) {
    PermutationKeyBuilder builder;
    builder.Init();

    EXPECT_TRUE(builder.RegisterFeature("HAS_NORMAL_MAP"));
    EXPECT_FALSE(builder.RegisterFeature("HAS_NORMAL_MAP")); // Duplicate

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, BuildKeyFromFeatures) {
    PermutationKeyBuilder builder;
    builder.Init();

    builder.RegisterFeature("A"); // bit 0
    builder.RegisterFeature("B"); // bit 1
    builder.RegisterFeature("C"); // bit 2

    u64 key = builder.BuildKey({"A", "C"});
    EXPECT_EQ(key, 0b101u); // bits 0 and 2

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, BuildKeyEmpty) {
    PermutationKeyBuilder builder;
    builder.Init();

    builder.RegisterFeature("A");
    builder.RegisterFeature("B");

    u64 key = builder.BuildKey({});
    EXPECT_EQ(key, 0u);

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, BuildKeyUnknownFeature) {
    PermutationKeyBuilder builder;
    builder.Init();

    builder.RegisterFeature("A");

    u64 key = builder.BuildKey({"A", "UNKNOWN"});
    EXPECT_EQ(key, 1u); // Only A set

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, BuildDefaultKey) {
    PermutationKeyBuilder builder;
    builder.Init();

    builder.RegisterFeature("A", true);  // Default on
    builder.RegisterFeature("B", false); // Default off
    builder.RegisterFeature("C", true);  // Default on

    u64 key = builder.BuildDefaultKey();
    EXPECT_EQ(key, 0b101u); // A and C

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, SetFeature) {
    PermutationKeyBuilder builder;
    builder.Init();

    builder.RegisterFeature("A");
    builder.RegisterFeature("B");

    u64 key = 0;
    key = builder.SetFeature(key, "B");
    EXPECT_EQ(key, 0b10u);

    key = builder.SetFeature(key, "A");
    EXPECT_EQ(key, 0b11u);

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, ClearFeature) {
    PermutationKeyBuilder builder;
    builder.Init();

    builder.RegisterFeature("A");
    builder.RegisterFeature("B");

    u64 key = 0b11u; // Both set
    key = builder.ClearFeature(key, "A");
    EXPECT_EQ(key, 0b10u); // Only B

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, HasFeature) {
    PermutationKeyBuilder builder;
    builder.Init();

    builder.RegisterFeature("A");
    builder.RegisterFeature("B");

    u64 key = builder.BuildKey({"A"});
    EXPECT_TRUE(builder.HasFeature(key, "A"));
    EXPECT_FALSE(builder.HasFeature(key, "B"));
    EXPECT_FALSE(builder.HasFeature(key, "UNKNOWN"));

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, InvalidCombination) {
    PermutationKeyBuilder builder;
    builder.Init();

    builder.RegisterFeature("FORWARD_SHADING");
    builder.RegisterFeature("DEFERRED_SHADING");
    builder.RegisterFeature("HAS_NORMAL_MAP");

    builder.RegisterInvalidCombination("FORWARD_SHADING", "DEFERRED_SHADING");

    u64 validKey = builder.BuildKey({"FORWARD_SHADING", "HAS_NORMAL_MAP"});
    EXPECT_TRUE(builder.IsValidKey(validKey));

    u64 invalidKey = builder.BuildKey({"FORWARD_SHADING", "DEFERRED_SHADING"});
    EXPECT_FALSE(builder.IsValidKey(invalidKey));

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, IsValidKeyNoValidation) {
    PermutationKeyBuilder builder;
    PermutationKeyConfig config;
    config.validateCombinations = false;
    builder.Init(config);

    builder.RegisterFeature("A");
    builder.RegisterFeature("B");
    builder.RegisterInvalidCombination("A", "B");

    u64 key = builder.BuildKey({"A", "B"});
    EXPECT_TRUE(builder.IsValidKey(key)); // Validation disabled

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, DescribeKey) {
    PermutationKeyBuilder builder;
    builder.Init();

    builder.RegisterFeature("NORMAL_MAP");
    builder.RegisterFeature("SKINNING");
    builder.RegisterFeature("ALPHA_TEST");

    u64 key = builder.BuildKey({"NORMAL_MAP", "ALPHA_TEST"});
    std::string desc = builder.DescribeKey(key);

    EXPECT_NE(desc.find("NORMAL_MAP"), std::string::npos);
    EXPECT_NE(desc.find("ALPHA_TEST"), std::string::npos);
    EXPECT_EQ(desc.find("SKINNING"), std::string::npos);

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, EnumerateValidPermutations) {
    PermutationKeyBuilder builder;
    builder.Init();

    builder.RegisterFeature("A");
    builder.RegisterFeature("B");
    builder.RegisterFeature("C");

    // No invalid combos -> 2^3 = 8 permutations
    auto perms = builder.EnumerateValidPermutations();
    EXPECT_EQ(perms.size(), 8u);

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, EnumerateWithInvalidCombos) {
    PermutationKeyBuilder builder;
    builder.Init();

    builder.RegisterFeature("A");
    builder.RegisterFeature("B");
    builder.RegisterFeature("C");

    builder.RegisterInvalidCombination("A", "B");

    // 8 total - combos with both A and B (A+B, A+B+C) = 8 - 2 = 6
    auto perms = builder.EnumerateValidPermutations();
    EXPECT_EQ(perms.size(), 6u);

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, GetBitIndex) {
    PermutationKeyBuilder builder;
    builder.Init();

    builder.RegisterFeature("FIRST");
    builder.RegisterFeature("SECOND");
    builder.RegisterFeature("THIRD");

    EXPECT_EQ(builder.GetBitIndex("FIRST"), 0u);
    EXPECT_EQ(builder.GetBitIndex("SECOND"), 1u);
    EXPECT_EQ(builder.GetBitIndex("THIRD"), 2u);
    EXPECT_EQ(builder.GetBitIndex("UNKNOWN"), UINT32_MAX);

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, MaxFeaturesLimit) {
    PermutationKeyBuilder builder;
    PermutationKeyConfig config;
    config.maxFeatures = 3;
    builder.Init(config);

    EXPECT_TRUE(builder.RegisterFeature("A"));
    EXPECT_TRUE(builder.RegisterFeature("B"));
    EXPECT_TRUE(builder.RegisterFeature("C"));
    EXPECT_FALSE(builder.RegisterFeature("D")); // Exceeds

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, StatsTracking) {
    PermutationKeyBuilder builder;
    builder.Init();

    builder.RegisterFeature("A");
    builder.RegisterFeature("B");

    builder.BuildKey({"A"});
    builder.BuildKey({"A"}); // Same key again
    builder.BuildKey({"B"});

    auto stats = builder.GetStats();
    EXPECT_EQ(stats.totalFeatures, 2u);
    EXPECT_EQ(stats.totalKeysBuilt, 3u);
    EXPECT_EQ(stats.uniqueKeys, 2u); // {A} and {B}
    EXPECT_EQ(stats.warmupPermutations, 4u); // 2^2 = 4, no invalid combos

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, ResetClearsAll) {
    PermutationKeyBuilder builder;
    builder.Init();

    builder.RegisterFeature("A");
    builder.BuildKey({"A"});

    builder.Reset();

    EXPECT_EQ(builder.GetFeatureCount(), 0u);
    auto stats = builder.GetStats();
    EXPECT_EQ(stats.totalKeysBuilt, 0u);
    EXPECT_EQ(stats.uniqueKeys, 0u);

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, GetFeatureUnknown) {
    PermutationKeyBuilder builder;
    builder.Init();

    EXPECT_EQ(builder.GetFeature("UNKNOWN"), nullptr);

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, SetFeatureUnknown) {
    PermutationKeyBuilder builder;
    builder.Init();

    builder.RegisterFeature("A");

    u64 key = 1u;
    u64 result = builder.SetFeature(key, "UNKNOWN");
    EXPECT_EQ(result, key); // Unchanged

    builder.Shutdown();
}

TEST(PermutationKeyBuilder, AllFeaturesEnabled) {
    PermutationKeyBuilder builder;
    builder.Init();

    builder.RegisterFeature("A");
    builder.RegisterFeature("B");
    builder.RegisterFeature("C");
    builder.RegisterFeature("D");

    u64 key = builder.BuildKey({"A", "B", "C", "D"});
    EXPECT_EQ(key, 0b1111u);

    EXPECT_TRUE(builder.HasFeature(key, "A"));
    EXPECT_TRUE(builder.HasFeature(key, "B"));
    EXPECT_TRUE(builder.HasFeature(key, "C"));
    EXPECT_TRUE(builder.HasFeature(key, "D"));

    builder.Shutdown();
}
