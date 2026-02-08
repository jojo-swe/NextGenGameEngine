#include "engine/core/platform/input.h"
#include <cstring>

namespace nge::platform {

void Input::Update() {
    std::memcpy(s_keysPrevious, s_keysCurrent, sizeof(s_keysCurrent));
    std::memcpy(s_mousePrevious, s_mouseCurrent, sizeof(s_mouseCurrent));
    s_mousePrevX = s_mouseX;
    s_mousePrevY = s_mouseY;
    s_scrollDelta = 0;
}

bool Input::IsKeyDown(Key key)     { return s_keysCurrent[static_cast<u32>(key)]; }
bool Input::IsKeyPressed(Key key)  { u32 k = static_cast<u32>(key); return s_keysCurrent[k] && !s_keysPrevious[k]; }
bool Input::IsKeyReleased(Key key) { u32 k = static_cast<u32>(key); return !s_keysCurrent[k] && s_keysPrevious[k]; }

bool Input::IsMouseDown(MouseButton b)     { return s_mouseCurrent[static_cast<u32>(b)]; }
bool Input::IsMousePressed(MouseButton b)  { u32 i = static_cast<u32>(b); return s_mouseCurrent[i] && !s_mousePrevious[i]; }
bool Input::IsMouseReleased(MouseButton b) { u32 i = static_cast<u32>(b); return !s_mouseCurrent[i] && s_mousePrevious[i]; }

f32 Input::GetMouseX() { return s_mouseX; }
f32 Input::GetMouseY() { return s_mouseY; }
f32 Input::GetMouseDeltaX() { return s_mouseX - s_mousePrevX; }
f32 Input::GetMouseDeltaY() { return s_mouseY - s_mousePrevY; }
f32 Input::GetMouseScrollDelta() { return s_scrollDelta; }

void Input::OnKeyEvent(Key key, bool down) {
    s_keysCurrent[static_cast<u32>(key)] = down;
}

void Input::OnMouseButtonEvent(MouseButton button, bool down) {
    s_mouseCurrent[static_cast<u32>(button)] = down;
}

void Input::OnMouseMoveEvent(f32 x, f32 y) {
    s_mouseX = x;
    s_mouseY = y;
}

void Input::OnMouseScrollEvent(f32 delta) {
    s_scrollDelta = delta;
}

} // namespace nge::platform
