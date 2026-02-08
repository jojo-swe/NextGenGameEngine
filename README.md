# NextGenGameEngine

[![CI](https://github.com/jojo-swe/NextGenGameEngine/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/jojo-swe/NextGenGameEngine/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/)
[![CMake](https://img.shields.io/badge/CMake-3.28%2B-blue)](https://cmake.org/)

A next-generation 3D game engine built from scratch in C++20, targeting Vulkan 1.3 with a GPU-driven rendering pipeline, real-time path tracing, and novel mathematical foundations using Projective Geometric Algebra (PGA).

## Key Features

- **GPU-Driven Rendering** — Visibility buffer, mesh shaders, meshlet LOD selection, two-pass HZB occlusion culling, indirect draw argument builder, clustered light culling, GPU frustum culling, instance manager, mesh registry, GPU scene buffer, draw call merger, indirect cull pipeline, occlusion feedback, texture atlas
- **Real-Time Path Tracing** — ReSTIR direct illumination + hybrid GI with SH probes + SVGF denoiser
- **Projective Geometric Algebra** — PGA motors for all transforms (rotation + translation in a single algebraic element)
- **Render Graph** — Automatic pass scheduling with async compute, barrier insertion, transient resource aliasing, dead-code elimination, cross-queue timeline semaphore sync, resource versioning (RAW/WAR/WAW hazards), work graph scheduler (multi-queue dependency resolution), frame graph compiler (pass ordering + aliasing optimization), pass profiler (auto-injected GPU timers), transient resource pool (typed allocation + eviction)
- **Scene Renderer** — Top-level render loop tying ECS → mesh extraction → GPU upload → culling → render graph → present, TAA jitter via Halton sequence
- **Render Compositor** — Orchestrates rasterization/path-tracing paths, full post-process chain, async compute passes
- **Material System** — PBR material instances with 8 bindless texture slots, dirty tracking, GPU structured buffer upload
- **Virtual Texturing** — Page pool with LRU eviction, GPU feedback buffer, mip-level streaming, compute mip generator (Box/Kaiser/Lanczos + SPD), sparse binding manager
- **Virtual Shadow Maps** — Clipmap-based page pool with LRU eviction
- **Virtual Geometry** — Nanite-style LOD streaming with screen-space error, priority queue, LRU eviction (512 MB budget)
- **CDLOD Terrain** — Clipmap rendering with procedural generation and 16-layer material splatting
- **GPU Particles** — Compute-driven emit/simulate/sort with curl noise turbulence
- **Full Post-Processing Stack** — TAA, FXAA, bloom, TSR, tone mapping, DOF bokeh, motion blur, chromatic aberration, film grain, vignette, SVGF denoise, auto-exposure, CAS sharpen, GTAO (horizon-based + temporal + spatial denoise), SSR, VRS, volumetric clouds, screen-space contact shadows, volumetric light scattering (god rays), screen-space subsurface scattering (separable SSS + transmittance), lens flare (ghosts + halo + starburst + lens dirt), SSGI (screen-space global illumination with temporal + spatial denoise)
- **Asset Pipeline** — glTF 2.0 importer, shader permutation system, DXC→SPIR-V compilation, hot-reload, dependency-aware include resolver, async loader, SPIR-V shader reflection, shader variant warm-up, persistent shader variant cache (on-disk .svc binary format), shader file watcher (filesystem monitoring + recompilation trigger)
- **GPU Memory Management** — Buffer pool, frame allocator, staging manager, transient resource pool (aliased), memory defragmenter, aliasing optimizer (graph-coloring), deletion queue, upload ring buffer, descriptor heap + ring buffer, fence pool, resource lifetime manager, buffer suballocator (first-fit + coalescing), memory budget tracker (VK_EXT_memory_budget), async copy engine (dedicated transfer queue DMA), render target pool (format/size-aware recycling), command pool ring (per-frame recycling), descriptor set allocator (transient + persistent), dynamic buffer allocator (ring-buffer per-draw constants), sampler feedback manager (texture streaming residency), bindless texture manager (16K global array + residency), descriptor pool manager (auto-growing + per-type tracking), heap inspector (per-heap allocation tracking + visualization), ping-pong buffer manager (double/triple buffer rotation)
- **GPU Submission** — Submission batcher (minimize vkQueueSubmit), render pass cache, pipeline layout cache, sampler pool, command signature builder, PSO builder (fluent API + validation + presets), PSO hash (FNV-1a deduplication), multi-queue sync manager (timeline semaphores), bindless table updater (batched descriptor writes), timestamp calibration (CPU↔GPU clock sync), viewport state manager (dynamic stack), push constant manager (type-safe validation + range merging), specialization constant manager (named presets), pipeline cache manager (disk persistence + PSO dedup), shader module cache (SPIR-V deduplication + lazy loading), image layout tracker (automatic transitions), resource state validator (debug-mode hazard detection), query readback manager (async collection + aggregation), indirect count builder (VK_KHR_draw_indirect_count), frame timeline manager (CPU/GPU overlap + pacing), render pass manager (automatic pass merging + subpass deps)
- **Debug Systems** — GPU profiler overlay, hierarchical GPU timer, query heap (timestamp/occlusion/pipeline stats), debug line/text renderer, 14-mode debug visualization, render statistics collector (300-frame rolling history), VK_EXT_debug_utils markers/labels/object names, dynamic rendering (VK_KHR_dynamic_rendering), descriptor update templates, render graph visualizer (DOT/Graphviz + Mermaid export), shader objects (VK_EXT_shader_object + binary cache)

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
│  RHI (Vulkan 1.3 + caches + barrier + submission batcher) │
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
│   │   ├── common/     # RHI abstraction, buffer pool, staging, timeline fence, barrier tracker, query heap, format utils, indirect buffers, transient pool, memory defrag, aliasing optimizer, render state, attachment builder, command pool, submission batcher, descriptor heap, upload ring, render pass cache, pipeline layout cache, fence pool, sampler pool, resource lifetime, command signature, buffer suballocator, timestamp calibration, bindless updater, PSO builder/hash, queue sync, memory budget, async copy, viewport state, push constants, spec constants, pipeline cache manager, shader module cache, image layout tracker, render target pool, command pool ring, descriptor allocator, resource validator
│   │   └── vulkan/     # Vulkan 1.3 backend, sampler/pipeline/descriptor caches, swapchain presenter+manager, sparse binding, debug markers, dynamic rendering, descriptor templates
│   ├── renderer/
│   │   ├── pipeline/   # Render compositor, GPU culling, instance manager, mesh registry, mip generator, draw call merger, GPU scene buffer, occlusion feedback, indirect cull pipeline, texture atlas
│   │   ├── graph/      # Render graph (async compute, cross-queue sync), resource versioning, work graph scheduler, frame graph compiler, pass profiler, resource pool, graph visualizer
│   │   ├── materials/  # PBR material system (bindless textures)
│   │   ├── lighting/   # Clustered light culling (5 light types, 3D grid)
│   │   ├── streaming/  # Virtual texture streaming, LOD streaming manager
│   │   └── debug/      # Debug renderer, text, profiler overlay, debug visualization (14 modes), render stats, GPU timer
│   ├── app/            # Application bootstrap (init/shutdown/main loop)
│   ├── assets/         # Mesh/texture/shader loaders, resource manager, glTF importer, shader permutations, include resolver, shader warmup, async loader, shader reflection, shader variant cache, file watcher
│   ├── scene/          # Camera, transforms, serialization, prefab system
│   ├── network/        # UDP socket, server, client, reliable delivery
│   ├── physics/        # Jolt wrapper + stub Euler simulation
│   ├── audio/          # Miniaudio wrapper + stub
│   ├── animation/      # Skeleton, clips, blend tree, PGA interpolation
│   ├── scripting/      # Lua/Sol2 wrapper, hot-reload
│   └── ai/             # Behavior tree, nav mesh (A* pathfinding)
├── editor/             # ImGui docking editor (viewport, hierarchy, inspector, console, assets, profiler)
├── shaders/            # 59 HLSL compute/vertex/fragment shaders
│   ├── common/         # Shared math, BRDF
│   ├── compute/        # HZB build, VRS, GPU skinning, particles, occlusion cull, meshlet LOD, indirect draw, VT feedback, cluster lights, frustum cull, mip downsample
│   ├── visibility/     # Material resolve, visibility buffer resolve
│   ├── lighting/       # GI probes, SSR, SSAO, GTAO, decals, ReSTIR, contact shadows
│   ├── postprocess/    # Bloom, tonemap, TSR, DOF bokeh, motion blur, TAA, FXAA, SVGF denoise, auto-exposure, CAS, chromatic aberration, ocean, depth of field
│   ├── atmosphere/     # Sky, volumetric fog, volumetric clouds
│   ├── shadows/        # Shadow rasterization
│   ├── terrain/        # Terrain CDLOD rendering
│   └── debug/          # Debug lines + text rendering
├── samples/triangle/   # Minimal sample app
├── tests/              # 33 test files (unit + integration)
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
