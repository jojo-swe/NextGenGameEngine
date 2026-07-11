#include "editor/editor_app.h"
#include "editor/panels/viewport_panel.h"

#ifdef _WIN32
#include <windows.h>
#define DBG_PRINT(msg) OutputDebugStringA(msg)
#else
#define DBG_PRINT(msg)
#endif

using namespace nge;
using namespace nge::editor;

int main() {
    DBG_PRINT("[EDITOR] main() entered\n");

    EditorConfig config;
    config.projectPath = ".";
    config.enableDocking = true;

    DBG_PRINT("[EDITOR] creating EditorApp\n");
    EditorApp app(config);
    DBG_PRINT("[EDITOR] EditorApp created\n");

    // Register built-in panels
    DBG_PRINT("[EDITOR] creating ViewportPanel\n");
    auto viewport = std::make_unique<ViewportPanel>();
    viewport->SetEditorApp(&app);
    app.AddPanel(std::move(viewport));

    DBG_PRINT("[EDITOR] creating HierarchyPanel\n");
    auto hierarchy = std::make_unique<HierarchyPanel>();
    hierarchy->SetEditorApp(&app);
    app.AddPanel(std::move(hierarchy));

    DBG_PRINT("[EDITOR] creating InspectorPanel\n");
    auto inspector = std::make_unique<InspectorPanel>();
    inspector->SetEditorApp(&app);
    app.AddPanel(std::move(inspector));

    DBG_PRINT("[EDITOR] creating ConsolePanel\n");
    auto console = std::make_unique<ConsolePanel>();
    app.AddPanel(std::move(console));

    DBG_PRINT("[EDITOR] creating AssetBrowserPanel\n");
    auto assets = std::make_unique<AssetBrowserPanel>();
    assets->SetRootPath(config.projectPath);
    app.AddPanel(std::move(assets));

    DBG_PRINT("[EDITOR] creating ProfilerPanel\n");
    auto profiler = std::make_unique<ProfilerPanel>();
    app.AddPanel(std::move(profiler));

    DBG_PRINT("[EDITOR] calling app.Run()\n");
    return app.Run();
}
