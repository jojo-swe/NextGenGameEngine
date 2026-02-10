#include "engine/rhi/common/rhi_transient_attachment_allocator.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

static u32 FormatBytesPerPixel(AttachmentFormat fmt) {
    switch (fmt) {
        case AttachmentFormat::RGBA8_UNORM:
        case AttachmentFormat::RGBA8_SRGB:           return 4;
        case AttachmentFormat::RGBA16_FLOAT:          return 8;
        case AttachmentFormat::RGBA32_FLOAT:          return 16;
        case AttachmentFormat::R16_FLOAT:             return 2;
        case AttachmentFormat::R32_FLOAT:             return 4;
        case AttachmentFormat::RG16_FLOAT:            return 4;
        case AttachmentFormat::RG32_FLOAT:            return 8;
        case AttachmentFormat::R11G11B10_FLOAT:       return 4;
        case AttachmentFormat::D32_FLOAT:             return 4;
        case AttachmentFormat::D24_UNORM_S8_UINT:     return 4;
        case AttachmentFormat::D32_FLOAT_S8_UINT:     return 8;
        default: return 4;
    }
}

bool TransientAttachmentAllocator::Init(const TransientAllocatorConfig& config) {
    m_config = config;
    m_slots.reserve(config.maxAttachments);
    m_nextId = 1;
    m_totalMemoryUsed = 0;
    m_memoryWithoutAliasing = 0;
    m_totalAllocations = 0;
    m_aliasedAllocations = 0;
    m_peakActive = 0;
    m_totalRecycled = 0;

    NGE_LOG_INFO("Transient attachment allocator initialized: maxAttachments={}, budget={} MB, aliasing={}, lazy={}",
                 config.maxAttachments, config.memoryBudget / (1024 * 1024),
                 config.enableAliasing, config.preferLazyAllocation);
    return true;
}

void TransientAttachmentAllocator::Shutdown() {
    m_slots.clear();
    m_slotIndex.clear();
}

TransientAttachmentHandle TransientAttachmentAllocator::Allocate(const TransientAttachmentDesc& desc,
                                                                   u32 firstPass, u32 lastPass) {
    std::lock_guard lock(m_mutex);

    u64 size = ComputeAttachmentSize(desc);
    m_memoryWithoutAliasing += size;

    TransientAttachmentHandle handle{};
    handle.id = m_nextId++;

    // Try aliasing: find an existing slot with non-overlapping lifetime and compatible size
    u64 aliasBlock = 0;
    if (m_config.enableAliasing) {
        aliasBlock = FindAliasingCandidate(desc, firstPass, lastPass);
    }

    if (aliasBlock != 0) {
        handle.memoryBlock = aliasBlock;
        m_aliasedAllocations++;
    } else {
        // Check budget
        if (m_totalMemoryUsed + size > m_config.memoryBudget) {
            NGE_LOG_WARN("Transient attachment allocator: budget exceeded ({} + {} > {} MB)",
                         m_totalMemoryUsed / (1024 * 1024), size / (1024 * 1024),
                         m_config.memoryBudget / (1024 * 1024));
        }

        handle.memoryBlock = AllocateMemoryBlock(size);
        m_totalMemoryUsed += size;
    }

    // TODO: Create VkImage with VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT
    handle.imageHandle = handle.id * 1000; // Placeholder

    TransientAttachmentSlot slot;
    slot.desc = desc;
    slot.handle = handle;
    slot.firstPassUsed = firstPass;
    slot.lastPassUsed = lastPass;
    slot.memorySizeBytes = size;
    slot.inUse = true;

    u32 index = static_cast<u32>(m_slots.size());
    m_slotIndex[handle.id] = index;
    m_slots.push_back(std::move(slot));

    m_totalAllocations++;

    u32 activeCount = GetActiveCount();
    if (activeCount > m_peakActive) m_peakActive = activeCount;

    return handle;
}

void TransientAttachmentAllocator::Release(u64 allocationId) {
    std::lock_guard lock(m_mutex);

    auto it = m_slotIndex.find(allocationId);
    if (it == m_slotIndex.end()) return;

    m_slots[it->second].inUse = false;
}

