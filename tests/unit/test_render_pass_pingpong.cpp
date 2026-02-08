#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_render_pass_manager.h"
#include "engine/rhi/common/rhi_ping_pong_buffer.h"

using namespace nge::rhi;

// ─── Render Pass Manager Tests ───────────────────────────────────────────

TEST(RenderPassManager, InitNoMerge) {
    RenderPassManager mgr;
    RenderPassManagerConfig config;
    config.enableAutoMerge = false;
    EXPECT_TRUE(mgr.Init(nullptr, config));

    RenderPassDesc passA;
    passA.name = "GBuffer";
    passA.width = 1920;
    passA.height = 1080;
    passA.colorAttachments.push_back({1, 44, AttachmentLoadOp::Clear, AttachmentStoreOp::Store, false, "albedo"});

    mgr.SubmitPassSequence({passA});
    auto merged = mgr.GetMergedPasses();
    EXPECT_EQ(merged.size(), 1u);
    EXPECT_EQ(merged[0].subpasses.size(), 1u);

    mgr.Shutdown();
}

TEST(RenderPassManager, MergeCompatiblePasses) {
    RenderPassManager mgr;
    RenderPassManagerConfig config;
    config.enableAutoMerge = true;
    config.tileBasedGPU = true; // More aggressive merging
    mgr.Init(nullptr, config);

    PassAttachment sharedColor = {100, 44, AttachmentLoadOp::Clear, AttachmentStoreOp::Store, false, "color"};

    RenderPassDesc passA;
    passA.name = "GBuffer";
    passA.width = 1920;
    passA.height = 1080;
    passA.colorAttachments.push_back(sharedColor);

    RenderPassDesc passB;
    passB.name = "Lighting";
    passB.width = 1920;
    passB.height = 1080;
    passB.colorAttachments.push_back(sharedColor); // Same attachment = input attachment pattern

    mgr.SubmitPassSequence({passA, passB});
    auto merged = mgr.GetMergedPasses();

    // Should merge into 1 pass with 2 subpasses
    EXPECT_EQ(merged.size(), 1u);
    EXPECT_EQ(merged[0].subpasses.size(), 2u);
    EXPECT_FALSE(merged[0].dependencies.empty());

    mgr.Shutdown();
}

TEST(RenderPassManager, NoMergeResolutionMismatch) {
    RenderPassManager mgr;
    RenderPassManagerConfig config;
    config.enableAutoMerge = true;
    config.tileBasedGPU = true;
    mgr.Init(nullptr, config);

    RenderPassDesc passA;
    passA.name = "FullRes";
    passA.width = 1920;
    passA.height = 1080;
    passA.colorAttachments.push_back({1, 44, AttachmentLoadOp::Clear, AttachmentStoreOp::Store, false, "a"});

    RenderPassDesc passB;
    passB.name = "HalfRes";
    passB.width = 960;
    passB.height = 540;
    passB.colorAttachments.push_back({2, 44, AttachmentLoadOp::Clear, AttachmentStoreOp::Store, false, "b"});

    mgr.SubmitPassSequence({passA, passB});
    auto merged = mgr.GetMergedPasses();
    EXPECT_EQ(merged.size(), 2u); // Cannot merge different resolutions

    mgr.Shutdown();
}

TEST(RenderPassManager, AnalyzeMergeOpportunities) {
    RenderPassManager mgr;
    RenderPassManagerConfig config;
    config.enableAutoMerge = false;
    mgr.Init(nullptr, config);

    PassAttachment sharedDepth = {200, 24, AttachmentLoadOp::Clear, AttachmentStoreOp::Store, true, "depth"};

    RenderPassDesc passA;
    passA.name = "DepthPrepass";
    passA.width = 1920;
    passA.height = 1080;
    passA.hasDepth = true;
    passA.depthAttachment = sharedDepth;

    RenderPassDesc passB;
    passB.name = "GBuffer";
    passB.width = 1920;
    passB.height = 1080;
    passB.hasDepth = true;
    passB.depthAttachment = sharedDepth; // Same depth = merge opportunity

    mgr.SubmitPassSequence({passA, passB});
    auto opportunities = mgr.AnalyzeMergeOpportunities();
    EXPECT_EQ(opportunities.size(), 1u);
    EXPECT_TRUE(opportunities[0].canMerge);
    EXPECT_GT(opportunities[0].savingsEstimate, 0.0f);

    mgr.Shutdown();
}

