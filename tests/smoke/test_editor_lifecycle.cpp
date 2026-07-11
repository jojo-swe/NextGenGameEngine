#include <gtest/gtest.h>

#include "engine/core/app/application.h"
#include "engine/core/platform/window.h"
#include "engine/rhi/common/rhi_device.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace nge;
using namespace nge::platform;
using namespace nge::rhi;

// ─── Window lifecycle smoke tests ──────────────────────────────────────────

TEST(WindowLifecycle, CreateAndDestroy) {
    WindowDesc desc;
    desc.title  = "Test Window";
    desc.width  = 320;
    desc.height = 240;

    auto window = Window::Create(desc);
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(window->GetWidth(), 320u);
    EXPECT_EQ(window->GetHeight(), 240u);
    EXPECT_NE(window->GetNativeHandle(), nullptr);
    EXPECT_FALSE(window->ShouldClose());
}

TEST(WindowLifecycle, SetShouldClose) {
    WindowDesc desc;
    desc.title  = "Test Close";
    desc.width  = 320;
    desc.height = 240;

    auto window = Window::Create(desc);
    ASSERT_NE(window, nullptr);
    EXPECT_FALSE(window->ShouldClose());

    window->SetShouldClose(true);
    EXPECT_TRUE(window->ShouldClose());
}

TEST(WindowLifecycle, SetTitle) {
    WindowDesc desc;
    desc.title  = "Original Title";
    desc.width  = 320;
    desc.height = 240;

    auto window = Window::Create(desc);
    ASSERT_NE(window, nullptr);

    // Should not crash
    window->SetTitle("New Title");
    SUCCEED();
}

TEST(WindowLifecycle, PollEventsNoCrash) {
    WindowDesc desc;
    desc.title  = "Poll Test";
    desc.width  = 320;
    desc.height = 240;

    auto window = Window::Create(desc);
    ASSERT_NE(window, nullptr);

    // Polling events should not crash even with no interaction
    for (int i = 0; i < 10; ++i) {
        window->PollEvents();
    }
    SUCCEED();
}

TEST(WindowLifecycle, EventCallback) {
    WindowDesc desc;
    desc.title  = "Callback Test";
    desc.width  = 320;
    desc.height = 240;

    auto window = Window::Create(desc);
    ASSERT_NE(window, nullptr);

    std::atomic<bool> callbackCalled{false};
    window->SetEventCallback([&callbackCalled](void*, u32, u64, i64) -> bool {
        callbackCalled = true;
        return false;
    });

    // Poll events to trigger the callback if there are any messages
    window->PollEvents();
    // The callback may or may not be called depending on pending messages,
    // but setting it should not crash.
    SUCCEED();
}

// ─── RHI device creation smoke test (requires Vulkan) ──────────────────────

TEST(RHISmoke, CreateDevice) {
    WindowDesc desc;
    desc.title  = "RHI Test";
    desc.width  = 320;
    desc.height = 240;

    auto window = Window::Create(desc);
    ASSERT_NE(window, nullptr);

    auto device = IDevice::Create(GraphicsAPI::Vulkan);
    if (!device) {
        GTEST_SKIP() << "Vulkan not available, skipping RHI smoke test";
    }

    ASSERT_TRUE(device->Init(
        window->GetNativeHandle(),
        window->GetInstanceHandle(),
        320, 240));

    EXPECT_GT(device->GetSwapchainWidth(), 0u);
    EXPECT_GT(device->GetSwapchainHeight(), 0u);

    device->WaitIdle();
    device->Shutdown();
}

// ─── Application init/shutdown smoke test ──────────────────────────────────

namespace {

class SmokeApp : public Application {
public:
    SmokeApp() : Application({.title = "Smoke App", .width = 320, .height = 240}) {}

    std::atomic<int> initCount{0};
    std::atomic<int> updateCount{0};
    std::atomic<int> shutdownCount{0};

    void OnInit() override { initCount++; }
    void OnUpdate(f32) override {
        updateCount++;
        if (updateCount > 3) {
            m_window->SetShouldClose(true);
        }
    }
    void OnShutdown() override { shutdownCount++; }
};

} // namespace

TEST(AppSmoke, InitUpdateShutdown) {
    SmokeApp app;

    EXPECT_EQ(app.Run(), 0);

    // After Run() completes, OnInit should have been called once,
    // OnUpdate at least 4 times (before setting should close),
    // and OnShutdown once.
    EXPECT_EQ(app.initCount.load(), 1);
    EXPECT_GE(app.updateCount.load(), 4);
    EXPECT_EQ(app.shutdownCount.load(), 1);
}
