#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Resource Barrier Deduplicator ───────────────────────────────────
// Eliminates redundant pipeline barriers before submission. Detects and
// removes duplicate transitions, merges compatible barriers, and tracks
// barrier statistics for profiling.
//
// Use cases:
//   - Remove redundant same-state transitions
//   - Merge multiple barriers on the same resource into one
//   - Batch barriers for vkCmdPipelineBarrier2
//   - Profile barrier overhead (count + stage stalls)

enum class BarrierResourceType : u8 {
    Buffer,
    Image,
    Global,
};

enum class PipelineStage : u32 {
    None             = 0,
    TopOfPipe        = 1 << 0,
    DrawIndirect     = 1 << 1,
    VertexInput      = 1 << 2,
    VertexShader     = 1 << 3,
    FragmentShader   = 1 << 4,
    EarlyFragment    = 1 << 5,
    LateFragment     = 1 << 6,
    ColorOutput      = 1 << 7,
    ComputeShader    = 1 << 8,
    Transfer         = 1 << 9,
    BottomOfPipe     = 1 << 10,
    AllGraphics      = 1 << 11,
    AllCommands      = 1 << 12,
    RayTracing       = 1 << 13,
};

enum class AccessMask : u32 {
    None                = 0,
    IndirectRead        = 1 << 0,
    IndexRead           = 1 << 1,
    VertexRead          = 1 << 2,
    UniformRead         = 1 << 3,
    InputAttachmentRead = 1 << 4,
    ShaderRead          = 1 << 5,
    ShaderWrite         = 1 << 6,
    ColorAttachmentRead = 1 << 7,
    ColorAttachmentWrite = 1 << 8,
    DepthStencilRead    = 1 << 9,
    DepthStencilWrite   = 1 << 10,
    TransferRead        = 1 << 11,
    TransferWrite       = 1 << 12,
    HostRead            = 1 << 13,
    HostWrite           = 1 << 14,
    MemoryRead          = 1 << 15,
    MemoryWrite         = 1 << 16,
};

enum class ImageLayoutBarrier : u8 {
    Undefined,
    General,
    ColorAttachment,
    DepthStencilAttachment,
    DepthStencilReadOnly,
    ShaderReadOnly,
    TransferSrc,
    TransferDst,
    PresentSrc,
};

struct BarrierDesc {
    u64                 resourceHandle;
    BarrierResourceType resourceType;
    u32                 srcStageMask;    // PipelineStage flags
    u32                 dstStageMask;
    u32                 srcAccessMask;   // AccessMask flags
    u32                 dstAccessMask;
    ImageLayoutBarrier  oldLayout;       // For images only
    ImageLayoutBarrier  newLayout;
    u32                 srcQueueFamily;  // UINT32_MAX = ignored
    u32                 dstQueueFamily;
    u32                 baseMipLevel;
    u32                 mipCount;
    u32                 baseArrayLayer;
    u32                 layerCount;
    std::string         debugName;
};

struct BarrierDedupConfig {
    bool removeRedundant = true;       // Remove same-state transitions
    bool mergeCompatible = true;       // Merge barriers on same resource
    bool trackStats = true;
    u32  maxBarriersPerBatch = 256;
};

struct BarrierDedupStats {
    u32 totalBarriersSubmitted;
    u32 redundantRemoved;
    u32 merged;
    u32 totalBatchesFlushed;
    u32 barriersAfterDedup;
    float reductionRatio;              // (removed + merged) / submitted
};

class BarrierDeduplicator {
public:
    bool Init(const BarrierDedupConfig& config = {});
    void Shutdown();

    // Queue a barrier for deduplication
    void QueueBarrier(const BarrierDesc& barrier);

    // Process all queued barriers: deduplicate and return final list
    std::vector<BarrierDesc> Flush();

    // Get pending barrier count (before dedup)
    u32 GetPendingCount() const;

    // Check if a barrier is redundant (same src/dst state)
    bool IsRedundant(const BarrierDesc& barrier) const;

    // Discard all pending barriers
    void DiscardAll();

    void Reset();

    BarrierDedupStats GetStats() const;

private:
    bool AreOnSameResource(const BarrierDesc& a, const BarrierDesc& b) const;
    BarrierDesc MergeBarriers(const BarrierDesc& a, const BarrierDesc& b) const;

    BarrierDedupConfig m_config;
    std::vector<BarrierDesc> m_pending;

    u32 m_totalSubmitted = 0;
    u32 m_redundantRemoved = 0;
    u32 m_merged = 0;
    u32 m_totalFlushed = 0;
    u32 m_barriersAfterDedup = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
