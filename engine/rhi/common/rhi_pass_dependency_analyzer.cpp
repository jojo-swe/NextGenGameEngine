#include "engine/rhi/common/rhi_pass_dependency_analyzer.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <queue>

namespace nge::rhi {

bool PassDependencyAnalyzer::Init(const PassDependencyConfig& config) {
    m_config = config;
    m_redundantRemoved = 0;
    m_passes.reserve(config.maxPasses);

    NGE_LOG_INFO("Pass dependency analyzer initialized: maxPasses={}, crossQueue={}, warnRedundant={}",
                 config.maxPasses, config.detectCrossQueue, config.warnOnRedundant);
    return true;
}

void PassDependencyAnalyzer::Shutdown() {
    m_passes.clear();
    m_dependencies.clear();
}

void PassDependencyAnalyzer::DeclarePass(const PassDeclaration& pass) {
    std::lock_guard lock(m_mutex);
    if (m_passes.size() >= m_config.maxPasses) {
        NGE_LOG_WARN("Pass dependency analyzer: max passes reached ({})", m_config.maxPasses);
        return;
    }
    m_passes.push_back(pass);
}

void PassDependencyAnalyzer::Analyze() {
    std::lock_guard lock(m_mutex);
    m_dependencies.clear();

    // Build resource -> writer/reader maps
    // For each resource, track which passes write and which read
    struct ResourceUsage {
        std::vector<std::pair<u32, PipelineStage>> writers; // passIndex, stage
        std::vector<std::pair<u32, PipelineStage>> readers;
    };

    std::unordered_map<u64, ResourceUsage> resourceUsages;

    for (const auto& pass : m_passes) {
        for (const auto& access : pass.accesses) {
            auto& usage = resourceUsages[access.resourceId];
            if (access.access == AccessType::Write || access.access == AccessType::ReadWrite) {
                usage.writers.push_back({pass.passIndex, access.stage});
            }
            if (access.access == AccessType::Read || access.access == AccessType::ReadWrite) {
                usage.readers.push_back({pass.passIndex, access.stage});
            }
        }
    }

    // Infer dependencies
    for (const auto& [resId, usage] : resourceUsages) {
        std::string resName;
        // Find resource name from first access
        for (const auto& pass : m_passes) {
            for (const auto& acc : pass.accesses) {
                if (acc.resourceId == resId) { resName = acc.resourceName; break; }
            }
            if (!resName.empty()) break;
        }

        // RAW: writer before reader
        for (const auto& [writerPass, writerStage] : usage.writers) {
            for (const auto& [readerPass, readerStage] : usage.readers) {
                if (writerPass >= readerPass) continue; // Only forward deps

                InferredDependency dep;
                dep.srcPass = writerPass;
                dep.dstPass = readerPass;
                dep.srcStage = writerStage;
                dep.dstStage = readerStage;
                dep.resourceId = resId;
                dep.resourceName = resName;
                dep.hazardType = "RAW";

                // Find pass names
                for (const auto& p : m_passes) {
                    if (p.passIndex == writerPass) dep.srcPassName = p.passName;
                    if (p.passIndex == readerPass) dep.dstPassName = p.passName;
                }

                // Cross-queue detection
                dep.isCrossQueue = false;
                if (m_config.detectCrossQueue) {
                    u32 srcQueue = 0, dstQueue = 0;
                    for (const auto& p : m_passes) {
                        if (p.passIndex == writerPass) srcQueue = p.queueFamily;
                        if (p.passIndex == readerPass) dstQueue = p.queueFamily;
                    }
                    dep.isCrossQueue = (srcQueue != dstQueue);
                }

                m_dependencies.push_back(std::move(dep));
            }
        }

        // WAW: writer before writer
        for (size_t i = 0; i < usage.writers.size(); ++i) {
            for (size_t j = i + 1; j < usage.writers.size(); ++j) {
                u32 srcPass = usage.writers[i].first;
                u32 dstPass = usage.writers[j].first;
                if (srcPass >= dstPass) continue;

                InferredDependency dep;
                dep.srcPass = srcPass;
                dep.dstPass = dstPass;
                dep.srcStage = usage.writers[i].second;
                dep.dstStage = usage.writers[j].second;
                dep.resourceId = resId;
                dep.resourceName = resName;
                dep.hazardType = "WAW";

                for (const auto& p : m_passes) {
                    if (p.passIndex == srcPass) dep.srcPassName = p.passName;
                    if (p.passIndex == dstPass) dep.dstPassName = p.passName;
                }
                dep.isCrossQueue = false;

                m_dependencies.push_back(std::move(dep));
            }
        }

        // WAR: reader before writer (write-after-read)
        for (const auto& [readerPass, readerStage] : usage.readers) {
            for (const auto& [writerPass, writerStage] : usage.writers) {
                if (readerPass >= writerPass) continue;

                InferredDependency dep;
                dep.srcPass = readerPass;
                dep.dstPass = writerPass;
                dep.srcStage = readerStage;
                dep.dstStage = writerStage;
                dep.resourceId = resId;
                dep.resourceName = resName;
                dep.hazardType = "WAR";

                for (const auto& p : m_passes) {
                    if (p.passIndex == readerPass) dep.srcPassName = p.passName;
                    if (p.passIndex == writerPass) dep.dstPassName = p.passName;
                }
                dep.isCrossQueue = false;

                m_dependencies.push_back(std::move(dep));
            }
        }
    }

    // Remove redundant (transitive) dependencies
    if (m_config.warnOnRedundant) {
        RemoveRedundantDeps();
    }
}

const std::vector<InferredDependency>& PassDependencyAnalyzer::GetDependencies() const {
    return m_dependencies;
}

std::vector<InferredDependency> PassDependencyAnalyzer::GetDependenciesForPass(u32 passIndex) const {
    std::lock_guard lock(m_mutex);
    std::vector<InferredDependency> result;
    for (const auto& dep : m_dependencies) {
        if (dep.dstPass == passIndex) result.push_back(dep);
    }
    return result;
}

bool PassDependencyAnalyzer::WouldCreateCycle(u32 srcPass, u32 dstPass) const {
    std::lock_guard lock(m_mutex);

    // Build adjacency from existing deps + proposed edge
    std::unordered_map<u32, std::vector<u32>> adj;
    for (const auto& dep : m_dependencies) {
        adj[dep.srcPass].push_back(dep.dstPass);
    }
    adj[srcPass].push_back(dstPass);

    // Check if there's a path from dstPass back to srcPass (cycle)
    return HasPath(dstPass, srcPass, adj);
}

std::vector<u32> PassDependencyAnalyzer::GetTopologicalOrder() const {
    std::lock_guard lock(m_mutex);

    std::unordered_map<u32, std::vector<u32>> adj;
    std::unordered_map<u32, u32> inDegree;

    // Collect all pass indices
    std::unordered_set<u32> allPasses;
    for (const auto& p : m_passes) {
        allPasses.insert(p.passIndex);
        inDegree[p.passIndex] = 0;
    }

    for (const auto& dep : m_dependencies) {
        adj[dep.srcPass].push_back(dep.dstPass);
        inDegree[dep.dstPass]++;
    }

    // Kahn's algorithm
    std::queue<u32> q;
    for (auto [pass, deg] : inDegree) {
        if (deg == 0) q.push(pass);
    }

    std::vector<u32> order;
    while (!q.empty()) {
        u32 node = q.front(); q.pop();
        order.push_back(node);

        for (u32 neighbor : adj[node]) {
            if (--inDegree[neighbor] == 0) {
                q.push(neighbor);
            }
        }
    }

    return order;
}

void PassDependencyAnalyzer::Clear() {
    std::lock_guard lock(m_mutex);
    m_passes.clear();
    m_dependencies.clear();
    m_redundantRemoved = 0;
}

PassDependencyStats PassDependencyAnalyzer::GetStats() const {
    std::lock_guard lock(m_mutex);
    PassDependencyStats stats{};
    stats.totalPasses = static_cast<u32>(m_passes.size());
    stats.totalDependencies = static_cast<u32>(m_dependencies.size());
    stats.redundantDepsRemoved = m_redundantRemoved;

    for (const auto& dep : m_dependencies) {
        if (dep.hazardType == "RAW") stats.rawHazards++;
        else if (dep.hazardType == "WAR") stats.warHazards++;
        else if (dep.hazardType == "WAW") stats.wawHazards++;
        if (dep.isCrossQueue) stats.crossQueueDeps++;
    }

    return stats;
}

bool PassDependencyAnalyzer::HasPath(u32 from, u32 to,
                                       const std::unordered_map<u32, std::vector<u32>>& adj) const {
    std::unordered_set<u32> visited;
    std::queue<u32> bfs;
    bfs.push(from);
    visited.insert(from);

    while (!bfs.empty()) {
        u32 node = bfs.front(); bfs.pop();
        if (node == to) return true;

        auto it = adj.find(node);
        if (it != adj.end()) {
            for (u32 next : it->second) {
                if (visited.insert(next).second) {
                    bfs.push(next);
                }
            }
        }
    }

    return false;
}

void PassDependencyAnalyzer::RemoveRedundantDeps() {
    // A dependency A->C is redundant if A->B->C exists
    std::unordered_map<u32, std::vector<u32>> adj;
    for (const auto& dep : m_dependencies) {
        adj[dep.srcPass].push_back(dep.dstPass);
    }

    std::vector<InferredDependency> filtered;
    for (const auto& dep : m_dependencies) {
        // Check if there's an alternative path from src to dst (length > 1)
        bool hasAlternate = false;

        // BFS from src, but skip direct edge to dst
        std::unordered_set<u32> visited;
        std::queue<u32> bfs;

        auto it = adj.find(dep.srcPass);
        if (it != adj.end()) {
            for (u32 next : it->second) {
                if (next != dep.dstPass && visited.insert(next).second) {
                    bfs.push(next);
                }
            }
        }

        while (!bfs.empty()) {
            u32 node = bfs.front(); bfs.pop();
            if (node == dep.dstPass) { hasAlternate = true; break; }

            auto adjIt = adj.find(node);
            if (adjIt != adj.end()) {
                for (u32 next : adjIt->second) {
                    if (visited.insert(next).second) bfs.push(next);
                }
            }
        }

        if (!hasAlternate) {
            filtered.push_back(dep);
        } else {
            m_redundantRemoved++;
        }
    }

    m_dependencies = std::move(filtered);
}

} // namespace nge::rhi
