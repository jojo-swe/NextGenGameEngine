#pragma once

#include "editor/editor_app.h"
#include "engine/scene/camera/camera.h"

namespace nge::editor {

// ─── Viewport Panel ──────────────────────────────────────────────────────
// Renders the 3D scene into an ImGui image widget.
// Handles gizmo interaction (translate, rotate, scale) and camera control.

class ViewportPanel : public EditorPanel {
public:
    const char* GetName() const override { return "Viewport"; }
    void OnDraw() override;
    void OnUpdate(f32 deltaTime) override;

    void SetEditorApp(EditorApp* app) { m_app = app; }

    enum class GizmoMode : u8 { Translate, Rotate, Scale };
    void SetGizmoMode(GizmoMode mode) { m_gizmoMode = mode; }
    GizmoMode GetGizmoMode() const { return m_gizmoMode; }

    bool IsHovered() const { return m_hovered; }
    bool IsFocused() const { return m_focused; }

private:
    EditorApp*  m_app = nullptr;
    GizmoMode   m_gizmoMode = GizmoMode::Translate;
    bool        m_hovered = false;
    bool        m_focused = false;
    f32         m_viewportWidth = 0;
    f32         m_viewportHeight = 0;
};

// ─── Hierarchy Panel ─────────────────────────────────────────────────────
// Displays the scene entity tree. Allows selection, reparenting, and creation.

class HierarchyPanel : public EditorPanel {
public:
    const char* GetName() const override { return "Hierarchy"; }
    void OnDraw() override;
    void OnUpdate(f32 deltaTime) override;

    void SetEditorApp(EditorApp* app) { m_app = app; }

private:
    void DrawEntityNode(ecs::Entity entity);

    EditorApp* m_app = nullptr;
    std::string m_searchFilter;
};

// ─── Inspector Panel ─────────────────────────────────────────────────────
// Shows and edits components of the selected entity.

class InspectorPanel : public EditorPanel {
public:
    const char* GetName() const override { return "Inspector"; }
    void OnDraw() override;
    void OnUpdate(f32 deltaTime) override;

    void SetEditorApp(EditorApp* app) { m_app = app; }

private:
    void DrawTransformComponent();
    void DrawCameraComponent();
    void DrawMaterialComponent();
    void DrawLightComponent();
    void DrawPhysicsComponent();

    EditorApp* m_app = nullptr;
};

// ─── Console Panel ───────────────────────────────────────────────────────
// Displays engine log output with filtering and search.

class ConsolePanel : public EditorPanel {
public:
    const char* GetName() const override { return "Console"; }
    void OnDraw() override;
    void OnUpdate(f32 deltaTime) override;

    void AddLog(const std::string& msg, u8 level = 0);
    void Clear();

private:
    struct LogEntry {
        std::string message;
        u8 level; // 0=info, 1=warn, 2=error, 3=debug
    };
    std::vector<LogEntry> m_logs;
    std::string m_filter;
    bool m_autoScroll = true;
    bool m_showInfo = true;
    bool m_showWarn = true;
    bool m_showError = true;
    bool m_showDebug = false;
    static constexpr u32 MAX_LOGS = 10000;
};

// ─── Asset Browser Panel ─────────────────────────────────────────────────
// File browser for project assets with drag-and-drop.

class AssetBrowserPanel : public EditorPanel {
public:
    const char* GetName() const override { return "Assets"; }
    void OnDraw() override;
    void OnUpdate(f32 deltaTime) override;

    void SetRootPath(const std::string& path) { m_rootPath = path; }

private:
    std::string m_rootPath;
    std::string m_currentPath;
};

// ─── Profiler Panel ──────────────────────────────────────────────────────
// Displays frame timing, GPU/CPU profiler data, and memory stats.

class ProfilerPanel : public EditorPanel {
public:
    const char* GetName() const override { return "Profiler"; }
    void OnDraw() override;
    void OnUpdate(f32 deltaTime) override;

private:
    static constexpr u32 FRAME_HISTORY = 300;
    f32 m_frameTimesMs[FRAME_HISTORY] = {};
    u32 m_frameIndex = 0;
    f32 m_avgFrameTime = 0;
    f32 m_maxFrameTime = 0;
};

} // namespace nge::editor
