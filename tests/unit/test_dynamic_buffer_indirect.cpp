#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_dynamic_buffer.h"
#include "engine/rhi/common/rhi_indirect_count.h"

using namespace nge::rhi;

// ─── Dynamic Buffer Allocator Tests ──────────────────────────────────────

TEST(DynamicBuffer, AlignUpBasic) {
    // Test alignment helper via allocation behavior
    DynamicBufferAllocator alloc;
    DynamicBufferConfig config;
    config.bufferSizeBytes = 1024 * 1024; // 1MB
    config.framesInFlight = 3;
    config.minAlignment = 256;
    EXPECT_TRUE(alloc.Init(nullptr, config));

    alloc.BeginFrame(0);

    // First allocation should be at aligned offset
    auto a1 = alloc.Allocate(64, 256);
    EXPECT_TRUE(a1.valid);
    EXPECT_EQ(a1.offset % 256, 0u);
    EXPECT_EQ(a1.size, 64u);

    alloc.Shutdown();
}

TEST(DynamicBuffer, MultipleAllocationsNoOverlap) {
    DynamicBufferAllocator alloc;
    DynamicBufferConfig config;
    config.bufferSizeBytes = 1024 * 1024;
    config.framesInFlight = 2;
    config.minAlignment = 64;
    alloc.Init(nullptr, config);

    alloc.BeginFrame(0);

    auto a1 = alloc.Allocate(128, 64);
    auto a2 = alloc.Allocate(256, 64);
    auto a3 = alloc.Allocate(64, 64);

    EXPECT_TRUE(a1.valid);
    EXPECT_TRUE(a2.valid);
    EXPECT_TRUE(a3.valid);

    // No overlap
    EXPECT_GE(a2.offset, a1.offset + a1.size);
    EXPECT_GE(a3.offset, a2.offset + a2.size);

    alloc.Shutdown();
}

TEST(DynamicBuffer, FrameResetReclaims) {
    DynamicBufferAllocator alloc;
    DynamicBufferConfig config;
    config.bufferSizeBytes = 4096;
    config.framesInFlight = 2;
    config.minAlignment = 16;
    alloc.Init(nullptr, config);

    alloc.BeginFrame(0);
    auto a1 = alloc.Allocate(512);
    u64 firstOffset = a1.offset;

    // Next cycle of same frame index should reset
    alloc.BeginFrame(2); // Frame 2 % 2 = 0, same slot
    auto a2 = alloc.Allocate(512);
    EXPECT_EQ(a2.offset, firstOffset); // Reclaimed same region

    alloc.Shutdown();
}

TEST(DynamicBuffer, OutOfSpaceReturnsInvalid) {
    DynamicBufferAllocator alloc;
    DynamicBufferConfig config;
    config.bufferSizeBytes = 1024;
    config.framesInFlight = 2;
    config.minAlignment = 16;
    alloc.Init(nullptr, config);

    alloc.BeginFrame(0);

    // Region per frame = 512 bytes
    auto a1 = alloc.Allocate(480);
    EXPECT_TRUE(a1.valid);

    // This should fail — not enough space
    auto a2 = alloc.Allocate(256);
    EXPECT_FALSE(a2.valid);

    alloc.Shutdown();
}

TEST(DynamicBuffer, ZeroSizeReturnsInvalid) {
    DynamicBufferAllocator alloc;
    DynamicBufferConfig config;
    config.bufferSizeBytes = 4096;
    config.framesInFlight = 2;
    config.minAlignment = 16;
    alloc.Init(nullptr, config);

    alloc.BeginFrame(0);
    auto a = alloc.Allocate(0);
    EXPECT_FALSE(a.valid);

    alloc.Shutdown();
}

TEST(DynamicBuffer, StatsTracking) {
    DynamicBufferAllocator alloc;
    DynamicBufferConfig config;
    config.bufferSizeBytes = 1024 * 1024;
    config.framesInFlight = 3;
    config.minAlignment = 256;
    alloc.Init(nullptr, config);

    alloc.BeginFrame(0);
    alloc.Allocate(1024, 256);
    alloc.Allocate(2048, 256);

    auto stats = alloc.GetStats();
    EXPECT_EQ(stats.allocationsThisFrame, 2u);
    EXPECT_GT(stats.usedThisFrame, 0u);
    EXPECT_GT(stats.peakUsage, 0u);
    EXPECT_EQ(stats.totalSize, 1024u * 1024u);

    alloc.Shutdown();
}

// ─── Indirect Count Builder Tests ────────────────────────────────────────

TEST(IndirectCount, CreateAndDestroy) {
    IndirectCountBuilder builder;
    builder.Init(nullptr);

    IndirectCountBufferDesc desc;
    desc.type = IndirectCountType::DrawIndexed;
    desc.maxDrawCount = 1024;
    desc.debugName = "MainDraws";

    u32 id = builder.CreateBuffer(desc);
    EXPECT_EQ(id, 0u);
    EXPECT_EQ(builder.GetMaxDrawCount(id), 1024u);

    builder.DestroyBuffer(id);
    builder.Shutdown();
}

TEST(IndirectCount, MultipleBufferTypes) {
    IndirectCountBuilder builder;
    builder.Init(nullptr);

    IndirectCountBufferDesc drawDesc;
    drawDesc.type = IndirectCountType::Draw;
    drawDesc.maxDrawCount = 512;
    drawDesc.debugName = "Draws";

    IndirectCountBufferDesc meshDesc;
    meshDesc.type = IndirectCountType::MeshTasks;
    meshDesc.maxDrawCount = 256;
    meshDesc.debugName = "MeshTasks";

    IndirectCountBufferDesc dispatchDesc;
    dispatchDesc.type = IndirectCountType::Dispatch;
    dispatchDesc.maxDrawCount = 64;
    dispatchDesc.debugName = "Dispatches";

    u32 id0 = builder.CreateBuffer(drawDesc);
    u32 id1 = builder.CreateBuffer(meshDesc);
    u32 id2 = builder.CreateBuffer(dispatchDesc);

    EXPECT_NE(id0, id1);
    EXPECT_NE(id1, id2);
    EXPECT_EQ(builder.GetMaxDrawCount(id0), 512u);
    EXPECT_EQ(builder.GetMaxDrawCount(id1), 256u);
    EXPECT_EQ(builder.GetMaxDrawCount(id2), 64u);

    auto stats = builder.GetStats();
    EXPECT_EQ(stats.activeBuffers, 3u);
    EXPECT_EQ(stats.totalMaxDraws, 512u + 256u + 64u);
    EXPECT_GT(stats.totalMemoryBytes, 0u);

    builder.Shutdown();
}

TEST(IndirectCount, DestroyReducesActive) {
    IndirectCountBuilder builder;
    builder.Init(nullptr);

    IndirectCountBufferDesc desc;
    desc.type = IndirectCountType::DrawIndexed;
    desc.maxDrawCount = 100;
    desc.debugName = "Test";

    u32 id0 = builder.CreateBuffer(desc);
    u32 id1 = builder.CreateBuffer(desc);

    EXPECT_EQ(builder.GetStats().activeBuffers, 2u);

    builder.DestroyBuffer(id0);
    EXPECT_EQ(builder.GetStats().activeBuffers, 1u);

    builder.Shutdown();
}

TEST(IndirectCount, InvalidIdReturnsZero) {
    IndirectCountBuilder builder;
    builder.Init(nullptr);

    EXPECT_EQ(builder.GetMaxDrawCount(999), 0u);

    builder.Shutdown();
}
