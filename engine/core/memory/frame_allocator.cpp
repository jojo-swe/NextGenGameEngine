#include "engine/core/memory/frame_allocator.h"
#include <cstring>
#include <algorithm>

namespace nge::memory {

FrameAllocator::FrameAllocator(usize blockSize)
    : m_blockSize(blockSize) {
    m_blocks[0] = static_cast<u8*>(std::malloc(blockSize));
    m_blocks[1] = static_cast<u8*>(std::malloc(blockSize));
    m_currentBlock = 0;
    m_offset = 0;
}

FrameAllocator::~FrameAllocator() {
    std::free(m_blocks[0]);
    std::free(m_blocks[1]);
    m_blocks[0] = nullptr;
    m_blocks[1] = nullptr;
}

void FrameAllocator::BeginFrame() {
    // Track peak usage
    m_peakUsage = std::max(m_peakUsage, m_offset);

    // Swap to the other block and reset
    m_currentBlock = 1 - m_currentBlock;
    m_offset = 0;
}

void* FrameAllocator::Allocate(usize size, usize alignment) {
    uintptr_t base = reinterpret_cast<uintptr_t>(m_blocks[m_currentBlock]);
    uintptr_t alignedPtr = AlignUp(base + m_offset, alignment);
    usize alignedOffset = static_cast<usize>(alignedPtr - base);

    if (alignedOffset + size > m_blockSize) {
        // Out of memory — in production, could chain overflow blocks
        assert(false && "FrameAllocator: out of memory");
        return nullptr;
    }

    void* ptr = reinterpret_cast<void*>(alignedPtr);
    m_offset = alignedOffset + size;
    return ptr;
}

usize FrameAllocator::GetRemainingBytes() const {
    return m_blockSize > m_offset ? m_blockSize - m_offset : 0;
}

} // namespace nge::memory
