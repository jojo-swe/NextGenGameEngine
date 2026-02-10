#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_viewport_scissor_manager.h"

using namespace nge::rhi;

TEST(ViewportScissorManager, InitAndShutdown) {
    ViewportScissorManager mgr;
    EXPECT_TRUE(mgr.Init());
    EXPECT_EQ(mgr.GetStackDepth(), 0u);
    mgr.Shutdown();
}

TEST(ViewportScissorManager, SetViewport) {
    ViewportScissorManager mgr;
    mgr.Init();

    Viewport vp{0, 0, 1920, 1080, 0.0f, 1.0f};
    mgr.SetViewport(vp);

    auto result = mgr.GetViewport();
    EXPECT_EQ(result.width, 1920.0f);
    EXPECT_EQ(result.height, 1080.0f);

    mgr.Shutdown();
}

TEST(ViewportScissorManager, SetScissor) {
    ViewportScissorManager mgr;
    mgr.Init();

    ScissorRect sc{0, 0, 1920, 1080};
    mgr.SetScissor(sc);

    auto result = mgr.GetScissor();
    EXPECT_EQ(result.width, 1920u);
    EXPECT_EQ(result.height, 1080u);

    mgr.Shutdown();
}

TEST(ViewportScissorManager, PushPop) {
    ViewportScissorManager mgr;
    mgr.Init();

    Viewport vp1{0, 0, 1920, 1080, 0.0f, 1.0f};
    mgr.SetViewport(vp1);

    mgr.Push();
    EXPECT_EQ(mgr.GetStackDepth(), 1u);

    Viewport vp2{0, 0, 512, 512, 0.0f, 1.0f};
    mgr.SetViewport(vp2);

    auto current = mgr.GetViewport();
    EXPECT_EQ(current.width, 512.0f);

    mgr.Pop();
    EXPECT_EQ(mgr.GetStackDepth(), 0u);

    auto restored = mgr.GetViewport();
    EXPECT_EQ(restored.width, 1920.0f);

    mgr.Shutdown();
}

TEST(ViewportScissorManager, StackOverflow) {
    ViewportScissorManager mgr;
    ViewportScissorConfig config;
    config.maxStackDepth = 2;
    mgr.Init(config);

    EXPECT_TRUE(mgr.Push());
    EXPECT_TRUE(mgr.Push());
    EXPECT_FALSE(mgr.Push()); // Overflow

    mgr.Shutdown();
}

TEST(ViewportScissorManager, StackUnderflow) {
    ViewportScissorManager mgr;
    mgr.Init();

    EXPECT_FALSE(mgr.Pop()); // Nothing to pop

    mgr.Shutdown();
}

TEST(ViewportScissorManager, ValidateViewportSuccess) {
    ViewportScissorManager mgr;
    mgr.Init();

    mgr.SetViewport({0, 0, 1920, 1080, 0.0f, 1.0f});
    EXPECT_TRUE(mgr.ValidateViewport());

    mgr.Shutdown();
}

TEST(ViewportScissorManager, ValidateViewportFailZeroSize) {
    ViewportScissorManager mgr;
    mgr.Init();

    mgr.SetViewport({0, 0, 0, 0, 0.0f, 1.0f});
    EXPECT_FALSE(mgr.ValidateViewport());

    mgr.Shutdown();
}

TEST(ViewportScissorManager, ValidateViewportFailDepthRange) {
    ViewportScissorManager mgr;
    mgr.Init();

    mgr.SetViewport({0, 0, 100, 100, 0.8f, 0.2f}); // min > max
    EXPECT_FALSE(mgr.ValidateViewport());

    mgr.Shutdown();
}

TEST(ViewportScissorManager, ValidateScissorSuccess) {
    ViewportScissorManager mgr;
    mgr.Init();

    mgr.SetScissor({0, 0, 1920, 1080});
    EXPECT_TRUE(mgr.ValidateScissor());

    mgr.Shutdown();
}

TEST(ViewportScissorManager, ValidateScissorFailZero) {
    ViewportScissorManager mgr;
    mgr.Init();

    mgr.SetScissor({0, 0, 0, 0});
    EXPECT_FALSE(mgr.ValidateScissor());

    mgr.Shutdown();
}

