#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_transient_attachment_allocator.h"

using namespace nge;
using namespace nge::rhi;

static TransientAttachmentDesc MakeDesc(u32 w, u32 h, AttachmentFormat fmt = AttachmentFormat::RGBA8_UNORM,
                                         u32 samples = 1, const std::string& name = "Test") {
    TransientAttachmentDesc desc;
    desc.width = w;
    desc.height = h;
    desc.format = fmt;
    desc.samples = samples;
    desc.arrayLayers = 1;
    desc.debugName = name;
    return desc;
}

TEST(TransientAttachmentAllocator, InitAndShutdown) {
    TransientAttachmentAllocator alloc;
    EXPECT_TRUE(alloc.Init());

    auto stats = alloc.GetStats();
    EXPECT_EQ(stats.totalAllocations, 0u);
    EXPECT_EQ(stats.totalMemoryUsed, 0u);

    alloc.Shutdown();
}

TEST(TransientAttachmentAllocator, BasicAllocation) {
    TransientAttachmentAllocator alloc;
    alloc.Init();

    auto handle = alloc.Allocate(MakeDesc(1920, 1080, AttachmentFormat::RGBA8_UNORM), 0, 2);
    EXPECT_NE(handle.id, 0u);
    EXPECT_NE(handle.memoryBlock, 0u);
    EXPECT_NE(handle.imageHandle, 0u);

    auto stats = alloc.GetStats();
    EXPECT_EQ(stats.totalAllocations, 1u);
    EXPECT_GT(stats.totalMemoryUsed, 0u);

    alloc.Shutdown();
}

TEST(TransientAttachmentAllocator, MultipleAllocations) {
    TransientAttachmentAllocator alloc;
    alloc.Init();

    auto h1 = alloc.Allocate(MakeDesc(1920, 1080, AttachmentFormat::RGBA8_UNORM, 1, "Color"), 0, 1);
    auto h2 = alloc.Allocate(MakeDesc(1920, 1080, AttachmentFormat::D32_FLOAT, 1, "Depth"), 0, 1);
    auto h3 = alloc.Allocate(MakeDesc(1920, 1080, AttachmentFormat::RGBA16_FLOAT, 1, "HDR"), 0, 1);

    EXPECT_NE(h1.id, h2.id);
    EXPECT_NE(h2.id, h3.id);

    auto stats = alloc.GetStats();
    EXPECT_EQ(stats.totalAllocations, 3u);
    EXPECT_EQ(stats.activeAllocations, 3u);

    alloc.Shutdown();
}

TEST(TransientAttachmentAllocator, AliasingNonOverlapping) {
    TransientAttachmentAllocator alloc;
    TransientAllocatorConfig config;
    config.enableAliasing = true;
    alloc.Init(config);

    // Pass 0-1: color attachment
    auto h1 = alloc.Allocate(MakeDesc(1920, 1080), 0, 1);
    alloc.Release(h1.id);

    // Pass 2-3: another color attachment (non-overlapping, should alias)
    auto h2 = alloc.Allocate(MakeDesc(1920, 1080), 2, 3);

    auto stats = alloc.GetStats();
    EXPECT_EQ(stats.aliasedAllocations, 1u);
    EXPECT_GT(stats.memorySaved, 0u);

    alloc.Shutdown();
}

TEST(TransientAttachmentAllocator, NoAliasingOverlapping) {
    TransientAttachmentAllocator alloc;
    TransientAllocatorConfig config;
    config.enableAliasing = true;
    alloc.Init(config);

    // Pass 0-2
    auto h1 = alloc.Allocate(MakeDesc(1920, 1080), 0, 2);
    // Pass 1-3 (overlaps with h1)
    auto h2 = alloc.Allocate(MakeDesc(1920, 1080), 1, 3);

    auto stats = alloc.GetStats();
    EXPECT_EQ(stats.aliasedAllocations, 0u); // No aliasing possible

    alloc.Shutdown();
}

TEST(TransientAttachmentAllocator, AliasingDisabled) {
    TransientAttachmentAllocator alloc;
    TransientAllocatorConfig config;
    config.enableAliasing = false;
    alloc.Init(config);

    auto h1 = alloc.Allocate(MakeDesc(1920, 1080), 0, 1);
    alloc.Release(h1.id);
    auto h2 = alloc.Allocate(MakeDesc(1920, 1080), 2, 3);

    auto stats = alloc.GetStats();
    EXPECT_EQ(stats.aliasedAllocations, 0u); // Disabled

    alloc.Shutdown();
}

TEST(TransientAttachmentAllocator, CanAliasQuery) {
    TransientAttachmentAllocator alloc;
    alloc.Init();

    auto h1 = alloc.Allocate(MakeDesc(1920, 1080), 0, 1);
    auto h2 = alloc.Allocate(MakeDesc(1920, 1080), 2, 3);
    auto h3 = alloc.Allocate(MakeDesc(1920, 1080), 0, 3);

    EXPECT_TRUE(alloc.CanAlias(h1.id, h2.id));   // Non-overlapping
    EXPECT_FALSE(alloc.CanAlias(h1.id, h3.id));   // Overlapping
    EXPECT_FALSE(alloc.CanAlias(h2.id, h3.id));   // Overlapping
    EXPECT_FALSE(alloc.CanAlias(999, h1.id));      // Non-existent

    alloc.Shutdown();
}

TEST(TransientAttachmentAllocator, Release) {
    TransientAttachmentAllocator alloc;
    alloc.Init();

    auto h = alloc.Allocate(MakeDesc(1920, 1080), 0, 2);
    EXPECT_EQ(alloc.GetStats().activeAllocations, 1u);

    alloc.Release(h.id);
    EXPECT_EQ(alloc.GetStats().activeAllocations, 0u);

    alloc.Shutdown();
}

