#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_mip_bias_controller.h"
#include "engine/rhi/common/rhi_draw_call_merger.h"

using namespace nge;
using namespace nge::rhi;

// ─── Mip Bias Controller Tests ───────────────────────────────────────────

TEST(MipBiasController, InitAndShutdown) {
    MipBiasController controller;
    EXPECT_TRUE(controller.Init());

    auto stats = controller.GetStats();
    EXPECT_EQ(stats.trackedMaterials, 0u);

    controller.Shutdown();
}

TEST(MipBiasController, RegisterAndGetBias) {
    MipBiasController controller;
    controller.Init();

    controller.RegisterMaterial(0, MipBiasStrategy::Fixed, 0.5f);
    controller.RegisterMaterial(1, MipBiasStrategy::Fixed, -0.5f);

    EXPECT_FLOAT_EQ(controller.GetEffectiveBias(0), 0.5f);
    EXPECT_FLOAT_EQ(controller.GetEffectiveBias(1), -0.5f);

    auto stats = controller.GetStats();
    EXPECT_EQ(stats.trackedMaterials, 2u);

    controller.Shutdown();
}

TEST(MipBiasController, UnregisteredReturnsGlobalOffset) {
    MipBiasController controller;
    MipBiasControllerConfig config;
    config.globalBiasOffset = 0.25f;
    controller.Init(config);

    EXPECT_FLOAT_EQ(controller.GetEffectiveBias(999), 0.25f);

    controller.Shutdown();
}

TEST(MipBiasController, GlobalOffsetApplied) {
    MipBiasController controller;
    MipBiasControllerConfig config;
    config.globalBiasOffset = 1.0f;
    controller.Init(config);

    controller.RegisterMaterial(0, MipBiasStrategy::Fixed, 0.5f);
    EXPECT_FLOAT_EQ(controller.GetEffectiveBias(0), 1.5f);

    controller.SetGlobalOffset(0.0f);
    EXPECT_FLOAT_EQ(controller.GetEffectiveBias(0), 0.5f);

    controller.Shutdown();
}

TEST(MipBiasController, BiasClampedToRange) {
    MipBiasController controller;
    MipBiasControllerConfig config;
    config.minBias = -2.0f;
    config.maxBias = 4.0f;
    config.globalBiasOffset = 3.0f;
    controller.Init(config);

    controller.RegisterMaterial(0, MipBiasStrategy::Fixed, 2.0f);
    // 2.0 + 3.0 = 5.0, clamped to 4.0
    EXPECT_FLOAT_EQ(controller.GetEffectiveBias(0), 4.0f);

    controller.Shutdown();
}

TEST(MipBiasController, StreamingNotification) {
    MipBiasController controller;
    MipBiasControllerConfig config;
    config.streamingTransitionBias = 1.5f;
    controller.Init(config);

    controller.RegisterMaterial(0, MipBiasStrategy::StreamingAdaptive, 0.0f);

    controller.NotifyStreaming(0, true);
    // Target should be set to streaming bias
    // After update, currentBias should blend toward target
    controller.Update(1.0f / 60.0f, 0.5f);

    f32 bias = controller.GetEffectiveBias(0);
    EXPECT_GT(bias, 0.0f); // Should be moving toward 1.5

    controller.NotifyStreaming(0, false);
    // Multiple updates to blend back
    for (int i = 0; i < 600; ++i) {
        controller.Update(1.0f / 60.0f, 0.5f);
    }
    bias = controller.GetEffectiveBias(0);
    EXPECT_LT(bias, 0.1f); // Should be near 0

    controller.Shutdown();
}

TEST(MipBiasController, VRAMPressureResponse) {
    MipBiasController controller;
    MipBiasControllerConfig config;
    config.vramPressureThreshold = 0.85f;
    config.vramCriticalThreshold = 0.95f;
    config.maxBias = 4.0f;
    controller.Init(config);

    controller.RegisterMaterial(0, MipBiasStrategy::VRAMPressure, 0.0f);

    // Below threshold — no pressure bias
    controller.Update(1.0f / 60.0f, 0.5f);
    EXPECT_FLOAT_EQ(controller.GetEffectiveBias(0), 0.0f);

    // At critical threshold — max pressure
    for (int i = 0; i < 600; ++i) {
        controller.Update(1.0f / 60.0f, 0.95f);
    }
    f32 bias = controller.GetEffectiveBias(0);
    EXPECT_GT(bias, 2.0f); // Should be significant at critical

    controller.Shutdown();
}

