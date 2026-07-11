#include "engine/core/app/application.h"
#include "engine/core/profiling/profiler.h"

#include <cstdio>

static void AppTraceLog(const char* msg) {
    FILE* f = nullptr;
    fopen_s(&f, "editor_debug_trace.log", "a");
    if (f) {
        fprintf(f, "%s", msg);
        fflush(f);
        fclose(f);
    }
}

namespace nge {

Application::Application(const AppConfig& config)
    : m_config(config)
{
}

Application::~Application() = default;

int Application::Run() {
    if (!InitSubsystems()) return -1;

    OnInit();
    MainLoop();
    OnShutdown();

    ShutdownSubsystems();
    return 0;
}

bool Application::InitSubsystems() {
    // ─── Logging ──────────────────────────────────────────────────────
    AppTraceLog("[APP] Log::Init()\n");
    Log::Init();
    NGE_LOG_INFO("=== NextGen Engine v{}.{}.{} ===", 0, 1, 0);

    // ─── Clock ────────────────────────────────────────────────────────
    platform::Clock::Init();

    // ─── Job System ───────────────────────────────────────────────────
    jobs::JobSystem::Init(m_config.jobThreads);

    // ─── Window ───────────────────────────────────────────────────────
    platform::WindowDesc windowDesc;
    windowDesc.title  = m_config.title;
    windowDesc.width  = m_config.width;
    windowDesc.height = m_config.height;
    AppTraceLog("[APP] creating window\n");
    m_window = platform::Window::Create(windowDesc);
    if (!m_window) {
        NGE_LOG_ERROR("Failed to create window");
        return false;
    }

    // ─── RHI Device ───────────────────────────────────────────────────
    AppTraceLog("[APP] creating RHI device\n");
    m_device = rhi::IDevice::Create(m_config.graphicsAPI);
    if (!m_device) {
        NGE_LOG_ERROR("Failed to create RHI device");
        return false;
    }

    if (!m_device->Init(
            m_window->GetNativeHandle(),
            m_window->GetInstanceHandle(),
            m_config.width, m_config.height))
    {
        NGE_LOG_ERROR("Failed to initialize RHI device");
        return false;
    }

    // ─── Render Pipeline ──────────────────────────────────────────────
    AppTraceLog("[APP] init render pipeline\n");
    if (!m_renderPipeline.Init(m_device.get(), m_config.width, m_config.height)) {
        NGE_LOG_ERROR("Failed to initialize render pipeline");
        return false;
    }

    // ─── ECS: Register core components ────────────────────────────────
    m_world.RegisterComponent<scene::Transform>("Transform");
    m_world.RegisterComponent<scene::Camera>("Camera");

    // ─── Create default camera ────────────────────────────────────────
    m_cameraEntity = m_world.CreateEntity();
    auto& camTransform = m_world.AddComponent<scene::Transform>(m_cameraEntity);
    camTransform.localMotor = pga::Motor::Translation({0, 2, 5});
    camTransform.dirty = true;

    auto& cam = m_world.AddComponent<scene::Camera>(m_cameraEntity);
    cam.isActive = true;
    cam.projection.aspectRatio = static_cast<f32>(m_config.width) / static_cast<f32>(m_config.height);

    AppTraceLog("[APP] all subsystems initialized\n");
    NGE_LOG_INFO("All subsystems initialized");
    return true;
}

void Application::ShutdownSubsystems() {
    NGE_LOG_INFO("Shutting down subsystems...");

    if (m_device) {
        m_device->WaitIdle();
    }

    m_renderPipeline.Shutdown();

    if (m_device) {
        m_device->Shutdown();
    }

    m_window.reset();
    jobs::JobSystem::Shutdown();
    Log::Shutdown();
}

void Application::MainLoop() {
    f64 lastTime = platform::Clock::GetTimeSeconds();

    while (!m_window->ShouldClose()) {
        NGE_PROFILE_SCOPE("Frame");

        // ─── Timing ──────────────────────────────────────────────────
        f64 currentTime = platform::Clock::GetTimeSeconds();
        m_deltaTime = static_cast<f32>(currentTime - lastTime);
        lastTime = currentTime;
        m_totalTime += m_deltaTime;

        // Clamp delta time to prevent spiral of death
        if (m_deltaTime > 0.1f) m_deltaTime = 0.1f;

        // ─── Platform events ─────────────────────────────────────────
        m_window->PollEvents();
        platform::Input::Update();

        // ─── Handle resize ───────────────────────────────────────────
        u32 w = m_window->GetWidth();
        u32 h = m_window->GetHeight();
        if (w != m_config.width || h != m_config.height) {
            m_config.width = w;
            m_config.height = h;
            if (w > 0 && h > 0) {
                m_device->ResizeSwapchain(w, h);
                m_renderPipeline.Resize(w, h);

                // Update camera aspect ratio
                scene::Camera* cam = m_world.GetComponent<scene::Camera>(m_cameraEntity);
                if (cam) cam->projection.aspectRatio = static_cast<f32>(w) / static_cast<f32>(h);
            }
        }

        // Skip rendering if minimized
        if (w == 0 || h == 0) continue;

        // ─── User update ─────────────────────────────────────────────
        OnUpdate(m_deltaTime);

        // ─── Transform hierarchy update ──────────────────────────────
        scene::TransformSystem::UpdateHierarchy(m_world);

        // ─── Render ──────────────────────────────────────────────────
        m_device->BeginFrame();

        if (m_device->AcquireNextImage()) {
            renderer::FrameRenderData frameData;
            BuildFrameRenderData(frameData);

            OnRender();
            m_renderPipeline.RenderFrame(frameData);

            m_device->Present();
        }

        m_device->EndFrame();
        m_frameIndex++;
    }
}

void Application::BuildFrameRenderData(renderer::FrameRenderData& data) {
    scene::Transform* camTransform = m_world.GetComponent<scene::Transform>(m_cameraEntity);
    scene::Camera* cam = m_world.GetComponent<scene::Camera>(m_cameraEntity);

    if (camTransform && cam) {
        data.viewMatrix     = cam->GetViewMatrix(camTransform->worldMotor);
        data.projMatrix     = cam->projection.GetProjectionMatrix();
        data.viewProjMatrix = cam->GetViewProjectionMatrix(camTransform->worldMotor);
        data.cameraPosition = camTransform->GetWorldPosition();
        data.cameraForward  = camTransform->GetForward();
        data.cameraRight    = camTransform->GetRight();
        data.cameraUp       = camTransform->GetUp();
        data.nearPlane      = cam->projection.nearPlane;
        data.farPlane       = cam->projection.farPlane;
        data.jitterX        = cam->jitterX;
        data.jitterY        = cam->jitterY;
    }

    data.time         = m_totalTime;
    data.deltaTime    = m_deltaTime;
    data.frameIndex   = m_frameIndex;
    data.screenWidth  = m_config.width;
    data.screenHeight = m_config.height;

    // TODO: compute inverse VP, store previous frame VP for motion vectors
}

} // namespace nge
