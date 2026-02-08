# ─── Sample Applications ───────────────────────────────────────────────────

if(WIN32)
    add_subdirectory(triangle)
else()
    message(STATUS "Skipping sample builds on non-Windows until native window backend is implemented.")
endif()
# add_subdirectory(sponza)
# add_subdirectory(pathtracing_demo)
