#pragma once

#include "engine/core/types.h"
#include "engine/core/assert.h"

#if defined(NGE_PLATFORM_WINDOWS)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#elif defined(NGE_PLATFORM_LINUX)
    #include <sys/mman.h>
    #include <unistd.h>
#endif

namespace nge::platform {

// ─── Virtual Memory Operations ────────────────────────────────────────────
// Reserve: reserves address space without committing physical pages.
// Commit: commits physical pages within a reserved range.
// Decommit: releases physical pages but keeps address reservation.
// Release: fully releases the address range.

inline void* VirtualReserve(usize size) {
#if defined(NGE_PLATFORM_WINDOWS)
    return ::VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
#elif defined(NGE_PLATFORM_LINUX)
    void* ptr = ::mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return ptr == MAP_FAILED ? nullptr : ptr;
#else
    return nullptr;
#endif
}

inline bool VirtualCommit(void* ptr, usize size) {
#if defined(NGE_PLATFORM_WINDOWS)
    return ::VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
#elif defined(NGE_PLATFORM_LINUX)
    return ::mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0;
#else
    return false;
#endif
}

inline void VirtualDecommit(void* ptr, usize size) {
#if defined(NGE_PLATFORM_WINDOWS)
    ::VirtualFree(ptr, size, MEM_DECOMMIT);
#elif defined(NGE_PLATFORM_LINUX)
    ::madvise(ptr, size, MADV_DONTNEED);
    ::mprotect(ptr, size, PROT_NONE);
#endif
}

inline void VirtualRelease(void* ptr, [[maybe_unused]] usize size) {
#if defined(NGE_PLATFORM_WINDOWS)
    ::VirtualFree(ptr, 0, MEM_RELEASE);
#elif defined(NGE_PLATFORM_LINUX)
    ::munmap(ptr, size);
#endif
}

} // namespace nge::platform

namespace nge {

// ─── Virtual Memory Allocator ─────────────────────────────────────────────
// Reserves a large virtual address range, commits pages on demand.
// Ideal for large streaming data, page-based allocations.
class VirtualMemoryAllocator final : public IAllocator {
public:
    VirtualMemoryAllocator(usize reserveSize, const char* name = "VirtualMemory")
        : m_reserveSize(AlignUp(reserveSize, PAGE_SIZE))
        , m_committedSize(0)
        , m_name(name)
    {
        m_base = static_cast<byte*>(platform::VirtualReserve(m_reserveSize));
        NGE_ASSERT_MSG(m_base != nullptr, "Failed to reserve virtual memory");
    }

    ~VirtualMemoryAllocator() override {
        if (m_base) {
            platform::VirtualRelease(m_base, m_reserveSize);
        }
    }

    VirtualMemoryAllocator(const VirtualMemoryAllocator&) = delete;
    VirtualMemoryAllocator& operator=(const VirtualMemoryAllocator&) = delete;

    [[nodiscard]] void* Allocate(usize size, usize alignment = PAGE_SIZE) override {
        usize alignedOffset = AlignUp(m_committedSize, alignment);
        usize newCommit = AlignUp(alignedOffset + size, PAGE_SIZE);

        if (newCommit > m_reserveSize) return nullptr;

        // Commit new pages if needed
        if (newCommit > m_committedSize) {
            if (!platform::VirtualCommit(m_base + m_committedSize, newCommit - m_committedSize)) {
                return nullptr;
            }
        }

        void* ptr = m_base + alignedOffset;
        m_committedSize = newCommit;
        return ptr;
    }

    void Free(void* /*ptr*/) override {
        // Virtual memory allocator doesn't support individual frees.
        // Use Decommit or Reset.
    }

    void Decommit(usize fromOffset) {
        usize aligned = AlignUp(fromOffset, PAGE_SIZE);
        if (aligned < m_committedSize) {
            platform::VirtualDecommit(m_base + aligned, m_committedSize - aligned);
            m_committedSize = aligned;
        }
    }

    void Reset() override {
        if (m_committedSize > 0) {
            platform::VirtualDecommit(m_base, m_committedSize);
            m_committedSize = 0;
        }
    }

    byte* GetBase() const { return m_base; }
    usize GetAllocatedSize() const override { return m_committedSize; }
    usize GetReservedSize() const { return m_reserveSize; }
    const char* GetName() const override { return m_name; }

private:
    byte*       m_base;
    usize       m_reserveSize;
    usize       m_committedSize;
    const char* m_name;
};

} // namespace nge
