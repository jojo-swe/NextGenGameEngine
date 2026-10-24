enable_testing()

find_package(GTest CONFIG REQUIRED)

# ─── Core Tests ────────────────────────────────────────────────────────────
file(GLOB_RECURSE TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

if(NOT TEST_SOURCES)
    message(STATUS "No test sources found in tests/. Skipping EngineTests target.")
    return()
endif()

if(NOT NGE_ENABLE_VULKAN)
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_desclayout_pipestats|test_swapfmt_skinshader|test_sparse_residency_barrier|test_timeline_sem_shader_obj")
endif()

add_executable(EngineTests ${TEST_SOURCES})

target_compile_definitions(EngineTests PRIVATE
    NGE_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

target_link_libraries(EngineTests
    PRIVATE
        NextGenEngine
        GTest::gtest
)

include(GoogleTest)
gtest_discover_tests(EngineTests)
