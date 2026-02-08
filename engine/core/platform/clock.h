#pragma once

#include "engine/core/types.h"

#if defined(NGE_PLATFORM_WINDOWS)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#else
    #include <time.h>
#endif

namespace nge::platform {

// ─── High-Resolution Clock ──────────────────────────────────────────────
class Clock {
public:
    static void Init() {
#if defined(NGE_PLATFORM_WINDOWS)
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        s_frequency = static_cast<f64>(freq.QuadPart);
        s_startTicks = GetTicks();
#else
        s_startTicks = GetTicks();
#endif
    }

    // Ticks since process start
    static u64 GetTicks() {
#if defined(NGE_PLATFORM_WINDOWS)
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        return static_cast<u64>(counter.QuadPart);
#else
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<u64>(ts.tv_sec) * 1000000000ULL + static_cast<u64>(ts.tv_nsec);
#endif
    }

    // Seconds since Clock::Init()
    static f64 GetTimeSeconds() {
        u64 now = GetTicks();
#if defined(NGE_PLATFORM_WINDOWS)
        return static_cast<f64>(now - s_startTicks) / s_frequency;
#else
        return static_cast<f64>(now - s_startTicks) / 1e9;
#endif
    }

    // Milliseconds since Clock::Init()
    static f64 GetTimeMs() { return GetTimeSeconds() * 1000.0; }

    // Delta time helper
    static f32 DeltaSeconds(u64 startTicks, u64 endTicks) {
#if defined(NGE_PLATFORM_WINDOWS)
        return static_cast<f32>(static_cast<f64>(endTicks - startTicks) / s_frequency);
#else
        return static_cast<f32>(static_cast<f64>(endTicks - startTicks) / 1e9);
#endif
    }

private:
    inline static u64 s_startTicks = 0;
#if defined(NGE_PLATFORM_WINDOWS)
    inline static f64 s_frequency  = 1.0;
#endif
};

} // namespace nge::platform
