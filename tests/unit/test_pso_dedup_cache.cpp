#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_pso_dedup_cache.h"

using namespace nge::rhi;

static PSOKey MakeKey(u64 hash, PSOType type = PSOType::Graphics) {
    return PSOKey{hash, type};
}

TEST(PSODedupCache, InitAndShutdown) {
    PSODedupCache cache;
    EXPECT_TRUE(cache.Init());

    EXPECT_EQ(cache.GetCount(), 0u);
    auto stats = cache.GetStats();
    EXPECT_EQ(stats.totalEntries, 0u);
    EXPECT_EQ(stats.totalLookups, 0u);

    cache.Shutdown();
}

TEST(PSODedupCache, InsertAndLookup) {
    PSODedupCache cache;
    cache.Init();

    auto key = MakeKey(0xDEADBEEF);
    cache.Insert(key, 42, 100, 5000, "TestPSO");

    EXPECT_EQ(cache.GetCount(), 1u);

    u64 handle = cache.Lookup(key);
    EXPECT_EQ(handle, 42u);

    auto stats = cache.GetStats();
    EXPECT_EQ(stats.cacheHits, 1u);
    EXPECT_EQ(stats.cacheMisses, 0u);

    cache.Shutdown();
}

TEST(PSODedupCache, LookupMiss) {
    PSODedupCache cache;
    cache.Init();

    u64 handle = cache.Lookup(MakeKey(0x12345));
    EXPECT_EQ(handle, 0u);

    auto stats = cache.GetStats();
    EXPECT_EQ(stats.cacheHits, 0u);
    EXPECT_EQ(stats.cacheMisses, 1u);

    cache.Shutdown();
}

TEST(PSODedupCache, ContainsWithoutLRUUpdate) {
    PSODedupCache cache;
    cache.Init();

    auto key = MakeKey(0xAABB);
    cache.Insert(key, 10, 20);

    EXPECT_TRUE(cache.Contains(key));
    EXPECT_FALSE(cache.Contains(MakeKey(0xCCDD)));

    // Contains should NOT increment lookup stats
    auto stats = cache.GetStats();
    EXPECT_EQ(stats.totalLookups, 0u);

    cache.Shutdown();
}

TEST(PSODedupCache, DuplicateInsertUpdatesHandle) {
    PSODedupCache cache;
    cache.Init();

    auto key = MakeKey(0x1111);
    cache.Insert(key, 10, 20);
    cache.Insert(key, 99, 20); // Update

    EXPECT_EQ(cache.GetCount(), 1u); // Still 1
    EXPECT_EQ(cache.Lookup(key), 99u); // Updated handle

    cache.Shutdown();
}

TEST(PSODedupCache, LookupOrReserve) {
    PSODedupCache cache;
    cache.Init();

    auto key = MakeKey(0x2222);
    bool wasFound = false;

    u64 handle = cache.LookupOrReserve(key, wasFound);
    EXPECT_EQ(handle, 0u);
    EXPECT_FALSE(wasFound);

    cache.Insert(key, 55, 100);

    handle = cache.LookupOrReserve(key, wasFound);
    EXPECT_EQ(handle, 55u);
    EXPECT_TRUE(wasFound);

    cache.Shutdown();
}

TEST(PSODedupCache, Remove) {
    PSODedupCache cache;
    cache.Init();

    auto key = MakeKey(0x3333);
    cache.Insert(key, 10, 20);
    EXPECT_TRUE(cache.Contains(key));

    EXPECT_TRUE(cache.Remove(key));
    EXPECT_FALSE(cache.Contains(key));
    EXPECT_EQ(cache.GetCount(), 0u);

    // Remove non-existent
    EXPECT_FALSE(cache.Remove(MakeKey(0x9999)));

    cache.Shutdown();
}

