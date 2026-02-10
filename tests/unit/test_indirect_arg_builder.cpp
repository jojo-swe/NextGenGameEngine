#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_indirect_arg_builder.h"

using namespace nge;
using namespace nge::rhi;

TEST(IndirectArgBuilder, InitAndShutdown) {
    IndirectArgBuilder builder;
    EXPECT_TRUE(builder.Init());
    EXPECT_EQ(builder.GetTotalArgCount(), 0u);
    builder.Shutdown();
}

TEST(IndirectArgBuilder, AddDraw) {
    IndirectArgBuilder builder;
    builder.Init();

    EXPECT_TRUE(builder.AddDraw(36, 1));
    EXPECT_EQ(builder.GetDrawCount(), 1u);

    const auto& args = builder.GetDrawArgs();
    EXPECT_EQ(args[0].vertexCount, 36u);
    EXPECT_EQ(args[0].instanceCount, 1u);
    EXPECT_EQ(args[0].firstVertex, 0u);
    EXPECT_EQ(args[0].firstInstance, 0u);

    builder.Shutdown();
}

TEST(IndirectArgBuilder, AddDrawWithOffsets) {
    IndirectArgBuilder builder;
    builder.Init();

    EXPECT_TRUE(builder.AddDraw(100, 5, 200, 10));

    const auto& args = builder.GetDrawArgs();
    EXPECT_EQ(args[0].firstVertex, 200u);
    EXPECT_EQ(args[0].firstInstance, 10u);

    builder.Shutdown();
}

TEST(IndirectArgBuilder, AddDrawIndexed) {
    IndirectArgBuilder builder;
    builder.Init();

    EXPECT_TRUE(builder.AddDrawIndexed(1024, 10, 0, 0, 0));
    EXPECT_EQ(builder.GetDrawIndexedCount(), 1u);

    const auto& args = builder.GetDrawIndexedArgs();
    EXPECT_EQ(args[0].indexCount, 1024u);
    EXPECT_EQ(args[0].instanceCount, 10u);

    builder.Shutdown();
}

TEST(IndirectArgBuilder, AddDrawIndexedWithOffsets) {
    IndirectArgBuilder builder;
    builder.Init();

    EXPECT_TRUE(builder.AddDrawIndexed(500, 3, 100, -50, 7));

    const auto& args = builder.GetDrawIndexedArgs();
    EXPECT_EQ(args[0].firstIndex, 100u);
    EXPECT_EQ(args[0].vertexOffset, -50);
    EXPECT_EQ(args[0].firstInstance, 7u);

    builder.Shutdown();
}

TEST(IndirectArgBuilder, AddDispatch) {
    IndirectArgBuilder builder;
    builder.Init();

    EXPECT_TRUE(builder.AddDispatch(16, 16, 1));
    EXPECT_EQ(builder.GetDispatchCount(), 1u);

    const auto& args = builder.GetDispatchArgs();
    EXPECT_EQ(args[0].groupCountX, 16u);
    EXPECT_EQ(args[0].groupCountY, 16u);
    EXPECT_EQ(args[0].groupCountZ, 1u);

    builder.Shutdown();
}

TEST(IndirectArgBuilder, AddDrawMeshTasks) {
    IndirectArgBuilder builder;
    builder.Init();

    EXPECT_TRUE(builder.AddDrawMeshTasks(32, 1, 1));
    EXPECT_EQ(builder.GetMeshTaskCount(), 1u);

    const auto& args = builder.GetMeshTaskArgs();
    EXPECT_EQ(args[0].groupCountX, 32u);

    builder.Shutdown();
}

TEST(IndirectArgBuilder, ValidationZeroVertices) {
    IndirectArgBuilder builder;
    IndirectArgBuilderConfig config;
    config.validateArgs = true;
    builder.Init(config);

    EXPECT_FALSE(builder.AddDraw(0, 1)); // Zero vertices
    EXPECT_EQ(builder.GetDrawCount(), 0u);

    auto stats = builder.GetStats();
    EXPECT_EQ(stats.validationErrors, 1u);

    builder.Shutdown();
}

