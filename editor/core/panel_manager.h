#pragma once

#include "engine/core/types.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nge::editor {

class EditorPanel {
public:
    virtual ~EditorPanel() = default;

    virtual const char* GetName() const = 0;
    virtual void OnDraw() = 0;
    virtual void OnUpdate(f32 deltaTime) = 0;
    virtual bool IsVisible() const { return m_visible; }
    virtual void SetVisible(bool visible) { m_visible = visible; }

protected:
    bool m_visible = true;
};

class PanelManager {
public:
    bool Register(std::unique_ptr<EditorPanel> panel) {
        if (!panel || !panel->GetName() || std::string_view(panel->GetName()).empty()) {
            return false;
        }
        if (Find(panel->GetName())) return false;
        m_panels.push_back(std::move(panel));
        return true;
    }

    bool Unregister(std::string_view name) {
        for (auto it = m_panels.begin(); it != m_panels.end(); ++it) {
            if ((*it)->GetName() == name) {
                m_panels.erase(it);
                return true;
            }
        }
        return false;
    }

    EditorPanel* Find(std::string_view name) {
        for (auto& panel : m_panels) {
            if (panel->GetName() == name) return panel.get();
        }
        return nullptr;
    }

    const EditorPanel* Find(std::string_view name) const {
        for (const auto& panel : m_panels) {
            if (panel->GetName() == name) return panel.get();
        }
        return nullptr;
    }

    bool SetVisible(std::string_view name, bool visible) {
        auto* panel = Find(name);
        if (!panel) return false;
        panel->SetVisible(visible);
        return true;
    }

    void UpdateVisible(f32 deltaTime) {
        for (auto& panel : m_panels) {
            if (panel->IsVisible()) panel->OnUpdate(deltaTime);
        }
    }

    void DrawVisible() {
        for (auto& panel : m_panels) {
            if (panel->IsVisible()) panel->OnDraw();
        }
    }

    void Clear() { m_panels.clear(); }
    usize Size() const { return m_panels.size(); }

private:
    std::vector<std::unique_ptr<EditorPanel>> m_panels;
};

} // namespace nge::editor
