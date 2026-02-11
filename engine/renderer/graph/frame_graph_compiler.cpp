#include "engine/renderer/graph/frame_graph_compiler.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <queue>

namespace nge::renderer {

u32 FrameGraphCompiler::AddResource(const std::string& name, u64 sizeBytes, rhi::Format format,
                                      bool imported, bool transient) {
    GraphResource res;
    res.name = name;
    res.id = static_cast<u32>(m_resources.size());
    res.sizeBytes = sizeBytes;
    res.format = format;
    res.imported = imported;
    res.transient = transient && !imported;
    m_resources.push_back(std::move(res));
    return res.id;
}

u32 FrameGraphCompiler::AddPass(const std::string& name, PassType type, bool hasSideEffects) {
    GraphPass pass;
    pass.name = name;
    pass.id = static_cast<u32>(m_passes.size());
    pass.type = type;
    pass.hasSideEffects = hasSideEffects;
    m_passes.push_back(std::move(pass));
    return pass.id;
}

void FrameGraphCompiler::PassReads(u32 passId, u32 resourceId, ResourceUsage usage) {
    if (passId < m_passes.size()) {
        m_passes[passId].reads.emplace_back(resourceId, usage);
    }
}

void FrameGraphCompiler::PassWrites(u32 passId, u32 resourceId, ResourceUsage usage) {
    if (passId < m_passes.size()) {
        m_passes[passId].writes.emplace_back(resourceId, usage);
    }
}

void FrameGraphCompiler::MarkAsyncCapable(u32 passId) {
    if (passId < m_passes.size()) {
        m_passes[passId].canRunAsync = true;
    }
}

CompiledGraph FrameGraphCompiler::Compile() {
    CompiledGraph result;

    // 1. Dead-code elimination
    std::vector<bool> alive(m_passes.size(), false);
    EliminateDeadPasses(alive);

    for (u32 i = 0; i < static_cast<u32>(m_passes.size()); ++i) {
        if (!alive[i]) result.eliminatedPasses.push_back(i);
    }

    // 2. Topological sort
    auto order = TopologicalSort(alive);

    // 3. Resource lifetime analysis
    std::unordered_map<u32, u32> firstUse, lastUse;
    AnalyzeLifetimes(order, firstUse, lastUse);

    // 4. Aliasing opportunities
    result.aliasingGroups = FindAliasingOpportunities(firstUse, lastUse);

    // 5. Barrier placement
    auto barriers = PlaceBarriers(order);

    // 6. Build compiled passes
    result.passes.reserve(order.size());
    for (u32 idx = 0; idx < static_cast<u32>(order.size()); ++idx) {
        u32 passId = order[idx];
        const auto& pass = m_passes[passId];

        CompiledPass cp;
        cp.passId = passId;
        cp.executionOrder = idx;
        cp.queueType = pass.canRunAsync ? PassType::AsyncCompute : pass.type;

        // Collect barriers for this pass
        for (const auto& b : barriers) {
            if (b.afterPass == passId) {
                cp.barriers.push_back(b);
            }
        }

        if (pass.canRunAsync) result.asyncPassCount++;
        result.passes.push_back(std::move(cp));
    }

    // Calculate memory stats
    u64 totalMemory = 0;
    u64 aliasedSavings = 0;
    for (const auto& res : m_resources) {
        if (res.transient) totalMemory += res.sizeBytes;
    }
    for (const auto& group : result.aliasingGroups) {
        u64 groupTotal = 0;
        for (u32 rid : group.resourceIds) {
            groupTotal += m_resources[rid].sizeBytes;
        }
        aliasedSavings += groupTotal - group.peakSize;
    }
    result.peakMemory = totalMemory - aliasedSavings;
    result.aliasedMemory = aliasedSavings;

    NGE_LOG_INFO("Frame graph compiled: {} passes ({} eliminated, {} async), peak {} KB, saved {} KB via aliasing",
                 result.passes.size(), result.eliminatedPasses.size(), result.asyncPassCount,
                 result.peakMemory / 1024, result.aliasedMemory / 1024);

    m_lastResult = result;
    return result;
}

void FrameGraphCompiler::Reset() {
    m_passes.clear();
    m_resources.clear();
}

void FrameGraphCompiler::EliminateDeadPasses(std::vector<bool>& alive) {
    // Start from passes with side effects or that write to imported resources
    std::queue<u32> workQueue;

    for (u32 i = 0; i < static_cast<u32>(m_passes.size()); ++i) {
        const auto& pass = m_passes[i];
        if (pass.hasSideEffects) {
            alive[i] = true;
            workQueue.push(i);
            continue;
        }
        for (const auto& [resId, usage] : pass.writes) {
            if (resId < m_resources.size() && m_resources[resId].imported) {
                alive[i] = true;
                workQueue.push(i);
                break;
            }
        }
    }

    // Backward propagation: if a pass is alive, all passes it reads from are alive
    while (!workQueue.empty()) {
        u32 passId = workQueue.front();
        workQueue.pop();

        for (const auto& [resId, usage] : m_passes[passId].reads) {
            // Find which pass writes this resource
            for (u32 j = 0; j < static_cast<u32>(m_passes.size()); ++j) {
                if (alive[j]) continue;
                for (const auto& [wResId, wUsage] : m_passes[j].writes) {
                    if (wResId == resId) {
                        alive[j] = true;
                        workQueue.push(j);
                    }
                }
            }
        }
    }
}

std::vector<u32> FrameGraphCompiler::TopologicalSort(const std::vector<bool>& alive) {
    u32 n = static_cast<u32>(m_passes.size());

    // Build adjacency: pass A → pass B if B reads a resource that A writes
    std::vector<std::vector<u32>> adj(n);
    std::vector<u32> inDegree(n, 0);

    for (u32 b = 0; b < n; ++b) {
        if (!alive[b]) continue;
        for (const auto& [readRes, readUsage] : m_passes[b].reads) {
            for (u32 a = 0; a < n; ++a) {
                if (a == b || !alive[a]) continue;
                for (const auto& [writeRes, writeUsage] : m_passes[a].writes) {
                    if (writeRes == readRes) {
                        adj[a].push_back(b);
                        inDegree[b]++;
                    }
                }
            }
        }
    }

    // Kahn's algorithm
    std::queue<u32> q;
    for (u32 i = 0; i < n; ++i) {
        if (alive[i] && inDegree[i] == 0) q.push(i);
    }

    std::vector<u32> order;
    order.reserve(n);

    while (!q.empty()) {
        u32 curr = q.front();
        q.pop();
        order.push_back(curr);

        for (u32 next : adj[curr]) {
            if (--inDegree[next] == 0) {
                q.push(next);
            }
        }
    }

    return order;
}

void FrameGraphCompiler::AnalyzeLifetimes(const std::vector<u32>& order,
                                            std::unordered_map<u32, u32>& firstUse,
                                            std::unordered_map<u32, u32>& lastUse) {
    for (u32 idx = 0; idx < static_cast<u32>(order.size()); ++idx) {
        u32 passId = order[idx];
        const auto& pass = m_passes[passId];

        auto touchResource = [&](u32 resId) {
            if (firstUse.find(resId) == firstUse.end()) {
                firstUse[resId] = idx;
            }
            lastUse[resId] = idx;
        };

        for (const auto& [resId, usage] : pass.reads) touchResource(resId);
        for (const auto& [resId, usage] : pass.writes) touchResource(resId);
    }
}

std::vector<AliasingGroup> FrameGraphCompiler::FindAliasingOpportunities(
    const std::unordered_map<u32, u32>& firstUse,
    const std::unordered_map<u32, u32>& lastUse) {

    std::vector<AliasingGroup> groups;

    // Collect transient resources with lifetime info
    struct ResLifetime {
        u32 id;
        u32 first;
        u32 last;
        u64 size;
    };
    std::vector<ResLifetime> transients;
    for (const auto& res : m_resources) {
        if (!res.transient) continue;
        auto fit = firstUse.find(res.id);
        auto lit = lastUse.find(res.id);
        if (fit == firstUse.end() || lit == lastUse.end()) continue;
        transients.push_back({res.id, fit->second, lit->second, res.sizeBytes});
    }

    // Sort by first use
    std::sort(transients.begin(), transients.end(),
        [](const ResLifetime& a, const ResLifetime& b) { return a.first < b.first; });

    // Greedy grouping: resources whose lifetimes don't overlap can share memory
    std::vector<bool> grouped(transients.size(), false);
    for (u32 i = 0; i < static_cast<u32>(transients.size()); ++i) {
        if (grouped[i]) continue;

        AliasingGroup group;
        group.resourceIds.push_back(transients[i].id);
        group.peakSize = transients[i].size;
        u32 groupEnd = transients[i].last;

        for (u32 j = i + 1; j < static_cast<u32>(transients.size()); ++j) {
            if (grouped[j]) continue;
            if (transients[j].first > groupEnd) {
                // Non-overlapping — can alias
                group.resourceIds.push_back(transients[j].id);
                group.peakSize = std::max(group.peakSize, transients[j].size);
                groupEnd = transients[j].last;
                grouped[j] = true;
            }
        }

        if (group.resourceIds.size() > 1) {
            groups.push_back(std::move(group));
        }
    }

    return groups;
}

std::vector<CompiledBarrier> FrameGraphCompiler::PlaceBarriers(const std::vector<u32>& order) {
    std::vector<CompiledBarrier> barriers;

    // Track last usage per resource
    std::unordered_map<u32, std::pair<u32, ResourceUsage>> lastAccess; // resId → (passId, usage)

    for (u32 passId : order) {
        const auto& pass = m_passes[passId];

        auto checkBarrier = [&](u32 resId, ResourceUsage newUsage) {
            auto it = lastAccess.find(resId);
            if (it != lastAccess.end()) {
                auto [prevPass, prevUsage] = it->second;
                if (prevUsage != newUsage) {
                    CompiledBarrier barrier;
                    barrier.resourceId = resId;
                    barrier.beforeUsage = prevUsage;
                    barrier.afterUsage = newUsage;
                    barrier.beforePass = prevPass;
                    barrier.afterPass = passId;
                    barriers.push_back(barrier);
                }
            }
        };

        for (const auto& [resId, usage] : pass.reads) {
            checkBarrier(resId, usage);
            lastAccess[resId] = {passId, usage};
        }
        for (const auto& [resId, usage] : pass.writes) {
            checkBarrier(resId, usage);
            lastAccess[resId] = {passId, usage};
        }
    }

    return barriers;
}

} // namespace nge::renderer
