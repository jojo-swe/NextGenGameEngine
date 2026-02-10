#include "engine/rhi/common/rhi_indirect_arg_builder.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool IndirectArgBuilder::Init(const IndirectArgBuilderConfig& config) {
    m_config = config;
    m_validationErrors = 0;

    m_drawArgs.reserve(config.maxDrawArgs);
    m_drawIndexedArgs.reserve(config.maxDrawArgs);
    m_dispatchArgs.reserve(config.maxDispatchArgs);
    m_meshTaskArgs.reserve(config.maxDispatchArgs);

    NGE_LOG_INFO("Indirect arg builder initialized: maxDraw={}, maxDispatch={}, validate={}",
                 config.maxDrawArgs, config.maxDispatchArgs, config.validateArgs);
    return true;
}

void IndirectArgBuilder::Shutdown() {
    m_drawArgs.clear();
    m_drawIndexedArgs.clear();
    m_dispatchArgs.clear();
    m_meshTaskArgs.clear();
}

bool IndirectArgBuilder::AddDraw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) {
    std::lock_guard lock(m_mutex);

    if (m_drawArgs.size() >= m_config.maxDrawArgs) {
        NGE_LOG_WARN("Indirect arg builder: max draw args reached ({})", m_config.maxDrawArgs);
        return false;
    }

    if (m_config.validateArgs && !ValidateDraw(vertexCount, instanceCount)) {
        m_validationErrors++;
        return false;
    }

    DrawArgs args;
    args.vertexCount = vertexCount;
    args.instanceCount = instanceCount;
    args.firstVertex = firstVertex;
    args.firstInstance = firstInstance;

    m_drawArgs.push_back(args);
    return true;
}

bool IndirectArgBuilder::AddDrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex,
                                          i32 vertexOffset, u32 firstInstance) {
    std::lock_guard lock(m_mutex);

    if (m_drawIndexedArgs.size() >= m_config.maxDrawArgs) {
        NGE_LOG_WARN("Indirect arg builder: max draw indexed args reached ({})", m_config.maxDrawArgs);
        return false;
    }

    if (m_config.validateArgs && !ValidateDraw(indexCount, instanceCount)) {
        m_validationErrors++;
        return false;
    }

    DrawIndexedArgs args;
    args.indexCount = indexCount;
    args.instanceCount = instanceCount;
    args.firstIndex = firstIndex;
    args.vertexOffset = vertexOffset;
    args.firstInstance = firstInstance;

    m_drawIndexedArgs.push_back(args);
    return true;
}

bool IndirectArgBuilder::AddDispatch(u32 groupX, u32 groupY, u32 groupZ) {
    std::lock_guard lock(m_mutex);

    if (m_dispatchArgs.size() >= m_config.maxDispatchArgs) {
        NGE_LOG_WARN("Indirect arg builder: max dispatch args reached ({})", m_config.maxDispatchArgs);
        return false;
    }

    if (m_config.validateArgs && !ValidateDispatch(groupX, groupY, groupZ)) {
        m_validationErrors++;
        return false;
    }

    DispatchArgs args;
    args.groupCountX = groupX;
    args.groupCountY = groupY;
    args.groupCountZ = groupZ;

    m_dispatchArgs.push_back(args);
    return true;
}

bool IndirectArgBuilder::AddDrawMeshTasks(u32 groupX, u32 groupY, u32 groupZ) {
    std::lock_guard lock(m_mutex);

    if (m_meshTaskArgs.size() >= m_config.maxDispatchArgs) {
        NGE_LOG_WARN("Indirect arg builder: max mesh task args reached ({})", m_config.maxDispatchArgs);
        return false;
    }

    if (m_config.validateArgs && !ValidateDispatch(groupX, groupY, groupZ)) {
        m_validationErrors++;
        return false;
    }

    DrawMeshTasksArgs args;
    args.groupCountX = groupX;
    args.groupCountY = groupY;
    args.groupCountZ = groupZ;

    m_meshTaskArgs.push_back(args);
    return true;
}

const std::vector<DrawArgs>& IndirectArgBuilder::GetDrawArgs() const {
    return m_drawArgs;
}

const std::vector<DrawIndexedArgs>& IndirectArgBuilder::GetDrawIndexedArgs() const {
    return m_drawIndexedArgs;
}

const std::vector<DispatchArgs>& IndirectArgBuilder::GetDispatchArgs() const {
    return m_dispatchArgs;
}

const std::vector<DrawMeshTasksArgs>& IndirectArgBuilder::GetMeshTaskArgs() const {
    return m_meshTaskArgs;
}

u32 IndirectArgBuilder::GetDrawCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_drawArgs.size());
}

u32 IndirectArgBuilder::GetDrawIndexedCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_drawIndexedArgs.size());
}

u32 IndirectArgBuilder::GetDispatchCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_dispatchArgs.size());
}

u32 IndirectArgBuilder::GetMeshTaskCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_meshTaskArgs.size());
}

u32 IndirectArgBuilder::GetTotalArgCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_drawArgs.size() + m_drawIndexedArgs.size() +
                             m_dispatchArgs.size() + m_meshTaskArgs.size());
}

u64 IndirectArgBuilder::GetDrawBufferSize() const {
    std::lock_guard lock(m_mutex);
    return m_drawArgs.size() * sizeof(DrawArgs);
}

u64 IndirectArgBuilder::GetDrawIndexedBufferSize() const {
    std::lock_guard lock(m_mutex);
    return m_drawIndexedArgs.size() * sizeof(DrawIndexedArgs);
}

u64 IndirectArgBuilder::GetDispatchBufferSize() const {
    std::lock_guard lock(m_mutex);
    return m_dispatchArgs.size() * sizeof(DispatchArgs);
}

