#include "engine/core/input/input_action_map.h"
#include "engine/core/logging/log.h"
#include <cmath>

namespace nge::input {

void ActionMap::AddAction(const std::string& name, std::vector<InputBinding> bindings) {
    InputAction action;
    action.name = name;
    action.bindings = std::move(bindings);
    m_actions[name] = std::move(action);
}

void ActionMap::AddAction(const std::string& name, platform::Key key) {
    InputBinding binding;
    binding.type = BindingType::Key;
    binding.key = key;
    AddAction(name, {binding});
}

void ActionMap::AddAction(const std::string& name, platform::MouseButton button) {
    InputBinding binding;
    binding.type = BindingType::MouseButton;
    binding.mouseButton = button;
    AddAction(name, {binding});
}

void ActionMap::RemoveAction(const std::string& name) {
    m_actions.erase(name);
    m_actionDown.erase(name);
    m_actionPressed.erase(name);
    m_actionReleased.erase(name);
}

void ActionMap::AddAxis(const std::string& name, platform::Key positive, platform::Key negative,
                          f32 sensitivity, f32 smoothing) {
    InputAxis axis;
    axis.name = name;
    axis.positive.type = BindingType::Key;
    axis.positive.key = positive;
    axis.negative.type = BindingType::Key;
    axis.negative.key = negative;
    axis.sensitivity = sensitivity;
    axis.smoothing = smoothing;
    m_axes[name] = std::move(axis);
}

void ActionMap::AddMouseAxis(const std::string& name, u8 axisIndex, f32 sensitivity) {
    InputAxis axis;
    axis.name = name;
    axis.positive.type = BindingType::MouseAxis;
    axis.positive.mouseAxis = axisIndex;
    axis.positive.scale = sensitivity;
    axis.negative.type = BindingType::MouseAxis;
    axis.negative.mouseAxis = axisIndex;
    axis.negative.scale = -sensitivity;
    axis.sensitivity = sensitivity;
    m_axes[name] = std::move(axis);
}

void ActionMap::RemoveAxis(const std::string& name) {
    m_axes.erase(name);
}

void ActionMap::AddContext(const std::string& name, const std::vector<std::string>& actions,
                             const std::vector<std::string>& axes) {
    ActionMapContext ctx;
    ctx.name = name;
    ctx.enabled = true;
    ctx.actionNames = actions;
    ctx.axisNames = axes;
    m_contexts[name] = std::move(ctx);
}

void ActionMap::EnableContext(const std::string& name) {
    auto it = m_contexts.find(name);
    if (it != m_contexts.end()) it->second.enabled = true;
}

void ActionMap::DisableContext(const std::string& name) {
    auto it = m_contexts.find(name);
    if (it != m_contexts.end()) it->second.enabled = false;
}

bool ActionMap::IsContextEnabled(const std::string& name) const {
    auto it = m_contexts.find(name);
    return it != m_contexts.end() && it->second.enabled;
}

void ActionMap::SetActionCallback(const std::string& name,
                                    std::function<void()> onPressed,
                                    std::function<void()> onReleased) {
    auto it = m_actions.find(name);
    if (it != m_actions.end()) {
        it->second.onPressed = std::move(onPressed);
        it->second.onReleased = std::move(onReleased);
    }
}

bool ActionMap::IsActionDown(const std::string& name) const {
    auto it = m_actionDown.find(name);
    return it != m_actionDown.end() && it->second;
}

bool ActionMap::IsActionPressed(const std::string& name) const {
    auto it = m_actionPressed.find(name);
    return it != m_actionPressed.end() && it->second;
}

bool ActionMap::IsActionReleased(const std::string& name) const {
    auto it = m_actionReleased.find(name);
    return it != m_actionReleased.end() && it->second;
}

f32 ActionMap::GetAxis(const std::string& name) const {
    auto it = m_axes.find(name);
    return it != m_axes.end() ? it->second.currentValue : 0.0f;
}

void ActionMap::Rebind(const std::string& actionName, u32 bindingIndex, const InputBinding& newBinding) {
    auto it = m_actions.find(actionName);
    if (it == m_actions.end()) return;
    if (bindingIndex >= it->second.bindings.size()) return;
    it->second.bindings[bindingIndex] = newBinding;
    NGE_LOG_INFO("Rebound action '{}' binding {} to key {}",
                 actionName, bindingIndex, static_cast<u32>(newBinding.key));
}

void ActionMap::Update(f32 deltaTime) {
    // Update actions
    for (auto& [name, action] : m_actions) {
        bool wasDown = m_actionDown[name];
        bool isDown = false;

        for (const auto& binding : action.bindings) {
            if (EvaluateBinding(binding)) {
                isDown = true;
                break;
            }
        }

        m_actionDown[name] = isDown;
        m_actionPressed[name] = isDown && !wasDown;
        m_actionReleased[name] = !isDown && wasDown;

        // Fire callbacks
        if (m_actionPressed[name] && action.onPressed) action.onPressed();
        if (m_actionReleased[name] && action.onReleased) action.onReleased();
    }

    // Update axes
    for (auto& [name, axis] : m_axes) {
        f32 target = 0;

        if (axis.positive.type == BindingType::MouseAxis) {
            // Mouse axis: read delta directly
            auto [mx, my] = platform::Input::GetMouseDelta();
            f32 delta = (axis.positive.mouseAxis == 0) ? mx : my;
            target = delta * axis.sensitivity;
        } else {
            // Digital axis from key bindings
            if (EvaluateBinding(axis.positive)) target += 1.0f;
            if (EvaluateBinding(axis.negative)) target -= 1.0f;
            target *= axis.sensitivity;
        }

        // Dead zone
        if (std::abs(target) < axis.deadZone) target = 0;

        axis.targetValue = target;

        // Smoothing
        if (axis.smoothing > 0.0f) {
            f32 blend = 1.0f - std::pow(axis.smoothing, deltaTime * 60.0f);
            axis.currentValue += (axis.targetValue - axis.currentValue) * blend;
        } else {
            axis.currentValue = axis.targetValue;
        }
    }
}

bool ActionMap::EvaluateBinding(const InputBinding& binding) const {
    switch (binding.type) {
        case BindingType::Key:
            return platform::Input::IsKeyDown(binding.key);
        case BindingType::MouseButton:
            return platform::Input::IsMouseButtonDown(binding.mouseButton);
        default:
            return false;
    }
}

bool ActionMap::EvaluateBindingPressed(const InputBinding& binding) const {
    switch (binding.type) {
        case BindingType::Key:
            return platform::Input::IsKeyPressed(binding.key);
        case BindingType::MouseButton:
            return platform::Input::IsMouseButtonPressed(binding.mouseButton);
        default:
            return false;
    }
}

bool ActionMap::EvaluateBindingReleased(const InputBinding& binding) const {
    switch (binding.type) {
        case BindingType::Key:
            return platform::Input::IsKeyReleased(binding.key);
        case BindingType::MouseButton:
            return platform::Input::IsMouseButtonReleased(binding.mouseButton);
        default:
            return false;
    }
}

} // namespace nge::input
