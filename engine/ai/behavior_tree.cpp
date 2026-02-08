#include "engine/ai/behavior_tree.h"
#include "engine/core/logging/log.h"
#include <queue>
#include <algorithm>
#include <cmath>
#include <limits>

namespace nge::ai {

// ─── A* Pathfinding ──────────────────────────────────────────────────────

std::vector<math::Vec3> NavMesh::FindPath(u32 startNode, u32 endNode) const {
    if (startNode >= m_nodes.size() || endNode >= m_nodes.size()) return {};
    if (startNode == endNode) return {m_nodes[startNode].position};

    struct AStarNode {
        u32 index;
        f32 gCost; // Cost from start
        f32 fCost; // gCost + heuristic
        u32 parent;
        bool operator>(const AStarNode& o) const { return fCost > o.fCost; }
    };

    u32 nodeCount = static_cast<u32>(m_nodes.size());
    std::vector<f32> gCosts(nodeCount, std::numeric_limits<f32>::max());
    std::vector<u32> parents(nodeCount, UINT32_MAX);
    std::vector<bool> closed(nodeCount, false);

    auto heuristic = [&](u32 a, u32 b) -> f32 {
        math::Vec3 diff = m_nodes[a].position - m_nodes[b].position;
        return diff.Length();
    };

    std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> openSet;

    gCosts[startNode] = 0;
    openSet.push({startNode, 0, heuristic(startNode, endNode), UINT32_MAX});

    while (!openSet.empty()) {
        AStarNode current = openSet.top();
        openSet.pop();

        if (current.index == endNode) break;
        if (closed[current.index]) continue;
        closed[current.index] = true;
        parents[current.index] = current.parent;

        for (u32 neighborIdx : m_nodes[current.index].neighbors) {
            if (closed[neighborIdx]) continue;

            math::Vec3 diff = m_nodes[neighborIdx].position - m_nodes[current.index].position;
            f32 edgeCost = diff.Length() * m_nodes[neighborIdx].cost;
            f32 newG = gCosts[current.index] + edgeCost;

            if (newG < gCosts[neighborIdx]) {
                gCosts[neighborIdx] = newG;
                f32 fCost = newG + heuristic(neighborIdx, endNode);
                openSet.push({neighborIdx, newG, fCost, current.index});
            }
        }
    }

    // Reconstruct path
    if (gCosts[endNode] >= std::numeric_limits<f32>::max()) return {}; // No path found

    std::vector<math::Vec3> path;
    u32 current = endNode;
    while (current != UINT32_MAX) {
        path.push_back(m_nodes[current].position);
        current = parents[current];
    }
    std::reverse(path.begin(), path.end());
    return path;
}

u32 NavMesh::FindNearestNode(const math::Vec3& pos) const {
    u32 nearest = UINT32_MAX;
    f32 minDist = std::numeric_limits<f32>::max();

    for (u32 i = 0; i < static_cast<u32>(m_nodes.size()); ++i) {
        math::Vec3 diff = m_nodes[i].position - pos;
        f32 dist = diff.LengthSquared();
        if (dist < minDist) {
            minDist = dist;
            nearest = i;
        }
    }

    return nearest;
}

} // namespace nge::ai
