#pragma once

#include "engine/core/types.h"
#include "engine/core/ecs/world.h"
#include "engine/core/platform/window.h"
#include "engine/core/platform/clock.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/renderer/scene_renderer.h"
#include <string>
#include <functional>

namespace nge {

// ─── Application ─────────────────────────────────────────────────────────
// Engine bootstrap: initializes all subsystems, runs the main loop,
// and shuts everything down in the correct order.
//
// Usage:
//   Application app;
//   app.Init({.title = "MyGame", .width = 1920, .height = 1080});
//   app.SetUpdateCallback([](f32 dt) { /* game logic */ });
//   app.Run();

struct ApplicationConfig {
    std::string title = "NextGenGameEngine";
    u32         width = 1920;
    u32         height = 1080;
    bool        fullscreen = false;
    bool        vsync = true;
    bool        enableEditor = true;
    bool        enablePathTracing = false;
    bool        enableAsyncCompute = true;
    bool        enableValidation = true;    // Vulkan validation layers
    u32         framesInFlight = 3;
    f32         renderScale = 1.0f;
};

class Application {
public:
    Application() = default;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool Init(const ApplicationConfig& config = {});
    void Shutdown();

    // Main loop — blocks until window is closed
    void Run();

    // Request quit
    void Quit() { m_running = false; }

    // Callbacks
    using UpdateFunc = std::function<void(f32 deltaTime)>;
    using RenderFunc = std::function<void(renderer::SceneRenderer&)>;
    using ResizeFunc = std::function<void(u32 width, u32 height)>;

    void SetUpdateCallback(UpdateFunc func) { m_updateFunc = std::move(func); }
    void SetRenderCallback(RenderFunc func) { m_renderFunc = std::move(func); }
    void SetResizeCallback(ResizeFunc func) { m_resizeFunc = std::move(func); }

    // Access subsystems
    ecs::World& GetWorld() { return m_world; }
    rhi::IDevice* GetDevice() { return m_device; }
    renderer::SceneRenderer& GetRenderer() { return m_renderer; }
    platform::Window& GetWindow() { return m_window; }

    // Stats
    f32 GetDeltaTime() const { return m_deltaTime; }
    f32 GetFPS() const { return m_fps; }
    u64 GetFrameCount() const { return m_frameCount; }
    bool IsRunning() const { return m_running; }

private:
    void InitSubsystems();
    void ShutdownSubsystems();
    void ProcessEvents();
    void Update(f32 dt);
    void Render();
    void OnResize(u32 width, u32 height);

    ApplicationConfig m_config;

    // Core systems
    platform::Window   m_window;
    platform::Clock    m_clock;
    rhi::IDevice*      m_device = nullptr;
    ecs::World         m_world;
    renderer::SceneRenderer m_renderer;

    // Callbacks
    UpdateFunc m_updateFunc;
    RenderFunc m_renderFunc;
    ResizeFunc m_resizeFunc;

    // Frame timing
    f32 m_deltaTime = 0;
    f32 m_fps = 0;
    f32 m_fpsAccum = 0;
    u32 m_fpsFrameCount = 0;
    f32 m_fpsTimer = 0;
    u64 m_frameCount = 0;

    bool m_running = false;
    bool m_minimized = false;
};

} // namespace nge