TEST(PSODedupCache, LRUEvictionAtCapacity) {
    PSODedupCache cache;
    PSODedupCacheConfig config;
    config.maxEntries = 3;
    config.enableLRU = true;
    cache.Init(config);

    cache.Insert(MakeKey(1), 10, 0);
    cache.Insert(MakeKey(2), 20, 0);
    cache.Insert(MakeKey(3), 30, 0);
    EXPECT_EQ(cache.GetCount(), 3u);

    // Insert 4th -> should evict LRU (key 1)
    cache.Insert(MakeKey(4), 40, 0);
    EXPECT_EQ(cache.GetCount(), 3u);

    EXPECT_FALSE(cache.Contains(MakeKey(1))); // Evicted
    EXPECT_TRUE(cache.Contains(MakeKey(2)));
    EXPECT_TRUE(cache.Contains(MakeKey(3)));
    EXPECT_TRUE(cache.Contains(MakeKey(4)));

    auto stats = cache.GetStats();
    EXPECT_EQ(stats.evictions, 1u);

    cache.Shutdown();
}

TEST(PSODedupCache, LRUOrderUpdatedByLookup) {
    PSODedupCache cache;
    PSODedupCacheConfig config;
    config.maxEntries = 3;
    config.enableLRU = true;
    cache.Init(config);

    cache.Insert(MakeKey(1), 10, 0);
    cache.Insert(MakeKey(2), 20, 0);
    cache.Insert(MakeKey(3), 30, 0);

    // Access key 1 to move it to front
    cache.Lookup(MakeKey(1));

    // Insert 4th -> should evict LRU (now key 2, since key 1 was touched)
    cache.Insert(MakeKey(4), 40, 0);

    EXPECT_TRUE(cache.Contains(MakeKey(1)));  // Recently accessed
    EXPECT_FALSE(cache.Contains(MakeKey(2))); // Evicted (was LRU)
    EXPECT_TRUE(cache.Contains(MakeKey(3)));
    EXPECT_TRUE(cache.Contains(MakeKey(4)));

    cache.Shutdown();
}

TEST(PSODedupCache, TouchUpdatesLRU) {
    PSODedupCache cache;
    PSODedupCacheConfig config;
    config.maxEntries = 3;
    config.enableLRU = true;
    cache.Init(config);

    cache.Insert(MakeKey(1), 10, 0);
    cache.Insert(MakeKey(2), 20, 0);
    cache.Insert(MakeKey(3), 30, 0);

    // Touch key 1
    cache.Touch(MakeKey(1), 100);

    // Evict -> should evict key 2 (LRU since key 1 was touched)
    cache.Insert(MakeKey(4), 40, 0);

    EXPECT_TRUE(cache.Contains(MakeKey(1)));
    EXPECT_FALSE(cache.Contains(MakeKey(2)));

    cache.Shutdown();
}

TEST(PSODedupCache, EvictToCount) {
    PSODedupCache cache;
    cache.Init();

    for (u32 i = 0; i < 10; ++i) {
        cache.Insert(MakeKey(i), i * 10, 0);
    }
    EXPECT_EQ(cache.GetCount(), 10u);

    u32 evicted = cache.EvictToCount(5);
    EXPECT_EQ(evicted, 5u);
    EXPECT_EQ(cache.GetCount(), 5u);

    cache.Shutdown();
}

TEST(PSODedupCache, EvictOlderThan) {
    PSODedupCache cache;
    cache.Init();

    cache.Insert(MakeKey(1), 10, 0);
    cache.Touch(MakeKey(1), 50);

    cache.Insert(MakeKey(2), 20, 0);
    cache.Touch(MakeKey(2), 100);

    cache.Insert(MakeKey(3), 30, 0);
    cache.Touch(MakeKey(3), 200);

    // Evict entries last used before frame 100
    u32 evicted = cache.EvictOlderThan(100);
    EXPECT_GE(evicted, 1u);

    EXPECT_FALSE(cache.Contains(MakeKey(1))); // frame 50 < 100
    EXPECT_TRUE(cache.Contains(MakeKey(2)));  // frame 100 >= 100
    EXPECT_TRUE(cache.Contains(MakeKey(3)));  // frame 200 >= 100

    cache.Shutdown();
}

