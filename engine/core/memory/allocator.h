#pragma once

#include "engine/core/types.h"

namespace nge {

// ─── Allocator Interface ──────────────────────────────────────────────────
class IAllocator {
public:
    virtual ~IAllocator() = default;

    [[nodiscard]] virtual void* Allocate(usize size, usize alignment = alignof(std::max_align_t)) = 0;
    virtual void Free(void* ptr) = 0;
    virtual void Reset() = 0;

    virtual usize GetAllocatedSize() const = 0;
    virtual const char* GetName() const = 0;

    // Typed helpers
    template <typename T, typename... Args>
    T* New(Args&&... args) {
        void* mem = Allocate(sizeof(T), alignof(T));
        return new (mem) T(static_cast<Args&&>(args)...);
    }

    template <typename T>
    void Delete(T* ptr) {
        if (ptr) {
            ptr->~T();
            Free(ptr);
        }
    }

    template <typename T>
    T* AllocateArray(usize count) {
        return static_cast<T*>(Allocate(sizeof(T) * count, alignof(T)));
    }
};

// ─── Memory tracking tag (debug builds) ──────────────────────────────────
#if defined(NGE_ENABLE_MEMORY_TRACKING)

struct AllocationInfo {
    const char* file     = nullptr;
    i32         line     = 0;
    const char* tag      = nullptr;
    usize       size     = 0;
    usize       alignment = 0;
};

#define NGE_ALLOC(allocator, size, align) \
    (allocator).Allocate(size, align)

#define NGE_FREE(allocator, ptr) \
    (allocator).Free(ptr)

#else

#define NGE_ALLOC(allocator, size, align) \
    (allocator).Allocate(size, align)

#define NGE_FREE(allocator, ptr) \
    (allocator).Free(ptr)

#endif

} // namespace nge
