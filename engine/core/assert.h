#pragma once

#include "engine/core/types.h"
#include <cstdio>
#include <cstdlib>

#if defined(_MSC_VER)
    #define NGE_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
    #define NGE_DEBUG_BREAK() __builtin_trap()
#else
    #define NGE_DEBUG_BREAK() std::abort()
#endif

namespace nge::detail {

[[noreturn]] inline void AssertFail(const char* expr, const char* file, int line, const char* msg = nullptr) {
    if (msg) {
        std::fprintf(stderr, "[NGE ASSERT] %s:%d: '%s' failed — %s\n", file, line, expr, msg);
    } else {
        std::fprintf(stderr, "[NGE ASSERT] %s:%d: '%s' failed\n", file, line, expr);
    }
    NGE_DEBUG_BREAK();
    std::abort();
}

} // namespace nge::detail

// ─── Assertions ──────────────────────────────────────────────────────────
#if defined(NGE_ENABLE_ASSERTS)

#define NGE_ASSERT(expr) \
    do { if (!(expr)) { ::nge::detail::AssertFail(#expr, __FILE__, __LINE__); } } while(0)

#define NGE_ASSERT_MSG(expr, msg) \
    do { if (!(expr)) { ::nge::detail::AssertFail(#expr, __FILE__, __LINE__, msg); } } while(0)

#define NGE_VERIFY(expr) NGE_ASSERT(expr)

#else

#define NGE_ASSERT(expr)          ((void)0)
#define NGE_ASSERT_MSG(expr, msg) ((void)0)
#define NGE_VERIFY(expr)          ((void)(expr))

#endif

// Always-on fatal check (even in release)
#define NGE_FATAL(msg) \
    do { ::nge::detail::AssertFail("FATAL", __FILE__, __LINE__, msg); } while(0)

#define NGE_CHECK(expr, msg) \
    do { if (!(expr)) { ::nge::detail::AssertFail(#expr, __FILE__, __LINE__, msg); } } while(0)
