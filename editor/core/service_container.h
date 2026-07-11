#pragma once

#include "engine/core/types.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nge::editor {

class EditorService {
public:
    virtual ~EditorService() = default;
    virtual bool Initialize() { return true; }
    virtual void Shutdown() {}
};

class EditorServiceContainer {
public:
    template <typename T, typename... Args>
    T& Emplace(Args&&... args) {
        static_assert(std::is_base_of_v<EditorService, T>);
        const std::type_index key(typeid(T));
        if (m_services.contains(key)) {
            throw std::logic_error("Editor service type is already registered");
        }

        auto service = std::make_unique<T>(std::forward<Args>(args)...);
        T& result = *service;
        m_order.push_back(key);
        m_services.emplace(key, std::move(service));
        return result;
    }

    template <typename T>
    T* Get() {
        const auto it = m_services.find(std::type_index(typeid(T)));
        return it == m_services.end() ? nullptr : static_cast<T*>(it->second.get());
    }

    template <typename T>
    const T* Get() const {
        const auto it = m_services.find(std::type_index(typeid(T)));
        return it == m_services.end() ? nullptr : static_cast<const T*>(it->second.get());
    }

    bool InitializeAll() {
        if (m_initialized) return true;

        usize initializedCount = 0;
        for (const auto& key : m_order) {
            if (!m_services.at(key)->Initialize()) {
                while (initializedCount > 0) {
                    m_services.at(m_order[--initializedCount])->Shutdown();
                }
                return false;
            }
            ++initializedCount;
        }

        m_initialized = true;
        return true;
    }

    void ShutdownAll() {
        if (!m_initialized) return;
        for (auto it = m_order.rbegin(); it != m_order.rend(); ++it) {
            m_services.at(*it)->Shutdown();
        }
        m_initialized = false;
    }

    usize Size() const { return m_services.size(); }
    bool IsInitialized() const { return m_initialized; }

private:
    std::unordered_map<std::type_index, std::unique_ptr<EditorService>> m_services;
    std::vector<std::type_index> m_order;
    bool m_initialized = false;
};

} // namespace nge::editor
