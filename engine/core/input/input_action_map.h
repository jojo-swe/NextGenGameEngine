#pragma once

#include "engine/core/types.h"
#include "engine/core/platform/input.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace nge::input {

// ─── Input Action Mapping ────────────────────────────────────────────────
// Rebindable input system that maps physical inputs to named actions.
// Supports keyboard, mouse buttons, and gamepad (future).
//
// Usage:
//   ActionMap map;
//   map.AddAction("Jump", {Key::Space});
//   map.AddAction("Fire", {MouseButton::Left});
//   map.AddAxis("MoveForward", Key::W, Key::S);
//   if (map.IsActionPressed("Jump")) { ... }
//   float forward = map.GetAxis("MoveForward");

// ─── Input Binding ───────────────────────────────────────────────────────

enum class BindingType : u8 {
    Key,
    MouseButton,
    MouseAxis,      // Mouse X/Y delta
    MouseScroll,
    // GamepadButton,
    // GamepadAxis,
};

struct InputBinding {
    BindingType type = BindingType::Key;
    platform::Key key = platform::Key::Unknown;
    platform::MouseButton mouseButton = platform::MouseButton::Left;
    u8 mouseAxis = 0; // 0=X, 1=Y
    f32 scale = 1.0f; // Multiplier (use -1 for inverted axis)
};

// ─── Action (digital: pressed/released) ──────────────────────────────────

struct InputAction {
    std::string name;
    std::vector<InputBinding> bindings; // Any binding triggers the action

    // Callbacks
    std::function<void()> onPressed;
    std::function<void()> onReleased;
};

// ─── Axis (analog: -1 to +1) ─────────────────────────────────────────────

struct InputAxis {
    std::string name;
    InputBinding positive; // Key/button that contributes +1
    InputBinding negative; // Key/button that contributes -1
    f32 deadZone = 0.0f;
    f32 sensitivity = 1.0f;
    f32 smoothing = 0.0f;  // 0 = instant, 1 = very smooth

    // Runtime state
    f32 currentValue = 0;
    f32 targetValue = 0;
};

// ─── Action Map Context ──────────────────────────────────────────────────
// Groups of actions that can be enabled/disabled together.
// e.g., "Gameplay", "UI", "Vehicle", "Editor"

struct ActionMapContext {
    std::string name;
    bool enabled = true;
    std::vector<std::string> actionNames;
    std::vector<std::string> axisNames;
};

// ─── Action Map ──────────────────────────────────────────────────────────

class ActionMap {
public:
    // Action management
    void AddAction(const std::string& name, std::vector<InputBinding> bindings);
    void AddAction(const std::string& name, platform::Key key);
    void AddAction(const std::string& name, platform::MouseButton button);
    void RemoveAction(const std::string& name);

    // Axis management
    void AddAxis(const std::string& name, platform::Key positive, platform::Key negative,
                  f32 sensitivity = 1.0f, f32 smoothing = 0.0f);
    void AddMouseAxis(const std::string& name, u8 axis, f32 sensitivity = 1.0f);
    void RemoveAxis(const std::string& name);

    // Context management
    void AddContext(const std::string& name, const std::vector<std::string>& actions,
                     const std::vector<std::string>& axes);
    void EnableContext(const std::string& name);
    void DisableContext(const std::string& name);
    bool IsContextEnabled(const std::string& name) const;

    // Callbacks
    void SetActionCallback(const std::string& name,
                            std::function<void()> onPressed,
                            std::function<void()> onReleased = nullptr);

    // Query state (call after Update)
    bool IsActionDown(const std::string& name) const;
    bool IsActionPressed(const std::string& name) const;  // Just pressed this frame
    bool IsActionReleased(const std::string& name) const; // Just released this frame
    f32  GetAxis(const std::string& name) const;

    // Rebinding
    void Rebind(const std::string& actionName, u32 bindingIndex, const InputBinding& newBinding);

    // Per-frame update (reads Input state)
    void Update(f32 deltaTime);

    // Serialization (for saving/loading key bindings)
    // TODO: Save/Load to JSON or binary config file

private:
    bool EvaluateBinding(const InputBinding& binding) const;
    bool EvaluateBindingPressed(const InputBinding& binding) const;
    bool EvaluateBindingReleased(const InputBinding& binding) const;

    std::unordered_map<std::string, InputAction> m_actions;
    std::unordered_map<std::string, InputAxis>   m_axes;
    std::unordered_map<std::string, ActionMapContext> m_contexts;

    // Per-frame state cache
    std::unordered_map<std::string, bool> m_actionDown;
    std::unordered_map<std::string, bool> m_actionPressed;
    std::unordered_map<std::string, bool> m_actionReleased;
};

} // namespace nge::input