TEST(ViewportScissorManager, SetFullscreen) {
    ViewportScissorManager mgr;
    mgr.Init();

    mgr.SetFullscreen(1920, 1080);

    auto vp = mgr.GetViewport();
    EXPECT_EQ(vp.x, 0.0f);
    EXPECT_EQ(vp.y, 0.0f);
    EXPECT_EQ(vp.width, 1920.0f);
    EXPECT_EQ(vp.height, 1080.0f);

    auto sc = mgr.GetScissor();
    EXPECT_EQ(sc.x, 0);
    EXPECT_EQ(sc.y, 0);
    EXPECT_EQ(sc.width, 1920u);
    EXPECT_EQ(sc.height, 1080u);

    mgr.Shutdown();
}

TEST(ViewportScissorManager, MultiViewport) {
    ViewportScissorManager mgr;
    mgr.Init();

    mgr.SetViewport({0, 0, 960, 1080, 0, 1}, 0);
    mgr.SetViewport({960, 0, 960, 1080, 0, 1}, 1);

    auto vp0 = mgr.GetViewport(0);
    auto vp1 = mgr.GetViewport(1);

    EXPECT_EQ(vp0.x, 0.0f);
    EXPECT_EQ(vp1.x, 960.0f);
    EXPECT_EQ(mgr.GetActiveViewportCount(), 2u);

    mgr.Shutdown();
}

TEST(ViewportScissorManager, RedundantViewportDetection) {
    ViewportScissorManager mgr;
    ViewportScissorConfig config;
    config.trackRedundantSets = true;
    mgr.Init(config);

    Viewport vp{0, 0, 1920, 1080, 0, 1};
    mgr.SetViewport(vp);
    mgr.SetViewport(vp); // Redundant

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.viewportRedundant, 1u);
    EXPECT_EQ(stats.viewportSets, 2u);

    mgr.Shutdown();
}

TEST(ViewportScissorManager, RedundantScissorDetection) {
    ViewportScissorManager mgr;
    ViewportScissorConfig config;
    config.trackRedundantSets = true;
    mgr.Init(config);

    ScissorRect sc{0, 0, 1920, 1080};
    mgr.SetScissor(sc);
    mgr.SetScissor(sc); // Redundant

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.scissorRedundant, 1u);

    mgr.Shutdown();
}

TEST(ViewportScissorManager, NestedPushPop) {
    ViewportScissorManager mgr;
    mgr.Init();

    mgr.SetFullscreen(1920, 1080);
    mgr.Push();

    mgr.SetFullscreen(512, 512); // Shadow map
    mgr.Push();

    mgr.SetFullscreen(256, 256); // Reflection
    EXPECT_EQ(mgr.GetViewport().width, 256.0f);

    mgr.Pop();
    EXPECT_EQ(mgr.GetViewport().width, 512.0f);

    mgr.Pop();
    EXPECT_EQ(mgr.GetViewport().width, 1920.0f);

    mgr.Shutdown();
}

TEST(ViewportScissorManager, StatsTracking) {
    ViewportScissorManager mgr;
    mgr.Init();

    mgr.SetFullscreen(1920, 1080);
    mgr.Push();
    mgr.SetFullscreen(512, 512);
    mgr.Pop();

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.viewportSets, 2u);
    EXPECT_EQ(stats.scissorSets, 2u);
    EXPECT_EQ(stats.pushCount, 1u);
    EXPECT_EQ(stats.popCount, 1u);
    EXPECT_EQ(stats.currentStackDepth, 0u);

    mgr.Shutdown();
}

TEST(ViewportScissorManager, ResetFrameCounters) {
    ViewportScissorManager mgr;
    mgr.Init();

    mgr.SetFullscreen(1920, 1080);

    mgr.ResetFrameCounters();

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.viewportSets, 0u);
    EXPECT_EQ(stats.scissorSets, 0u);

    mgr.Shutdown();
}

TEST(ViewportScissorManager, ResetClearsAll) {
    ViewportScissorManager mgr;
    mgr.Init();

    mgr.SetFullscreen(1920, 1080);
    mgr.Push();

    mgr.Reset();

    EXPECT_EQ(mgr.GetStackDepth(), 0u);
    EXPECT_EQ(mgr.GetActiveViewportCount(), 0u);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.pushCount, 0u);

    mgr.Shutdown();
}

TEST(ViewportScissorManager, ValidationDisabled) {
    ViewportScissorManager mgr;
    ViewportScissorConfig config;
    config.validateDimensions = false;
    mgr.Init(config);

    mgr.SetViewport({0, 0, 0, 0, 0, 1}); // Invalid but validation disabled
    EXPECT_TRUE(mgr.ValidateViewport());

    mgr.Shutdown();
}
