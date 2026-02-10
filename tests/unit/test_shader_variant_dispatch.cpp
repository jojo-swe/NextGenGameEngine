#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_shader_variant_dispatch.h"

using namespace nge;
using namespace nge::rhi;

TEST(ShaderVariantDispatch, InitAndShutdown) {
    ShaderVariantDispatchTable table;
    EXPECT_TRUE(table.Init());

    auto stats = table.GetStats();
    EXPECT_EQ(stats.totalVariants, 0u);
    EXPECT_EQ(stats.totalDispatches, 0u);

    table.Shutdown();
}

TEST(ShaderVariantDispatch, RegisterAndDispatch) {
    ShaderVariantDispatchTable table;
    table.Init();

    table.RegisterVariant(0x01, 100, "PBR", "SKINNING");
    table.RegisterVariant(0x02, 200, "PBR", "ALPHA_TEST");

    EXPECT_TRUE(table.HasVariant(0x01));
    EXPECT_TRUE(table.HasVariant(0x02));
    EXPECT_FALSE(table.HasVariant(0x03));

    EXPECT_EQ(table.Dispatch(0x01), 100u);
    EXPECT_EQ(table.Dispatch(0x02), 200u);

    auto stats = table.GetStats();
    EXPECT_EQ(stats.totalDispatches, 2u);
    EXPECT_EQ(stats.readyVariants, 2u);

    table.Shutdown();
}

TEST(ShaderVariantDispatch, PendingVariant) {
    ShaderVariantDispatchTable table;
    table.Init();

    table.RegisterPending(0x10, "DeferredLit");
    EXPECT_TRUE(table.IsPending(0x10));
    EXPECT_FALSE(table.HasVariant(0x10));

    // Dispatch should miss (pending, not ready)
    EXPECT_EQ(table.Dispatch(0x10), 0u);

    // Mark ready
    table.MarkReady(0x10, 500);
    EXPECT_TRUE(table.HasVariant(0x10));
    EXPECT_FALSE(table.IsPending(0x10));
    EXPECT_EQ(table.Dispatch(0x10), 500u);

    table.Shutdown();
}

TEST(ShaderVariantDispatch, FallbackChain) {
    ShaderVariantDispatchTable table;
    table.Init();

    // Register base variant
    table.RegisterVariant(0x00, 100, "PBR", "BASE");

    // Register fallback: 0x07 (all features) -> 0x03 -> 0x00 (base)
    table.RegisterFallback(0x07, 0x03);
    table.RegisterFallback(0x03, 0x00);

    // Dispatch 0x07: should fall back to 0x00 (0x03 not registered)
    u64 pso = table.Dispatch(0x07);
    EXPECT_EQ(pso, 100u);

    auto stats = table.GetStats();
    EXPECT_EQ(stats.fallbackDispatches, 1u);

    table.Shutdown();
}

TEST(ShaderVariantDispatch, FallbackToIntermediateVariant) {
    ShaderVariantDispatchTable table;
    table.Init();

    table.RegisterVariant(0x00, 100, "PBR", "BASE");
    table.RegisterVariant(0x03, 300, "PBR", "SKINNING+ALPHA");

    table.RegisterFallback(0x07, 0x03);
    table.RegisterFallback(0x03, 0x00);

    // 0x07 falls back to 0x03 which is ready
    EXPECT_EQ(table.Dispatch(0x07), 300u);

    table.Shutdown();
}

TEST(ShaderVariantDispatch, MissedDispatch) {
    ShaderVariantDispatchTable table;
    table.Init();

    // No variants registered
    EXPECT_EQ(table.Dispatch(0xFF), 0u);

    auto stats = table.GetStats();
    EXPECT_EQ(stats.missedDispatches, 1u);

    table.Shutdown();
}

TEST(ShaderVariantDispatch, RemoveVariant) {
    ShaderVariantDispatchTable table;
    table.Init();

    table.RegisterVariant(0x01, 100, "PBR", "A");
    EXPECT_TRUE(table.HasVariant(0x01));

    table.RemoveVariant(0x01);
    EXPECT_FALSE(table.HasVariant(0x01));

    table.Shutdown();
}