u64 IndirectArgBuilder::GetMeshTaskBufferSize() const {
    std::lock_guard lock(m_mutex);
    return m_meshTaskArgs.size() * sizeof(DrawMeshTasksArgs);
}

u64 IndirectArgBuilder::GetTotalBufferSize() const {
    std::lock_guard lock(m_mutex);
    return m_drawArgs.size() * sizeof(DrawArgs) +
           m_drawIndexedArgs.size() * sizeof(DrawIndexedArgs) +
           m_dispatchArgs.size() * sizeof(DispatchArgs) +
           m_meshTaskArgs.size() * sizeof(DrawMeshTasksArgs);
}

void IndirectArgBuilder::SortDrawsByInstanceCount() {
    std::lock_guard lock(m_mutex);

    std::sort(m_drawArgs.begin(), m_drawArgs.end(),
              [](const DrawArgs& a, const DrawArgs& b) {
                  return a.instanceCount > b.instanceCount;
              });

    std::sort(m_drawIndexedArgs.begin(), m_drawIndexedArgs.end(),
              [](const DrawIndexedArgs& a, const DrawIndexedArgs& b) {
                  return a.instanceCount > b.instanceCount;
              });
}

u32 IndirectArgBuilder::MergeCompatibleDraws() {
    std::lock_guard lock(m_mutex);

    u32 merged = 0;

    // Merge consecutive Draw args with same firstVertex and adjacent vertex ranges
    if (m_drawArgs.size() > 1) {
        std::vector<DrawArgs> mergedArgs;
        mergedArgs.push_back(m_drawArgs[0]);

        for (size_t i = 1; i < m_drawArgs.size(); ++i) {
            auto& prev = mergedArgs.back();
            const auto& curr = m_drawArgs[i];

            if (prev.instanceCount == curr.instanceCount &&
                prev.firstInstance == curr.firstInstance &&
                prev.firstVertex + prev.vertexCount == curr.firstVertex) {
                prev.vertexCount += curr.vertexCount;
                merged++;
            } else {
                mergedArgs.push_back(curr);
            }
        }

        m_drawArgs = std::move(mergedArgs);
    }

    return merged;
}

void IndirectArgBuilder::Clear() {
    std::lock_guard lock(m_mutex);
    m_drawArgs.clear();
    m_drawIndexedArgs.clear();
    m_dispatchArgs.clear();
    m_meshTaskArgs.clear();
}

void IndirectArgBuilder::Reset() {
    std::lock_guard lock(m_mutex);
    m_drawArgs.clear();
    m_drawIndexedArgs.clear();
    m_dispatchArgs.clear();
    m_meshTaskArgs.clear();
    m_validationErrors = 0;
}

IndirectArgBuilderStats IndirectArgBuilder::GetStats() const {
    std::lock_guard lock(m_mutex);

    IndirectArgBuilderStats stats{};
    stats.drawArgsCount = static_cast<u32>(m_drawArgs.size());
    stats.drawIndexedArgsCount = static_cast<u32>(m_drawIndexedArgs.size());
    stats.dispatchArgsCount = static_cast<u32>(m_dispatchArgs.size());
    stats.meshTaskArgsCount = static_cast<u32>(m_meshTaskArgs.size());
    stats.totalArgs = stats.drawArgsCount + stats.drawIndexedArgsCount +
                       stats.dispatchArgsCount + stats.meshTaskArgsCount;

    u32 totalInstances = 0;
    u64 totalVertices = 0;
    for (const auto& d : m_drawArgs) {
        totalInstances += d.instanceCount;
        totalVertices += d.vertexCount;
    }
    for (const auto& d : m_drawIndexedArgs) {
        totalInstances += d.instanceCount;
        totalVertices += d.indexCount;
    }

    stats.totalInstances = totalInstances;
    stats.totalVertices = totalVertices;
    stats.validationErrors = m_validationErrors;

    stats.bufferSizeBytes = m_drawArgs.size() * sizeof(DrawArgs) +
                             m_drawIndexedArgs.size() * sizeof(DrawIndexedArgs) +
                             m_dispatchArgs.size() * sizeof(DispatchArgs) +
                             m_meshTaskArgs.size() * sizeof(DrawMeshTasksArgs);

    return stats;
}

bool IndirectArgBuilder::ValidateDraw(u32 vertexCount, u32 instanceCount) const {
    if (vertexCount == 0) {
        NGE_LOG_WARN("Indirect arg: draw with 0 vertices");
        return false;
    }
    if (instanceCount == 0) {
        NGE_LOG_WARN("Indirect arg: draw with 0 instances");
        return false;
    }
    if (instanceCount > m_config.maxInstancesPerDraw) {
        NGE_LOG_WARN("Indirect arg: instanceCount {} exceeds max {}", instanceCount, m_config.maxInstancesPerDraw);
        return false;
    }
    if (vertexCount > m_config.maxVerticesPerDraw) {
        NGE_LOG_WARN("Indirect arg: vertexCount {} exceeds max {}", vertexCount, m_config.maxVerticesPerDraw);
        return false;
    }
    return true;
}

bool IndirectArgBuilder::ValidateDispatch(u32 groupX, u32 groupY, u32 groupZ) const {
    if (groupX == 0 || groupY == 0 || groupZ == 0) {
        NGE_LOG_WARN("Indirect arg: dispatch with 0 group count");
        return false;
    }
    if (groupX > 65535 || groupY > 65535 || groupZ > 65535) {
        NGE_LOG_WARN("Indirect arg: dispatch group count exceeds 65535");
        return false;
    }
    return true;
}

} // namespace nge::rhi
