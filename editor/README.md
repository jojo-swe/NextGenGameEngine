# NextGenEditor

NextGenEditor is the engine's ImGui-based authoring application. It is built as
the `NextGenEditor` executable when both `NGE_BUILD_EDITOR` and
`NGE_ENABLE_VULKAN` are enabled.

## Startup and shutdown

`main.cpp` constructs an `EditorConfig`, creates `EditorApp`, registers the
initial editor panels, and enters `Application::Run()`. The application owns the
platform window, RHI device, and frame loop. `EditorApp` owns editor UI state
and performs editor-specific initialization and shutdown through `OnInit()` and
`OnShutdown()`.

Initialization failures are returned as a non-zero process exit code. Owned
resources are released in reverse order during shutdown.

## Current maturity

| Capability | State | Evidence |
|---|---|---|
| Editor executable and startup sequence | Verified | `NextGenEditor` CMake target and application lifecycle smoke tests |
| ImGui docking workspace | Integrated | Root dock space is created by `EditorApp` |
| Workspace layout persistence | Prototype | ImGui INI persistence is enabled; named presets are not implemented |
| Editor service container | Verified | Typed service ownership, ordered lifecycle, rollback tests |
| Project selection flow | Sketch | The current process opens the working directory |
| Panel manager | Verified | Unique registration, visibility routing, lookup, and unit tests |

This table must be updated when editor capabilities change maturity state.