TEST(TransientAttachmentAllocator, BeginFrameRecycles) {
    TransientAttachmentAllocator alloc;
    alloc.Init();

    alloc.Allocate(MakeDesc(1920, 1080), 0, 2);
    alloc.Allocate(MakeDesc(1920, 1080), 0, 1);
    EXPECT_EQ(alloc.GetStats().activeAllocations, 2u);

    alloc.BeginFrame();
    EXPECT_EQ(alloc.GetStats().activeAllocations, 0u);
    EXPECT_EQ(alloc.GetMemoryUsed(), 0u);
    EXPECT_GE(alloc.GetStats().totalRecycled, 2u);

    alloc.Shutdown();
}

TEST(TransientAttachmentAllocator, GetSlot) {
    TransientAttachmentAllocator alloc;
    alloc.Init();

    auto h = alloc.Allocate(MakeDesc(1920, 1080, AttachmentFormat::RGBA16_FLOAT, 1, "HDR"), 0, 3);

    const auto* slot = alloc.GetSlot(h.id);
    EXPECT_NE(slot, nullptr);
    EXPECT_EQ(slot->desc.width, 1920u);
    EXPECT_EQ(slot->desc.height, 1080u);
    EXPECT_EQ(slot->desc.format, AttachmentFormat::RGBA16_FLOAT);
    EXPECT_EQ(slot->desc.debugName, "HDR");
    EXPECT_EQ(slot->firstPassUsed, 0u);
    EXPECT_EQ(slot->lastPassUsed, 3u);
    EXPECT_TRUE(slot->inUse);
    EXPECT_GT(slot->memorySizeBytes, 0u);

    EXPECT_EQ(alloc.GetSlot(999), nullptr);

    alloc.Shutdown();
}

TEST(TransientAttachmentAllocator, FormatSizeVariation) {
    TransientAttachmentAllocator alloc;
    alloc.Init();

    auto h8 = alloc.Allocate(MakeDesc(256, 256, AttachmentFormat::RGBA8_UNORM), 0, 0);
    u64 mem8 = alloc.GetMemoryUsed();

    alloc.BeginFrame();

    auto h16 = alloc.Allocate(MakeDesc(256, 256, AttachmentFormat::RGBA16_FLOAT), 0, 0);
    u64 mem16 = alloc.GetMemoryUsed();

    alloc.BeginFrame();

    auto h32 = alloc.Allocate(MakeDesc(256, 256, AttachmentFormat::RGBA32_FLOAT), 0, 0);
    u64 mem32 = alloc.GetMemoryUsed();

    // RGBA8=4B, RGBA16F=8B, RGBA32F=16B per pixel
    EXPECT_LT(mem8, mem16);
    EXPECT_LT(mem16, mem32);

    alloc.Shutdown();
}

TEST(TransientAttachmentAllocator, MSAASampleCount) {
    TransientAttachmentAllocator alloc;
    alloc.Init();

    auto h1x = alloc.Allocate(MakeDesc(256, 256, AttachmentFormat::RGBA8_UNORM, 1), 0, 0);
    u64 mem1 = alloc.GetMemoryUsed();

    alloc.BeginFrame();

    auto h4x = alloc.Allocate(MakeDesc(256, 256, AttachmentFormat::RGBA8_UNORM, 4), 0, 0);
    u64 mem4 = alloc.GetMemoryUsed();

    // 4x MSAA should use ~4x memory
    EXPECT_GT(mem4, mem1);

    alloc.Shutdown();
}

TEST(TransientAttachmentAllocator, PeakActiveCount) {
    TransientAttachmentAllocator alloc;
    alloc.Init();

    auto h1 = alloc.Allocate(MakeDesc(256, 256), 0, 0);
    auto h2 = alloc.Allocate(MakeDesc(256, 256), 0, 0);
    auto h3 = alloc.Allocate(MakeDesc(256, 256), 0, 0);

    alloc.Release(h1.id);
    alloc.Release(h2.id);

    // Peak should still be 3
    EXPECT_EQ(alloc.GetStats().peakActiveCount, 3u);

    alloc.Shutdown();
}

TEST(TransientAttachmentAllocator, ResetClearsAll) {
    TransientAttachmentAllocator alloc;
    alloc.Init();

    alloc.Allocate(MakeDesc(1920, 1080), 0, 2);
    alloc.Allocate(MakeDesc(1920, 1080), 0, 1);

    alloc.Reset();

    auto stats = alloc.GetStats();
    EXPECT_EQ(stats.totalAllocations, 0u);
    EXPECT_EQ(stats.activeAllocations, 0u);
    EXPECT_EQ(stats.totalMemoryUsed, 0u);
    EXPECT_EQ(stats.peakActiveCount, 0u);
    EXPECT_EQ(stats.aliasedAllocations, 0u);

    alloc.Shutdown();
}

TEST(TransientAttachmentAllocator, MemorySavedTracking) {
    TransientAttachmentAllocator alloc;
    TransientAllocatorConfig config;
    config.enableAliasing = true;
    alloc.Init(config);

    // Allocate and release, then allocate again (should alias)
    auto h1 = alloc.Allocate(MakeDesc(512, 512), 0, 0);
    alloc.Release(h1.id);
    auto h2 = alloc.Allocate(MakeDesc(512, 512), 1, 1);

    auto stats = alloc.GetStats();
    EXPECT_GT(stats.memoryWithoutAliasing, stats.totalMemoryUsed);
    EXPECT_GT(stats.memorySaved, 0u);

    alloc.Shutdown();
}
