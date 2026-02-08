#pragma once

#include "engine/core/types.h"
#include "engine/renderer/graph/frame_graph_compiler.h"
#include <string>
#include <vector>
#include <sstream>

namespace nge::renderer {

// ─── Render Graph Dependency Visualizer ──────────────────────────────────
// Exports compiled render graphs to DOT/Graphviz format for debugging.
// Visualizes passes as nodes, dependencies as edges, resource lifetimes,
// and async compute boundaries.

struct GraphVizConfig {
    bool showResourceLifetimes = true;
    bool showBarriers = true;
    bool showAsyncCompute = true;
    bool showDeadPasses = false;     // Show eliminated passes (greyed out)
    bool clusterByQueue = true;      // Group async compute in subgraphs
    std::string graphName = "RenderGraph";
};

class GraphVisualizer {
public:
    // Export compiled graph to DOT format string
    static std::string ToDot(const FrameGraphCompiler& compiler, const GraphVizConfig& config = {});

    // Export to DOT file
    static bool ToFile(const FrameGraphCompiler& compiler, const std::string& filePath,
                       const GraphVizConfig& config = {});

    // Generate a simple text-based dependency list (for logging)
    static std::string ToTextReport(const FrameGraphCompiler& compiler);

    // Generate Mermaid diagram format (for markdown docs)
    static std::string ToMermaid(const FrameGraphCompiler& compiler);

private:
    static std::string PassColor(PassType type);
    static std::string ResourceColor(ResourceUsage usage);
    static std::string EscapeDot(const std::string& str);
};

} // namespace nge::renderer
