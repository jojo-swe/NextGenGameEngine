# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## Project Overview

NextGenGameEngine is a C++20 game engine targeting Vulkan 1.3 with GPU-driven rendering, real-time path tracing, and Projective Geometric Algebra (PGA) for transforms. The engine uses a bindless resource architecture with a feature-tier system (Tier0_Baseline through Tier3_Neural).

## Build Commands

```powershell
# Configure (requires VCPKG_ROOT env var pointing to vcpkg installation)
cmake --preset windows-debug

# Build
cmake --build --preset debug
cmake --build --preset release
cmake --build --preset profile    # RelWithDebInfo

# With optional vcpkg features (rendering + gameplay dependencies)
cmake --preset windows-debug -DVCPKG_MANIFEST_FEATURES="rendering;gameplay"

# Run tests
ctest --test-dir build -C Debug --output-on-failure

# Run sample
.\build\bin\Debug\SampleTriangle.exe
```

**CMake options:** `NGE_ENABLE_VULKAN` (ON), `NGE_BUILD_SAMPLES` (ON), `NGE_BUILD_TESTS` (ON)

**Build presets:** `windows-debug`, `windows-release`, `linux-debug` (Clang)

## Architecture

### Engine Subsystems (`engine/`)

The engine compiles as a single static library `NextGenEngine` from 11 subsystems:

