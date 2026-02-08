#pragma once

#include "engine/core/types.h"
#include "engine/core/assert.h"
#include <new>
#include <utility>

namespace nge {

struct NullOpt_t { explicit constexpr NullOpt_t() = default; };
inline constexpr NullOpt_t NullOpt{};

// ─── Optional<T> ─────────────────────────────────────────────────────────
template <typename T>
class Optional {
public:
    constexpr Optional() : m_hasValue(false) {}
    constexpr Optional(NullOpt_t) : m_hasValue(false) {}

    Optional(const T& value) : m_hasValue(true) {
        new (&m_storage) T(value);
    }

    Optional(T&& value) : m_hasValue(true) {
        new (&m_storage) T(static_cast<T&&>(value));
    }

    ~Optional() { Reset(); }

    Optional(const Optional& other) : m_hasValue(other.m_hasValue) {
        if (m_hasValue) new (&m_storage) T(other.Value());
    }

    Optional(Optional&& other) noexcept : m_hasValue(other.m_hasValue) {
        if (m_hasValue) {
            new (&m_storage) T(static_cast<T&&>(other.Value()));
            other.Reset();
        }
    }

    Optional& operator=(const Optional& other) {
        if (this != &other) {
            Reset();
            m_hasValue = other.m_hasValue;
            if (m_hasValue) new (&m_storage) T(other.Value());
        }
        return *this;
    }

    Optional& operator=(Optional&& other) noexcept {
        if (this != &other) {
            Reset();
            m_hasValue = other.m_hasValue;
            if (m_hasValue) {
                new (&m_storage) T(static_cast<T&&>(other.Value()));
                other.Reset();
            }
        }
        return *this;
    }

    Optional& operator=(NullOpt_t) { Reset(); return *this; }

    bool HasValue() const { return m_hasValue; }
    explicit operator bool() const { return m_hasValue; }

    T& Value() { NGE_ASSERT(m_hasValue); return *reinterpret_cast<T*>(&m_storage); }
    const T& Value() const { NGE_ASSERT(m_hasValue); return *reinterpret_cast<const T*>(&m_storage); }

    T& operator*() { return Value(); }
    const T& operator*() const { return Value(); }
    T* operator->() { return &Value(); }
    const T* operator->() const { return &Value(); }

    T ValueOr(const T& defaultVal) const { return m_hasValue ? Value() : defaultVal; }

    void Reset() {
        if (m_hasValue) {
            reinterpret_cast<T*>(&m_storage)->~T();
            m_hasValue = false;
        }
    }

    template <typename... Args>
    T& Emplace(Args&&... args) {
        Reset();
        new (&m_storage) T(static_cast<Args&&>(args)...);
        m_hasValue = true;
        return Value();
    }

private:
    alignas(T) byte m_storage[sizeof(T)];
    bool m_hasValue;
};

// ─── Result<T, E> ────────────────────────────────────────────────────────
template <typename T, typename E>
class Result {
public:
    static Result Ok(const T& value) {
        Result r;
        r.m_isOk = true;
        new (&r.m_storage) T(value);
        return r;
    }

    static Result Ok(T&& value) {
        Result r;
        r.m_isOk = true;
        new (&r.m_storage) T(static_cast<T&&>(value));
        return r;
    }

    static Result Err(const E& error) {
        Result r;
        r.m_isOk = false;
        new (&r.m_errStorage) E(error);
        return r;
    }

    static Result Err(E&& error) {
        Result r;
        r.m_isOk = false;
        new (&r.m_errStorage) E(static_cast<E&&>(error));
        return r;
    }

    ~Result() {
        if (m_isOk) reinterpret_cast<T*>(&m_storage)->~T();
        else reinterpret_cast<E*>(&m_errStorage)->~E();
    }

    bool IsOk() const { return m_isOk; }
    bool IsErr() const { return !m_isOk; }
    explicit operator bool() const { return m_isOk; }

    T& Value() { NGE_ASSERT(m_isOk); return *reinterpret_cast<T*>(&m_storage); }
    const T& Value() const { NGE_ASSERT(m_isOk); return *reinterpret_cast<const T*>(&m_storage); }
    E& Error() { NGE_ASSERT(!m_isOk); return *reinterpret_cast<E*>(&m_errStorage); }
    const E& Error() const { NGE_ASSERT(!m_isOk); return *reinterpret_cast<const E*>(&m_errStorage); }

private:
    Result() = default;
    union {
        alignas(T) byte m_storage[sizeof(T)];
        alignas(E) byte m_errStorage[sizeof(E)];
    };
    bool m_isOk;
};

} // namespace nge
