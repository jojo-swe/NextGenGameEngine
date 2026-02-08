#include "engine/renderer/debug/gpu_timer.h"
#include "engine/core/logging/log.h"

namespace nge::renderer {

// ─── GPUTimerScope ───────────────────────────────────────────────────────

GPUTimerScope::GPUTimerScope(GPUTimerProfiler* profiler, rhi::ICommandList* cmd, u32 nodeIndex)
    : m_profiler(profiler), m_cmd(cmd), m_nodeIndex(nodeIndex) {}

GPUTimerScope::~GPUTimerScope() {
    if (m_profiler && m_nodeIndex != UINT32_MAX) {
        m_profiler->EndTimer(m_cmd, m_nodeIndex);
    }
}

GPUTimerScope::GPUTimerScope(GPUTimerScope&& other) noexcept
    : m_profiler(other.m_profiler), m_cmd(other.m_cmd), m_nodeIndex(other.m_nodeIndex) {
    other.m_profiler = nullptr;
    other.m_nodeIndex = UINT32_MAX;
}

// ─── GPUTimerProfiler ────────────────────────────────────────────────────

bool GPUTimerProfiler::Init(rhi::IDevice* device, const Config& config) {
    m_device = device;
    m_config = config;

    rhi::QueryHeapManager::Config qhConfig;
    qhConfig.maxTimestampQueries = config.maxTimers;
    qhConfig.maxOcclusionQueries = 0;
    qhConfig.maxPipelineStatQueries = 0;
    qhConfig.framesInFlight = config.framesInFlight;

    if (!m_queryHeap.Init(device, qhConfig)) {
        return false;
    }

    m_currentNodes.reserve(config.maxTimers);
    m_results.reserve(config.maxTimers);

    NGE_LOG_INFO("GPU timer profiler initialized: max {} timers", config.maxTimers);
    return true;
}

void GPUTimerProfiler::Shutdown() {
    m_queryHeap.Shutdown();
    m_currentNodes.clear();
    m_results.clear();
}

void GPUTimerProfiler::BeginFrame(rhi::ICommandList* cmd, u32 frameIndex) {
    m_queryHeap.BeginFrame(frameIndex);

    // Collect results from the readback (N-2 frames ago)
    CollectResults();

    // Reset current frame state
    m_currentNodes.clear();
    while (!m_activeStack.empty()) m_activeStack.pop();
    m_currentDepth = 0;

    (void)cmd;
}

void GPUTimerProfiler::EndFrame(rhi::ICommandList* cmd) {
    // Close any unclosed timers (shouldn't happen with RAII scopes)
    while (!m_activeStack.empty()) {
        u32 idx = m_activeStack.top();
        m_activeStack.pop();
        m_queryHeap.EndTimestamp(cmd, m_currentNodes[idx].queryId);
    }

    m_queryHeap.EndFrame(cmd);
}

GPUTimerScope GPUTimerProfiler::Scope(rhi::ICommandList* cmd, const std::string& name) {
    u32 index = BeginTimer(cmd, name);
    return GPUTimerScope(this, cmd, index);
}

u32 GPUTimerProfiler::BeginTimer(rhi::ICommandList* cmd, const std::string& name) {
    u32 nodeIndex = static_cast<u32>(m_currentNodes.size());

    GPUTimerNode node;
    node.name = name;
    node.depth = m_currentDepth;
    node.queryId = m_queryHeap.BeginTimestamp(cmd, name);

    // Link to parent
    if (!m_activeStack.empty()) {
        node.parentIndex = m_activeStack.top();
        m_currentNodes[node.parentIndex].children.push_back(nodeIndex);
    }

    m_currentNodes.push_back(std::move(node));
    m_activeStack.push(nodeIndex);
    m_currentDepth++;

    return nodeIndex;
}

void GPUTimerProfiler::EndTimer(rhi::ICommandList* cmd, u32 timerIndex) {
    if (timerIndex >= m_currentNodes.size()) return;

    m_queryHeap.EndTimestamp(cmd, m_currentNodes[timerIndex].queryId);

    if (!m_activeStack.empty()) {
        m_activeStack.pop();
    }
    if (m_currentDepth > 0) m_currentDepth--;
}

void GPUTimerProfiler::CollectResults() {
    const auto& timestamps = m_queryHeap.GetTimestampResults();

    // Copy current nodes as results, filling in durations from query readback
    m_results = m_currentNodes;

    for (auto& node : m_results) {
        f64 ms = m_queryHeap.GetTimestampMs(node.name);
        node.durationMs = (ms >= 0) ? ms : 0;
    }
}

f64 GPUTimerProfiler::GetTimerMs(const std::string& name) const {
    for (const auto& node : m_results) {
        if (node.name == name) return node.durationMs;
    }
    return -1.0;
}

f64 GPUTimerProfiler::GetTotalFrameMs() const {
    f64 total = 0;
    for (const auto& node : m_results) {
        if (node.depth == 0) total += node.durationMs;
    }
    return total;
}

std::vector<const GPUTimerNode*> GPUTimerProfiler::GetFlatResults() const {
    std::vector<const GPUTimerNode*> flat;
    flat.reserve(m_results.size());

    // Find root nodes and DFS
    for (u32 i = 0; i < static_cast<u32>(m_results.size()); ++i) {
        if (m_results[i].parentIndex == UINT32_MAX) {
            FlattenDFS(i, flat);
        }
    }
    return flat;
}

void GPUTimerProfiler::FlattenDFS(u32 nodeIndex, std::vector<const GPUTimerNode*>& out) const {
    if (nodeIndex >= m_results.size()) return;
    out.push_back(&m_results[nodeIndex]);
    for (u32 child : m_results[nodeIndex].children) {
        FlattenDFS(child, out);
    }
}

} // namespace nge::renderer
