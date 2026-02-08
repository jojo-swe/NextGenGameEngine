# NextGenGameEngine

[![CI](https://github.com/jojo-swe/NextGenGameEngine/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/jojo-swe/NextGenGameEngine/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/)
[![CMake](https://img.shields.io/badge/CMake-3.28%2B-blue)](https://cmake.org/)

A next-generation 3D game engine built from scratch in C++20, targeting Vulkan 1.3 with a GPU-driven rendering pipeline, real-time path tracing, and novel mathematical foundations using Projective Geometric Algebra (PGA).

## Key Features

- **GPU-Driven Rendering** — Visibility buffer, mesh shaders, meshlet LOD selection, two-pass HZB occlusion culling, indirect draw argument builder, clustered light culling, GPU frustum culling, instance manager, mesh registry
- **Real-Time Path Tracing** — ReSTIR direct illumination + hybrid GI with SH probes + SVGF denoiser
- **Projective Geometric Algebra** — PGA motors for all transforms (rotation + translation in a single algebraic element)
- **Render Graph** — Automatic pass scheduling with async compute, barrier insertion, transient resource aliasing, dead-code elimination, cross-queue timeline semaphore sync
- **Scene Renderer** — Top-level render loop tying ECS → mesh extraction → GPU upload → culling → render graph → present, TAA jitter via Halton sequence
- **Render Compositor** — Orchestrates rasterization/path-tracing paths, full post-process chain, async compute passes
- **Material System** — PBR material instances with 8 bindless texture slots, dirty tracking, GPU structured buffer upload
- **Virtual Texturing** — Page pool with LRU eviction, GPU feedback buffer, mip-level streaming, compute mip generator (Box/Kaiser/Lanczos + SPD)
- **Virtual Shadow Maps** — Clipmap-based page pool with LRU eviction
- **CDLOD Terrain** — Clipmap rendering with procedural generation and 16-layer material splatting
- **GPU Particles** — Compute-driven emit/simulate/sort with curl noise turbulence
- **Full Post-Processing Stack** — TAA, FXAA, bloom, TSR, tone mapping, DOF bokeh, motion blur, chromatic aberration, film grain, vignette, SVGF denoise, auto-exposure, CAS sharpen, GTAO, SSR, VRS, volumetric clouds, screen-space contact shadows
- **Asset Pipeline** — glTF 2.0 importer, shader permutation system, DXC→SPIR-V compilation, hot-reload, dependency-aware include resolver
- **GPU Memory Management** — Buffer pool, frame allocator, staging manager, transient resource pool (aliased), memory defragmenter, deletion queue
- **Debug Systems** — GPU profiler overlay, query heap (timestamp/occlusion/pipeline stats), debug line/text renderer with built-in bitmap font

## Architecture

```text
┌─────────────────────────────────────────────────────────┐
│                      Editor (ImGui)                     │
├─────────────┬──────────┬──────────┬─────────┬───────────┤
│  Scripting  │ Physics  │  Audio   │Animation│    AI     │
│  (Lua/Sol2) │  (Jolt)  │(miniaudio│ (PGA    │(Behavior  │
│             │          │  stub)   │  Motor) │  Tree)    │
├─────────────┴──────────┴──────────┴─────────┴───────────┤
│              Scene Graph + ECS + Events                  │
├──────────────────────────────────────────────────────────┤
│   Scene Renderer → Render Compositor → Render Graph     │
│  ┌──────────────────────────────────────────────────┐   │
│  │ Cull → LOD → VisBuffer → HZB → Material → Light │   │
│  │ OR PathTrace → SVGF Denoise → Post → Composite  │   │
│  │ Post: TAA/FXAA → Bloom → Exposure → Clouds → CA │   │
│  └──────────────────────────────────────────────────┘   │
├──────────────────────────────────────────────────────────┤
│  RHI (Vulkan 1.3 + caches + barrier tracker + indirect)  │
├──────────────────────────────────────────────────────────┤
│   Core (Types, Memory, Containers, Jobs, PGA Math, I/O)  │
└──────────────────────────────────────────────────────────┘
```

## Repository Layout

```text
.
├── engine/
│   ├── core/           # Types, memory, containers, ECS, jobs, events, math, platform
│   ├── rhi/
│   │   ├── common/     # RHI abstraction, buffer pool, staging, timeline fence, barrier tracker, query heap, format utils, indirect buffers, transient pool, memory defrag, render state, attachment builder, command pool
│   │   └── vulkan/     # Vulkan 1.3 backend, sampler/pipeline/descriptor caches, swapchain presenter
│   ├── renderer/
│   │   ├── pipeline/   # Render compositor, GPU culling, instance manager, mesh registry, mip generator
│   │   ├── graph/      # Render graph (async compute, cross-queue sync)
│   │   ├── materials/  # PBR material system (bindless textures)
│   │   ├── lighting/   # Clustered light culling (5 light types, 3D grid)
│   │   ├── streaming/  # Virtual texture streaming
│   │   └── debug/      # Debug renderer, text, profiler overlay
│   ├── app/            # Application bootstrap (init/shutdown/main loop)
│   ├── assets/         # Mesh/texture/shader loaders, resource manager, glTF importer, shader permutations, include resolver
│   ├── scene/          # Camera, transforms, serialization, prefab system
│   ├── network/        # UDP socket, server, client, reliable delivery
│   ├── physics/        # Jolt wrapper + stub Euler simulation
│   ├── audio/          # Miniaudio wrapper + stub
│   ├── animation/      # Skeleton, clips, blend tree, PGA interpolation
│   ├── scripting/      # Lua/Sol2 wrapper, hot-reload
│   └── ai/             # Behavior tree, nav mesh (A* pathfinding)
├── editor/             # ImGui docking editor (viewport, hierarchy, inspector, console, assets, profiler)
├── shaders/            # 49 HLSL compute/vertex/fragment shaders
│   ├── common/         # Shared math, BRDF
│   ├── compute/        # HZB, VRS, GPU skinning, particles, occlusion cull, meshlet LOD, indirect draw, VT feedback, cluster lights, frustum cull
│   ├── visibility/     # Material resolve
│   ├── lighting/       # GI probes, SSR, SSAO, decals, ReSTIR, contact shadows
│   ├── postprocess/    # Bloom, tonemap, TSR, DOF, motion blur, TAA, FXAA, SVGF denoise, auto-exposure, CAS, chromatic aberration, ocean
│   ├── atmosphere/     # Sky, volumetric fog, volumetric clouds
│   ├── shadows/        # Shadow rasterization
│   ├── terrain/        # Terrain CDLOD rendering
│   └── debug/          # Debug lines + text rendering
├── samples/triangle/   # Minimal sample app
├── tests/              # 23 test files (unit + integration)
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
└── frolicking-tumbling-pond.md  # Master implementation plan
```

## Requirements

- CMake `>= 3.28`
- C++20 compiler
  - Windows: MSVC
  - Linux: Clang
- Ninja
- Vulkan SDK (headers, loader, validation layers)
- vcpkg with `VCPKG_ROOT` set

## Getting Started

### 1. Configure (Windows)

```powershell
cmake --preset windows-debug
```

### 2. Build (Windows)

```powershell
cmake --build --preset debug
```

### 3. Run Sample (Windows)

```powershell
.\build\bin\Debug\SampleTriangle.exe
```

### Linux configure/build

```bash
cmake --preset linux-debug
cmake --build build --config Debug
./build/bin/Debug/SampleTriangle
```

## Tests

Run tests with:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Note: test infrastructure is configured, but test coverage is still being built out.

## Dependencies

Managed by `vcpkg.json`.

Core (default) dependencies:
- fmt + spdlog
- GTest

Optional manifest features:
- `rendering`: volk/VMA/meshoptimizer/imgui/stb/cgltf/tracy
- `gameplay`: joltphysics/sol2/lua/entt

Example (enable optional sets during configure):

```powershell
cmake --preset windows-debug -DVCPKG_MANIFEST_FEATURES="rendering;gameplay"
```

## Roadmap

Execution follows phased delivery:
- Foundation and build stability
- RHI (Vulkan and DX12 parity)
- GPU-driven rendering pipeline
- Lighting, GI, and path tracing
- Tooling, scripting, and networking

Detailed milestones, gates, and scene profile IDs live in:
- `frolicking-tumbling-pond.md`

Direct links to key plan sections:
- [Phase 0: Delivery and Risk](frolicking-tumbling-pond.md#phase-0-delivery-risk)
- [Phase 2.4: DirectX 12 Backend](frolicking-tumbling-pond.md#phase-2-4-directx12-backend)
- [Phase 6.4: Serialization and Versioning](frolicking-tumbling-pond.md#phase-6-4-serialization-versioning)
- [Phase 10.2: Protocol, Rollback, Determinism](frolicking-tumbling-pond.md#phase-10-2-network-protocol-rollback)
- [CI/CD Quality Gates](frolicking-tumbling-pond.md#ci-cd-quality-gates)

## Contributing

Pull requests are welcome. For larger changes, open an issue first with:
- Problem statement
- Proposed approach
- Validation plan (tests, perf, compatibility)

CI runs repository sanity checks plus:
- Cross-platform configure/build/test jobs on Windows and Linux
- A fuller Windows lane with `rendering;gameplay` manifest features enabled

## License

Licensed under the MIT License.
See `LICENSE`.
