#include "engine/rhi/common/rhi_buffer_pool.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool BufferPool::Init(IDevice* device, const Config& config) {
    m_device = device;
    m_config = config;
    m_currentBlock = 0;
    m_currentFrame = 0;
    m_totalAllocated = 0;

    NGE_LOG_INFO("Buffer pool initialized: {} MB blocks, {} alignment, {} max frames",
                 config.blockSize / (1024 * 1024), config.alignment, config.maxFramesInFlight);
    return true;
}

void BufferPool::Shutdown() {
    for (auto& block : m_blocks) {
        if (block.buffer.IsValid()) {
            if (block.mappedBase) {
                m_device->UnmapBuffer(block.buffer);
            }
            m_device->DestroyBuffer(block.buffer);
        }
    }
    m_blocks.clear();
    m_totalAllocated = 0;
}

void BufferPool::BeginFrame(u64 frameIndex) {
    m_currentFrame = frameIndex;
    m_totalAllocated = 0;

    // Reset blocks that are no longer in flight
    u64 safeFrame = (frameIndex >= m_config.maxFramesInFlight)
                    ? frameIndex - m_config.maxFramesInFlight : 0;

    for (auto& block : m_blocks) {
        if (block.lastUsedFrame <= safeFrame) {
            block.currentOffset = 0;
        }
    }

    m_currentBlock = 0;
}

void BufferPool::EndFrame() {
    // Mark active blocks with current frame
    for (auto& block : m_blocks) {
        if (block.currentOffset > 0) {
            block.lastUsedFrame = m_currentFrame;
        }
    }
}

BufferPoolAllocation BufferPool::Allocate(u32 size) {
    std::lock_guard lock(m_mutex);

    u32 alignedSize = AlignUp(size, m_config.alignment);

    // Try current block
    while (m_currentBlock < m_blocks.size()) {
        auto& block = m_blocks[m_currentBlock];
        u32 alignedOffset = AlignUp(block.currentOffset, m_config.alignment);

        if (alignedOffset + alignedSize <= m_config.blockSize) {
            BufferPoolAllocation alloc;
            alloc.buffer = block.buffer;
            alloc.offset = alignedOffset;
            alloc.size = alignedSize;
            alloc.mappedPtr = static_cast<u8*>(block.mappedBase) + alignedOffset;

            block.currentOffset = alignedOffset + alignedSize;
            m_totalAllocated += alignedSize;
            return alloc;
        }

        m_currentBlock++;
    }

    // Need a new block
    auto& newBlock = GetOrCreateBlock();
    u32 alignedOffset = 0;

    BufferPoolAllocation alloc;
    alloc.buffer = newBlock.buffer;
    alloc.offset = alignedOffset;
    alloc.size = alignedSize;
    alloc.mappedPtr = static_cast<u8*>(newBlock.mappedBase) + alignedOffset;

    newBlock.currentOffset = alignedSize;
    m_totalAllocated += alignedSize;
    return alloc;
}

void BufferPool::Reset() {
    for (auto& block : m_blocks) {
        block.currentOffset = 0;
        block.lastUsedFrame = 0;
    }
    m_currentBlock = 0;
    m_totalAllocated = 0;
}

f32 BufferPool::GetUtilization() const {
    u32 capacity = GetTotalCapacity();
    return capacity > 0 ? static_cast<f32>(m_totalAllocated) / static_cast<f32>(capacity) : 0.0f;
}

BufferPool::Block& BufferPool::GetOrCreateBlock() {
    // Check if any existing block has space
    for (u32 i = 0; i < static_cast<u32>(m_blocks.size()); ++i) {
        if (m_blocks[i].currentOffset == 0) {
            m_currentBlock = i;
            return m_blocks[i];
        }
    }

    // Allocate new GPU buffer
    BufferDesc desc;
    desc.size = m_config.blockSize;
    desc.usage = m_config.usage;
    desc.memoryUsage = m_config.memoryUsage;
    desc.debugName = "BufferPool_Block";

    Block block;
    block.buffer = m_device->CreateBuffer(desc);
    block.mappedBase = m_device->MapBuffer(block.buffer);
    block.currentOffset = 0;
    block.lastUsedFrame = m_currentFrame;

    m_blocks.push_back(block);
    m_currentBlock = static_cast<u32>(m_blocks.size()) - 1;

    NGE_LOG_DEBUG("Buffer pool: allocated new {} MB block (total: {} blocks)",
                  m_config.blockSize / (1024 * 1024), m_blocks.size());

    return m_blocks.back();
}

} // namespace nge::rhi
