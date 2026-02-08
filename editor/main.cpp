#include "editor/editor_app.h"
#include "editor/panels/viewport_panel.h"

using namespace nge;
using namespace nge::editor;

int main() {
    EditorConfig config;
    config.projectPath = ".";
    config.enableDocking = true;

    EditorApp app(config);

    // Register built-in panels
    auto viewport = std::make_unique<ViewportPanel>();
    viewport->SetEditorApp(&app);
    app.AddPanel(std::move(viewport));

    auto hierarchy = std::make_unique<HierarchyPanel>();
    hierarchy->SetEditorApp(&app);
    app.AddPanel(std::move(hierarchy));

    auto inspector = std::make_unique<InspectorPanel>();
    inspector->SetEditorApp(&app);
    app.AddPanel(std::move(inspector));

    auto console = std::make_unique<ConsolePanel>();
    app.AddPanel(std::move(console));

    auto assets = std::make_unique<AssetBrowserPanel>();
    assets->SetRootPath(config.projectPath);
    app.AddPanel(std::move(assets));

    auto profiler = std::make_unique<ProfilerPanel>();
    app.AddPanel(std::move(profiler));

    return app.Run();
}
