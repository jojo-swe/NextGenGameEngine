#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Descriptor Set Update Batcher ───────────────────────────────────
// Coalesces descriptor write operations to minimize vkUpdateDescriptorSets
// calls. Batches writes per descriptor set and flushes them in a single
// API call when the batch is full or explicitly flushed.
//
// Use cases:
//   - Material system: batch all texture/UBO updates per material
//   - Reduce driver overhead from many small update calls
//   - Deferred descriptor writes: queue writes, flush before draw
//   - Debug: track update frequency and write counts

enum class DescWriteType : u8 {
    UniformBuffer,
    StorageBuffer,
    UniformBufferDynamic,
    StorageBufferDynamic,
    CombinedImageSampler,
    SampledImage,
    StorageImage,
    UniformTexelBuffer,
    StorageTexelBuffer,
    InputAttachment,
    AccelerationStructure,
};

struct BufferWriteInfo {
    u64 bufferHandle;
    u64 offset;
    u64 range;
};

struct ImageWriteInfo {
    u64 imageViewHandle;
    u64 samplerHandle;
    u32 imageLayout;   // ImageLayout enum value
};

struct DescriptorWrite {
    u64            descriptorSet;
    u32            binding;
    u32            arrayElement;
    u32            count;
    DescWriteType  type;
    BufferWriteInfo bufferInfo;
    ImageWriteInfo  imageInfo;
};

struct DescriptorCopy {
    u64 srcSet;
    u32 srcBinding;
    u32 srcArrayElement;
    u64 dstSet;
    u32 dstBinding;
    u32 dstArrayElement;
    u32 count;
};

struct DescUpdateBatcherConfig {
    u32  maxPendingWrites = 1024;
    u32  maxPendingCopies = 128;
    u32  autoFlushThreshold = 256;  // Auto-flush when this many writes pending
    bool enableCoalescing = true;    // Merge consecutive binding writes
    bool trackPerSetStats = true;
};

struct DescUpdateBatcherStats {
    u32 totalWritesQueued;
    u32 totalCopiesQueued;
    u32 totalFlushes;
    u32 totalWritesFlushed;
    u32 totalCopiesFlushed;
    u32 coalescedWrites;          // Writes merged with prior same-set writes
    u32 pendingWrites;
    u32 pendingCopies;
    u32 uniqueSetsUpdated;
};

class DescriptorUpdateBatcher {
public:
    bool Init(const DescUpdateBatcherConfig& config = {});
    void Shutdown();

    // Queue a descriptor write
    void WriteBuffer(u64 descriptorSet, u32 binding, u32 arrayElement,
                      DescWriteType type, u64 bufferHandle, u64 offset, u64 range);

    void WriteImage(u64 descriptorSet, u32 binding, u32 arrayElement,
                     DescWriteType type, u64 imageViewHandle, u64 samplerHandle, u32 imageLayout);

    // Queue a descriptor copy
    void CopyDescriptor(u64 srcSet, u32 srcBinding, u32 srcArrayElement,
                          u64 dstSet, u32 dstBinding, u32 dstArrayElement, u32 count);

    // Flush all pending writes/copies (call before draw/dispatch)
    u32 Flush();

    // Flush only writes for a specific descriptor set
    u32 FlushSet(u64 descriptorSet);

    // Get pending write count
    u32 GetPendingWriteCount() const;
    u32 GetPendingCopyCount() const;

    // Check if a set has pending writes
    bool HasPendingWrites(u64 descriptorSet) const;

    // Discard all pending writes
    void DiscardAll();

    // Reset stats and state
    void Reset();

    DescUpdateBatcherStats GetStats() const;

private:
    void AutoFlushIfNeeded();
    bool TryCoalesce(const DescriptorWrite& write);

    DescUpdateBatcherConfig m_config;
    std::vector<DescriptorWrite> m_pendingWrites;
    std::vector<DescriptorCopy> m_pendingCopies;

    u32 m_totalWritesQueued = 0;
    u32 m_totalCopiesQueued = 0;
    u32 m_totalFlushes = 0;
    u32 m_totalWritesFlushed = 0;
    u32 m_totalCopiesFlushed = 0;
    u32 m_coalescedWrites = 0;

    // Per-set tracking
    std::unordered_map<u64, u32> m_perSetWriteCount;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
