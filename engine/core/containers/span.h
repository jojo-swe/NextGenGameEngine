#pragma once

#include "engine/core/types.h"
#include "engine/core/assert.h"

namespace nge {

// ─── Non-owning view over contiguous elements ────────────────────────────
template <typename T>
class Span {
public:
    constexpr Span() : m_data(nullptr), m_size(0) {}
    constexpr Span(T* data, usize size) : m_data(data), m_size(size) {}
    constexpr Span(T* begin, T* end) : m_data(begin), m_size(static_cast<usize>(end - begin)) {}

    template <usize N>
    constexpr Span(T (&arr)[N]) : m_data(arr), m_size(N) {}

    constexpr T* Data() const { return m_data; }
    constexpr usize Size() const { return m_size; }
    constexpr usize SizeBytes() const { return m_size * sizeof(T); }
    constexpr bool IsEmpty() const { return m_size == 0; }

    constexpr T& operator[](usize index) const {
        NGE_ASSERT(index < m_size);
        return m_data[index];
    }

    constexpr T& Front() const { NGE_ASSERT(m_size > 0); return m_data[0]; }
    constexpr T& Back() const { NGE_ASSERT(m_size > 0); return m_data[m_size - 1]; }

    constexpr Span Subspan(usize offset, usize count) const {
        NGE_ASSERT(offset + count <= m_size);
        return Span(m_data + offset, count);
    }

    constexpr T* begin() const { return m_data; }
    constexpr T* end() const { return m_data + m_size; }

private:
    T*    m_data;
    usize m_size;
};

} // namespace nge
