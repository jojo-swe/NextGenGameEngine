#include "editor/panels/viewport_panel.h"
#include "engine/core/logging/log.h"
#include "engine/core/platform/input.h"

#ifdef NGE_HAS_IMGUI
#include <imgui.h>
#endif

namespace nge::editor {

void ViewportPanel::OnUpdate(f32 /*deltaTime*/) {
    using Key = platform::Key;
    if (m_focused) {
        if (platform::Input::IsKeyPressed(Key::W)) m_gizmoMode = GizmoMode::Translate;
        if (platform::Input::IsKeyPressed(Key::E)) m_gizmoMode = GizmoMode::Rotate;
        if (platform::Input::IsKeyPressed(Key::R)) m_gizmoMode = GizmoMode::Scale;
    }
}

void ViewportPanel::OnDraw() {
#ifdef NGE_HAS_IMGUI
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("Viewport", &m_visible)) {
        m_hovered = ImGui::IsWindowHovered();
        m_focused = ImGui::IsWindowFocused();

        auto size = ImGui::GetContentRegionAvail();
        m_viewportWidth = size.x;
        m_viewportHeight = size.y;

        ImGui::TextDisabled("Scene render target (not yet wired)");
    }
    ImGui::End();
    ImGui::PopStyleVar();
#endif
}

void HierarchyPanel::OnUpdate(f32 /*deltaTime*/) {}

void HierarchyPanel::OnDraw() {
#ifdef NGE_HAS_IMGUI
    if (ImGui::Begin("Hierarchy", &m_visible)) {
        char searchBuf[256] = {};
        ImGui::InputTextWithHint("##search", "Search...", searchBuf, sizeof(searchBuf));
        ImGui::Separator();

        if (ImGui::BeginPopupContextWindow()) {
            if (ImGui::MenuItem("Create Empty Entity")) { NGE_LOG_INFO("Create Empty Entity"); }
            if (ImGui::MenuItem("Create Cube")) { NGE_LOG_INFO("Create Cube"); }
            if (ImGui::MenuItem("Create Light")) { NGE_LOG_INFO("Create Light"); }
            if (ImGui::MenuItem("Create Camera")) { NGE_LOG_INFO("Create Camera"); }
            ImGui::EndPopup();
        }

        if (m_app) {
            auto* world = m_app->GetWorld();
            (void)world;
            // TODO: iterate root entities when ECS query API is available
        }
    }
    ImGui::End();
#endif
}

void HierarchyPanel::DrawEntityNode(ecs::Entity /*entity*/) {
#ifdef NGE_HAS_IMGUI
    // TODO: implement when entity name system is available
#endif
}

void InspectorPanel::OnUpdate(f32 /*deltaTime*/) {}

void InspectorPanel::OnDraw() {
#ifdef NGE_HAS_IMGUI
    if (ImGui::Begin("Inspector", &m_visible)) {
        if (m_app && m_app->GetSelectedEntity().IsValid()) {
            DrawTransformComponent();
            DrawCameraComponent();
            DrawMaterialComponent();
            DrawLightComponent();
            DrawPhysicsComponent();

            ImGui::Separator();
            if (ImGui::Button("Add Component")) {
                ImGui::OpenPopup("AddComponent");
            }
            if (ImGui::BeginPopup("AddComponent")) {
                if (ImGui::MenuItem("Camera")) {}
                if (ImGui::MenuItem("Light")) {}
                if (ImGui::MenuItem("Mesh Renderer")) {}
                if (ImGui::MenuItem("Rigid Body")) {}
                if (ImGui::MenuItem("Collider")) {}
                ImGui::EndPopup();
            }
        } else {
            ImGui::TextDisabled("No entity selected");
        }
    }
    ImGui::End();
#endif
}

void InspectorPanel::DrawTransformComponent() {
#ifdef NGE_HAS_IMGUI
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        float pos[3] = {0, 0, 0};
        float rot[3] = {0, 0, 0};
        float scale[3] = {1, 1, 1};
        ImGui::DragFloat3("Position", pos, 0.1f);
        ImGui::DragFloat3("Rotation", rot, 1.0f);
        ImGui::DragFloat3("Scale", scale, 0.01f);
    }
