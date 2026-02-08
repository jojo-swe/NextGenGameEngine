#pragma once

#include "engine/core/types.h"
#include "engine/core/assert.h"
#include "engine/core/hash.h"
#include <cstring>
#include <new>
#include <utility>
#include <functional>

namespace nge {

// ─── Robin Hood Open-Addressing Hash Map ─────────────────────────────────
// Low variance probe lengths, cache-friendly, fast for both lookup and insert.
template <typename K, typename V, typename Hash = std::hash<K>, typename KeyEq = std::equal_to<K>>
class HashMap {
    static constexpr f32  MAX_LOAD_FACTOR = 0.85f;
    static constexpr u8   EMPTY_DIST      = 0;
    static constexpr usize MIN_CAPACITY   = 16;

    struct Slot {
        u8 dist = EMPTY_DIST; // 0 = empty, 1+ = distance from ideal + 1
        alignas(K) byte keyStorage[sizeof(K)];
        alignas(V) byte valStorage[sizeof(V)];

        bool IsOccupied() const { return dist != EMPTY_DIST; }
        K& Key() { return *reinterpret_cast<K*>(keyStorage); }
        V& Value() { return *reinterpret_cast<V*>(valStorage); }
        const K& Key() const { return *reinterpret_cast<const K*>(keyStorage); }
        const V& Value() const { return *reinterpret_cast<const V*>(valStorage); }
    };

public:
    HashMap() = default;
    ~HashMap() { Clear(); FreeBuckets(); }

    HashMap(const HashMap& other) {
        if (other.m_size > 0) {
            AllocBuckets(other.m_capacity);
            for (usize i = 0; i < other.m_capacity; ++i) {
                if (other.m_slots[i].IsOccupied()) {
                    m_slots[i].dist = other.m_slots[i].dist;
                    new (m_slots[i].keyStorage) K(other.m_slots[i].Key());
                    new (m_slots[i].valStorage) V(other.m_slots[i].Value());
                }
            }
            m_size = other.m_size;
        }
    }

    HashMap& operator=(const HashMap& other) {
        if (this != &other) {
            Clear(); FreeBuckets();
            if (other.m_size > 0) {
                AllocBuckets(other.m_capacity);
                for (usize i = 0; i < other.m_capacity; ++i) {
                    if (other.m_slots[i].IsOccupied()) {
                        m_slots[i].dist = other.m_slots[i].dist;
                        new (m_slots[i].keyStorage) K(other.m_slots[i].Key());
                        new (m_slots[i].valStorage) V(other.m_slots[i].Value());
                    }
                }
                m_size = other.m_size;
            }
        }
        return *this;
    }

    HashMap(HashMap&& other) noexcept
        : m_slots(other.m_slots), m_capacity(other.m_capacity), m_size(other.m_size)
    {
        other.m_slots = nullptr;
        other.m_capacity = 0;
        other.m_size = 0;
    }

    HashMap& operator=(HashMap&& other) noexcept {
        if (this != &other) {
            Clear(); FreeBuckets();
            m_slots = other.m_slots;
            m_capacity = other.m_capacity;
            m_size = other.m_size;
            other.m_slots = nullptr;
            other.m_capacity = 0;
            other.m_size = 0;
        }
        return *this;
    }

    // ─── Lookup ───────────────────────────────────────────────────────
    V* Find(const K& key) {
        if (m_size == 0) return nullptr;
        usize idx = DesiredIndex(key);
        u8 dist = 1;
        while (true) {
            Slot& s = m_slots[idx];
            if (s.dist == EMPTY_DIST || dist > s.dist) return nullptr;
            if (s.dist == dist && KeyEq{}(s.Key(), key)) return &s.Value();
            idx = (idx + 1) & (m_capacity - 1);
            ++dist;
        }
    }

    const V* Find(const K& key) const {
        return const_cast<HashMap*>(this)->Find(key);
    }

    bool Contains(const K& key) const { return Find(key) != nullptr; }

    V& operator[](const K& key) {
        V* found = Find(key);
        if (found) return *found;
        return Insert(key, V{});
    }

    // ─── Insert ───────────────────────────────────────────────────────
    V& Insert(const K& key, const V& value) {
        if (ShouldGrow()) Rehash(m_capacity == 0 ? MIN_CAPACITY : m_capacity * 2);

        usize idx = DesiredIndex(key);
        u8 dist = 1;
        K insertKey = key;
        V insertVal = value;

        while (true) {
            Slot& s = m_slots[idx];
            if (s.dist == EMPTY_DIST) {
                s.dist = dist;
                new (s.keyStorage) K(static_cast<K&&>(insertKey));
                new (s.valStorage) V(static_cast<V&&>(insertVal));
                ++m_size;
                return s.Value();
            }
            if (s.dist == dist && KeyEq{}(s.Key(), insertKey)) {
                s.Value() = insertVal;
                return s.Value();
            }
            // Robin Hood: steal from rich
            if (dist > s.dist) {
                std::swap(dist, s.dist);
                K tmpK = static_cast<K&&>(s.Key());
                s.Key().~K();
                new (s.keyStorage) K(static_cast<K&&>(insertKey));
                insertKey = static_cast<K&&>(tmpK);

                V tmpV = static_cast<V&&>(s.Value());
                s.Value().~V();
                new (s.valStorage) V(static_cast<V&&>(insertVal));
                insertVal = static_cast<V&&>(tmpV);
            }
            idx = (idx + 1) & (m_capacity - 1);
            ++dist;
        }
    }

