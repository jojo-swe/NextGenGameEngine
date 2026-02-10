#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::rhi {

// ─── GPU Command Buffer State Cache ──────────────────────────────────────
// Tracks currently bound state in a command buffer to eliminate redundant
// vkCmdBind* and vkCmdSet* calls. Compares new state against cached state
// and only issues GPU commands when state actually changes.
//
// Use cases:
//   - Skip redundant vkCmdBindPipeline when same PSO already bound
//   - Skip redundant vkCmdBindDescriptorSets for unchanged sets
//   - Skip redundant vkCmdBindVertexBuffers / vkCmdBindIndexBuffer
//   - Skip redundant vkCmdSetViewport / vkCmdSetScissor
//   - Track push constant dirty state
//   - Profile: count avoided vs actual state changes

struct Viewport {
    float x, y, width, height, minDepth, maxDepth;

    bool operator==(const Viewport& o) const {
        return x == o.x && y == o.y && width == o.width && height == o.height &&
               minDepth == o.minDepth && maxDepth == o.maxDepth;
    }
    bool operator!=(const Viewport& o) const { return !(*this == o); }
};

struct ScissorRect {
    i32 x, y;
    u32 width, height;

    bool operator==(const ScissorRect& o) const {
        return x == o.x && y == o.y && width == o.width && height == o.height;
    }
    bool operator!=(const ScissorRect& o) const { return !(*this == o); }
};

struct VertexBufferBinding {
    u64 bufferHandle;
    u64 offset;

    bool operator==(const VertexBufferBinding& o) const {
        return bufferHandle == o.bufferHandle && offset == o.offset;
    }
    bool operator!=(const VertexBufferBinding& o) const { return !(*this == o); }
};

struct IndexBufferBinding {
    u64 bufferHandle;
    u64 offset;
    u32 indexType; // 0=U16, 1=U32

    bool operator==(const IndexBufferBinding& o) const {
        return bufferHandle == o.bufferHandle && offset == o.offset && indexType == o.indexType;
    }
    bool operator!=(const IndexBufferBinding& o) const { return !(*this == o); }
};

struct StateCacheConfig {
    u32  maxVertexBindings = 16;
    u32  maxDescriptorSets = 4;
    bool trackStats = true;
};

struct StateCacheStats {
    u32 pipelineBinds;
    u32 pipelineBindsAvoided;
    u32 descriptorSetBinds;
    u32 descriptorSetBindsAvoided;
    u32 vertexBufferBinds;
    u32 vertexBufferBindsAvoided;
    u32 indexBufferBinds;
    u32 indexBufferBindsAvoided;
    u32 viewportSets;
    u32 viewportSetsAvoided;
    u32 scissorSets;
    u32 scissorSetsAvoided;
    u32 pushConstantUpdates;
    u32 totalAvoided;
    u32 totalIssued;
    float avoidanceRatio;
};

class CommandBufferStateCache {
public:
    bool Init(const StateCacheConfig& config = {});
    void Shutdown();

    // Returns true if state changed (caller should issue the GPU command)
    bool BindPipeline(u64 pipelineHandle);
    bool BindDescriptorSet(u32 setIndex, u64 setHandle);
    bool BindVertexBuffer(u32 binding, u64 bufferHandle, u64 offset);
    bool BindIndexBuffer(u64 bufferHandle, u64 offset, u32 indexType);
    bool SetViewport(const Viewport& viewport);
    bool SetScissor(const ScissorRect& scissor);
    void MarkPushConstantDirty();

    // Query current state
    u64 GetBoundPipeline() const;
    u64 GetBoundDescriptorSet(u32 setIndex) const;
    bool IsPushConstantDirty() const;

    // Reset all cached state (call at command buffer begin)
    void Invalidate();

    // Reset stats
    void ResetStats();

    StateCacheStats GetStats() const;

private:
    StateCacheConfig m_config;

    u64 m_boundPipeline = 0;
    std::vector<u64> m_boundDescriptorSets;
    std::vector<VertexBufferBinding> m_boundVertexBuffers;
    IndexBufferBinding m_boundIndexBuffer = {};
    Viewport m_currentViewport = {};
    ScissorRect m_currentScissor = {};
    bool m_pushConstantDirty = true;
    bool m_pipelineValid = false;
    bool m_indexBufferValid = false;
    bool m_viewportValid = false;
    bool m_scissorValid = false;

    // Stats
    u32 m_pipelineBinds = 0;
    u32 m_pipelineAvoided = 0;
    u32 m_descSetBinds = 0;
    u32 m_descSetAvoided = 0;
    u32 m_vtxBufBinds = 0;
    u32 m_vtxBufAvoided = 0;
    u32 m_idxBufBinds = 0;
    u32 m_idxBufAvoided = 0;
    u32 m_viewportSets = 0;
    u32 m_viewportAvoided = 0;
    u32 m_scissorSets = 0;
    u32 m_scissorAvoided = 0;
    u32 m_pushConstantUpdates = 0;
};

} // namespace nge::rhi
