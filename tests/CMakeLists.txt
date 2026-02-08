enable_testing()

find_package(GTest CONFIG REQUIRED)

# ─── Core Tests ────────────────────────────────────────────────────────────
file(GLOB_RECURSE TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(EngineTests ${TEST_SOURCES})

target_link_libraries(EngineTests
    PRIVATE
        NextGenEngine
        GTest::gtest
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(EngineTests)