TEST(MipBiasController, LockPreventAutoAdjust) {
    MipBiasController controller;
    controller.Init();

    controller.RegisterMaterial(0, MipBiasStrategy::StreamingAdaptive, 1.0f);
    controller.LockBias(0, true);

    controller.NotifyStreaming(0, true);
    controller.Update(1.0f / 60.0f, 0.5f);

    // Locked — should not change from initial 1.0
    EXPECT_FLOAT_EQ(controller.GetEffectiveBias(0), 1.0f);

    controller.Shutdown();
}

TEST(MipBiasController, ForceAllBias) {
    MipBiasController controller;
    controller.Init();

    controller.RegisterMaterial(0, MipBiasStrategy::Fixed, 0.0f);
    controller.RegisterMaterial(1, MipBiasStrategy::Fixed, 1.0f);
    controller.RegisterMaterial(2, MipBiasStrategy::Fixed, -1.0f);

    controller.ForceAllBias(2.0f);

    EXPECT_FLOAT_EQ(controller.GetEffectiveBias(0), 2.0f);
    EXPECT_FLOAT_EQ(controller.GetEffectiveBias(1), 2.0f);
    EXPECT_FLOAT_EQ(controller.GetEffectiveBias(2), 2.0f);

    controller.Shutdown();
}

TEST(MipBiasController, GetAllBiases) {
    MipBiasController controller;
    controller.Init();

    controller.RegisterMaterial(0, MipBiasStrategy::Fixed, 0.5f);
    controller.RegisterMaterial(2, MipBiasStrategy::Fixed, 1.0f);

    auto biases = controller.GetAllBiases(4);
    EXPECT_EQ(biases.size(), 4u);
    EXPECT_FLOAT_EQ(biases[0], 0.5f);
    EXPECT_FLOAT_EQ(biases[1], 0.0f); // Unregistered → global offset (0)
    EXPECT_FLOAT_EQ(biases[2], 1.0f);
    EXPECT_FLOAT_EQ(biases[3], 0.0f);

    controller.Shutdown();
}

// ─── Draw Call Merger Tests ──────────────────────────────────────────────

TEST(DrawCallMerger, InitAndShutdown) {
    DrawCallMerger merger;
    EXPECT_TRUE(merger.Init());
    merger.Shutdown();
}

TEST(DrawCallMerger, SingleDrawNoBatching) {
    DrawCallMerger merger;
    merger.Init();

    DrawRequest req;
    req.psoHash = 100;
    req.materialId = 0;
    req.vertexBufferHandle = 1;
    req.indexBufferHandle = 2;
    req.indexCount = 36;
    req.firstIndex = 0;
    req.vertexOffset = 0;
    req.instanceCount = 1;
    req.firstInstance = 0;

    merger.Submit(req);
    merger.Merge();

    EXPECT_EQ(merger.GetBatches().size(), 1u);
    EXPECT_EQ(merger.GetBatches()[0].drawCount, 1u);

    merger.Shutdown();
}

TEST(DrawCallMerger, MergeCompatibleDraws) {
    DrawCallMerger merger;
    merger.Init();

    // 5 draws with same PSO + material + buffers → should merge into 1 batch
    for (u32 i = 0; i < 5; ++i) {
        DrawRequest req;
        req.psoHash = 42;
        req.materialId = 7;
        req.vertexBufferHandle = 100;
        req.indexBufferHandle = 200;
        req.indexCount = 36;
        req.firstIndex = i * 36;
        req.vertexOffset = 0;
        req.instanceCount = 1;
        req.firstInstance = i;
        merger.Submit(req);
    }

    merger.Merge();

    EXPECT_EQ(merger.GetBatches().size(), 1u);
    EXPECT_EQ(merger.GetBatches()[0].drawCount, 5u);

    auto stats = merger.GetStats();
    EXPECT_EQ(stats.inputDrawCalls, 5u);
    EXPECT_EQ(stats.outputBatches, 1u);
    EXPECT_EQ(stats.largestBatchSize, 5u);
    EXPECT_GT(stats.reductionPercent, 50.0f);

    merger.Shutdown();
}

