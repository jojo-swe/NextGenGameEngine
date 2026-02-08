# ─── Engine Library ─────────────────────────────────────────────────────────
# Collects all engine subsystems into a single static library.

file(GLOB_RECURSE ENGINE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rhi/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/scene/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/physics/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/audio/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/scripting/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/animation/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ai/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/network/*.cpp"
)

file(GLOB_RECURSE ENGINE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/rhi/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/scene/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/physics/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/audio/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/scripting/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/animation/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ai/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/network/*.h"
)

add_library(NextGenEngine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

target_include_directories(NextGenEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# ─── Dependencies ──────────────────────────────────────────────────────────
target_link_libraries(NextGenEngine
    PUBLIC
        Vulkan::Vulkan
        spdlog::spdlog
        fmt::fmt
)

# ─── Platform libraries ───────────────────────────────────────────────────
if(WIN32)
    target_link_libraries(NextGenEngine
        PUBLIC
            dwmapi
            xinput
            winmm
    )
endif()

# ─── Precompiled header (optional, speeds up compilation) ──────────────────
# target_precompile_headers(NextGenEngine PRIVATE core/pch.h)
