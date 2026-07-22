enable_testing()

find_package(GTest CONFIG REQUIRED)

# ─── Core Tests ────────────────────────────────────────────────────────────
file(GLOB_RECURSE TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

if(NOT TEST_SOURCES)
    message(STATUS "No test sources found in tests/. Skipping EngineTests target.")
    return()
endif()

# These test rhi/vulkan classes, whose sources are only compiled when
# NGE_ENABLE_VULKAN is ON (see engine/CMakeLists.txt).
if(NOT NGE_ENABLE_VULKAN)
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_desclayout_pipestats|test_swapfmt_skinshader|test_sparse_residency_barrier|test_timeline_sem_shader_obj")
endif()

# The editor lifecycle smoke test exercises the windowing backend, which is
# currently implemented only for Windows (window_win32.cpp, gated by WIN32 in
# engine/CMakeLists.txt). Without a window backend, Window::Create is an
# undefined reference, so exclude the test elsewhere to keep the suite linkable.
if(NOT WIN32)
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_editor_lifecycle")
endif()

add_executable(EngineTests ${TEST_SOURCES})

target_link_libraries(EngineTests
    PRIVATE
        NextGenEngine
        GTest::gtest
)

include(GoogleTest)
gtest_discover_tests(EngineTests)
