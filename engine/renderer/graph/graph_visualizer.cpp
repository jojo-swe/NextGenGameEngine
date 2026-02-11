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
    const auto& passes = compiler.GetPasses();
    const auto& resources = compiler.GetResources();

    auto passName = [&](u32 passId) -> std::string {
        return (passId < passes.size()) ? passes[passId].name : "Unknown";
    };
    auto passType = [&](const CompiledPass& cp) -> PassType {
        return cp.queueType;
    };

    // Async compute cluster
    if (config.clusterByQueue && config.showAsyncCompute) {
        dot << "  subgraph cluster_graphics {\n";
        dot << "    label=\"Graphics Queue\";\n";
        dot << "    style=dashed;\n";
        dot << "    color=blue;\n";
        for (const auto& cp : result.passes) {
            if (passType(cp) != PassType::AsyncCompute) {
                std::string name = passName(cp.passId);
                dot << "    \"" << EscapeDot(name) << "\" [fillcolor=\""
                    << PassColor(passType(cp)) << "\", label=\"" << EscapeDot(name) << "\"];\n";
            }
        }
        dot << "  }\n\n";

        dot << "  subgraph cluster_async {\n";
        dot << "    label=\"Async Compute Queue\";\n";
        dot << "    style=dashed;\n";
        dot << "    color=red;\n";
        for (const auto& cp : result.passes) {
            if (passType(cp) == PassType::AsyncCompute) {
                std::string name = passName(cp.passId);
                dot << "    \"" << EscapeDot(name) << "\" [fillcolor=\""
                    << PassColor(passType(cp)) << "\", label=\"" << EscapeDot(name) << "\"];\n";
            }
        }
        dot << "  }\n\n";
    } else {
        for (const auto& cp : result.passes) {
            std::string name = passName(cp.passId);
            dot << "  \"" << EscapeDot(name) << "\" [fillcolor=\""
                << PassColor(passType(cp)) << "\", label=\"" << EscapeDot(name) << "\"];\n";
        }
        dot << "\n";
    }

    // Dependencies (edges from syncBefore)
    for (const auto& cp : result.passes) {
        for (u32 dep : cp.syncBefore) {
            dot << "  \"" << EscapeDot(passName(dep)) << "\" -> \"" << EscapeDot(passName(cp.passId)) << "\";\n";
        }
    }

    // Resource lifetimes as notes
    if (config.showResourceLifetimes) {
        dot << "\n  // Resource lifetimes\n";
        for (size_t i = 0; i < resources.size(); ++i) {
            const auto& res = resources[i];
            dot << "  \"res_" << EscapeDot(res.name) << "\" [shape=ellipse, style=filled, "
                << "fillcolor=\"#E0FFE0\", "
                << "label=\"" << EscapeDot(res.name) << "\"];\n";
        }
    }

    // Barriers
    if (config.showBarriers) {
        dot << "\n  // Barriers\n";
        for (const auto& cp : result.passes) {
            for (const auto& b : cp.barriers) {
                std::string resName = (b.resourceId < resources.size()) ? resources[b.resourceId].name : "?";
                dot << "  // Barrier: " << resName << " at pass " << passName(cp.passId) << "\n";
            }
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
    const auto& passes = compiler.GetPasses();

    report << "=== Render Graph Report ===\n";
    report << "Passes: " << result.passes.size() << "\n";
    report << "Eliminated: " << result.eliminatedPasses.size() << "\n";
    report << "Async: " << result.asyncPassCount << "\n\n";

    report << "--- Pass Execution Order ---\n";
    for (const auto& cp : result.passes) {
        const char* typeStr = "Graphics";
        if (cp.queueType == PassType::Compute) typeStr = "Compute";
        if (cp.queueType == PassType::AsyncCompute) typeStr = "AsyncCompute";
        if (cp.queueType == PassType::Transfer) typeStr = "Transfer";

        std::string name = (cp.passId < passes.size()) ? passes[cp.passId].name : "Unknown";
        report << "  [" << cp.executionOrder << "] " << name << " (" << typeStr << ")";
        if (!cp.syncBefore.empty()) {
            report << " waits on: ";
            for (size_t d = 0; d < cp.syncBefore.size(); ++d) {
                if (d > 0) report << ", ";
                u32 depId = cp.syncBefore[d];
                report << ((depId < passes.size()) ? passes[depId].name : "?");
            }
        }
        report << "\n";
    }

    return report.str();
}

std::string GraphVisualizer::ToMermaid(const FrameGraphCompiler& compiler) {
    std::ostringstream mermaid;
    const auto& result = compiler.GetResult();
    const auto& passes = compiler.GetPasses();

    mermaid << "```mermaid\ngraph TD\n";

    // Passes
    for (const auto& cp : result.passes) {
        std::string name = (cp.passId < passes.size()) ? passes[cp.passId].name : "Unknown";
        std::string shape = (cp.queueType == PassType::Compute || cp.queueType == PassType::AsyncCompute)
            ? "{" + name + "}" : "[" + name + "]";
        mermaid << "  " << EscapeDot(name) << shape << "\n";
    }

    // Edges
    for (const auto& cp : result.passes) {
        std::string name = (cp.passId < passes.size()) ? passes[cp.passId].name : "Unknown";
        for (u32 dep : cp.syncBefore) {
            std::string depName = (dep < passes.size()) ? passes[dep].name : "?";
            mermaid << "  " << EscapeDot(depName) << " --> " << EscapeDot(name) << "\n";
        }
    }

    // Style async compute
    for (const auto& cp : result.passes) {
        if (cp.queueType == PassType::AsyncCompute) {
            std::string name = (cp.passId < passes.size()) ? passes[cp.passId].name : "Unknown";
            mermaid << "  style " << EscapeDot(name) << " fill:#f96\n";
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
        case ResourceUsage::ColorAttachmentWrite:  return "#FFDAB9";
        case ResourceUsage::DepthAttachmentWrite:  return "#D8BFD8";
        case ResourceUsage::ShaderRead:            return "#E0FFE0";
        case ResourceUsage::ShaderWrite:           return "#FFE0E0";
        case ResourceUsage::TransferSrc:           return "#E0E0FF";
        case ResourceUsage::TransferDst:           return "#FFFFE0";
        case ResourceUsage::Present:               return "#E0FFFF";
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
