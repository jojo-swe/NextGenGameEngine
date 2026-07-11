#include "editor/editor_app.h"
#include "editor/panels/viewport_panel.h"

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

using namespace nge;
using namespace nge::editor;

int main() {
    TraceLog("[EDITOR] main() entered\n");

    EditorConfig config;
    config.projectPath = ".";
    config.enableDocking = true;

    TraceLog("[EDITOR] creating EditorApp\n");
    EditorApp app(config);
    TraceLog("[EDITOR] EditorApp created\n");

    // Register built-in panels
    TraceLog("[EDITOR] creating ViewportPanel\n");
    auto viewport = std::make_unique<ViewportPanel>();
    TraceLog("[EDITOR] ViewportPanel constructed\n");
    viewport->SetEditorApp(&app);
    TraceLog("[EDITOR] ViewportPanel SetEditorApp\n");
    app.AddPanel(std::move(viewport));
    TraceLog("[EDITOR] ViewportPanel added\n");

    TraceLog("[EDITOR] creating HierarchyPanel\n");
    auto hierarchy = std::make_unique<HierarchyPanel>();
    hierarchy->SetEditorApp(&app);
    app.AddPanel(std::move(hierarchy));

    TraceLog("[EDITOR] creating InspectorPanel\n");
    auto inspector = std::make_unique<InspectorPanel>();
    inspector->SetEditorApp(&app);
    app.AddPanel(std::move(inspector));

    TraceLog("[EDITOR] creating ConsolePanel\n");
    auto console = std::make_unique<ConsolePanel>();
    app.AddPanel(std::move(console));

    TraceLog("[EDITOR] creating AssetBrowserPanel\n");
    auto assets = std::make_unique<AssetBrowserPanel>();
    assets->SetRootPath(config.projectPath);
    app.AddPanel(std::move(assets));

    TraceLog("[EDITOR] creating ProfilerPanel\n");
    auto profiler = std::make_unique<ProfilerPanel>();
    app.AddPanel(std::move(profiler));

    TraceLog("[EDITOR] calling app.Run()\n");
    return app.Run();
}
