#include "engine/rhi/common/rhi_cmd_buf_state_cache.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool CommandBufferStateCache::Init(const StateCacheConfig& config) {
    m_config = config;
    m_boundDescriptorSets.resize(config.maxDescriptorSets, 0);
    m_boundVertexBuffers.resize(config.maxVertexBindings, VertexBufferBinding{0, 0});

    Invalidate();
    ResetStats();

    return true;
}

void CommandBufferStateCache::Shutdown() {
    m_boundDescriptorSets.clear();
    m_boundVertexBuffers.clear();
}

bool CommandBufferStateCache::BindPipeline(u64 pipelineHandle) {
    if (m_pipelineValid && m_boundPipeline == pipelineHandle) {
        if (m_config.trackStats) m_pipelineAvoided++;
        return false;
    }

    m_boundPipeline = pipelineHandle;
    m_pipelineValid = true;
    m_pushConstantDirty = true; // Pipeline change invalidates push constants
    if (m_config.trackStats) m_pipelineBinds++;
    return true;
}

bool CommandBufferStateCache::BindDescriptorSet(u32 setIndex, u64 setHandle) {
    if (setIndex >= m_boundDescriptorSets.size()) return true; // Out of range, issue anyway

    if (m_boundDescriptorSets[setIndex] == setHandle && setHandle != 0) {
        if (m_config.trackStats) m_descSetAvoided++;
        return false;
    }

    m_boundDescriptorSets[setIndex] = setHandle;
    if (m_config.trackStats) m_descSetBinds++;
    return true;
}

bool CommandBufferStateCache::BindVertexBuffer(u32 binding, u64 bufferHandle, u64 offset) {
    if (binding >= m_boundVertexBuffers.size()) return true;

    VertexBufferBinding newBinding{bufferHandle, offset};
    if (m_boundVertexBuffers[binding] == newBinding && bufferHandle != 0) {
        if (m_config.trackStats) m_vtxBufAvoided++;
        return false;
    }

    m_boundVertexBuffers[binding] = newBinding;
    if (m_config.trackStats) m_vtxBufBinds++;
    return true;
}

bool CommandBufferStateCache::BindIndexBuffer(u64 bufferHandle, u64 offset, u32 indexType) {
    IndexBufferBinding newBinding{bufferHandle, offset, indexType};
    if (m_indexBufferValid && m_boundIndexBuffer == newBinding) {
        if (m_config.trackStats) m_idxBufAvoided++;
        return false;
    }

    m_boundIndexBuffer = newBinding;
    m_indexBufferValid = true;
    if (m_config.trackStats) m_idxBufBinds++;
    return true;
}

bool CommandBufferStateCache::SetViewport(const Viewport& viewport) {
    if (m_viewportValid && m_currentViewport == viewport) {
        if (m_config.trackStats) m_viewportAvoided++;
        return false;
    }

    m_currentViewport = viewport;
    m_viewportValid = true;
    if (m_config.trackStats) m_viewportSets++;
    return true;
}

bool CommandBufferStateCache::SetScissor(const ScissorRect& scissor) {
    if (m_scissorValid && m_currentScissor == scissor) {
        if (m_config.trackStats) m_scissorAvoided++;
        return false;
    }

    m_currentScissor = scissor;
    m_scissorValid = true;
    if (m_config.trackStats) m_scissorSets++;
    return true;
}

void CommandBufferStateCache::MarkPushConstantDirty() {
    m_pushConstantDirty = true;
    if (m_config.trackStats) m_pushConstantUpdates++;
}

u64 CommandBufferStateCache::GetBoundPipeline() const {
    return m_pipelineValid ? m_boundPipeline : 0;
}

u64 CommandBufferStateCache::GetBoundDescriptorSet(u32 setIndex) const {
    if (setIndex >= m_boundDescriptorSets.size()) return 0;
    return m_boundDescriptorSets[setIndex];
}

bool CommandBufferStateCache::IsPushConstantDirty() const {
    return m_pushConstantDirty;
}

void CommandBufferStateCache::Invalidate() {
    m_boundPipeline = 0;
    m_pipelineValid = false;

    for (auto& s : m_boundDescriptorSets) s = 0;
    for (auto& v : m_boundVertexBuffers) v = {0, 0};

    m_boundIndexBuffer = {0, 0, 0};
    m_indexBufferValid = false;

    m_currentViewport = {};
    m_viewportValid = false;

    m_currentScissor = {};
    m_scissorValid = false;

    m_pushConstantDirty = true;
}

void CommandBufferStateCache::ResetStats() {
    m_pipelineBinds = 0;
    m_pipelineAvoided = 0;
    m_descSetBinds = 0;
    m_descSetAvoided = 0;
    m_vtxBufBinds = 0;
    m_vtxBufAvoided = 0;
    m_idxBufBinds = 0;
    m_idxBufAvoided = 0;
    m_viewportSets = 0;
    m_viewportAvoided = 0;
    m_scissorSets = 0;
    m_scissorAvoided = 0;
    m_pushConstantUpdates = 0;
}

StateCacheStats CommandBufferStateCache::GetStats() const {
    StateCacheStats stats{};
    stats.pipelineBinds = m_pipelineBinds;
    stats.pipelineBindsAvoided = m_pipelineAvoided;
    stats.descriptorSetBinds = m_descSetBinds;
    stats.descriptorSetBindsAvoided = m_descSetAvoided;
    stats.vertexBufferBinds = m_vtxBufBinds;
    stats.vertexBufferBindsAvoided = m_vtxBufAvoided;
    stats.indexBufferBinds = m_idxBufBinds;
    stats.indexBufferBindsAvoided = m_idxBufAvoided;
    stats.viewportSets = m_viewportSets;
    stats.viewportSetsAvoided = m_viewportAvoided;
    stats.scissorSets = m_scissorSets;
    stats.scissorSetsAvoided = m_scissorAvoided;
    stats.pushConstantUpdates = m_pushConstantUpdates;

    stats.totalAvoided = m_pipelineAvoided + m_descSetAvoided + m_vtxBufAvoided +
                          m_idxBufAvoided + m_viewportAvoided + m_scissorAvoided;
    stats.totalIssued = m_pipelineBinds + m_descSetBinds + m_vtxBufBinds +
                         m_idxBufBinds + m_viewportSets + m_scissorSets;

    u32 total = stats.totalAvoided + stats.totalIssued;
    stats.avoidanceRatio = total > 0 ? static_cast<float>(stats.totalAvoided) / static_cast<float>(total) : 0.0f;

    return stats;
}

} // namespace nge::rhi
