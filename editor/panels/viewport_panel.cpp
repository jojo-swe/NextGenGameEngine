#include "editor/panels/viewport_panel.h"
#include "engine/core/logging/log.h"
#include "engine/core/platform/input.h"

namespace nge::editor {

// ─── Viewport Panel ──────────────────────────────────────────────────────

void ViewportPanel::OnUpdate(f32 /*deltaTime*/) {
    // Handle gizmo mode switching via keyboard
    using Key = platform::Key;
    if (m_focused) {
        if (platform::Input::IsKeyPressed(Key::W)) m_gizmoMode = GizmoMode::Translate;
        if (platform::Input::IsKeyPressed(Key::E)) m_gizmoMode = GizmoMode::Rotate;
        if (platform::Input::IsKeyPressed(Key::R)) m_gizmoMode = GizmoMode::Scale;
    }
}

void ViewportPanel::OnDraw() {
    // ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    // if (ImGui::Begin("Viewport", &m_visible)) {
    //     m_hovered = ImGui::IsWindowHovered();
    //     m_focused = ImGui::IsWindowFocused();
    //
    //     auto size = ImGui::GetContentRegionAvail();
    //     m_viewportWidth = size.x;
    //     m_viewportHeight = size.y;
    //
    //     // Render scene texture into viewport
    //     // ImGui::Image(sceneTextureId, size);
    //
    //     // Draw gizmo overlay for selected entity
    //     // if (m_app && m_app->GetSelectedEntity().IsValid()) {
    //     //     ImGuizmo::SetDrawlist();
    //     //     ImGuizmo::SetRect(pos.x, pos.y, size.x, size.y);
    //     //     // Draw translate/rotate/scale gizmo
    //     // }
    // }
    // ImGui::End();
    // ImGui::PopStyleVar();
}

// ─── Hierarchy Panel ─────────────────────────────────────────────────────

void HierarchyPanel::OnUpdate(f32 /*deltaTime*/) {}

void HierarchyPanel::OnDraw() {
    // if (ImGui::Begin("Hierarchy", &m_visible)) {
    //     // Search bar
    //     ImGui::InputTextWithHint("##search", "Search...", &m_searchFilter);
    //     ImGui::Separator();
    //
    //     // Iterate root entities and draw tree
    //     // for (auto entity : m_app->GetWorld().GetRootEntities()) {
    //     //     DrawEntityNode(entity);
    //     // }
    //
    //     // Right-click context menu
    //     if (ImGui::BeginPopupContextWindow()) {
    //         if (ImGui::MenuItem("Create Empty Entity")) { /* ... */ }
    //         if (ImGui::MenuItem("Create Cube")) { /* ... */ }
    //         if (ImGui::MenuItem("Create Light")) { /* ... */ }
    //         if (ImGui::MenuItem("Create Camera")) { /* ... */ }
    //         ImGui::EndPopup();
    //     }
    // }
    // ImGui::End();
}

void HierarchyPanel::DrawEntityNode(ecs::Entity /*entity*/) {
    // ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    // if (m_app->GetSelectedEntity() == entity) flags |= ImGuiTreeNodeFlags_Selected;
    // if (/* no children */) flags |= ImGuiTreeNodeFlags_Leaf;
    //
    // bool opened = ImGui::TreeNodeEx((void*)(u64)entity.id, flags, "%s", entityName);
    // if (ImGui::IsItemClicked()) m_app->Select(entity);
    //
    // // Drag-drop for reparenting
    // if (ImGui::BeginDragDropSource()) { /* ... */ ImGui::EndDragDropSource(); }
    // if (ImGui::BeginDragDropTarget()) { /* ... */ ImGui::EndDragDropTarget(); }
    //
    // if (opened) {
    //     // Recurse children
    //     ImGui::TreePop();
    // }
}

// ─── Inspector Panel ─────────────────────────────────────────────────────

void InspectorPanel::OnUpdate(f32 /*deltaTime*/) {}

void InspectorPanel::OnDraw() {
    // if (ImGui::Begin("Inspector", &m_visible)) {
    //     if (m_app && m_app->GetSelectedEntity().IsValid()) {
    //         // Draw component editors based on what the entity has
    //         DrawTransformComponent();
    //         DrawCameraComponent();
    //         DrawMaterialComponent();
    //         DrawLightComponent();
    //         DrawPhysicsComponent();
    //
    //         // Add component button
    //         ImGui::Separator();
    //         if (ImGui::Button("Add Component")) {
    //             ImGui::OpenPopup("AddComponent");
    //         }
    //         if (ImGui::BeginPopup("AddComponent")) {
    //             if (ImGui::MenuItem("Camera")) { /* ... */ }
    //             if (ImGui::MenuItem("Light")) { /* ... */ }
    //             if (ImGui::MenuItem("Mesh Renderer")) { /* ... */ }
    //             if (ImGui::MenuItem("Rigid Body")) { /* ... */ }
    //             if (ImGui::MenuItem("Collider")) { /* ... */ }
    //             ImGui::EndPopup();
    //         }
    //     } else {
    //         // ImGui::TextDisabled("No entity selected");
    //     }
    // }
    // ImGui::End();
}

void InspectorPanel::DrawTransformComponent() {
    // if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    //     auto* transform = m_app->GetWorld().GetComponent<scene::Transform>(entity);
    //     if (transform) {
    //         // Position, rotation (Euler), scale editors
    //         // ImGui::DragFloat3("Position", &pos.x, 0.1f);
    //         // ImGui::DragFloat3("Rotation", &euler.x, 1.0f);
    //         // ImGui::DragFloat3("Scale", &scale.x, 0.01f);
    //     }
    // }
}

void InspectorPanel::DrawCameraComponent() {}
void InspectorPanel::DrawMaterialComponent() {}
void InspectorPanel::DrawLightComponent() {}
void InspectorPanel::DrawPhysicsComponent() {}

// ─── Console Panel ───────────────────────────────────────────────────────

void ConsolePanel::OnUpdate(f32 /*deltaTime*/) {}

void ConsolePanel::OnDraw() {
    // if (ImGui::Begin("Console", &m_visible)) {
    //     // Toolbar: filter buttons, clear, auto-scroll toggle
    //     // if (ImGui::Button("Clear")) Clear();
    //     // ImGui::SameLine();
    //     // ImGui::Checkbox("Auto-scroll", &m_autoScroll);
    //     // ImGui::SameLine();
    //     // ImGui::Checkbox("Info", &m_showInfo); ...
    //
    //     // Log entries
    //     // ImGui::BeginChild("ScrollRegion");
    //     // for (const auto& entry : m_logs) {
    //     //     if (!PassesFilter(entry)) continue;
    //     //     ImVec4 color = GetColorForLevel(entry.level);
    //     //     ImGui::PushStyleColor(ImGuiCol_Text, color);
    //     //     ImGui::TextUnformatted(entry.message.c_str());
    //     //     ImGui::PopStyleColor();
    //     // }
    //     // if (m_autoScroll) ImGui::SetScrollHereY(1.0f);
    //     // ImGui::EndChild();
    // }
    // ImGui::End();
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

// ─── Asset Browser Panel ─────────────────────────────────────────────────

void AssetBrowserPanel::OnUpdate(f32 /*deltaTime*/) {}

void AssetBrowserPanel::OnDraw() {
    // if (ImGui::Begin("Assets", &m_visible)) {
    //     // Breadcrumb path bar
    //     // Directory tree (left) + file grid (right)
    //     // Thumbnail previews for textures/meshes
    //     // Drag-drop to scene or inspector
    //     // Right-click: import, create, rename, delete
    // }
    // ImGui::End();
}

// ─── Profiler Panel ──────────────────────────────────────────────────────

void ProfilerPanel::OnUpdate(f32 deltaTime) {
    f32 frameMs = deltaTime * 1000.0f;
    m_frameTimesMs[m_frameIndex % FRAME_HISTORY] = frameMs;
    m_frameIndex++;

    // Compute stats
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
    // if (ImGui::Begin("Profiler", &m_visible)) {
    //     ImGui::Text("FPS: %.1f", 1000.0f / m_avgFrameTime);
    //     ImGui::Text("Frame: %.2f ms (avg), %.2f ms (max)", m_avgFrameTime, m_maxFrameTime);
    //     ImGui::Separator();
    //
    //     // Frame time graph
    //     ImGui::PlotLines("Frame Time (ms)", m_frameTimesMs, FRAME_HISTORY,
    //                      m_frameIndex % FRAME_HISTORY, nullptr, 0, 33.3f, ImVec2(0, 80));
    //
    //     // Memory stats
    //     // ImGui::Text("RAM: %.1f MB", memUsage / (1024.0f * 1024.0f));
    //     // ImGui::Text("VRAM: %.1f MB", vramUsage / (1024.0f * 1024.0f));
    //
    //     // GPU timing breakdown
    //     // Draw bars for each render pass timing
    // }
    // ImGui::End();
}

} // namespace nge::editor