    V& Insert(const K& key, V&& value) {
        if (ShouldGrow()) Rehash(m_capacity == 0 ? MIN_CAPACITY : m_capacity * 2);

        usize idx = DesiredIndex(key);
        u8 dist = 1;
        K insertKey = key;
        V insertVal = static_cast<V&&>(value);

        while (true) {
            Slot& s = m_slots[idx];
            if (s.dist == EMPTY_DIST) {
                s.dist = dist;
                new (s.keyStorage) K(static_cast<K&&>(insertKey));
                new (s.valStorage) V(static_cast<V&&>(insertVal));
                ++m_size;
                return s.Value();
            }
            if (s.dist == dist && KeyEq{}(s.Key(), insertKey)) {
                s.Value() = static_cast<V&&>(insertVal);
                return s.Value();
            }
            if (dist > s.dist) {
                std::swap(dist, s.dist);
                K tmpK = static_cast<K&&>(s.Key());
                s.Key().~K();
                new (s.keyStorage) K(static_cast<K&&>(insertKey));
                insertKey = static_cast<K&&>(tmpK);

                V tmpV = static_cast<V&&>(s.Value());
                s.Value().~V();
                new (s.valStorage) V(static_cast<V&&>(insertVal));
                insertVal = static_cast<V&&>(tmpV);
            }
            idx = (idx + 1) & (m_capacity - 1);
            ++dist;
        }
    }

    // ─── Erase ────────────────────────────────────────────────────────
    bool Erase(const K& key) {
        if (m_size == 0) return false;
        usize idx = DesiredIndex(key);
        u8 dist = 1;
        while (true) {
            Slot& s = m_slots[idx];
            if (s.dist == EMPTY_DIST || dist > s.dist) return false;
            if (s.dist == dist && KeyEq{}(s.Key(), key)) {
                s.Key().~K();
                s.Value().~V();
                // Backward shift deletion
                usize next = (idx + 1) & (m_capacity - 1);
                while (m_slots[next].dist > 1) {
                    m_slots[idx] = m_slots[next];
                    m_slots[idx].dist--;
                    idx = next;
                    next = (next + 1) & (m_capacity - 1);
                }
                m_slots[idx].dist = EMPTY_DIST;
                --m_size;
                return true;
            }
            idx = (idx + 1) & (m_capacity - 1);
            ++dist;
        }
    }

    void Clear() {
        if (m_slots) {
            for (usize i = 0; i < m_capacity; ++i) {
                if (m_slots[i].IsOccupied()) {
                    m_slots[i].Key().~K();
                    m_slots[i].Value().~V();
                    m_slots[i].dist = EMPTY_DIST;
                }
            }
        }
        m_size = 0;
    }

    usize Size() const { return m_size; }
    bool IsEmpty() const { return m_size == 0; }
    usize Capacity() const { return m_capacity; }

    // ─── Iteration ────────────────────────────────────────────────────
    template <typename Fn>
    void ForEach(Fn&& fn) {
        for (usize i = 0; i < m_capacity; ++i) {
            if (m_slots[i].IsOccupied()) {
                fn(m_slots[i].Key(), m_slots[i].Value());
            }
        }
    }

    template <typename Fn>
    void ForEach(Fn&& fn) const {
        for (usize i = 0; i < m_capacity; ++i) {
            if (m_slots[i].IsOccupied()) {
                fn(m_slots[i].Key(), m_slots[i].Value());
            }
        }
    }

private:
    usize DesiredIndex(const K& key) const {
        return Hash{}(key) & (m_capacity - 1);
    }

    bool ShouldGrow() const {
        return m_capacity == 0 || static_cast<f32>(m_size + 1) > static_cast<f32>(m_capacity) * MAX_LOAD_FACTOR;
    }

    void Rehash(usize newCapacity) {
        NGE_ASSERT(IsPowerOfTwo(static_cast<u64>(newCapacity)));
        Slot* oldSlots = m_slots;
        usize oldCapacity = m_capacity;

        AllocBuckets(newCapacity);
        m_size = 0;

        if (oldSlots) {
            for (usize i = 0; i < oldCapacity; ++i) {
                if (oldSlots[i].IsOccupied()) {
                    Insert(static_cast<K&&>(oldSlots[i].Key()), static_cast<V&&>(oldSlots[i].Value()));
                    oldSlots[i].Key().~K();
                    oldSlots[i].Value().~V();
                }
            }
            ::operator delete(oldSlots);
        }
    }

    void AllocBuckets(usize cap) {
        m_capacity = cap;
        m_slots = static_cast<Slot*>(::operator new(sizeof(Slot) * cap));
        for (usize i = 0; i < cap; ++i) {
            m_slots[i].dist = EMPTY_DIST;
        }
    }

    void FreeBuckets() {
        if (m_slots) {
            ::operator delete(m_slots);
            m_slots = nullptr;
            m_capacity = 0;
        }
    }

    Slot*  m_slots    = nullptr;
    usize  m_capacity = 0;
    usize  m_size     = 0;
};

} // namespace nge
