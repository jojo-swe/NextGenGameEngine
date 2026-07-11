#pragma once

#include "engine/core/types.h"
#include <functional>
#include <vector>
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <mutex>
#include <algorithm>

namespace nge::events {

// ─── Event System ────────────────────────────────────────────────────────
// Type-safe publish/subscribe for engine-wide messaging.
// Events are dispatched synchronously (immediate) or queued for deferred processing.
//
// Usage:
//   EventBus bus;
//   auto id = bus.Subscribe<WindowResizeEvent>([](const WindowResizeEvent& e) { ... });
//   bus.Publish(WindowResizeEvent{1920, 1080});
//   bus.Unsubscribe<WindowResizeEvent>(id);

using SubscriptionId = u64;
inline constexpr SubscriptionId INVALID_SUBSCRIPTION = 0;

// ─── Base Event ──────────────────────────────────────────────────────────

struct Event {
    virtual ~Event() = default;
    bool handled = false;
};

// ─── Handler Storage ─────────────────────────────────────────────────────

class IHandlerList {
public:
    virtual ~IHandlerList() = default;
    virtual void Remove(SubscriptionId id) = 0;
};

template<typename T>
class HandlerList : public IHandlerList {
public:
    struct Entry {
        SubscriptionId id;
        std::function<void(const T&)> callback;
    };

    SubscriptionId Add(std::function<void(const T&)> cb) {
        SubscriptionId id = ++m_nextId;
        m_handlers.push_back({id, std::move(cb)});
        return id;
    }

    void Remove(SubscriptionId id) override {
        m_handlers.erase(
            std::remove_if(m_handlers.begin(), m_handlers.end(),
                [id](const Entry& e) { return e.id == id; }),
            m_handlers.end());
    }

    void Dispatch(const T& event) {
        for (auto& entry : m_handlers) {
            entry.callback(event);
        }
    }

    bool Empty() const { return m_handlers.empty(); }

private:
    std::vector<Entry> m_handlers;
    SubscriptionId m_nextId = 0;
};

// ─── Event Bus ───────────────────────────────────────────────────────────

class EventBus {
public:
    template<typename T>
    SubscriptionId Subscribe(std::function<void(const T&)> callback) {
        std::lock_guard lock(m_mutex);
        auto& list = GetOrCreate<T>();
        return list.Add(std::move(callback));
    }

    template<typename T>
    void Unsubscribe(SubscriptionId id) {
        std::lock_guard lock(m_mutex);
        auto key = std::type_index(typeid(T));
        auto it = m_handlers.find(key);
        if (it != m_handlers.end()) {
            it->second->Remove(id);
        }
    }

    template<typename T>
    void Publish(const T& event) {
        std::lock_guard lock(m_mutex);
        auto key = std::type_index(typeid(T));
        auto it = m_handlers.find(key);
        if (it != m_handlers.end()) {
            static_cast<HandlerList<T>*>(it->second.get())->Dispatch(event);
        }
    }

    template<typename T, typename... Args>
    void Emit(Args&&... args) {
        T event{std::forward<Args>(args)...};
        Publish(event);
    }

    void Clear() {
        std::lock_guard lock(m_mutex);
        m_handlers.clear();
    }

private:
    template<typename T>
    HandlerList<T>& GetOrCreate() {
        auto key = std::type_index(typeid(T));
        auto it = m_handlers.find(key);
        if (it == m_handlers.end()) {
            auto list = std::make_unique<HandlerList<T>>();
            auto* ptr = list.get();
            m_handlers[key] = std::move(list);
            return *ptr;
        }
        return *static_cast<HandlerList<T>*>(it->second.get());
    }

    std::mutex m_mutex;
    std::unordered_map<std::type_index, std::unique_ptr<IHandlerList>> m_handlers;
};

// ─── Common Engine Events ────────────────────────────────────────────────

struct WindowResizeEvent : Event {
    u32 width, height;
};

struct WindowCloseEvent : Event {};

struct KeyEvent : Event {
    u32  keyCode;
    bool pressed;
    bool repeat;
};

struct MouseButtonEvent : Event {
    u32  button;
    bool pressed;
    f32  x, y;
};

struct MouseMoveEvent : Event {
    f32 x, y;
    f32 deltaX, deltaY;
};

struct MouseScrollEvent : Event {
    f32 offsetX, offsetY;
};

struct EntityCreatedEvent : Event {
    u64 entityId;
};

struct EntityDestroyedEvent : Event {
    u64 entityId;
};

struct SceneLoadedEvent : Event {
    std::string scenePath;
};

struct SceneSavedEvent : Event {
    std::string scenePath;
};

struct FrameBeginEvent : Event {
    u64 frameIndex;
    f32 deltaTime;
};

struct FrameEndEvent : Event {
    u64 frameIndex;
    f32 frameTimeMs;
};

// ─── Global Event Bus ────────────────────────────────────────────────────

EventBus& GetGlobalEventBus();

} // namespace nge::events
