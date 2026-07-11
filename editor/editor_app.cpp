#include "editor/editor_app.h"
#include "engine/core/logging/log.h"
#include "engine/core/platform/input.h"
#include "engine/core/platform/window.h"
#include "engine/rhi/common/rhi_device.h"

#include <cstdio>

static void EditorTraceLog(const char* msg) {
    FILE* f = nullptr;
    fopen_s(&f, "editor_debug_trace.log", "a");
    if (f) {
        fprintf(f, "%s", msg);
        fflush(f);
        fclose(f);
    }
}

#ifdef NGE_HAS_IMGUI
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_win32.h>

#include <vulkan/vulkan.h>

#include "engine/rhi/vulkan/vulkan_device.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

namespace nge::editor {

#ifdef NGE_HAS_IMGUI
static VkDescriptorPool g_imguiDescriptorPool = VK_NULL_HANDLE;

static void CheckVkResult(VkResult err) {
    if (err == VK_SUCCESS) return;
    NGE_LOG_ERROR("[ImGui Vulkan] VkResult = {}", static_cast<int>(err));
}
#endif

EditorApp::EditorApp(const EditorConfig& config)
    : Application({
        .title  = "NextGen Engine Editor",
        .width  = 1920,
        .height = 1080,
    })
    , m_editorConfig(config) {}

EditorApp::~EditorApp() = default;

void EditorApp::OnInit() {
    EditorTraceLog("[EDITOR] OnInit()\n");
    NGE_LOG_INFO("Editor initializing...");

    EditorTraceLog("[EDITOR] calling InitImGui()\n");
    InitImGui();
    EditorTraceLog("[EDITOR] InitImGui() done\n");

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

    for (auto& panel : m_panels) {
        if (panel->IsVisible()) {
            panel->OnUpdate(deltaTime);
        }
    }

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
    if (m_device) {
        m_device->WaitIdle();
    }
    ShutdownImGui();
    m_panels.clear();
    NGE_LOG_INFO("Editor shut down");
}

void EditorApp::AddPanel(std::unique_ptr<EditorPanel> panel) {
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
    m_redoStack.clear();
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
#ifdef NGE_HAS_IMGUI
    EditorTraceLog("[EDITOR] InitImGui: CreateContext\n");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = m_editorConfig.layoutFile.c_str();

    ImGui::StyleColorsDark();

    EditorTraceLog("[EDITOR] InitImGui: dynamic_cast\n");
    auto* vkDevice = dynamic_cast<nge::rhi::vulkan::VulkanDevice*>(m_device.get());
    if (!vkDevice) {
        NGE_LOG_ERROR("Editor requires Vulkan backend for ImGui");
        return;
    }

    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    EditorTraceLog("[EDITOR] InitImGui: vkCreateDescriptorPool\n");
    vkCreateDescriptorPool(vkDevice->GetVkDevice(), &poolInfo, nullptr, &g_imguiDescriptorPool);

    EditorTraceLog("[EDITOR] InitImGui: ImGui_ImplWin32_Init\n");
    ImGui_ImplWin32_Init(m_window->GetNativeHandle());

    // Route Win32 messages through ImGui's WndProc handler
    extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    m_window->SetEventCallback([](void* hwnd, u32 msg, u64 wParam, i64 lParam) -> bool {
        return ImGui_ImplWin32_WndProcHandler(
            static_cast<HWND>(hwnd),
            static_cast<UINT>(msg),
            static_cast<WPARAM>(wParam),
            static_cast<LPARAM>(lParam)) != 0;
    });

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.Instance = vkDevice->GetVkInstance();
    initInfo.PhysicalDevice = vkDevice->GetVkPhysicalDevice();
    initInfo.Device = vkDevice->GetVkDevice();
    initInfo.QueueFamily = vkDevice->GetGraphicsQueueFamily();
    initInfo.Queue = vkDevice->GetGraphicsQueue();
    initInfo.DescriptorPool = g_imguiDescriptorPool;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = vkDevice->GetSwapchainImageCount();
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;
    initInfo.CheckVkResultFn = CheckVkResult;

    // Configure dynamic rendering with swapchain format
    initInfo.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    VkFormat colorFormat = vkDevice->GetVkSwapchainFormat();
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;

    EditorTraceLog("[EDITOR] InitImGui: ImGui_ImplVulkan_Init\n");
    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        NGE_LOG_ERROR("Failed to initialize ImGui Vulkan backend");
        return;
    }

    EditorTraceLog("[EDITOR] InitImGui: CreateFontsTexture\n");
    ImGui_ImplVulkan_CreateFontsTexture();

    m_renderPipeline.SetPostRenderCallback([this](nge::rhi::ICommandList* cmd) {
        if (!m_imguiInitialized) return;
        auto* vkDev = dynamic_cast<nge::rhi::vulkan::VulkanDevice*>(m_device.get());
        if (!vkDev) return;

        // ImGui needs to render inside a dynamic rendering pass on the swapchain
        rhi::TextureHandle swapchain = m_device->GetSwapchainTexture();
        rhi::Viewport viewport{0, 0,
            static_cast<f32>(m_device->GetSwapchainWidth()),
            static_cast<f32>(m_device->GetSwapchainHeight()), 0, 1};
        rhi::Scissor scissor{0, 0,
            m_device->GetSwapchainWidth(),
            m_device->GetSwapchainHeight()};
        rhi::LoadOp loadOp = rhi::LoadOp::Load;
        cmd->BeginRendering(&swapchain, 1, rhi::TextureHandle{}, nullptr,
            viewport, scissor, &loadOp);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkDev->GetCurrentCommandBuffer());
        cmd->EndRendering();
    });

    m_imguiInitialized = true;
    NGE_LOG_INFO("ImGui initialized (Vulkan + Win32 backend)");
#else
    m_imguiInitialized = true;
    NGE_LOG_INFO("ImGui initialized (stub - no NGE_HAS_IMGUI)");
#endif
}

void EditorApp::ShutdownImGui() {
    if (!m_imguiInitialized) return;

#ifdef NGE_HAS_IMGUI
    if (m_device) m_device->WaitIdle();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    auto* vkDevice = dynamic_cast<nge::rhi::vulkan::VulkanDevice*>(m_device.get());
    if (vkDevice && g_imguiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vkDevice->GetVkDevice(), g_imguiDescriptorPool, nullptr);
        g_imguiDescriptorPool = VK_NULL_HANDLE;
    }
#endif

    m_imguiInitialized = false;
}

void EditorApp::BeginImGuiFrame() {
#ifdef NGE_HAS_IMGUI
    if (!m_imguiInitialized) return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
#endif
}

void EditorApp::EndImGuiFrame() {
#ifdef NGE_HAS_IMGUI
    if (!m_imguiInitialized) return;
    ImGui::Render();
#endif
}

void EditorApp::DrawMenuBar() {
#ifdef NGE_HAS_IMGUI
    if (ImGui::BeginMainMenuBar()) {
        for (const auto& menu : m_menus) {
            if (ImGui::BeginMenu(menu.name.c_str())) {
                for (const auto& item : menu.items) {
                    if (ImGui::MenuItem(item.name.c_str())) {
                        item.callback();
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndMainMenuBar();
    }
#endif
}

void EditorApp::DrawDockSpace() {
#ifdef NGE_HAS_IMGUI
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpace", nullptr, flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockSpaceId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockSpaceId, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();
#endif
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
