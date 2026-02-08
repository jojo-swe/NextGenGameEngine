#include "engine/renderer/graph/graph_visualizer.h"
#include "engine/core/logging/log.h"
#include <fstream>
#include <algorithm>

namespace nge::renderer {

std::string GraphVisualizer::ToDot(const FrameGraphCompiler& compiler, const GraphVizConfig& config) {
    std::ostringstream dot;
    dot << "digraph " << EscapeDot(config.graphName) << " {\n";
    dot << "  rankdir=TB;\n";
    dot << "  node [shape=box, style=filled, fontname=\"Helvetica\"];\n";
    dot << "  edge [fontname=\"Helvetica\", fontsize=10];\n\n";

    const auto& result = compiler.GetResult();

    // Async compute cluster
    if (config.clusterByQueue && config.showAsyncCompute) {
        dot << "  subgraph cluster_graphics {\n";
        dot << "    label=\"Graphics Queue\";\n";
        dot << "    style=dashed;\n";
        dot << "    color=blue;\n";
        for (const auto& pass : result.orderedPasses) {
            if (pass.type != PassType::AsyncCompute) {
                dot << "    \"" << EscapeDot(pass.name) << "\" [fillcolor=\""
                    << PassColor(pass.type) << "\", label=\"" << EscapeDot(pass.name) << "\"];\n";
            }
        }
        dot << "  }\n\n";

        dot << "  subgraph cluster_async {\n";
        dot << "    label=\"Async Compute Queue\";\n";
        dot << "    style=dashed;\n";
        dot << "    color=red;\n";
        for (const auto& pass : result.orderedPasses) {
            if (pass.type == PassType::AsyncCompute) {
                dot << "    \"" << EscapeDot(pass.name) << "\" [fillcolor=\""
                    << PassColor(pass.type) << "\", label=\"" << EscapeDot(pass.name) << "\"];\n";
            }
        }
        dot << "  }\n\n";
    } else {
        for (const auto& pass : result.orderedPasses) {
            dot << "  \"" << EscapeDot(pass.name) << "\" [fillcolor=\""
                << PassColor(pass.type) << "\", label=\"" << EscapeDot(pass.name) << "\"];\n";
        }
        dot << "\n";
    }

    // Dependencies (edges)
    for (const auto& pass : result.orderedPasses) {
        for (const auto& dep : pass.dependencies) {
            dot << "  \"" << EscapeDot(dep) << "\" -> \"" << EscapeDot(pass.name) << "\";\n";
        }
    }

    // Resource lifetimes as notes
    if (config.showResourceLifetimes) {
        dot << "\n  // Resource lifetimes\n";
        for (const auto& res : result.resources) {
            dot << "  \"res_" << EscapeDot(res.name) << "\" [shape=ellipse, style=filled, "
                << "fillcolor=\"" << ResourceColor(res.usage) << "\", "
                << "label=\"" << EscapeDot(res.name) << "\\n"
                << res.width << "x" << res.height << "\"];\n";
        }

        // Connect resources to passes
        for (const auto& res : result.resources) {
            if (!res.producerPass.empty()) {
                dot << "  \"" << EscapeDot(res.producerPass) << "\" -> \"res_"
                    << EscapeDot(res.name) << "\" [style=dashed, color=gray];\n";
            }
            for (const auto& consumer : res.consumerPasses) {
                dot << "  \"res_" << EscapeDot(res.name) << "\" -> \""
                    << EscapeDot(consumer) << "\" [style=dashed, color=gray];\n";
            }
        }
    }

    // Barriers
    if (config.showBarriers) {
        dot << "\n  // Barriers\n";
        for (const auto& barrier : result.barriers) {
            dot << "  // Barrier: " << barrier.resourceName << " "
                << barrier.beforePass << " -> " << barrier.afterPass << "\n";
        }
    }

    dot << "}\n";
    return dot.str();
}

bool GraphVisualizer::ToFile(const FrameGraphCompiler& compiler, const std::string& filePath,
                              const GraphVizConfig& config) {
    std::string dot = ToDot(compiler, config);
    std::ofstream file(filePath);
    if (!file.is_open()) {
        NGE_LOG_ERROR("Failed to write graph visualization to: {}", filePath);
        return false;
    }
    file << dot;
    NGE_LOG_INFO("Render graph exported to: {}", filePath);
    return true;
}

std::string GraphVisualizer::ToTextReport(const FrameGraphCompiler& compiler) {
    std::ostringstream report;
    const auto& result = compiler.GetResult();

    report << "=== Render Graph Report ===\n";
    report << "Passes: " << result.orderedPasses.size() << "\n";
    report << "Resources: " << result.resources.size() << "\n";
    report << "Barriers: " << result.barriers.size() << "\n\n";

    report << "--- Pass Execution Order ---\n";
    for (size_t i = 0; i < result.orderedPasses.size(); ++i) {
        const auto& pass = result.orderedPasses[i];
        const char* typeStr = "Graphics";
        if (pass.type == PassType::Compute) typeStr = "Compute";
        if (pass.type == PassType::AsyncCompute) typeStr = "AsyncCompute";
        if (pass.type == PassType::Transfer) typeStr = "Transfer";

        report << "  [" << i << "] " << pass.name << " (" << typeStr << ")";
        if (!pass.dependencies.empty()) {
            report << " depends on: ";
            for (size_t d = 0; d < pass.dependencies.size(); ++d) {
                if (d > 0) report << ", ";
                report << pass.dependencies[d];
            }
        }
        report << "\n";
    }

    report << "\n--- Resource Lifetimes ---\n";
    for (const auto& res : result.resources) {
        report << "  " << res.name << ": produced by '" << res.producerPass << "', consumed by [";
        for (size_t c = 0; c < res.consumerPasses.size(); ++c) {
            if (c > 0) report << ", ";
            report << res.consumerPasses[c];
        }
        report << "]\n";
    }

    return report.str();
}

std::string GraphVisualizer::ToMermaid(const FrameGraphCompiler& compiler) {
    std::ostringstream mermaid;
    const auto& result = compiler.GetResult();

    mermaid << "```mermaid\ngraph TD\n";

    // Passes
    for (const auto& pass : result.orderedPasses) {
        std::string shape = (pass.type == PassType::Compute || pass.type == PassType::AsyncCompute)
            ? "{" + pass.name + "}" : "[" + pass.name + "]";
        mermaid << "  " << EscapeDot(pass.name) << shape << "\n";
    }

    // Edges
    for (const auto& pass : result.orderedPasses) {
        for (const auto& dep : pass.dependencies) {
            mermaid << "  " << EscapeDot(dep) << " --> " << EscapeDot(pass.name) << "\n";
        }
    }

    // Style async compute
    for (const auto& pass : result.orderedPasses) {
        if (pass.type == PassType::AsyncCompute) {
            mermaid << "  style " << EscapeDot(pass.name) << " fill:#f96\n";
        }
    }

    mermaid << "```\n";
    return mermaid.str();
}

std::string GraphVisualizer::PassColor(PassType type) {
    switch (type) {
        case PassType::Graphics:     return "#A3D9FF";
        case PassType::Compute:      return "#FFD700";
        case PassType::AsyncCompute: return "#FF6B6B";
        case PassType::Transfer:     return "#98FB98";
    }
    return "#FFFFFF";
}

std::string GraphVisualizer::ResourceColor(ResourceUsage usage) {
    switch (usage) {
        case ResourceUsage::ColorAttachment:  return "#FFDAB9";
        case ResourceUsage::DepthAttachment:  return "#D8BFD8";
        case ResourceUsage::ShaderRead:       return "#E0FFE0";
        case ResourceUsage::ShaderWrite:      return "#FFE0E0";
        case ResourceUsage::TransferSrc:      return "#E0E0FF";
        case ResourceUsage::TransferDst:      return "#FFFFE0";
    }
    return "#F0F0F0";
}

std::string GraphVisualizer::EscapeDot(const std::string& str) {
    std::string escaped;
    escaped.reserve(str.size());
    for (char c : str) {
        if (c == '"' || c == '\\') escaped += '\\';
        if (c == ' ') { escaped += '_'; continue; }
        escaped += c;
    }
    return escaped;
}

} // namespace nge::renderer