TEST(IndirectArgBuilder, ValidationZeroInstances) {
    IndirectArgBuilder builder;
    IndirectArgBuilderConfig config;
    config.validateArgs = true;
    builder.Init(config);

    EXPECT_FALSE(builder.AddDraw(36, 0)); // Zero instances

    builder.Shutdown();
}

TEST(IndirectArgBuilder, ValidationZeroDispatchGroups) {
    IndirectArgBuilder builder;
    IndirectArgBuilderConfig config;
    config.validateArgs = true;
    builder.Init(config);

    EXPECT_FALSE(builder.AddDispatch(0, 16, 1)); // Zero X

    builder.Shutdown();
}

TEST(IndirectArgBuilder, ValidationDisabled) {
    IndirectArgBuilder builder;
    IndirectArgBuilderConfig config;
    config.validateArgs = false;
    builder.Init(config);

    // With validation disabled, zero vertices should still be accepted
    EXPECT_TRUE(builder.AddDraw(0, 1));
    EXPECT_EQ(builder.GetDrawCount(), 1u);

    builder.Shutdown();
}

TEST(IndirectArgBuilder, MaxDrawArgsLimit) {
    IndirectArgBuilder builder;
    IndirectArgBuilderConfig config;
    config.maxDrawArgs = 3;
    config.validateArgs = false;
    builder.Init(config);

    EXPECT_TRUE(builder.AddDraw(36, 1));
    EXPECT_TRUE(builder.AddDraw(36, 1));
    EXPECT_TRUE(builder.AddDraw(36, 1));
    EXPECT_FALSE(builder.AddDraw(36, 1)); // Exceeds limit

    builder.Shutdown();
}

TEST(IndirectArgBuilder, MaxDispatchArgsLimit) {
    IndirectArgBuilder builder;
    IndirectArgBuilderConfig config;
    config.maxDispatchArgs = 2;
    builder.Init(config);

    EXPECT_TRUE(builder.AddDispatch(8, 8, 1));
    EXPECT_TRUE(builder.AddDispatch(8, 8, 1));
    EXPECT_FALSE(builder.AddDispatch(8, 8, 1)); // Exceeds limit

    builder.Shutdown();
}

TEST(IndirectArgBuilder, TotalArgCount) {
    IndirectArgBuilder builder;
    builder.Init();

    builder.AddDraw(36, 1);
    builder.AddDrawIndexed(100, 2);
    builder.AddDispatch(8, 8, 1);
    builder.AddDrawMeshTasks(4, 1, 1);

    EXPECT_EQ(builder.GetTotalArgCount(), 4u);

    builder.Shutdown();
}

TEST(IndirectArgBuilder, BufferSizes) {
    IndirectArgBuilder builder;
    builder.Init();

    builder.AddDraw(36, 1);
    builder.AddDrawIndexed(100, 2);
    builder.AddDispatch(8, 8, 1);

    EXPECT_EQ(builder.GetDrawBufferSize(), sizeof(DrawArgs));
    EXPECT_EQ(builder.GetDrawIndexedBufferSize(), sizeof(DrawIndexedArgs));
    EXPECT_EQ(builder.GetDispatchBufferSize(), sizeof(DispatchArgs));
    EXPECT_GT(builder.GetTotalBufferSize(), 0u);

    builder.Shutdown();
}

TEST(IndirectArgBuilder, SortDrawsByInstanceCount) {
    IndirectArgBuilder builder;
    builder.Init();

    builder.AddDraw(36, 1);
    builder.AddDraw(100, 50);
    builder.AddDraw(200, 10);

    builder.SortDrawsByInstanceCount();

    const auto& args = builder.GetDrawArgs();
    EXPECT_EQ(args[0].instanceCount, 50u);
    EXPECT_EQ(args[1].instanceCount, 10u);
    EXPECT_EQ(args[2].instanceCount, 1u);

    builder.Shutdown();
}

