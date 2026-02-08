#include "editor/editor_app.h"
#include "engine/core/logging/log.h"
#include "engine/core/platform/input.h"

// ImGui integration — when available via vcpkg:
// #include <imgui.h>
// #include <imgui_impl_vulkan.h>
// #include <imgui_impl_win32.h>

namespace nge::editor {

EditorApp::EditorApp(const EditorConfig& config)
    : Application({
        .title  = "NextGen Engine Editor",
        .width  = 1920,
        .height = 1080,
    })
    , m_editorConfig(config) {}

EditorApp::~EditorApp() = default;

void EditorApp::OnInit() {
    NGE_LOG_INFO("Editor initializing...");

    InitImGui();

    // Set up default menu structure
    m_menus.push_back({"File", {
        {"New Scene",  [this]() { NGE_LOG_INFO("New Scene"); }},
        {"Open Scene", [this]() { NGE_LOG_INFO("Open Scene"); }},
        {"Save Scene", [this]() { NGE_LOG_INFO("Save Scene"); }},
        {"Exit",       [this]() { m_window->SetShouldClose(true); }},
    }});

    m_menus.push_back({"Edit", {
        {"Undo",       [this]() { Undo(); }},
        {"Redo",       [this]() { Redo(); }},
        {"Preferences",[this]() { NGE_LOG_INFO("Preferences"); }},
    }});

    m_menus.push_back({"View", {
        {"Viewport",   [this]() { if (auto* p = GetPanel("Viewport")) p->SetVisible(true); }},
        {"Hierarchy",  [this]() { if (auto* p = GetPanel("Hierarchy")) p->SetVisible(true); }},
        {"Inspector",  [this]() { if (auto* p = GetPanel("Inspector")) p->SetVisible(true); }},
        {"Console",    [this]() { if (auto* p = GetPanel("Console")) p->SetVisible(true); }},
        {"Profiler",   [this]() { if (auto* p = GetPanel("Profiler")) p->SetVisible(true); }},
    }});

    m_menus.push_back({"Help", {
        {"About", [this]() { NGE_LOG_INFO("NextGen Engine Editor v0.1"); }},
    }});

    NGE_LOG_INFO("Editor initialized with {} panels", m_panels.size());
}

void EditorApp::OnUpdate(f32 deltaTime) {
    HandleShortcuts();

    // Update all panels
    for (auto& panel : m_panels) {
        if (panel->IsVisible()) {
            panel->OnUpdate(deltaTime);
        }
    }

    // ImGui rendering
    BeginImGuiFrame();
    DrawDockSpace();
    DrawMenuBar();

    for (auto& panel : m_panels) {
        if (panel->IsVisible()) {
            panel->OnDraw();
        }
    }

    EndImGuiFrame();
}

void EditorApp::OnShutdown() {
    ShutdownImGui();
    m_panels.clear();
    NGE_LOG_INFO("Editor shut down");
}

void EditorApp::AddPanel(std::unique_ptr<EditorPanel> panel) {
    NGE_LOG_INFO("Added editor panel: {}", panel->GetName());
    m_panels.push_back(std::move(panel));
}

EditorPanel* EditorApp::GetPanel(const char* name) {
    for (auto& panel : m_panels) {
        if (std::string(panel->GetName()) == name) return panel.get();
    }
    return nullptr;
}

void EditorApp::AddMenuItem(const std::string& menu, const std::string& item, MenuCallback callback) {
    for (auto& entry : m_menus) {
        if (entry.name == menu) {
            entry.items.push_back({item, std::move(callback)});
            return;
        }
    }
    m_menus.push_back({menu, {{item, std::move(callback)}}});
}

void EditorApp::PushUndoAction(const std::string& description,
                                std::function<void()> undo,
                                std::function<void()> redo) {
    if (m_undoStack.size() >= MAX_UNDO_HISTORY) {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_undoStack.push_back({description, std::move(undo), std::move(redo)});
    m_redoStack.clear(); // New action invalidates redo history
}

void EditorApp::Undo() {
    if (m_undoStack.empty()) return;
    auto action = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    action.undo();
    m_redoStack.push_back(std::move(action));
    NGE_LOG_DEBUG("Undo: {}", m_redoStack.back().description);
}

void EditorApp::Redo() {
    if (m_redoStack.empty()) return;
    auto action = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    action.redo();
    m_undoStack.push_back(std::move(action));
    NGE_LOG_DEBUG("Redo: {}", m_undoStack.back().description);
}

void EditorApp::Select(ecs::Entity entity) {
    m_selectedEntity = entity;
}

void EditorApp::ClearSelection() {
    m_selectedEntity = ecs::Entity{};
}

void EditorApp::InitImGui() {
    // TODO: Initialize ImGui with Vulkan backend when imgui is available via vcpkg
    // ImGui::CreateContext();
    // ImGuiIO& io = ImGui::GetIO();
    // io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    // ImGui_ImplWin32_Init(m_window->GetNativeHandle());
    // ImGui_ImplVulkan_Init(...);
    m_imguiInitialized = true;
    NGE_LOG_INFO("ImGui initialized (stub)");
}

void EditorApp::ShutdownImGui() {
    if (!m_imguiInitialized) return;
    // ImGui_ImplVulkan_Shutdown();
    // ImGui_ImplWin32_Shutdown();
    // ImGui::DestroyContext();
    m_imguiInitialized = false;
}

void EditorApp::BeginImGuiFrame() {
    // ImGui_ImplVulkan_NewFrame();
    // ImGui_ImplWin32_NewFrame();
    // ImGui::NewFrame();
}

void EditorApp::EndImGuiFrame() {
    // ImGui::Render();
    // ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), ...);
    // ImGui::UpdatePlatformWindows();
    // ImGui::RenderPlatformWindowsDefault();
}

void EditorApp::DrawMenuBar() {
    // if (ImGui::BeginMainMenuBar()) {
    //     for (const auto& menu : m_menus) {
    //         if (ImGui::BeginMenu(menu.name.c_str())) {
    //             for (const auto& item : menu.items) {
    //                 if (ImGui::MenuItem(item.name.c_str())) {
    //                     item.callback();
    //                 }
    //             }
    //             ImGui::EndMenu();
    //         }
    //     }
    //     ImGui::EndMainMenuBar();
    // }
}

void EditorApp::DrawDockSpace() {
    // ImGuiViewport* viewport = ImGui::GetMainViewport();
    // ImGui::SetNextWindowPos(viewport->WorkPos);
    // ImGui::SetNextWindowSize(viewport->WorkSize);
    // ImGui::SetNextWindowViewport(viewport->ID);
    // ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
    //     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
    //     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
    //     ImGuiWindowFlags_MenuBar;
    // ImGui::Begin("DockSpace", nullptr, flags);
    // ImGuiID dockSpaceId = ImGui::GetID("MainDockSpace");
    // ImGui::DockSpace(dockSpaceId, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    // ImGui::End();
}

void EditorApp::HandleShortcuts() {
    using Key = platform::Key;
    bool ctrl = platform::Input::IsKeyDown(Key::LeftCtrl) || platform::Input::IsKeyDown(Key::RightCtrl);

    if (ctrl && platform::Input::IsKeyPressed(Key::Z)) Undo();
    if (ctrl && platform::Input::IsKeyPressed(Key::Y)) Redo();
    if (ctrl && platform::Input::IsKeyPressed(Key::S)) {
        NGE_LOG_INFO("Save (Ctrl+S)");
    }
    if (platform::Input::IsKeyPressed(Key::Delete)) {
        if (m_selectedEntity.IsValid()) {
            NGE_LOG_INFO("Delete selected entity");
        }
    }
}

} // namespace nge::editor