TEST(DrawCallMerger, DifferentPSOSeparateBatches) {
    DrawCallMerger merger;
    merger.Init();

    DrawRequest req1;
    req1.psoHash = 10;
    req1.materialId = 0;
    req1.vertexBufferHandle = 1;
    req1.indexBufferHandle = 2;
    req1.indexCount = 36;
    merger.Submit(req1);

    DrawRequest req2;
    req2.psoHash = 20; // Different PSO
    req2.materialId = 0;
    req2.vertexBufferHandle = 1;
    req2.indexBufferHandle = 2;
    req2.indexCount = 24;
    merger.Submit(req2);

    merger.Merge();

    EXPECT_EQ(merger.GetBatches().size(), 2u);

    merger.Shutdown();
}

TEST(DrawCallMerger, DifferentMaterialSeparateBatches) {
    DrawCallMerger merger;
    merger.Init();

    DrawRequest req1;
    req1.psoHash = 10;
    req1.materialId = 1;
    req1.vertexBufferHandle = 1;
    req1.indexBufferHandle = 2;
    req1.indexCount = 36;
    merger.Submit(req1);

    DrawRequest req2;
    req2.psoHash = 10;
    req2.materialId = 2; // Different material
    req2.vertexBufferHandle = 1;
    req2.indexBufferHandle = 2;
    req2.indexCount = 24;
    merger.Submit(req2);

    merger.Merge();

    EXPECT_EQ(merger.GetBatches().size(), 2u);

    merger.Shutdown();
}

TEST(DrawCallMerger, MergingDisabled) {
    DrawCallMerger merger;
    DrawCallMergerConfig config;
    config.enableMerging = false;
    merger.Init(config);

    for (u32 i = 0; i < 3; ++i) {
        DrawRequest req;
        req.psoHash = 42;
        req.materialId = 7;
        req.vertexBufferHandle = 100;
        req.indexBufferHandle = 200;
        req.indexCount = 36;
        merger.Submit(req);
    }

    merger.Merge();

    // Each draw becomes its own batch
    EXPECT_EQ(merger.GetBatches().size(), 3u);

    merger.Shutdown();
}

TEST(DrawCallMerger, GetIndirectBuffer) {
    DrawCallMerger merger;
    merger.Init();

    for (u32 i = 0; i < 3; ++i) {
        DrawRequest req;
        req.psoHash = 1;
        req.materialId = 0;
        req.vertexBufferHandle = 10;
        req.indexBufferHandle = 20;
        req.indexCount = 36;
        req.firstIndex = i * 36;
        req.vertexOffset = 0;
        req.instanceCount = 1;
        req.firstInstance = i;
        merger.Submit(req);
    }

    merger.Merge();

    auto cmds = merger.GetIndirectBuffer(0);
    EXPECT_EQ(cmds.size(), 3u);
    EXPECT_EQ(cmds[0].indexCount, 36u);

    // Invalid batch index
    auto empty = merger.GetIndirectBuffer(999);
    EXPECT_TRUE(empty.empty());

    merger.Shutdown();
}

TEST(DrawCallMerger, ClearResetsState) {
    DrawCallMerger merger;
    merger.Init();

    DrawRequest req;
    req.psoHash = 1;
    req.materialId = 0;
    req.vertexBufferHandle = 1;
    req.indexBufferHandle = 2;
    req.indexCount = 36;
    merger.Submit(req);
    merger.Merge();

    EXPECT_EQ(merger.GetBatches().size(), 1u);

    merger.Clear();
    EXPECT_TRUE(merger.GetBatches().empty());

    merger.Shutdown();
}