void TransientAttachmentAllocator::BeginFrame() {
    std::lock_guard lock(m_mutex);

    u32 recycled = 0;
    for (auto& slot : m_slots) {
        if (slot.inUse) {
            slot.inUse = false;
            recycled++;
        }
    }

    m_totalRecycled += recycled;

    // Clear for new frame
    m_slots.clear();
    m_slotIndex.clear();
    m_totalMemoryUsed = 0;
    m_memoryWithoutAliasing = 0;
}

const TransientAttachmentSlot* TransientAttachmentAllocator::GetSlot(u64 allocationId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_slotIndex.find(allocationId);
    if (it == m_slotIndex.end()) return nullptr;
    return &m_slots[it->second];
}

bool TransientAttachmentAllocator::CanAlias(u64 allocA, u64 allocB) const {
    std::lock_guard lock(m_mutex);

    auto itA = m_slotIndex.find(allocA);
    auto itB = m_slotIndex.find(allocB);
    if (itA == m_slotIndex.end() || itB == m_slotIndex.end()) return false;

    const auto& a = m_slots[itA->second];
    const auto& b = m_slots[itB->second];

    // Non-overlapping lifetimes can alias
    return a.lastPassUsed < b.firstPassUsed || b.lastPassUsed < a.firstPassUsed;
}

u64 TransientAttachmentAllocator::GetMemoryUsed() const {
    std::lock_guard lock(m_mutex);
    return m_totalMemoryUsed;
}

u32 TransientAttachmentAllocator::GetActiveCount() const {
    u32 count = 0;
    for (const auto& slot : m_slots) {
        if (slot.inUse) count++;
    }
    return count;
}

void TransientAttachmentAllocator::Reset() {
    std::lock_guard lock(m_mutex);
    m_slots.clear();
    m_slotIndex.clear();
    m_nextId = 1;
    m_totalMemoryUsed = 0;
    m_memoryWithoutAliasing = 0;
    m_totalAllocations = 0;
    m_aliasedAllocations = 0;
    m_peakActive = 0;
    m_totalRecycled = 0;
}

TransientAllocatorStats TransientAttachmentAllocator::GetStats() const {
    std::lock_guard lock(m_mutex);
    TransientAllocatorStats stats{};
    stats.totalAllocations = m_totalAllocations;

    u32 active = 0;
    for (const auto& slot : m_slots) {
        if (slot.inUse) active++;
    }
    stats.activeAllocations = active;
    stats.aliasedAllocations = m_aliasedAllocations;
    stats.totalMemoryUsed = m_totalMemoryUsed;
    stats.memoryWithoutAliasing = m_memoryWithoutAliasing;
    stats.memorySaved = m_memoryWithoutAliasing > m_totalMemoryUsed ?
                         m_memoryWithoutAliasing - m_totalMemoryUsed : 0;
    stats.peakActiveCount = m_peakActive;
    stats.totalRecycled = m_totalRecycled;

    return stats;
}

u64 TransientAttachmentAllocator::ComputeAttachmentSize(const TransientAttachmentDesc& desc) const {
    u64 bpp = FormatBytesPerPixel(desc.format);
    u64 size = static_cast<u64>(desc.width) * desc.height * bpp * desc.samples * desc.arrayLayers;
    // Align to 64KB (typical GPU page size)
    size = (size + 65535) & ~65535ULL;
    return size;
}

u64 TransientAttachmentAllocator::FindAliasingCandidate(const TransientAttachmentDesc& desc,
                                                          u32 firstPass, u32 lastPass) const {
    u64 requiredSize = ComputeAttachmentSize(desc);

    for (const auto& slot : m_slots) {
        // Must not be currently in use in overlapping passes
        if (slot.lastPassUsed >= firstPass && slot.firstPassUsed <= lastPass) continue;

        // Memory block must be large enough
        if (slot.memorySizeBytes >= requiredSize) {
            return slot.handle.memoryBlock;
        }
    }

    return 0;
}

u64 TransientAttachmentAllocator::AllocateMemoryBlock(u64 size) {
    // TODO: vkAllocateMemory with VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT
    // if m_config.preferLazyAllocation
    return m_nextId * 10000 + size; // Placeholder handle
}

} // namespace nge::rhi