TEST(ShaderVariantDispatch, InvalidateShader) {
    ShaderVariantDispatchTable table;
    table.Init();

    table.RegisterVariant(0x01, 100, "PBR", "A");
    table.RegisterVariant(0x02, 200, "PBR", "B");
    table.RegisterVariant(0x03, 300, "Shadow", "C");

    EXPECT_EQ(table.GetStats().totalVariants, 3u);

    u32 removed = table.InvalidateShader("PBR");
    EXPECT_EQ(removed, 2u);
    EXPECT_EQ(table.GetStats().totalVariants, 1u);
    EXPECT_TRUE(table.HasVariant(0x03)); // Shadow still exists

    table.Shutdown();
}

TEST(ShaderVariantDispatch, HitTracking) {
    ShaderVariantDispatchTable table;
    ShaderVariantDispatchConfig config;
    config.enableHitTracking = true;
    table.Init(config);

    table.RegisterVariant(0x01, 100, "PBR", "A");
    table.RegisterVariant(0x02, 200, "PBR", "B");

    // Dispatch 0x01 five times, 0x02 once
    for (int i = 0; i < 5; ++i) table.Dispatch(0x01);
    table.Dispatch(0x02);

    auto hot = table.GetHotVariants(2);
    EXPECT_EQ(hot.size(), 2u);
    EXPECT_EQ(hot[0], 0x01u); // Most dispatched

    const auto* entry = table.GetVariant(0x01);
    EXPECT_NE(entry, nullptr);
    EXPECT_EQ(entry->hitCount, 5u);

    table.Shutdown();
}

TEST(ShaderVariantDispatch, MaxVariantsLimit) {
    ShaderVariantDispatchTable table;
    ShaderVariantDispatchConfig config;
    config.maxVariants = 3;
    table.Init(config);

    table.RegisterVariant(1, 100, "A", "");
    table.RegisterVariant(2, 200, "B", "");
    table.RegisterVariant(3, 300, "C", "");
    table.RegisterVariant(4, 400, "D", ""); // Exceeds limit

    EXPECT_EQ(table.GetStats().totalVariants, 3u);
    EXPECT_FALSE(table.HasVariant(4));

    table.Shutdown();
}

TEST(ShaderVariantDispatch, ClearResetsAll) {
    ShaderVariantDispatchTable table;
    table.Init();

    table.RegisterVariant(0x01, 100, "PBR", "A");
    table.RegisterFallback(0x02, 0x01);
    table.Dispatch(0x01);

    table.Clear();

    auto stats = table.GetStats();
    EXPECT_EQ(stats.totalVariants, 0u);
    EXPECT_EQ(stats.totalDispatches, 0u);
    EXPECT_EQ(stats.totalFallbackRules, 0u);

    table.Shutdown();
}

TEST(ShaderVariantDispatch, GetVariantInfo) {
    ShaderVariantDispatchTable table;
    table.Init();

    table.RegisterVariant(0xAB, 999, "Terrain", "TRIPLANAR+SNOW");

    const auto* entry = table.GetVariant(0xAB);
    EXPECT_NE(entry, nullptr);
    EXPECT_EQ(entry->key, 0xABu);
    EXPECT_EQ(entry->psoHandle, 999u);
    EXPECT_EQ(entry->shaderName, "Terrain");
    EXPECT_EQ(entry->variantDesc, "TRIPLANAR+SNOW");
    EXPECT_TRUE(entry->isReady);

    EXPECT_EQ(table.GetVariant(0xFF), nullptr);

    table.Shutdown();
}

TEST(ShaderVariantDispatch, MaxFallbacksLimit) {
    ShaderVariantDispatchTable table;
    ShaderVariantDispatchConfig config;
    config.maxFallbacks = 2;
    table.Init(config);

    table.RegisterFallback(1, 0);
    table.RegisterFallback(2, 0);
    table.RegisterFallback(3, 0); // Exceeds limit

    EXPECT_EQ(table.GetStats().totalFallbackRules, 2u);

    table.Shutdown();
}
