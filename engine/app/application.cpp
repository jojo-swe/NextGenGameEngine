#include "engine/app/application.h"
#include "engine/core/logging/log.h"

namespace nge {

Application::~Application() {
    if (m_running) {
        Shutdown();
    }
}

bool Application::Init(const ApplicationConfig& config) {
    m_config = config;

    NGE_LOG_INFO("Initializing NextGenGameEngine...");
    NGE_LOG_INFO("  Title: {}", config.title);
    NGE_LOG_INFO("  Resolution: {}x{} (scale: {})", config.width, config.height, config.renderScale);

    // Create window
    platform::WindowDesc windowDesc;
    windowDesc.title = config.title;
    windowDesc.width = config.width;
    windowDesc.height = config.height;
    windowDesc.fullscreen = config.fullscreen;
    if (!m_window.Init(windowDesc)) {
        NGE_LOG_ERROR("Failed to create window");
        return false;
    }

    m_window.SetResizeCallback([this](u32 w, u32 h) { OnResize(w, h); });

    // Create RHI device (Vulkan 1.3)
    rhi::DeviceDesc deviceDesc;
    deviceDesc.enableValidation = config.enableValidation;
    deviceDesc.windowHandle = m_window.GetNativeHandle();
    m_device = rhi::CreateDevice(deviceDesc);
    if (!m_device) {
        NGE_LOG_ERROR("Failed to create RHI device");
        return false;
    }

    // Initialize subsystems
    InitSubsystems();

    m_clock.Reset();
    m_running = true;

    NGE_LOG_INFO("Engine initialized successfully");
    return true;
}

void Application::Shutdown() {
    NGE_LOG_INFO("Shutting down engine...");
    m_running = false;

    ShutdownSubsystems();

    if (m_device) {
        rhi::DestroyDevice(m_device);
        m_device = nullptr;
    }

    m_window.Shutdown();
    NGE_LOG_INFO("Engine shutdown complete");
}

void Application::InitSubsystems() {
    // ECS world
    m_world.Init();

    // Scene renderer
    renderer::SceneRendererConfig rendererConfig;
    rendererConfig.windowWidth = m_config.width;
    rendererConfig.windowHeight = m_config.height;
    rendererConfig.renderScale = m_config.renderScale;
    rendererConfig.framesInFlight = m_config.framesInFlight;
    rendererConfig.enablePathTracing = m_config.enablePathTracing;
    rendererConfig.enableAsyncCompute = m_config.enableAsyncCompute;
    rendererConfig.vsync = m_config.vsync;
    m_renderer.Init(m_device, rendererConfig);
}

void Application::ShutdownSubsystems() {
    m_renderer.Shutdown();
    m_world.Shutdown();
}

void Application::Run() {
    NGE_LOG_INFO("Entering main loop");

    while (m_running) {
        m_clock.Tick();
        m_deltaTime = m_clock.GetDeltaTime();

        // FPS calculation
        m_fpsTimer += m_deltaTime;
        m_fpsFrameCount++;
        if (m_fpsTimer >= 1.0f) {
            m_fps = static_cast<f32>(m_fpsFrameCount) / m_fpsTimer;
            m_fpsTimer = 0;
            m_fpsFrameCount = 0;
        }

        ProcessEvents();

        if (!m_minimized) {
            Update(m_deltaTime);
            Render();
        }

        m_frameCount++;
    }

    NGE_LOG_INFO("Exiting main loop (frames: {}, avg FPS: {:.1f})",
                 m_frameCount, m_frameCount > 0 ? static_cast<f32>(m_frameCount) / m_clock.GetTotalTime() : 0.0f);
}

void Application::ProcessEvents() {
    m_window.PollEvents();

    if (m_window.ShouldClose()) {
        m_running = false;
    }
}

void Application::Update(f32 dt) {
    // Game-specific update
    if (m_updateFunc) {
        m_updateFunc(dt);
    }

    // ECS tick
    m_world.Tick(dt);
}

void Application::Render() {
    // Game-specific pre-render callback
    if (m_renderFunc) {
        m_renderFunc(m_renderer);
    }

    // GPU render
    m_renderer.RenderFrame(m_world, m_deltaTime);

    // TODO: Present swapchain
    // m_presenter.AcquireNextImage();
    // m_presenter.BlitToSwapchain(cmd, m_renderer.GetCompositor().GetOutputTexture());
    // m_presenter.Present();
}

void Application::OnResize(u32 width, u32 height) {
    if (width == 0 || height == 0) {
        m_minimized = true;
        return;
    }

    m_minimized = false;
    m_config.width = width;
    m_config.height = height;

    m_renderer.OnResize(width, height);

    if (m_resizeFunc) {
        m_resizeFunc(width, height);
    }

    NGE_LOG_INFO("Window resized: {}x{}", width, height);
}

} // namespace nge
