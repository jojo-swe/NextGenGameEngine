#pragma once

#include "engine/core/types.h"
#include <memory>
#include <string>
#include <functional>

namespace nge::platform {

struct WindowDesc {
    std::string title  = "NextGen Engine";
    u32         width  = 1920;
    u32         height = 1080;
    bool        fullscreen  = false;
    bool        resizable   = true;
    bool        vsync       = false;
};

// Platform-agnostic event callback for message interception (e.g. ImGui).
// Returns true if the message was handled and default processing should be skipped.
using EventCallback = std::function<bool(void* hwnd, u32 msg, u64 wParam, i64 lParam)>;

class Window {
public:
    virtual ~Window() = default;

    virtual void PollEvents() = 0;
    virtual bool ShouldClose() const = 0;
    virtual void SetShouldClose(bool close) = 0;
    virtual void SetTitle(const char* title) = 0;

    virtual u32 GetWidth() const = 0;
    virtual u32 GetHeight() const = 0;
    virtual bool IsMinimized() const = 0;
    virtual bool IsFocused() const = 0;

    virtual void* GetNativeHandle() const = 0; // HWND on Windows, Window on X11

    // Vulkan surface creation requires the native handle; the RHI layer
    // will query GetNativeHandle() and GetInstanceHandle() to create it.
#if defined(NGE_PLATFORM_WINDOWS)
    virtual void* GetInstanceHandle() const = 0; // HINSTANCE
#endif

    void SetEventCallback(EventCallback cb) { m_eventCallback = std::move(cb); }

    static std::unique_ptr<Window> Create(const WindowDesc& desc);

protected:
    EventCallback m_eventCallback;
};

} // namespace nge::platform
