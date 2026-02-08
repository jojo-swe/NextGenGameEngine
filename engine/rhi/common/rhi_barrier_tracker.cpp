#include "engine/rhi/common/rhi_barrier_tracker.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

void BarrierTracker::Init() {
    m_bufferStates.clear();
    m_textureStates.clear();
    m_pending.clear();
}

void BarrierTracker::Reset() {
    m_bufferStates.clear();
    m_textureStates.clear();
    m_pending.clear();
}

void BarrierTracker::TrackBuffer(BufferHandle buffer, ResourceState initialState) {
    m_bufferStates[buffer.GetId()] = initialState;
}

void BarrierTracker::TrackTexture(TextureHandle texture, ResourceState initialState,
                                    u32 mipLevels, u32 arrayLayers) {
    TextureState ts;
    ts.mipLevels = mipLevels;
    ts.arrayLayers = arrayLayers;
    ts.subresourceStates.resize(mipLevels * arrayLayers, initialState);
    m_textureStates[texture.GetId()] = std::move(ts);
}

bool BarrierTracker::TransitionBuffer(BufferHandle buffer, ResourceState newState) {
    auto it = m_bufferStates.find(buffer.GetId());
    if (it == m_bufferStates.end()) {
        // Auto-track with Undefined initial state
        m_bufferStates[buffer.GetId()] = ResourceState::Undefined;
        it = m_bufferStates.find(buffer.GetId());
    }

    ResourceState oldState = it->second;
    if (oldState == newState) return false; // No barrier needed

    PendingBarrier barrier;
    barrier.type = PendingBarrier::Type::Buffer;
    barrier.buffer = buffer;
    barrier.oldState = oldState;
    barrier.newState = newState;
    m_pending.push_back(barrier);

    it->second = newState;
    return true;
}

bool BarrierTracker::TransitionTexture(TextureHandle texture, ResourceState newState, u32 mipLevel) {
    auto it = m_textureStates.find(texture.GetId());
    if (it == m_textureStates.end()) {
        TrackTexture(texture, ResourceState::Undefined);
        it = m_textureStates.find(texture.GetId());
    }

    auto& ts = it->second;
    bool anyBarrier = false;

    if (mipLevel == UINT32_MAX) {
        // Transition all subresources
        for (u32 i = 0; i < static_cast<u32>(ts.subresourceStates.size()); ++i) {
            if (ts.subresourceStates[i] != newState) {
                PendingBarrier barrier;
                barrier.type = PendingBarrier::Type::Texture;
                barrier.texture = texture;
                barrier.oldState = ts.subresourceStates[i];
                barrier.newState = newState;
                barrier.subresource = i;
                m_pending.push_back(barrier);

                ts.subresourceStates[i] = newState;
                anyBarrier = true;
            }
        }
    } else {
        // Transition a specific mip level (all array layers for that mip)
        for (u32 layer = 0; layer < ts.arrayLayers; ++layer) {
            u32 idx = mipLevel * ts.arrayLayers + layer;
            if (idx < ts.subresourceStates.size() && ts.subresourceStates[idx] != newState) {
                PendingBarrier barrier;
                barrier.type = PendingBarrier::Type::Texture;
                barrier.texture = texture;
                barrier.oldState = ts.subresourceStates[idx];
                barrier.newState = newState;
                barrier.subresource = idx;
                m_pending.push_back(barrier);

                ts.subresourceStates[idx] = newState;
                anyBarrier = true;
            }
        }
    }

    return anyBarrier;
}

bool BarrierTracker::TransitionBufferCrossQueue(BufferHandle buffer, ResourceState newState,
                                                   QueueType srcQueue, QueueType dstQueue) {
    auto it = m_bufferStates.find(buffer.GetId());
    if (it == m_bufferStates.end()) {
        m_bufferStates[buffer.GetId()] = ResourceState::Undefined;
        it = m_bufferStates.find(buffer.GetId());
    }

    ResourceState oldState = it->second;

    PendingBarrier barrier;
    barrier.type = PendingBarrier::Type::Buffer;
    barrier.buffer = buffer;
    barrier.oldState = oldState;
    barrier.newState = newState;
    barrier.srcQueue = srcQueue;
    barrier.dstQueue = dstQueue;
    m_pending.push_back(barrier);

    it->second = newState;
    return true;
}

