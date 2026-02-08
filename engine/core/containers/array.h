#pragma once

#include "engine/core/types.h"
#include "engine/core/assert.h"
#include "engine/core/memory/allocator.h"
#include "engine/core/containers/span.h"
#include <cstring>
#include <new>
#include <utility>
#include <initializer_list>

namespace nge {

// ─── Dynamic Array with custom allocator support ─────────────────────────
template <typename T>
class Array {
public:
    Array() = default;

    explicit Array(IAllocator* alloc) : m_allocator(alloc) {}

    Array(std::initializer_list<T> init, IAllocator* alloc = nullptr)
        : m_allocator(alloc)
    {
        Reserve(init.size());
        for (const auto& item : init) {
            PushBack(item);
        }
    }

    ~Array() { Clear(); FreeBuffer(); }

    // Move
    Array(Array&& other) noexcept
        : m_data(other.m_data), m_size(other.m_size)
        , m_capacity(other.m_capacity), m_allocator(other.m_allocator)
    {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
    }

    Array& operator=(Array&& other) noexcept {
        if (this != &other) {
            Clear(); FreeBuffer();
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            m_allocator = other.m_allocator;
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }
        return *this;
    }

    // Copy
    Array(const Array& other) : m_allocator(other.m_allocator) {
        Reserve(other.m_size);
        for (usize i = 0; i < other.m_size; ++i) {
            new (&m_data[i]) T(other.m_data[i]);
        }
        m_size = other.m_size;
    }

    Array& operator=(const Array& other) {
        if (this != &other) {
            Clear();
            Reserve(other.m_size);
            for (usize i = 0; i < other.m_size; ++i) {
                new (&m_data[i]) T(other.m_data[i]);
            }
            m_size = other.m_size;
        }
        return *this;
    }

    // ─── Element access ───────────────────────────────────────────────
    T& operator[](usize index) { NGE_ASSERT(index < m_size); return m_data[index]; }
    const T& operator[](usize index) const { NGE_ASSERT(index < m_size); return m_data[index]; }

    T& Front() { NGE_ASSERT(m_size > 0); return m_data[0]; }
    const T& Front() const { NGE_ASSERT(m_size > 0); return m_data[0]; }
    T& Back() { NGE_ASSERT(m_size > 0); return m_data[m_size - 1]; }
    const T& Back() const { NGE_ASSERT(m_size > 0); return m_data[m_size - 1]; }

    T* Data() { return m_data; }
    const T* Data() const { return m_data; }

    // ─── Size / capacity ──────────────────────────────────────────────
    usize Size() const { return m_size; }
    usize Capacity() const { return m_capacity; }
    bool IsEmpty() const { return m_size == 0; }

    void Reserve(usize newCapacity) {
        if (newCapacity <= m_capacity) return;
        Grow(newCapacity);
    }

    void Resize(usize newSize) {
        if (newSize > m_capacity) Grow(newSize);
        // Construct new elements
        for (usize i = m_size; i < newSize; ++i) {
            new (&m_data[i]) T{};
        }
        // Destroy removed elements
        for (usize i = newSize; i < m_size; ++i) {
            m_data[i].~T();
        }
        m_size = newSize;
    }

    void Resize(usize newSize, const T& value) {
        if (newSize > m_capacity) Grow(newSize);
        for (usize i = m_size; i < newSize; ++i) {
            new (&m_data[i]) T(value);
        }
        for (usize i = newSize; i < m_size; ++i) {
            m_data[i].~T();
        }
        m_size = newSize;
    }

    // ─── Modifiers ────────────────────────────────────────────────────
    void PushBack(const T& value) {
        if (m_size == m_capacity) Grow(m_capacity == 0 ? 8 : m_capacity * 2);
        new (&m_data[m_size]) T(value);
        ++m_size;
    }

    void PushBack(T&& value) {
        if (m_size == m_capacity) Grow(m_capacity == 0 ? 8 : m_capacity * 2);
        new (&m_data[m_size]) T(static_cast<T&&>(value));
        ++m_size;
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        if (m_size == m_capacity) Grow(m_capacity == 0 ? 8 : m_capacity * 2);
        T* ptr = new (&m_data[m_size]) T(static_cast<Args&&>(args)...);
        ++m_size;
        return *ptr;
    }

    void PopBack() {
        NGE_ASSERT(m_size > 0);
        --m_size;
        m_data[m_size].~T();
    }

    void SwapRemove(usize index) {
        NGE_ASSERT(index < m_size);
        if (index != m_size - 1) {
            m_data[index] = static_cast<T&&>(m_data[m_size - 1]);
        }
        PopBack();
    }

    void Clear() {
        for (usize i = 0; i < m_size; ++i) {
            m_data[i].~T();
        }
        m_size = 0;
    }

    // ─── Iterators ────────────────────────────────────────────────────
    T* begin() { return m_data; }
    T* end() { return m_data + m_size; }
    const T* begin() const { return m_data; }
    const T* end() const { return m_data + m_size; }

    // ─── Conversion ───────────────────────────────────────────────────
    Span<T> ToSpan() { return Span<T>(m_data, m_size); }
    Span<const T> ToSpan() const { return Span<const T>(m_data, m_size); }

private:
    void Grow(usize newCapacity) {
        T* newData = static_cast<T*>(AllocMem(newCapacity * sizeof(T), alignof(T)));
        NGE_ASSERT(newData != nullptr);

        // Move existing elements
        if constexpr (std::is_trivially_copyable_v<T>) {
            if (m_data && m_size > 0) {
                std::memcpy(newData, m_data, m_size * sizeof(T));
            }
        } else {
            for (usize i = 0; i < m_size; ++i) {
                new (&newData[i]) T(static_cast<T&&>(m_data[i]));
                m_data[i].~T();
            }
        }

        FreeBuffer();
        m_data = newData;
        m_capacity = newCapacity;
    }

    void* AllocMem(usize size, usize align) {
        if (m_allocator) return m_allocator->Allocate(size, align);
        return ::operator new(size, std::align_val_t{align});
    }

    void FreeBuffer() {
        if (!m_data) return;
        if (m_allocator) {
            m_allocator->Free(m_data);
        } else {
            ::operator delete(m_data, std::align_val_t{alignof(T)});
        }
        m_data = nullptr;
        m_capacity = 0;
    }

    T*          m_data      = nullptr;
    usize       m_size      = 0;
    usize       m_capacity  = 0;
    IAllocator* m_allocator = nullptr;
};

} // namespace nge
