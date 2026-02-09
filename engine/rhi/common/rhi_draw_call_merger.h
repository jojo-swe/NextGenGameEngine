#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Draw Call Merger ────────────────────────────────────────────────
// Batches compatible draw calls into multi-draw indirect (MDI) commands
// to reduce CPU overhead and vkCmdDraw* call count. Groups draws by
// pipeline state, material, vertex/index buffer binding.
//
// Pipeline:
//   1. Collect individual draw requests from the scene renderer
//   2. Sort by batch key (PSO + material + vertex format)
//   3. Merge compatible draws into VkDrawIndexedIndirectCommand arrays
//   4. Output MDI buffers + per-batch metadata for submission
//
// Compatible with VK_KHR_draw_indirect_count for variable-count MDI.

struct DrawRequest {
    u64  psoHash;           // Pipeline state object hash
    u32  materialId;
    u64  vertexBufferHandle;
    u64  indexBufferHandle;
    u32  indexCount;
    u32  firstIndex;
    i32  vertexOffset;
    u32  instanceCount;
    u32  firstInstance;
    u32  sortKey;           // Optional: depth/material sort key
    std::string debugName;
};

struct MergedBatch {
    u64  psoHash;
    u32  materialId;
    u64  vertexBufferHandle;
    u64  indexBufferHandle;
    u32  drawCount;         // Number of draws in this batch

    struct IndirectCommand {
        u32 indexCount;
        u32 instanceCount;
        u32 firstIndex;
        i32 vertexOffset;
        u32 firstInstance;
    };

    std::vector<IndirectCommand> commands;
};

struct DrawCallMergerConfig {
    u32  maxDrawsPerBatch = 4096;   // Max draws in a single MDI call
    u32  maxBatches = 256;
    bool sortByDepth = false;        // Sort within batches by depth
    bool enableMerging = true;       // Can disable for debugging
};

struct DrawCallMergerStats {
    u32 inputDrawCalls;
    u32 outputBatches;
    u32 outputDrawCommands;
    u32 mergedDrawCalls;     // Draws saved by merging
    f32 reductionPercent;
    u32 largestBatchSize;
};

class DrawCallMerger {
public:
    bool Init(const DrawCallMergerConfig& config = {});
    void Shutdown();

    // Submit draw requests for the current frame
    void Submit(const DrawRequest& request);
    void SubmitBatch(const DrawRequest* requests, u32 count);

    // Process all submitted draws into merged batches
    void Merge();

    // Get merged output
    const std::vector<MergedBatch>& GetBatches() const { return m_batches; }

    // Get flat indirect command buffer for GPU upload
    std::vector<MergedBatch::IndirectCommand> GetIndirectBuffer(u32 batchIndex) const;

    // Clear for next frame
    void Clear();

    DrawCallMergerStats GetStats() const;

private:
    struct BatchKey {
        u64 psoHash;
        u32 materialId;
        u64 vertexBuffer;
        u64 indexBuffer;

        bool operator==(const BatchKey& other) const {
            return psoHash == other.psoHash && materialId == other.materialId &&
                   vertexBuffer == other.vertexBuffer && indexBuffer == other.indexBuffer;
        }
    };

    struct BatchKeyHash {
        size_t operator()(const BatchKey& k) const {
            size_t h = std::hash<u64>()(k.psoHash);
            h ^= std::hash<u32>()(k.materialId) << 1;
            h ^= std::hash<u64>()(k.vertexBuffer) << 2;
            h ^= std::hash<u64>()(k.indexBuffer) << 3;
            return h;
        }
    };

    DrawCallMergerConfig m_config;
    std::vector<DrawRequest> m_requests;
    std::vector<MergedBatch> m_batches;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