TEST(IndirectArgBuilder, MergeCompatibleDraws) {
    IndirectArgBuilder builder;
    IndirectArgBuilderConfig config;
    config.validateArgs = false;
    builder.Init(config);

    // Adjacent vertex ranges with same instance count
    builder.AddDraw(100, 1, 0, 0);    // vertices 0-99
    builder.AddDraw(100, 1, 100, 0);  // vertices 100-199 (adjacent)
    builder.AddDraw(50, 1, 200, 0);   // vertices 200-249 (adjacent)
    builder.AddDraw(100, 2, 300, 0);  // Different instance count, not merged

    u32 merged = builder.MergeCompatibleDraws();
    EXPECT_EQ(merged, 2u); // First three merged into one

    const auto& args = builder.GetDrawArgs();
    EXPECT_EQ(args.size(), 2u);
    EXPECT_EQ(args[0].vertexCount, 250u); // 100 + 100 + 50
    EXPECT_EQ(args[1].vertexCount, 100u); // Unchanged

    builder.Shutdown();
}

TEST(IndirectArgBuilder, Clear) {
    IndirectArgBuilder builder;
    builder.Init();

    builder.AddDraw(36, 1);
    builder.AddDrawIndexed(100, 2);
    builder.AddDispatch(8, 8, 1);

    builder.Clear();

    EXPECT_EQ(builder.GetDrawCount(), 0u);
    EXPECT_EQ(builder.GetDrawIndexedCount(), 0u);
    EXPECT_EQ(builder.GetDispatchCount(), 0u);
    EXPECT_EQ(builder.GetTotalArgCount(), 0u);

    builder.Shutdown();
}

TEST(IndirectArgBuilder, StatsTracking) {
    IndirectArgBuilder builder;
    builder.Init();

    builder.AddDraw(36, 5);
    builder.AddDraw(100, 10);
    builder.AddDrawIndexed(200, 3);

    auto stats = builder.GetStats();
    EXPECT_EQ(stats.drawArgsCount, 2u);
    EXPECT_EQ(stats.drawIndexedArgsCount, 1u);
    EXPECT_EQ(stats.totalArgs, 3u);
    EXPECT_EQ(stats.totalInstances, 18u); // 5 + 10 + 3
    EXPECT_EQ(stats.totalVertices, 336u); // 36 + 100 + 200
    EXPECT_GT(stats.bufferSizeBytes, 0u);

    builder.Shutdown();
}

TEST(IndirectArgBuilder, ResetClearsAll) {
    IndirectArgBuilder builder;
    builder.Init();

    builder.AddDraw(36, 1);
    builder.AddDraw(0, 1); // Validation error (if enabled)

    builder.Reset();

    EXPECT_EQ(builder.GetTotalArgCount(), 0u);
    auto stats = builder.GetStats();
    EXPECT_EQ(stats.validationErrors, 0u);

    builder.Shutdown();
}

TEST(IndirectArgBuilder, MultipleDrawTypes) {
    IndirectArgBuilder builder;
    builder.Init();

    for (u32 i = 0; i < 10; ++i) {
        builder.AddDraw(36 * (i + 1), i + 1);
    }
    for (u32 i = 0; i < 5; ++i) {
        builder.AddDrawIndexed(100 * (i + 1), i + 1);
    }
    for (u32 i = 0; i < 3; ++i) {
        builder.AddDispatch(8, 8, 1);
    }

    EXPECT_EQ(builder.GetDrawCount(), 10u);
    EXPECT_EQ(builder.GetDrawIndexedCount(), 5u);
    EXPECT_EQ(builder.GetDispatchCount(), 3u);
    EXPECT_EQ(builder.GetTotalArgCount(), 18u);

    builder.Shutdown();
}