- **core/** — Foundation: type aliases (`nge::u32`, `nge::f32`, etc.), ECS (archetype-based), job system, event system, memory allocators, PGA math, input, profiling, logging (spdlog)
- **rhi/** — Rendering Hardware Interface: `IDevice` and `ICommandList` abstract interfaces (`rhi/common/rhi_device.h`) with Vulkan 1.3 backend (`rhi/vulkan/`). The common/ layer has ~170 files covering buffer management, descriptor heaps, pipeline state caching, barrier tracking, sync primitives, and extensive debug validators
- **renderer/** — High-level rendering: render graph (`renderer/graph/`), GPU-driven pipeline with compute culling (`renderer/pipeline/`), PBR materials with bindless textures, clustered light culling, shadow maps, terrain (CDLOD), GPU particles, virtual texturing, post-processing
- **assets/** — Asset pipeline: glTF importer, shader compiler with permutation system, hot-reload via file watcher, async loader, SPIR-V reflection, shader variant cache (.svc binary format)
- **scene/** — Scene graph, camera system, transform hierarchy, serialization, prefab system
- **physics/** — Jolt Physics wrapper
- **audio/** — miniaudio wrapper
- **animation/** — Skeleton animation with blend trees, PGA interpolation
- **scripting/** — Lua/Sol2 integration with hot-reload
- **ai/** — Behavior trees, nav mesh, A* pathfinding
- **network/** — UDP with reliable delivery layer

### Key Interfaces

- `IDevice` (`engine/rhi/common/rhi_device.h`) — Core GPU device: resource creation, swapchain, bindless descriptors, command lists, capability queries
- `ICommandList` (`engine/rhi/common/rhi_device.h`) — Command recording: barriers, dynamic rendering (no render pass objects), draw/dispatch/trace, mesh shaders
- `rhi_types.h` (`engine/rhi/common/rhi_types.h`) — Typed handles (Buffer, Texture, Pipeline, etc.), format enums, pipeline descriptors, feature tiers

### Other Targets

- **editor/** — ImGui-based editor application
- **samples/** — Sample apps (e.g., SampleTriangle)
- **tests/** — GTest-based test suite (single `EngineTests` executable with `gtest_discover_tests`)

### Shaders (`shaders/`)

117 HLSL shaders organized by category: `common/`, `compute/`, `visibility/`, `lighting/`, `postprocess/`, `atmosphere/`, `shadows/`, `terrain/`, `mesh/`, `pathtracing/`, `debug/`. Common includes: `bindless.hlsl`, `brdf.hlsl`, `math.hlsl`.

## Conventions

- **Namespace:** `nge`
- **Type aliases:** Use `nge::u8`–`u64`, `i8`–`i64`, `f32`, `f64`, `usize`, `isize` from `engine/core/types.h`
- **Naming:** PascalCase for classes/types, camelCase for methods/variables, SCREAMING_SNAKE_CASE for constants/macros, member variables prefixed with `_` or `m_`
- **Headers:** `.h` extension (not `.hpp`)
- **C++ standard:** C++20, no extensions
- **Error handling:** Exceptions + RAII; logging via spdlog
- **Compiler flags (MSVC):** `/W4 /permissive- /MP /fp:fast /arch:AVX2` (warnings-as-errors disabled)
- **Compiler flags (Clang/GCC):** `-Wall -Wextra -Werror -mavx2 -mfma -ffast-math`
- **Debug defines:** `NGE_DEBUG`, `NGE_ENABLE_ASSERTS`, `NGE_ENABLE_MEMORY_TRACKING` (Debug only)

## Dependencies (vcpkg)

**Core:** spdlog, fmt, gtest

**Optional "rendering" feature:** volk (Vulkan loader), vulkan-memory-allocator, glm, meshoptimizer, imgui (docking + Vulkan/Win32 bindings), stb, cgltf, tracy

**Optional "gameplay" feature:** joltphysics, sol2, lua, miniaudio

## Testing

Tests use Google Test. All test sources under `tests/` compile into a single `EngineTests` executable.

```powershell
# Run all tests
ctest --test-dir build -C Debug --output-on-failure

# Run specific test (by filter)
.\build\bin\Debug\EngineTests.exe --gtest_filter="TestSuiteName.TestName"
```

Unit tests are in `tests/unit/`, integration tests in `tests/integration/`.

## CI

GitHub Actions (`.github/workflows/ci.yml`) runs three jobs:
1. **repo-sanity** — Validates required files and JSON configs
2. **build-and-test** — Cross-platform (Windows + Linux) with Vulkan disabled
3. **build-and-test-full-windows** — Full build with all vcpkg features enabled

## Repository Rules

These rules apply to all implementation work in this repository.

### Plan and scope

- Read `MASTERPLAN.md` before starting nontrivial work. Prioritize the current
  program gate and do not add later-phase features while the current gate is
  failing.
- Keep each change centered on one observable outcome. Avoid combining feature
  work, broad refactors, formatting, and cleanup in the same change.
- Preserve user changes and unrelated work in a dirty worktree. Never discard,
  overwrite, or reformat files outside the requested scope.
- Do not add a new subsystem, third-party dependency, or public abstraction
  without an explicit request and a demonstrated integration need.

### Truthful feature status

- Use the maturity terms from `MASTERPLAN.md`: Sketch, Prototype, Integrated,
  Verified, and Shippable.
- A header, class, shader, mock, or stub does not make a feature Integrated.
  Integration requires a real runtime call path in a sample or the editor.
- Do not describe a stubbed, mocked, commented-out, or unwired path as an
  implemented engine feature. Update nearby documentation when feature status
  changes.
- Never add silent stub or placeholder fallbacks to production paths. Return a
  clear error with enough context to diagnose the missing capability.
- Experimental shaders are promoted only together with shader compilation, a
  live C++ call site, runtime coverage, and relevant validation evidence.

### Implementation quality

- Prefer finishing an end-to-end vertical path over adding breadth to an
  isolated subsystem.
- Search for existing APIs and patterns before creating a parallel manager,
  cache, allocator, descriptor abstraction, or utility.
- Define ownership and lifetime for every GPU resource, asynchronous task, and
  callback. Destruction must be safe with frames and jobs in flight.
- Keep CPU structures and shader layouts explicitly synchronized with static
  assertions or reflection-based validation where practical.
- Treat Vulkan validation errors, resource leaks, data races, nondeterminism,
  and scene corruption as blockers for the affected path.
- Do not mix drive-by cleanup with a functional fix. Record adjacent problems
  as follow-up work packages in `MASTERPLAN.md`.

### Tests and verification

- Reproduce a defect before fixing it, preferably with a failing automated
  test. New behavior requires tests at the lowest useful layer.
- A test that exercises only a mock must not be used as evidence that the real
  Vulkan, audio, physics, scripting, filesystem, or network integration works.
- Run the narrowest relevant tests during iteration, then the applicable build
  and CTest suite before reporting completion.
- If CTest reports `No tests were found`, do not report success. Verify that the
  build was configured with `NGE_BUILD_TESTS=ON` and that `EngineTests` exists.
- For GPU changes, run a debug build with Vulkan validation enabled and report
  validation output. If no suitable GPU is available, explicitly mark runtime
  verification as not performed.
- Never claim a command passed unless it was run in the current worktree.
  Report unavailable environmental checks separately from verified results.
- Bug fixes should add regression coverage. Numerical tests must use deliberate
  tolerances and include boundary or long-composition cases where relevant.

### Generated files and diagnostics

- Do not write logs, traces, caches, editor layouts, screenshots, or generated
  assets to the repository root. Put build artifacts under `build/` and runtime
  diagnostics under an ignored, dedicated directory.
- Temporary diagnostic logging must be scoped, actionable, and removed or
  converted to the normal logging system once the issue is resolved.
- Do not commit generated binaries, local machine paths, credentials, SDK
  locations, or user-specific editor state.

### Performance and documentation

- Do not make performance claims without a repeatable workload, named build
  configuration, reference hardware, and before/after measurements.
- Performance work must preserve correctness checks and include the metric it
  intends to improve.
- Update README, samples, and maturity status in the same change when user-
  visible behavior or support changes.
- Add a short architecture decision record under `docs/adr/` for durable,
  cross-subsystem choices that would otherwise be repeatedly reconsidered.