TEST(PSODedupCache, GetEntry) {
    PSODedupCache cache;
    cache.Init();

    auto key = MakeKey(0xABCD, PSOType::Compute);
    cache.Insert(key, 42, 100, 12345, "ComputePSO");

    const auto* entry = cache.GetEntry(key);
    EXPECT_NE(entry, nullptr);
    EXPECT_EQ(entry->pipelineHandle, 42u);
    EXPECT_EQ(entry->layoutHandle, 100u);
    EXPECT_EQ(entry->type, PSOType::Compute);
    EXPECT_EQ(entry->creationTimeNs, 12345u);
    EXPECT_EQ(entry->debugName, "ComputePSO");

    EXPECT_EQ(cache.GetEntry(MakeKey(0x9999)), nullptr);

    cache.Shutdown();
}

TEST(PSODedupCache, DifferentPSOTypes) {
    PSODedupCache cache;
    cache.Init();

    auto gfx = MakeKey(100, PSOType::Graphics);
    auto comp = MakeKey(100, PSOType::Compute); // Same hash, different type

    cache.Insert(gfx, 10, 0);
    cache.Insert(comp, 20, 0);

    EXPECT_EQ(cache.GetCount(), 2u); // Distinct entries
    EXPECT_EQ(cache.Lookup(gfx), 10u);
    EXPECT_EQ(cache.Lookup(comp), 20u);

    cache.Shutdown();
}

TEST(PSODedupCache, HitRateCalculation) {
    PSODedupCache cache;
    cache.Init();

    cache.Insert(MakeKey(1), 10, 0);

    cache.Lookup(MakeKey(1)); // Hit
    cache.Lookup(MakeKey(1)); // Hit
    cache.Lookup(MakeKey(2)); // Miss

    auto stats = cache.GetStats();
    EXPECT_EQ(stats.totalLookups, 3u);
    EXPECT_EQ(stats.cacheHits, 2u);
    EXPECT_EQ(stats.cacheMisses, 1u);
    EXPECT_NEAR(stats.hitRate, 2.0f / 3.0f, 0.01f);

    cache.Shutdown();
}

TEST(PSODedupCache, CompilationTimeSaved) {
    PSODedupCache cache;
    PSODedupCacheConfig config;
    config.trackCompilationTime = true;
    cache.Init(config);

    auto key = MakeKey(1);
    cache.Insert(key, 10, 0, 5000); // 5000ns compile time

    cache.Lookup(key); // Hit -> saves 5000ns
    cache.Lookup(key); // Hit -> saves 5000ns

    auto stats = cache.GetStats();
    EXPECT_EQ(stats.totalCompilationTimeNs, 5000u);
    EXPECT_EQ(stats.savedCompilationTimeNs, 10000u); // 2 hits * 5000

    cache.Shutdown();
}

TEST(PSODedupCache, ClearKeepsConfig) {
    PSODedupCache cache;
    cache.Init();

    cache.Insert(MakeKey(1), 10, 0);
    cache.Insert(MakeKey(2), 20, 0);
    EXPECT_EQ(cache.GetCount(), 2u);

    cache.Clear();
    EXPECT_EQ(cache.GetCount(), 0u);

    // Stats should still show historical data
    auto stats = cache.GetStats();
    EXPECT_EQ(stats.insertions, 2u);

    cache.Shutdown();
}

TEST(PSODedupCache, ResetClearsAll) {
    PSODedupCache cache;
    cache.Init();

    cache.Insert(MakeKey(1), 10, 0);
    cache.Lookup(MakeKey(1));

    cache.Reset();

    EXPECT_EQ(cache.GetCount(), 0u);
    auto stats = cache.GetStats();
    EXPECT_EQ(stats.totalLookups, 0u);
    EXPECT_EQ(stats.cacheHits, 0u);
    EXPECT_EQ(stats.insertions, 0u);
    EXPECT_EQ(stats.evictions, 0u);

    cache.Shutdown();
}

TEST(PSODedupCache, PeakEntries) {
    PSODedupCache cache;
    PSODedupCacheConfig config;
    config.maxEntries = 5;
    cache.Init(config);

    for (u32 i = 0; i < 5; ++i) {
        cache.Insert(MakeKey(i), i * 10, 0);
    }

    cache.EvictToCount(2);
    EXPECT_EQ(cache.GetCount(), 2u);
    EXPECT_EQ(cache.GetStats().peakEntries, 5u);

    cache.Shutdown();
}