bool BarrierTracker::TransitionTextureCrossQueue(TextureHandle texture, ResourceState newState,
                                                    QueueType srcQueue, QueueType dstQueue) {
    auto it = m_textureStates.find(texture.GetId());
    if (it == m_textureStates.end()) {
        TrackTexture(texture, ResourceState::Undefined);
        it = m_textureStates.find(texture.GetId());
    }

    auto& ts = it->second;
    ResourceState oldState = ts.subresourceStates.empty() ? ResourceState::Undefined : ts.subresourceStates[0];

    PendingBarrier barrier;
    barrier.type = PendingBarrier::Type::Texture;
    barrier.texture = texture;
    barrier.oldState = oldState;
    barrier.newState = newState;
    barrier.srcQueue = srcQueue;
    barrier.dstQueue = dstQueue;
    m_pending.push_back(barrier);

    for (auto& s : ts.subresourceStates) s = newState;
    return true;
}

ResourceState BarrierTracker::GetBufferState(BufferHandle buffer) const {
    auto it = m_bufferStates.find(buffer.GetId());
    return it != m_bufferStates.end() ? it->second : ResourceState::Undefined;
}

ResourceState BarrierTracker::GetTextureState(TextureHandle texture, u32 mipLevel) const {
    auto it = m_textureStates.find(texture.GetId());
    if (it == m_textureStates.end()) return ResourceState::Undefined;

    const auto& ts = it->second;
    u32 idx = mipLevel * ts.arrayLayers; // First layer of the mip
    return idx < ts.subresourceStates.size() ? ts.subresourceStates[idx] : ResourceState::Undefined;
}

void BarrierTracker::FlushBarriers(ICommandList* cmd) {
    if (m_pending.empty()) return;

    // TODO: Batch barriers into a single vkCmdPipelineBarrier2 call
    // VkDependencyInfo depInfo{};
    // std::vector<VkBufferMemoryBarrier2> bufBarriers;
    // std::vector<VkImageMemoryBarrier2> imgBarriers;
    //
    // for (const auto& b : m_pending) {
    //     if (b.type == PendingBarrier::Type::Buffer) {
    //         VkBufferMemoryBarrier2 bb{};
    //         bb.srcStageMask = GetStageMask(b.oldState);
    //         bb.srcAccessMask = GetAccessMask(b.oldState);
    //         bb.dstStageMask = GetStageMask(b.newState);
    //         bb.dstAccessMask = GetAccessMask(b.newState);
    //         bb.buffer = GetVkBuffer(b.buffer);
    //         bb.size = VK_WHOLE_SIZE;
    //         if (b.srcQueue != b.dstQueue) {
    //             bb.srcQueueFamilyIndex = GetQueueFamily(b.srcQueue);
    //             bb.dstQueueFamilyIndex = GetQueueFamily(b.dstQueue);
    //         }
    //         bufBarriers.push_back(bb);
    //     } else {
    //         VkImageMemoryBarrier2 ib{};
    //         ib.oldLayout = ConvertLayout(b.oldState);
    //         ib.newLayout = ConvertLayout(b.newState);
    //         ib.image = GetVkImage(b.texture);
    //         ib.subresourceRange = { aspectFlags, mip, 1, layer, 1 };
    //         imgBarriers.push_back(ib);
    //     }
    // }
    // vkCmdPipelineBarrier2(cmd, &depInfo);

    (void)cmd;
    m_pending.clear();
}

void BarrierTracker::UntrackBuffer(BufferHandle buffer) {
    m_bufferStates.erase(buffer.GetId());
}

void BarrierTracker::UntrackTexture(TextureHandle texture) {
    m_textureStates.erase(texture.GetId());
}

} // namespace nge::rhi
