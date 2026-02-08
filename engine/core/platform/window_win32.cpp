#include "engine/core/platform/window.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"

#if defined(NGE_PLATFORM_WINDOWS)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace nge::platform {

class Win32Window final : public Window {
public:
    Win32Window(const WindowDesc& desc) {
        m_hInstance = GetModuleHandle(nullptr);

        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(WNDCLASSEXW);
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = m_hInstance;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"NGE_WindowClass";
        RegisterClassExW(&wc);

        DWORD style = WS_OVERLAPPEDWINDOW;
        if (!desc.resizable) style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

        RECT rect = { 0, 0, static_cast<LONG>(desc.width), static_cast<LONG>(desc.height) };
        AdjustWindowRect(&rect, style, FALSE);

        int windowWidth  = rect.right - rect.left;
        int windowHeight = rect.bottom - rect.top;

        // Convert title to wide string
        int titleLen = MultiByteToWideChar(CP_UTF8, 0, desc.title.c_str(), -1, nullptr, 0);
        std::wstring wideTitle(titleLen, 0);
        MultiByteToWideChar(CP_UTF8, 0, desc.title.c_str(), -1, wideTitle.data(), titleLen);

        m_hwnd = CreateWindowExW(
            0,
            L"NGE_WindowClass",
            wideTitle.c_str(),
            style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            windowWidth, windowHeight,
            nullptr, nullptr,
            m_hInstance,
            this
        );

        NGE_CHECK(m_hwnd != nullptr, "Failed to create Win32 window");

        m_width  = desc.width;
        m_height = desc.height;

        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);

        NGE_LOG_INFO("Created Win32 window: {}x{}", m_width, m_height);
    }

    ~Win32Window() override {
        if (m_hwnd) {
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
        UnregisterClassW(L"NGE_WindowClass", m_hInstance);
    }

    void PollEvents() override {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    bool ShouldClose() const override { return m_shouldClose; }

    void SetTitle(const char* title) override {
        int len = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
        std::wstring wTitle(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wTitle.data(), len);
        SetWindowTextW(m_hwnd, wTitle.c_str());
    }

    u32  GetWidth() const override  { return m_width; }
    u32  GetHeight() const override { return m_height; }
    bool IsMinimized() const override { return m_minimized; }
    bool IsFocused() const override { return m_focused; }

    void* GetNativeHandle() const override { return m_hwnd; }
    void* GetInstanceHandle() const override { return m_hInstance; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        Win32Window* self = nullptr;

        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<Win32Window*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self) {
            switch (msg) {
                case WM_CLOSE:
                    self->m_shouldClose = true;
                    return 0;

                case WM_SIZE: {
                    u32 w = LOWORD(lParam);
                    u32 h = HIWORD(lParam);
                    self->m_width  = w;
                    self->m_height = h;
                    self->m_minimized = (wParam == SIZE_MINIMIZED);
                    return 0;
                }

                case WM_SETFOCUS:
                    self->m_focused = true;
                    return 0;

                case WM_KILLFOCUS:
                    self->m_focused = false;
                    return 0;

                case WM_DESTROY:
                    PostQuitMessage(0);
                    return 0;
            }
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    HWND      m_hwnd        = nullptr;
    HINSTANCE m_hInstance    = nullptr;
    u32       m_width       = 0;
    u32       m_height      = 0;
    bool      m_shouldClose = false;
    bool      m_minimized   = false;
    bool      m_focused     = true;
};

std::unique_ptr<Window> Window::Create(const WindowDesc& desc) {
    return std::make_unique<Win32Window>(desc);
}

} // namespace nge::platform

#endif // NGE_PLATFORM_WINDOWS