TEST(RenderPassManager, StatsTracking) {
    RenderPassManager mgr;
    RenderPassManagerConfig config;
    config.enableAutoMerge = true;
    config.tileBasedGPU = true;
    mgr.Init(nullptr, config);

    PassAttachment shared = {100, 44, AttachmentLoadOp::Clear, AttachmentStoreOp::Store, false, "rt"};

    RenderPassDesc p1; p1.name = "A"; p1.width = 1920; p1.height = 1080;
    p1.colorAttachments.push_back(shared);
    RenderPassDesc p2; p2.name = "B"; p2.width = 1920; p2.height = 1080;
    p2.colorAttachments.push_back(shared);
    RenderPassDesc p3; p3.name = "C"; p3.width = 960; p3.height = 540;
    p3.colorAttachments.push_back({200, 44, AttachmentLoadOp::Clear, AttachmentStoreOp::Store, false, "rt2"});

    mgr.SubmitPassSequence({p1, p2, p3});
    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalPasses, 3u);
    EXPECT_LT(stats.mergedPasses, 3u); // At least p1+p2 should merge
    EXPECT_GT(stats.estimatedBandwidthSavings, 0.0f);

    mgr.Shutdown();
}

TEST(RenderPassManager, ClearResetsState) {
    RenderPassManager mgr;
    mgr.Init(nullptr);

    RenderPassDesc pass;
    pass.name = "Test";
    pass.width = 1920;
    pass.height = 1080;
    mgr.SubmitPassSequence({pass});

    EXPECT_EQ(mgr.GetMergedPasses().size(), 1u);
    mgr.Clear();
    EXPECT_EQ(mgr.GetMergedPasses().size(), 0u);

    mgr.Shutdown();
}

// ─── Ping-Pong Buffer Manager Tests ─────────────────────────────────────

TEST(PingPongBuffer, CreateBufferSet) {
    PingPongBufferManager mgr;
    EXPECT_TRUE(mgr.Init(nullptr));

    PingPongBufferDesc desc;
    desc.sizeBytes = 65536;
    desc.mode = PingPongMode::Triple;
    desc.debugName = "PerFrameUBO";

    u32 id = mgr.CreateBufferSet(desc);
    EXPECT_EQ(id, 0u);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalPingPongSets, 1u);
    EXPECT_EQ(stats.totalBuffers, 3u);
    EXPECT_EQ(stats.totalMemoryBytes, 65536u * 3);

    mgr.Shutdown();
}

TEST(PingPongBuffer, CreateTextureSet) {
    PingPongBufferManager mgr;
    mgr.Init(nullptr);

    PingPongTextureDesc desc;
    desc.width = 1920;
    desc.height = 1080;
    desc.format = 44;
    desc.mode = PingPongMode::Double;
    desc.debugName = "ParticleSim";

    u32 id = mgr.CreateTextureSet(desc);
    EXPECT_EQ(id, 0u);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalPingPongSets, 1u);
    EXPECT_EQ(stats.totalBuffers, 2u);

    mgr.Shutdown();
}

TEST(PingPongBuffer, AdvanceRotatesIndex) {
    PingPongBufferManager mgr;
    mgr.Init(nullptr);

    PingPongBufferDesc desc;
    desc.sizeBytes = 1024;
    desc.mode = PingPongMode::Triple;
    desc.debugName = "Test";
    u32 id = mgr.CreateBufferSet(desc);

    EXPECT_EQ(mgr.GetCurrentIndex(), 0u);
    mgr.Advance(1);
    EXPECT_EQ(mgr.GetCurrentIndex(), 1u);
    mgr.Advance(2);
    EXPECT_EQ(mgr.GetCurrentIndex(), 2u);
    mgr.Advance(3);
    EXPECT_EQ(mgr.GetCurrentIndex(), 3u);

    // Previous should wrap properly
    u32 prev = mgr.GetPreviousIndex(id);
    EXPECT_EQ(prev, (3 + 3 - 1) % 3); // = 2

    mgr.Shutdown();
}

TEST(PingPongBuffer, DoubleBufferRotation) {
    PingPongBufferManager mgr;
    mgr.Init(nullptr);

    PingPongBufferDesc desc;
    desc.sizeBytes = 512;
    desc.mode = PingPongMode::Double;
    desc.debugName = "DoubleTest";
    u32 id = mgr.CreateBufferSet(desc);

    // Frame 0: write=0, read=1
    // Frame 1: write=1, read=0
    u32 prev0 = mgr.GetPreviousIndex(id);
    EXPECT_EQ(prev0, 1u); // (0 + 2 - 1) % 2 = 1

    mgr.Advance(1);
    u32 prev1 = mgr.GetPreviousIndex(id);
    EXPECT_EQ(prev1, 0u); // (1 + 2 - 1) % 2 = 0

    mgr.Shutdown();
}

TEST(PingPongBuffer, DestroySet) {
    PingPongBufferManager mgr;
    mgr.Init(nullptr);

    PingPongBufferDesc desc;
    desc.sizeBytes = 1024;
    desc.mode = PingPongMode::Double;
    desc.debugName = "Temp";
    u32 id = mgr.CreateBufferSet(desc);

    EXPECT_EQ(mgr.GetStats().totalPingPongSets, 1u);
    mgr.DestroySet(id);
    EXPECT_EQ(mgr.GetStats().totalPingPongSets, 0u);

    mgr.Shutdown();
}
