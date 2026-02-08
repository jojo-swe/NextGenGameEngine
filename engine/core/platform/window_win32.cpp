#include "engine/core/platform/window.h"
#include "engine/core/platform/input.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"

#if defined(NGE_PLATFORM_WINDOWS)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace nge::platform {

// ─── Win32 Virtual Key → Engine Key mapping ──────────────────────────────
static Key MapVirtualKey(u32 vk) {
    if (vk >= 'A' && vk <= 'Z') return static_cast<Key>(vk);
    if (vk >= '0' && vk <= '9') return static_cast<Key>(vk);

    switch (vk) {
        case VK_F1:      return Key::F1;
        case VK_F2:      return Key::F2;
        case VK_F3:      return Key::F3;
        case VK_F4:      return Key::F4;
        case VK_F5:      return Key::F5;
        case VK_F6:      return Key::F6;
        case VK_F7:      return Key::F7;
        case VK_F8:      return Key::F8;
        case VK_F9:      return Key::F9;
        case VK_F10:     return Key::F10;
        case VK_F11:     return Key::F11;
        case VK_F12:     return Key::F12;
        case VK_ESCAPE:  return Key::Escape;
        case VK_RETURN:  return Key::Enter;
        case VK_TAB:     return Key::Tab;
        case VK_BACK:    return Key::Backspace;
        case VK_SPACE:   return Key::Space;
        case VK_DELETE:  return Key::Delete;
        case VK_INSERT:  return Key::Insert;
        case VK_LEFT:    return Key::Left;
        case VK_RIGHT:   return Key::Right;
        case VK_UP:      return Key::Up;
        case VK_DOWN:    return Key::Down;
        case VK_HOME:    return Key::Home;
        case VK_END:     return Key::End;
        case VK_PRIOR:   return Key::PageUp;
        case VK_NEXT:    return Key::PageDown;
        case VK_LSHIFT:  return Key::LeftShift;
        case VK_RSHIFT:  return Key::RightShift;
        case VK_LCONTROL: return Key::LeftCtrl;
        case VK_RCONTROL: return Key::RightCtrl;
        case VK_LMENU:   return Key::LeftAlt;
        case VK_RMENU:   return Key::RightAlt;
        case VK_SHIFT:   return Key::LeftShift;
        case VK_CONTROL: return Key::LeftCtrl;
        case VK_MENU:    return Key::LeftAlt;
        default:         return Key::Unknown;
    }
}

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
    void SetShouldClose(bool close) override { m_shouldClose = close; }

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

                // ─── Keyboard input forwarding ────────────────────────
                case WM_KEYDOWN:
                case WM_SYSKEYDOWN: {
                    Key key = MapVirtualKey(static_cast<u32>(wParam));
                    if (key != Key::Unknown) Input::OnKeyEvent(key, true);
                    return 0;
                }
                case WM_KEYUP:
                case WM_SYSKEYUP: {
                    Key key = MapVirtualKey(static_cast<u32>(wParam));
                    if (key != Key::Unknown) Input::OnKeyEvent(key, false);
                    return 0;
                }

                // ─── Mouse button forwarding ──────────────────────────
                case WM_LBUTTONDOWN: Input::OnMouseButtonEvent(MouseButton::Left, true);   SetCapture(hwnd); return 0;
                case WM_LBUTTONUP:   Input::OnMouseButtonEvent(MouseButton::Left, false);  ReleaseCapture(); return 0;
                case WM_RBUTTONDOWN: Input::OnMouseButtonEvent(MouseButton::Right, true);  SetCapture(hwnd); return 0;
                case WM_RBUTTONUP:   Input::OnMouseButtonEvent(MouseButton::Right, false); ReleaseCapture(); return 0;
                case WM_MBUTTONDOWN: Input::OnMouseButtonEvent(MouseButton::Middle, true);  return 0;
                case WM_MBUTTONUP:   Input::OnMouseButtonEvent(MouseButton::Middle, false); return 0;

                // ─── Mouse move forwarding ────────────────────────────
                case WM_MOUSEMOVE: {
                    f32 mx = static_cast<f32>(LOWORD(lParam));
                    f32 my = static_cast<f32>(HIWORD(lParam));
                    Input::OnMouseMoveEvent(mx, my);
                    return 0;
                }

                // ─── Mouse scroll forwarding ──────────────────────────
                case WM_MOUSEWHEEL: {
                    f32 delta = static_cast<f32>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
                    Input::OnMouseScrollEvent(delta);
                    return 0;
                }
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
