# ─── Editor Application ────────────────────────────────────────────────────
# ImGui-based editor built on top of the engine library.

file(GLOB_RECURSE EDITOR_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE EDITOR_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_executable(NextGenEditor ${EDITOR_SOURCES} ${EDITOR_HEADERS})

target_include_directories(NextGenEditor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(NextGenEditor PRIVATE NextGenEngine)

find_package(imgui CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
target_link_libraries(NextGenEditor PRIVATE imgui::imgui Vulkan::Vulkan)
target_compile_definitions(NextGenEditor PRIVATE NGE_HAS_IMGUI=1)
