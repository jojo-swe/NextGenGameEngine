#include "editor/editor_app.h"
#include "editor/panels/viewport_panel.h"

#ifdef NGE_DEBUG_TRACE
#include <cstdio>
static void TraceLog(const char* msg) {
    FILE* f = nullptr;
    fopen_s(&f, "editor_debug_trace.log", "a");
    if (f) {
        fprintf(f, "%s", msg);
        fflush(f);
        fclose(f);
    }
}
#define TRACE(msg) TraceLog(msg)
#else
#define TRACE(msg)
#endif

using namespace nge;
using namespace nge::editor;

int main() {
    TRACE("[EDITOR] main() entered\n");

    EditorConfig config;
    config.projectPath = ".";
    config.enableDocking = true;

    TRACE("[EDITOR] creating EditorApp\n");
    EditorApp app(config);
    TRACE("[EDITOR] EditorApp created\n");

    // Register built-in panels
    TRACE("[EDITOR] creating ViewportPanel\n");
    auto viewport = std::make_unique<ViewportPanel>();
    viewport->SetEditorApp(&app);
    app.AddPanel(std::move(viewport));

    TRACE("[EDITOR] creating HierarchyPanel\n");
    auto hierarchy = std::make_unique<HierarchyPanel>();
    hierarchy->SetEditorApp(&app);
    app.AddPanel(std::move(hierarchy));

    TRACE("[EDITOR] creating InspectorPanel\n");
    auto inspector = std::make_unique<InspectorPanel>();
    inspector->SetEditorApp(&app);
    app.AddPanel(std::move(inspector));

    TRACE("[EDITOR] creating ConsolePanel\n");
    auto console = std::make_unique<ConsolePanel>();
    app.AddPanel(std::move(console));

    TRACE("[EDITOR] creating AssetBrowserPanel\n");
    auto assets = std::make_unique<AssetBrowserPanel>();
    assets->SetRootPath(config.projectPath);
    app.AddPanel(std::move(assets));

    TRACE("[EDITOR] creating ProfilerPanel\n");
    auto profiler = std::make_unique<ProfilerPanel>();
    app.AddPanel(std::move(profiler));

    TRACE("[EDITOR] calling app.Run()\n");
    return app.Run();
}
