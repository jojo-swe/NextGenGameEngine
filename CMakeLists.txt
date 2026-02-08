cmake_minimum_required(VERSION 3.28)

project(NextGenGameEngine
    VERSION 0.1.0
    LANGUAGES CXX C
    DESCRIPTION "Next-generation 3D game engine with real-time path tracing"
)

# ─── C++ Standard ───────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Output directories ────────────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ─── Global compiler options ───────────────────────────────────────────────
if(MSVC)
    add_compile_options(
        /W4 /WX
        /permissive-
        /Zc:__cplusplus
        /MP                     # Multi-process compilation
        /fp:fast                # Fast floating point
        /arch:AVX2              # Enable AVX2 SIMD
    )
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(
        -Wall -Wextra -Werror
        -mavx2 -mfma
        -ffast-math
    )
endif()

# ─── Build type specific ───────────────────────────────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_CONFIGURATION_TYPES)
    add_compile_definitions($<$<CONFIG:Debug>:NGE_DEBUG=1>)
    add_compile_definitions($<$<CONFIG:Debug>:NGE_ENABLE_ASSERTS=1>)
    add_compile_definitions($<$<CONFIG:Debug>:NGE_ENABLE_MEMORY_TRACKING=1>)
endif()

add_compile_definitions($<$<CONFIG:Release>:NGE_RELEASE=1>)
add_compile_definitions($<$<CONFIG:RelWithDebInfo>:NGE_PROFILE=1>)
add_compile_definitions($<$<CONFIG:RelWithDebInfo>:NGE_ENABLE_ASSERTS=1>)

# ─── Platform detection ────────────────────────────────────────────────────
if(WIN32)
    add_compile_definitions(NGE_PLATFORM_WINDOWS=1)
elseif(UNIX AND NOT APPLE)
    add_compile_definitions(NGE_PLATFORM_LINUX=1)
endif()

# ─── Find packages ─────────────────────────────────────────────────────────
find_package(Vulkan REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

# ─── Subdirectories ────────────────────────────────────────────────────────
add_subdirectory(engine)
add_subdirectory(samples)
add_subdirectory(tests)
# add_subdirectory(tools)
# add_subdirectory(editor)