#endif
}

void InspectorPanel::DrawCameraComponent() {}
void InspectorPanel::DrawMaterialComponent() {}
void InspectorPanel::DrawLightComponent() {}
void InspectorPanel::DrawPhysicsComponent() {}

void ConsolePanel::OnUpdate(f32 /*deltaTime*/) {}

void ConsolePanel::OnDraw() {
#ifdef NGE_HAS_IMGUI
    if (ImGui::Begin("Console", &m_visible)) {
        if (ImGui::Button("Clear")) Clear();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_autoScroll);
        ImGui::SameLine();
        ImGui::Checkbox("Info", &m_showInfo);
        ImGui::SameLine();
        ImGui::Checkbox("Warn", &m_showWarn);
        ImGui::SameLine();
        ImGui::Checkbox("Error", &m_showError);
        ImGui::SameLine();
        ImGui::Checkbox("Debug", &m_showDebug);

        ImGui::Separator();
        ImGui::BeginChild("ScrollRegion");
        for (const auto& entry : m_logs) {
            bool show = (entry.level == 0 && m_showInfo) ||
                        (entry.level == 1 && m_showWarn) ||
                        (entry.level == 2 && m_showError) ||
                        (entry.level == 3 && m_showDebug);
            if (!show) continue;

            ImVec4 color = ImVec4(1, 1, 1, 1);
            if (entry.level == 1) color = ImVec4(1, 1, 0, 1);
            else if (entry.level == 2) color = ImVec4(1, 0.3f, 0.3f, 1);
            else if (entry.level == 3) color = ImVec4(0.5f, 0.5f, 0.5f, 1);

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(entry.message.c_str());
            ImGui::PopStyleColor();
        }
        if (m_autoScroll && !m_logs.empty()) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    ImGui::End();
#endif
}

void ConsolePanel::AddLog(const std::string& msg, u8 level) {
    if (m_logs.size() >= MAX_LOGS) {
        m_logs.erase(m_logs.begin());
    }
    m_logs.push_back({msg, level});
}

void ConsolePanel::Clear() {
    m_logs.clear();
}

void AssetBrowserPanel::OnUpdate(f32 /*deltaTime*/) {}

void AssetBrowserPanel::OnDraw() {
#ifdef NGE_HAS_IMGUI
    if (ImGui::Begin("Assets", &m_visible)) {
        ImGui::Text("Root: %s", m_rootPath.c_str());
        ImGui::Separator();
        // TODO: directory browsing with std::filesystem
    }
    ImGui::End();
#endif
}

void ProfilerPanel::OnUpdate(f32 deltaTime) {
    f32 frameMs = deltaTime * 1000.0f;
    m_frameTimesMs[m_frameIndex % FRAME_HISTORY] = frameMs;
    m_frameIndex++;

    f32 sum = 0, maxVal = 0;
    u32 count = math::Min(m_frameIndex, FRAME_HISTORY);
    for (u32 i = 0; i < count; ++i) {
        sum += m_frameTimesMs[i];
        if (m_frameTimesMs[i] > maxVal) maxVal = m_frameTimesMs[i];
    }
    m_avgFrameTime = count > 0 ? sum / static_cast<f32>(count) : 0;
    m_maxFrameTime = maxVal;
}

void ProfilerPanel::OnDraw() {
#ifdef NGE_HAS_IMGUI
    if (ImGui::Begin("Profiler", &m_visible)) {
        if (m_avgFrameTime > 0) {
            ImGui::Text("FPS: %.1f", 1000.0f / m_avgFrameTime);
        }
        ImGui::Text("Frame: %.2f ms (avg), %.2f ms (max)", m_avgFrameTime, m_maxFrameTime);
        ImGui::Separator();

        ImGui::PlotLines("Frame Time (ms)", m_frameTimesMs, FRAME_HISTORY,
                         m_frameIndex % FRAME_HISTORY, nullptr, 0, 33.3f, ImVec2(0, 80));
    }
    ImGui::End();
#endif
}

} // namespace nge::editor
