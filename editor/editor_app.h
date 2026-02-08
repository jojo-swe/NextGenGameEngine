#pragma once

#include "engine/core/app/application.h"
#include "engine/core/types.h"
#include <vector>
#include <memory>
#include <string>
#include <functional>

namespace nge::editor {

// ─── Editor Panel Interface ──────────────────────────────────────────────
// All editor panels (viewport, hierarchy, inspector, etc.) implement this.

class EditorPanel {
public:
    virtual ~EditorPanel() = default;

    virtual const char* GetName() const = 0;
    virtual void OnDraw() = 0;                   // ImGui draw
    virtual void OnUpdate(f32 deltaTime) = 0;    // Logic update
    virtual bool IsVisible() const { return m_visible; }
    virtual void SetVisible(bool v) { m_visible = v; }

protected:
    bool m_visible = true;
};

// ─── Editor Configuration ────────────────────────────────────────────────

struct EditorConfig {
    std::string projectPath;
    std::string layoutFile = "editor_layout.ini";
    bool        enableDocking = true;
    bool        enableViewportPanel = true;
    bool        enableHierarchyPanel = true;
    bool        enableInspectorPanel = true;
    bool        enableAssetBrowser = true;
    bool        enableConsolePanel = true;
    bool        enableProfilerPanel = true;
    f32         fontSize = 14.0f;
    std::string theme = "dark"; // "dark", "light", "custom"
};

// ─── Editor Application ──────────────────────────────────────────────────
// Extends the engine Application with ImGui-based editor UI.
// Uses ImGui docking for a multi-panel layout similar to Unity/Unreal.

class EditorApp : public Application {
public:
    EditorApp(const EditorConfig& config = {});
    ~EditorApp() override;

    void OnInit() override;
    void OnUpdate(f32 deltaTime) override;
    void OnShutdown() override;

    // Panel management
    void AddPanel(std::unique_ptr<EditorPanel> panel);
    EditorPanel* GetPanel(const char* name);

    // Menu bar callbacks
    using MenuCallback = std::function<void()>;
    void AddMenuItem(const std::string& menu, const std::string& item, MenuCallback callback);

    // Undo/Redo
    void PushUndoAction(const std::string& description, std::function<void()> undo, std::function<void()> redo);
    void Undo();
    void Redo();

    // Selection
    void Select(ecs::Entity entity);
    void ClearSelection();
    ecs::Entity GetSelectedEntity() const { return m_selectedEntity; }

    const EditorConfig& GetEditorConfig() const { return m_editorConfig; }

private:
    void InitImGui();
    void ShutdownImGui();
    void BeginImGuiFrame();
    void EndImGuiFrame();
    void DrawMenuBar();
    void DrawDockSpace();
    void HandleShortcuts();

    EditorConfig m_editorConfig;
    std::vector<std::unique_ptr<EditorPanel>> m_panels;

    // Menu system
    struct MenuItem {
        std::string name;
        MenuCallback callback;
    };
    struct MenuEntry {
        std::string name;
        std::vector<MenuItem> items;
    };
    std::vector<MenuEntry> m_menus;

    // Undo/Redo
    struct UndoAction {
        std::string description;
        std::function<void()> undo;
        std::function<void()> redo;
    };
    std::vector<UndoAction> m_undoStack;
    std::vector<UndoAction> m_redoStack;
    static constexpr u32 MAX_UNDO_HISTORY = 256;

    // Selection
    ecs::Entity m_selectedEntity;

    // ImGui state
    bool m_imguiInitialized = false;
};

} // namespace nge::editor
