#pragma once

#include "engine/core/types.h"
#include <chrono>

namespace nge {

// ─── CPU Scoped Timer ────────────────────────────────────────────────────
// Lightweight profiling. Will integrate Tracy for visualization later.
class ScopedTimer {
public:
    explicit ScopedTimer(const char* name)
        : m_name(name)
        , m_start(std::chrono::high_resolution_clock::now())
    {}

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - m_start);
        // Store/report timing — will integrate with profiler backend
        (void)duration;
        (void)m_name;
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    const char* m_name;
    std::chrono::high_resolution_clock::time_point m_start;
};

} // namespace nge

// ─── Profiling Macros ────────────────────────────────────────────────────
#if defined(NGE_PROFILE) || defined(NGE_DEBUG)
    #define NGE_PROFILE_SCOPE(name) ::nge::ScopedTimer _timer_##__LINE__(name)
    #define NGE_PROFILE_FUNCTION()  NGE_PROFILE_SCOPE(__FUNCTION__)
#else
    #define NGE_PROFILE_SCOPE(name)
    #define NGE_PROFILE_FUNCTION()
#endif
