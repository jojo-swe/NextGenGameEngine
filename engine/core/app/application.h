#pragma once

#include "engine/core/types.h"
#include "engine/core/platform/window.h"
#include "engine/core/platform/input.h"
#include "engine/core/platform/clock.h"
#include "engine/core/logging/log.h"
#include "engine/core/jobs/job_system.h"
#include "engine/core/ecs/world.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/renderer/pipeline/render_pipeline.h"
#include "engine/scene/camera/camera.h"
#include "engine/scene/transform/transform.h"
#include <memory>
#include <string>

namespace nge {

// ─── Application Configuration ───────────────────────────────────────────
struct AppConfig {
    std::string title       = "NextGen Engine";
    u32         width       = 1920;
    u32         height      = 1080;
    bool        fullscreen  = false;
    bool        vsync       = false;
    rhi::GraphicsAPI graphicsAPI = rhi::GraphicsAPI::Vulkan;
    u32         jobThreads  = 0; // 0 = auto
};

// ─── Application ─────────────────────────────────────────────────────────
// Main engine application loop. Creates and owns all subsystems.
// Users subclass this for their game and override OnInit/OnUpdate/OnShutdown.

class Application {
public:
    Application(const AppConfig& config = {});
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Run the main loop. Returns exit code.
    int Run();

    // Override these in your game class
    virtual void OnInit() {}
    virtual void OnUpdate(f32 deltaTime) {}
    virtual void OnRender() {}
    virtual void OnShutdown() {}

    // ─── Subsystem access ─────────────────────────────────────────────
    platform::Window*      GetWindow()    { return m_window.get(); }
    rhi::IDevice*          GetDevice()    { return m_device.get(); }
    renderer::RenderPipeline* GetRenderer() { return &m_renderPipeline; }
    ecs::World*            GetWorld()     { return &m_world; }
    const AppConfig&       GetConfig() const { return m_config; }

protected:
    AppConfig m_config;

    // Subsystems
    std::unique_ptr<platform::Window> m_window;
    std::unique_ptr<rhi::IDevice>     m_device;
    renderer::RenderPipeline          m_renderPipeline;
    ecs::World                        m_world;

    // Frame timing
    f32 m_deltaTime = 0;
    f32 m_totalTime = 0;
    u32 m_frameIndex = 0;

    // Active camera entity
    ecs::Entity m_cameraEntity;

private:
    bool InitSubsystems();
    void ShutdownSubsystems();
    void MainLoop();
    void BuildFrameRenderData(renderer::FrameRenderData& data);
};

} // namespace nge
