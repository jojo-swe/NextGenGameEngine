#include "engine/core/types.h"
#include "engine/core/logging/log.h"
#include "engine/core/platform/window.h"

using namespace nge;

int main() {
    Log::Init();
    NGE_LOG_INFO("NextGen Game Engine — Triangle Sample");

    platform::WindowDesc desc{};
    desc.title  = "NextGen Engine — Triangle";
    desc.width  = 1920;
    desc.height = 1080;

    auto window = platform::Window::Create(desc);
    if (!window) {
        NGE_LOG_FATAL("Failed to create window");
        return 1;
    }

    while (!window->ShouldClose()) {
        window->PollEvents();
        // TODO: RHI render loop
    }

    NGE_LOG_INFO("Shutting down");
    Log::Shutdown();
    return 0;
}
