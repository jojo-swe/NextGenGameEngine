#include "engine/core/app/application.h"
#include "engine/core/platform/input.h"
#include "engine/scene/camera/camera.h"
#include "engine/scene/transform/transform.h"

using namespace nge;

// ─── Triangle Sample ─────────────────────────────────────────────────────
// Minimal application demonstrating the engine's core systems:
//   - Window creation + event loop
//   - Vulkan RHI initialization
//   - ECS with Transform + Camera components
//   - Render pipeline (visibility buffer path)
//   - Free-fly camera controller

class TriangleSample : public Application {
public:
    TriangleSample() : Application({
        .title  = "NextGen Engine — Triangle",
        .width  = 1920,
        .height = 1080,
    }) {}

    void OnInit() override {
        NGE_LOG_INFO("Triangle sample initialized");

        // Register game-specific components
        m_world.RegisterComponent<scene::FreeFlyController>("FreeFlyController");

        // Add free-fly controller to camera
        auto& controller = m_world.AddComponent<scene::FreeFlyController>(m_cameraEntity);
        controller.yaw = 0.38f;
        controller.pitch = -0.10f;

        if (auto* transform = m_world.GetComponent<scene::Transform>(m_cameraEntity)) {
            pga::Motor translation = pga::Motor::Translation({-2.0f, 1.5f, 5.0f});
            pga::Motor rotY = pga::Motor::Rotation({0, 1, 0}, controller.yaw);
            pga::Motor rotX = pga::Motor::Rotation({1, 0, 0}, controller.pitch);
            pga::Motor rotation = pga::Motor::Multiply(rotY, rotX);
            transform->localMotor = pga::Motor::Multiply(translation, rotation);
            transform->dirty = true;
        }
    }

    void OnUpdate(f32 deltaTime) override {
        // ─── Camera movement ──────────────────────────────────────────
        auto* controller = m_world.GetComponent<scene::FreeFlyController>(m_cameraEntity);
        auto* transform  = m_world.GetComponent<scene::Transform>(m_cameraEntity);

        if (controller && transform) {
            bool forward  = platform::Input::IsKeyDown(platform::Key::W);
            bool backward = platform::Input::IsKeyDown(platform::Key::S);
            bool left     = platform::Input::IsKeyDown(platform::Key::A);
            bool right    = platform::Input::IsKeyDown(platform::Key::D);
            bool up       = platform::Input::IsKeyDown(platform::Key::Space);
            bool down     = platform::Input::IsKeyDown(platform::Key::LeftShift);
            bool sprint   = platform::Input::IsKeyDown(platform::Key::LeftCtrl);

            f32 mouseDX = platform::Input::GetMouseDeltaX();
            f32 mouseDY = platform::Input::GetMouseDeltaY();

            // Only look when right mouse is held
            if (!platform::Input::IsMouseDown(platform::MouseButton::Right)) {
                mouseDX = 0;
                mouseDY = 0;
            }

            controller->Update(
                transform->localMotor, deltaTime,
                forward, backward, left, right, up, down, sprint,
                mouseDX, mouseDY);
            transform->dirty = true;
        }

        // ─── Advance temporal jitter for TAA/TSR ─────────────────────
        auto* cam = m_world.GetComponent<scene::Camera>(m_cameraEntity);
        if (cam) {
            cam->AdvanceJitter(m_config.width, m_config.height);
        }

        // ─── Escape to close ─────────────────────────────────────────
        if (platform::Input::IsKeyPressed(platform::Key::Escape)) {
            m_window->SetShouldClose(true);
        }
    }

    void OnShutdown() override {
        NGE_LOG_INFO("Triangle sample shutting down");
    }
};

int main() {
    TriangleSample app;
    return app.Run();
}
