#pragma once

#include "engine/core/types.h"

namespace nge::platform {

// ─── Key Codes ───────────────────────────────────────────────────────────
enum class Key : u16 {
    Unknown = 0,

    // Letters
    A = 'A', B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    // Numbers
    Num0 = '0', Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    // Function keys
    F1 = 256, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    // Special
    Escape, Enter, Tab, Backspace, Space, Delete, Insert,
    Left, Right, Up, Down,
    Home, End, PageUp, PageDown,

    // Modifiers
    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt,

    Count
};

// ─── Mouse Buttons ───────────────────────────────────────────────────────
enum class MouseButton : u8 {
    Left = 0,
    Right,
    Middle,
    X1,
    X2,
    Count
};

// ─── Input State ─────────────────────────────────────────────────────────
class Input {
public:
    static void Update(); // Call once per frame, before polling

    // Keyboard
    static bool IsKeyDown(Key key);
    static bool IsKeyPressed(Key key);  // Just pressed this frame
    static bool IsKeyReleased(Key key); // Just released this frame

    // Mouse
    static bool IsMouseDown(MouseButton button);
    static bool IsMousePressed(MouseButton button);
    static bool IsMouseReleased(MouseButton button);
    static f32  GetMouseX();
    static f32  GetMouseY();
    static f32  GetMouseDeltaX();
    static f32  GetMouseDeltaY();
    static f32  GetMouseScrollDelta();

    // Called by platform layer
    static void OnKeyEvent(Key key, bool down);
    static void OnMouseButtonEvent(MouseButton button, bool down);
    static void OnMouseMoveEvent(f32 x, f32 y);
    static void OnMouseScrollEvent(f32 delta);

private:
    static constexpr u32 KEY_COUNT = static_cast<u32>(Key::Count);
    static constexpr u32 MOUSE_COUNT = static_cast<u32>(MouseButton::Count);

    inline static bool s_keysCurrent[KEY_COUNT] = {};
    inline static bool s_keysPrevious[KEY_COUNT] = {};
    inline static bool s_mouseCurrent[MOUSE_COUNT] = {};
    inline static bool s_mousePrevious[MOUSE_COUNT] = {};
    inline static f32  s_mouseX = 0, s_mouseY = 0;
    inline static f32  s_mousePrevX = 0, s_mousePrevY = 0;
    inline static f32  s_scrollDelta = 0;
};

} // namespace nge::platform
