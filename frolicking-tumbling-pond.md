# NextGen Game Engine — Master Implementation Plan

## Vision

A from-scratch 3D game engine rivaling Unreal Engine 5 with real-time path tracing, virtual geometry, GPU-driven rendering, and novel mathematical foundations. Written in C++20/23 with Vulkan 1.3 primary / DX12 secondary.

---

<a id="phase-0-delivery-risk"></a>
## Phase 0: Delivery & Risk (Cross-Cutting)

### 0.1 — Feature Tiers and Hardware Fallbacks

- Define runtime feature tiers and enforce graceful fallback:

| Tier | Required GPU Features | Render Path |
|------|------------------------|-------------|
| Tier 0 | Vulkan 1.3 / DX12 baseline | Raster + deferred/forward fallback (no mesh shaders, no RT) |
| Tier 1 | Mesh shaders + descriptor indexing | GPU-driven meshlet visibility pipeline |
| Tier 2 | Hardware ray tracing | Hybrid GI + production path tracing mode |
| Tier 3 | Tensor/ML acceleration | Neural denoiser + neural radiance cache acceleration |

- Each major feature must specify: capability probe, fallback path, and quality/perf delta.
- Ship-time startup report logs selected tier and disabled features.

### 0.2 — Milestones, Exit Criteria, and Budgets

- Every phase must include explicit exit criteria with numeric gates:
  - Stability: zero crashers in 30-minute soak scene replay.
  - Performance: p95 frame time target for a defined scene and hardware tier.
  - Memory: max RAM/VRAM budgets plus leak-free shutdown.
  - Build health: all required CI jobs green on `main`.
- Reference hardware SKUs for all performance gates:

| Label | CPU | GPU | RAM | Notes |
|-------|-----|-----|-----|-------|
| Baseline Desktop | Ryzen 5 5600 | RTX 3060 12 GB | 32 GB DDR4 | Primary pass/fail target for real-time gameplay gates |
| High-End Desktop | Ryzen 7 7800X3D | RTX 4080 16 GB | 32 GB DDR5 | Stretch target and regression early-warning target |
| Dedicated Server | Ryzen 7 7700 | None | 32 GB DDR5 | Authoritative server and replication soak target |

- Reference test scene/profile IDs for reproducible gates:

| ID | Canonical Content | Camera/Playback | Duration | Notes |
|----|-------------------|-----------------|----------|-------|
| `SCN_TRIANGLE_SMOKE_V1` | `samples/triangle` | static | 5 min | Smoke and startup performance gate |
| `SCN_SPONZA_VIS_V1` | `samples/sponza` visibility-only path | `cam_flythrough_a` | 10 min loop | Used by meshlet/culling benchmarks |
| `SCN_SPONZA_LIGHT_V1` | `samples/sponza` full lighting path | `cam_flythrough_b` | 10 min loop | Used by GI/lighting milestones |
| `SCN_EDITOR_ITERATION_V1` | editor scene set `city_block_small` | scripted editor interaction trace | 30 min | Used for editor responsiveness and memory drift |
| `NET_VERTICAL_SLICE_V1` | 2 clients + dedicated server vertical slice map | deterministic input replay | 120 min | Used for correction-rate and soak gates |

- Milestone checklist (pass if all gate columns pass, fail if any gate column fails):

| Milestone | Scene/Profile ID | Scope | Performance Gate (Pass/Fail) | Stability Gate (Pass/Fail) | Memory Gate (Pass/Fail) | Build/Quality Gate (Pass/Fail) | Status |
|-----------|------------------|-------|-------------------------------|-----------------------------|--------------------------|----------------------------------|--------|
| M1 | `SCN_TRIANGLE_SMOKE_V1` | Foundation + first triangle | Baseline Desktop: `samples/triangle` p95 <= 16.6 ms @ 1080p; High-End Desktop: p95 <= 8.3 ms @ 1080p | 30 min soak, 0 crashes | Leak-free shutdown, RAM <= 1 GB | CI: Windows/Linux builds + unit tests green | `[ ]` |
| M2 | `SCN_SPONZA_VIS_V1` | GPU-driven visibility + meshlets | Baseline Desktop: `samples/sponza` visibility path p95 <= 16.6 ms @ 1080p; High-End Desktop: p95 <= 10 ms | 60 min camera flythrough, 0 crashes | VRAM <= 6 GB @ 1080p | CPU vs GPU culling parity >= 99.9% | `[ ]` |
| M3 | `SCN_SPONZA_LIGHT_V1` | PBR + shadows + hybrid GI baseline | Baseline Desktop: Sponza lighting path p95 <= 25 ms @ 1080p; High-End Desktop: p95 <= 16.6 ms | 60 min lighting stress run, 0 crashes | VRAM <= 8 GB, no allocator leaks | Golden image delta <= 2% RMS on test scenes | `[ ]` |
| M4 | `SCN_EDITOR_ITERATION_V1` | Editor usable for iteration | Baseline Desktop: editor viewport p95 <= 20 ms @ 1080p; High-End Desktop: p95 <= 12 ms | 4 hr editor soak, 0 crashes | No growth > 2% RAM over 30 min idle | Undo/redo, asset import, hot reload test suite green | `[ ]` |
| M5 | `NET_VERTICAL_SLICE_V1` | Content streaming + multiplayer vertical slice | Baseline Desktop client + Dedicated Server: sim tick 60 Hz, correction rate <= 1/sec avg; High-End Desktop client: correction rate <= 0.5/sec avg | 2 hr network soak @ 2% loss/80 ms RTT, 0 crashes | Dedicated Server RAM <= 4 GB, Baseline Desktop client VRAM <= 8 GB | Deterministic replay and packet fuzz suite green | `[ ]` |

- Rule: Any content, camera path, replay script, runtime setting, or driver/toolchain change that affects a gate requires an ID version bump (for example `_V1` to `_V2`) and a baseline refresh in CI artifacts.

### 0.3 — Dependency Governance

- Pin versions in `vcpkg.json` and record lockfile state in CI.
- Track library policy per dependency: `adopt`, `wrap`, or `replace`.
- Add license and update policy:
  - SPDX license inventory generated in CI.
  - Monthly dependency update window with regression run.
  - Removal deadlines for temporary deps (`glm`, `spdlog`, `entt`) once custom subsystems are production-ready.

### 0.4 — Security and Untrusted Content

- Treat imported assets, scripts, and network packets as untrusted input.
- Add fuzzing targets for:
  - Asset importers (glTF/FBX/images/audio).
  - Shader compiler front-end and reflection parser.
  - Network packet decode/replication paths.
- Add hard limits and validation:
  - Max file sizes, node counts, texture dimensions, and recursion depth.
  - Deterministic parse failures with actionable error reporting.

---

## Directory Structure

```
NextGenGameEngine/
├── CMakeLists.txt                    # Root build
├── vcpkg.json                        # Dependency manifest
├── .gitignore
├── engine/
│   ├── CMakeLists.txt
│   ├── core/                         # Foundation layer
│   │   ├── memory/                   # Custom allocators
│   │   ├── containers/               # Custom containers (no STL in hot paths)
│   │   ├── math/                     # Geometric algebra math library
│   │   ├── jobs/                     # Lock-free job system
│   │   ├── logging/                  # Structured logging
│   │   ├── platform/                 # OS abstraction (window, input, filesystem)
│   │   ├── ecs/                      # Archetype-based ECS
│   │   └── profiling/                # CPU/GPU profiling instrumentation
│   ├── rhi/                          # Render Hardware Interface (abstraction)
│   │   ├── vulkan/                   # Vulkan 1.3 backend
│   │   ├── dx12/                     # DirectX 12 backend
│   │   └── common/                   # Shared types, enums, interfaces
│   ├── renderer/                     # High-level rendering systems
│   │   ├── pipeline/                 # GPU-driven render pipeline orchestration
│   │   ├── visibility/               # Visibility buffer, GPU culling
│   │   ├── geometry/                 # Virtual geometry (Nanite-like)
│   │   ├── lighting/                 # ReSTIR, RTXDI, hybrid GI, IBL
│   │   ├── shadows/                  # Virtual shadow maps
│   │   ├── materials/                # PBR material system, bindless
│   │   ├── atmosphere/               # Sky, volumetric clouds, fog
│   │   ├── postprocess/              # Temporal upscaling, bloom, tone mapping
│   │   ├── pathtracer/               # Full path tracer with ReSTIR
│   │   ├── neural/                   # Neural radiance cache, neural denoising
│   │   └── spectral/                 # Spectral rendering mode
│   ├── scene/                        # Scene graph + world management
│   │   ├── world/                    # World partition, streaming
│   │   ├── transform/                # GA-based transform hierarchy
│   │   └── camera/                   # Camera system
│   ├── assets/                       # Asset pipeline
│   │   ├── importers/                # FBX, glTF, PNG, HDR, WAV importers
│   │   ├── cooker/                   # Asset cooking/compilation
│   │   ├── streaming/                # Runtime asset streaming
│   │   └── virtual_texture/          # Virtual texture system
│   ├── physics/                      # Physics integration
│   │   ├── jolt/                     # Jolt Physics wrapper
│   │   ├── particles/                # GPU particle physics
│   │   └── fluid/                    # SPH fluid simulation
│   ├── audio/                        # Audio engine
│   │   ├── spatial/                  # HRTF spatial audio
│   │   ├── propagation/              # Ray-traced acoustics
│   │   └── dsp/                      # DSP effect chain
│   ├── scripting/                    # Scripting layer
│   │   ├── lua/                      # Lua integration
│   │   ├── visual/                   # Node-based visual scripting
│   │   └── hotreload/                # C++ DLL hot reload
│   ├── animation/                    # Animation systems
│   │   ├── skeletal/                 # Skeletal animation, blending
│   │   ├── motion_matching/          # Motion matching system
│   │   └── ik/                       # Inverse kinematics
│   ├── ai/                           # AI systems
│   │   ├── navigation/               # Navmesh, pathfinding
│   │   ├── behavior/                 # Behavior trees
│   │   └── perception/               # AI sensing
│   └── network/                      # Networking
│       ├── transport/                # UDP/TCP transport
│       ├── replication/              # State replication
│       └── rollback/                 # Rollback netcode
├── editor/                           # ImGui-based editor
│   ├── viewport/                     # 3D viewport
│   ├── inspector/                    # Property inspector
│   ├── hierarchy/                    # Scene hierarchy panel
│   ├── assets/                       # Asset browser
│   └── profiler/                     # Visual profiler
├── shaders/                          # All GPU shaders
│   ├── common/                       # Shared includes
│   ├── mesh/                         # Mesh shaders + amplification
│   ├── visibility/                   # Visibility buffer generation
│   ├── lighting/                     # Direct + indirect lighting
│   ├── pathtracing/                  # Path tracing shaders
│   ├── postprocess/                  # Post-processing
│   ├── compute/                      # General compute shaders
│   └── neural/                       # Neural network inference shaders
├── tools/                            # Offline tools
│   ├── meshlet_builder/              # Meshlet partitioning tool
│   ├── sdf_baker/                    # SDF volume baker
│   └── shader_compiler/              # Shader cross-compilation
├── tests/                            # Unit + integration tests
├── samples/                          # Demo applications
│   ├── triangle/                     # Hello triangle
│   ├── sponza/                       # Sponza scene demo
│   └── pathtracing_demo/             # Path tracing showcase
└── docs/                             # Technical documentation
```

---

## Phase 1: Foundation (Core Systems)

### 1.1 — Build System & Project Skeleton

- CMake 3.28+ with presets for Debug/Release/Profile
- vcpkg manifest mode for dependencies
- Git repo initialization with `.gitignore`
- Compiler: MSVC (Windows), Clang (Linux)
- C++20 modules where supported, headers otherwise
- **Files**: Root `CMakeLists.txt`, `vcpkg.json`, per-module `CMakeLists.txt`

### 1.2 — Core Type System & Macros

- `engine/core/types.h` — `u8, u16, u32, u64, i8, i16, i32, i64, f32, f64, usize`
- `engine/core/assert.h` — Debug/release assertions with source location
- `engine/core/hash.h` — FNV-1a, xxHash for runtime hashing
- `engine/core/uuid.h` — 128-bit UUID generation
- Compile-time type ID system using `__COUNTER__` or templates

### 1.3 — Memory Management System

Custom allocators replacing `new`/`delete` everywhere:

| Allocator | Use Case | Algorithm |
|-----------|----------|-----------|
| `LinearAllocator` | Per-frame temps | Bump pointer, O(1) alloc, bulk reset |
| `StackAllocator` | Nested scoped temps | Stack with markers, LIFO free |
| `PoolAllocator<T>` | Fixed-size objects (entities, components) | Free list, O(1) alloc/free |
| `TLSFAllocator` | General purpose heap | Two-Level Segregated Fit, O(1) alloc/free, low fragmentation |
| `VirtualMemoryAllocator` | Large streaming data | OS virtual memory commit/decommit |

- All allocators implement `IAllocator` interface: `void* Allocate(usize size, usize align)`, `void Free(void* ptr)`, `void Reset()`
- Memory tracking: tag every allocation with subsystem + source location in debug builds
- **Files**: `engine/core/memory/`

### 1.4 — Custom Containers (No STL in Hot Paths)

- `Array<T>` — Dynamic array with custom allocator support
- `HashMap<K,V>` — Robin Hood open addressing hash map
- `HashSet<T>` — Robin Hood hash set
- `RingBuffer<T>` — Lock-free SPSC ring buffer for job system
- `String` / `StringView` — SSO string with allocator
- `Span<T>` — Non-owning view
- `Optional<T>`, `Result<T,E>` — Error handling without exceptions
- **Files**: `engine/core/containers/`

### 1.5 — Geometric Algebra Math Library (NOVEL)

**Replace traditional matrix math with Projective Geometric Algebra (PGA)**:

```
Traditional: mat4 × vec4 for transforms, quaternion for rotation, separate translation
PGA: Motor (rotor + translator) = single object for all rigid transforms
```

- **Basis**: ℝ(3,0,1) — Projective Geometric Algebra
  - 4 basis vectors: e1, e2, e3, e0 (degenerate/null)
  - Geometric product, wedge product, inner product
  - Multivector grades 0-4: scalars, vectors, bivectors, trivectors, pseudoscalar

- **Key Types**:
  - `Point` — grade-3 trivector `(e032, e013, e021, e123)` representing homogeneous point
  - `Plane` — grade-1 vector `(e1, e2, e3, e0)` representing oriented plane
  - `Line` — grade-2 bivector `(e01, e02, e03, e23, e31, e12)` — Plücker coordinates
  - `Motor` — even-grade multivector for rigid body transforms (rotation + translation unified)
  - `Rotor` — motor with no translation component (pure rotation, replaces quaternions)
  - `Translator` — motor with no rotation component

- **Operations**:
  - `sandwich(M, X) = M * X * ~M` — apply motor M to any geometric object X
  - Motor interpolation via `exp(t * log(M))` — smooth geodesic interpolation
  - `meet(A, B)` — intersection of geometric objects
  - `join(A, B)` — span of geometric objects
  - Motor composition: `M_combined = M2 * M1` (replaces matrix multiplication)

- **Advantages over Matrices**:
  - 8 floats (motor) vs 16 floats (4x4 matrix) — 2x memory reduction
  - No gimbal lock, no matrix decomposition needed
  - Numerically stable interpolation via logarithmic map
  - Unified representation: points, planes, lines, transforms are all multivectors
  - Reflection, rotation, translation all via sandwich product

- **SIMD**: Implement using SSE/AVX intrinsics; motor sandwich product maps to ~20 SIMD instructions
- Reference implementation: Klein library (jeremyong.com/klein) for SIMD PGA
- **Files**: `engine/core/math/`

### 1.6 — Platform Abstraction Layer

- Window creation/management (Win32 API / Wayland+X11)
- Input: keyboard, mouse, gamepad (XInput/DirectInput, evdev)
- Filesystem: async file I/O with completion callbacks
- Clock: high-resolution timer (`QueryPerformanceCounter` / `clock_gettime`)
- Threading primitives: threads, mutexes, condition vars, atomics
- DLL loading for hot reload and plugin system
- **Files**: `engine/core/platform/`

### 1.7 — Job System (Lock-Free Work Stealing)

- **Architecture**: 1 thread per hardware core, each with local deque
- **Algorithm**: Chase-Lev work-stealing deque
  - Push/pop from bottom (owning thread) — O(1) no contention
  - Steal from top (other threads) — CAS-based, O(1)
- **Job Types**:
  - `Job` — fire-and-forget task
  - `JobHandle` — awaitable future
  - `JobChain` — dependency graph of jobs
  - `ParallelFor(range, lambda)` — auto-splits across cores
- **Fiber support** (optional): coroutine-based jobs for I/O waits without blocking threads
- **Files**: `engine/core/jobs/`

### 1.8 — Logging & Profiling

- Structured logging with severity levels, tags, and source locations
- Ring buffer log sink (no allocation per log in release)
- Profiling: scoped CPU timers with `PROFILE_SCOPE("name")` macro
- GPU timestamp queries for render profiling
- Tracy profiler integration for visualization
- **Files**: `engine/core/logging/`, `engine/core/profiling/`

### 1.9 — Entity Component System (Archetype-Based)

- **Architecture**: Archetype-based (like flecs/Unity DOTS)
  - Components grouped by archetype — entities with same component set share contiguous storage
  - Cache-friendly iteration: systems iterate linear arrays
  - O(1) component access per entity within archetype

- **Key Types**:
  - `World` — owns all entity storage, systems
  - `Entity` — 64-bit ID (32-bit index + 32-bit generation)
  - `ComponentArray<T>` — dense packed array per archetype column
  - `Archetype` — unique set of component types, owns component arrays
  - `Query<Cs...>` — compile-time typed query over component sets
  - `System` — function operating on queries, scheduled by job system

- **Features**:
  - Add/remove components triggers archetype migration (batched)
  - Deferred command buffers for structural changes during iteration
  - Singleton components for global state
  - Relationships/tags for entity associations
  - Event system for inter-system communication

- **Files**: `engine/core/ecs/`

---

## Phase 2: Render Hardware Interface (RHI)

### 2.1 — RHI Abstraction Layer

Thin abstraction over Vulkan/DX12 exposing modern GPU features:

- **Core Objects**:
  - `Device` — GPU device, queues, capabilities
  - `CommandList` — recorded GPU commands (graphics, compute, transfer)
  - `Buffer` — GPU buffer (vertex, index, uniform, storage, indirect)
  - `Texture` — 1D/2D/3D/Cube textures with views
  - `Sampler` — texture sampling state
  - `Pipeline` — graphics/compute/ray-tracing pipeline state
  - `ShaderModule` — compiled shader bytecode
  - `DescriptorSet` — resource bindings (bindless model)
  - `Swapchain` — presentation surface
  - `Fence`, `Semaphore` — synchronization primitives
  - `AccelerationStructure` — BVH for ray tracing (BLAS + TLAS)

- **Bindless Model**:
  - Global descriptor array per resource type (textures, buffers, samplers)
  - Resources referenced by index in shaders
  - Eliminates per-draw descriptor binding overhead

- **Multi-Queue**:
  - Graphics queue, async compute queue, transfer/copy queue
  - Automatic resource barriers/transitions

- **Files**: `engine/rhi/common/`, `engine/rhi/vulkan/`, `engine/rhi/dx12/`

### 2.2 — Vulkan 1.3 Backend

- Instance/device creation with required extensions:
  - `VK_KHR_mesh_shader`
  - `VK_KHR_ray_tracing_pipeline`, `VK_KHR_acceleration_structure`
  - `VK_EXT_descriptor_indexing` (bindless)
  - `VK_KHR_dynamic_rendering` (no render passes needed)
  - `VK_KHR_synchronization2`
  - `VK_EXT_mesh_shader`
- Vulkan Memory Allocator (VMA) integration for GPU memory
- SPIR-V shader loading
- Validation layer integration in debug builds

### 2.3 — Shader Compilation Pipeline

- Author shaders in HLSL (for DX12 compatibility) or GLSL
- Compile to SPIR-V via DXC (DirectX Shader Compiler) or glslangValidator
- Shader reflection for automatic descriptor layout generation
- Shader permutation system with `#define` variants
- Precompiled shader cache with hash-based invalidation
- **Files**: `tools/shader_compiler/`, `shaders/`

<a id="phase-2-4-directx12-backend"></a>
### 2.4 — DirectX 12 Backend

- Implement backend parity for all RHI core objects:
  - Device/queues/swapchain/command lists/resources/pipelines/descriptors/synchronization.
- Use DXC as primary compiler path for DXIL generation.
- Enable and validate key DX12 features:
  - Mesh shaders (`D3D12_OPTIONS7`), ray tracing (`DXR 1.1`), descriptor heap bindless model.
- Integrate robust resource barrier tracking (enhanced barrier model where available).
- Add debug tooling and validation:
  - D3D12 debug layer, GPU-based validation in debug builds, PIX markers/events.
- Add backend conformance tests:
  - Vulkan vs DX12 image parity checks for triangle, meshlet visibility, and path tracing smoke tests.

---

## Phase 3: GPU-Driven Rendering Pipeline

### 3.1 — Meshlet Builder (Offline Tool)

- Input: Standard mesh (vertices + indices)
- Output: Meshlet hierarchy with bounding data
- **Algorithm**:
  1. Build meshlets of ≤64 vertices, ≤124 triangles (hardware optimal)
  2. Compute per-meshlet bounding sphere + cone for backface culling
  3. Build hierarchical cluster DAG for LOD (simplified parent clusters)
  4. Store compressed: quantized positions, octahedral normals, delta-coded indices
- Uses meshoptimizer library for initial meshlet partitioning
- **Files**: `tools/meshlet_builder/`

### 3.2 — Visibility Buffer Rendering

Instead of traditional deferred (G-buffer), use visibility buffer:

- **Pass 1 — Visibility**: Rasterize triangle ID + meshlet ID + material ID to R32G32 target
- **Pass 2 — Material Resolve**: Full-screen compute reads visibility buffer, reconstructs position/normal from barycentrics + mesh data, evaluates material shaders
- **Advantages**: Decouples geometry from shading, reduces bandwidth (2 channels vs 5+ G-buffer channels), enables shading at arbitrary rate (VRS)

### 3.3 — GPU-Driven Culling & Draw Submission

All on GPU via compute shaders:

```
Compute: Instance Culling (frustum + occlusion via HZB)
    ↓
Compute: Meshlet Culling (frustum + backface cone + occlusion)
    ↓
Compute: Build Indirect Draw Commands
    ↓
Mesh Shader: Process visible meshlets → Visibility Buffer
    ↓
Compute: Material Resolve (shade visible pixels)
```

- **Hierarchical Z-Buffer (HZB)**: Mip-chain of depth buffer from previous frame for occlusion culling
- **Two-Phase Occlusion Culling**:
  1. Cull against last frame's HZB
  2. Render survivors, rebuild HZB
  3. Test previously-rejected instances against new HZB
  4. Render newly-visible instances

### 3.4 — Mesh Shader Pipeline

- **Amplification Shader**: Per-meshlet-group, performs culling, outputs surviving meshlet indices
- **Mesh Shader**: Per-meshlet, loads vertices/indices from global buffer via meshlet descriptor, transforms, outputs to rasterizer
- Shaders written in HLSL with `[numthreads(32,1,1)]` for wave-optimal occupancy
- **Files**: `shaders/mesh/`, `engine/renderer/visibility/`

### 3.5 — Virtual Geometry System (Nanite-like)

- **Hierarchical Cluster LOD**:
  - Mesh → Clusters (meshlets) → Cluster Groups → LOD hierarchy
  - Each level: simplified geometry of children
  - GPU traverses hierarchy, selects clusters based on screen-space error threshold
  - Seamless LOD transitions (no popping) via error metric continuity

- **Streaming**:
  - Geometry stored in pages (~64KB each)
  - GPU identifies required pages via feedback buffer
  - CPU streams pages from disk asynchronously
  - Page pool with LRU eviction

- **Compression**:
  - Quantized vertex positions (16-bit or variable)
  - Octahedral normal encoding (2 × 8-bit)
  - Delta-coded triangle indices
  - ~3:1 compression vs raw mesh data

- **Files**: `engine/renderer/geometry/`

---

## Phase 4: Lighting & Global Illumination

### 4.1 — Bindless PBR Material System

- **Material Model**: Disney/GGX BRDF
  - Base color, metallic, roughness, normal, AO, emissive
  - Extensions: clear coat, sheen, subsurface, thin film, anisotropy
  - Hair: Marschner/BCSDF model with R/TT/TRT lobes
  - Cloth: Ashikhmin velvet + subsurface
- **Material Graph**: Node-based material editor compiling to shader permutations
- **Bindless Textures**: All material textures in global descriptor array, material references by index
- **Files**: `engine/renderer/materials/`

### 4.2 — Direct Lighting with RTXDI

- **Problem**: Scenes with millions of emissive triangles / area lights
- **Solution**: ReSTIR-based direct illumination (RTXDI algorithm)
  1. For each pixel, generate N candidate lights (random sampling from light pool)
  2. Evaluate target PDF (unshadowed contribution × visibility estimate)
  3. Reservoir stores best sample with correct weight
  4. Spatial resampling: share reservoirs with neighbors
  5. Temporal resampling: reuse reservoir from previous frame
  6. Final: trace 1 shadow ray per pixel for selected light
- **Result**: 1 ray per pixel produces quality equivalent to thousands of shadow rays
- **Files**: `engine/renderer/lighting/`, `shaders/lighting/`

### 4.3 — Hybrid Global Illumination (Lumen-like)

Multiple GI methods, selected per-pixel by cost heuristic:

| Method | Range | Cost | Quality |
|--------|-------|------|---------|
| Screen-Space Ray Tracing | Near, on-screen | Low | Good for specular |
| SDF Ray Tracing | Mid-range | Medium | Good diffuse |
| Hardware RT | All ranges | High | Best quality |
| Radiance Cache (Probes) | Far-field | Low | Diffuse only |

- **SDF Global Distance Field**:
  - Per-object mesh distance fields merged into global SDF
  - Ray march through SDF for software ray tracing (~10x faster than HW RT)
  - Update incrementally as objects move

- **Irradiance Probes**:
  - World-space probe grid storing SH coefficients (L2, 9 coefficients per color)
  - Updated via ray tracing (HW or SDF) each frame (subset of probes)
  - Trilinear interpolation for smooth indirect diffuse

- **Files**: `engine/renderer/lighting/`, `shaders/lighting/`

### 4.4 — Virtual Shadow Maps

- **Architecture**: Clipmap-based virtual shadow map per directional light
  - 16K×16K virtual shadow map divided into 128×128 pages
  - Only allocate pages visible to camera
  - Cache pages across frames, invalidate on geometry change
- **Point/Spot Lights**: Cubemap virtual shadow maps
- **Integration**: Shadows sampled during material resolve pass
- **Files**: `engine/renderer/shadows/`

### 4.5 — Real-Time Path Tracer (Reference + Production Modes)

Two modes:

- **Production**: 1 SPP path tracing + ReSTIR temporal/spatial resampling + neural denoising
- **Reference**: Progressive accumulation, unlimited bounces, spectral option

**Path Tracer Architecture**:

```
Ray Generation → Primary Hit → Material Evaluation → Next Event Estimation (NEE)
    ↓                                                            ↓
Russian Roulette → Continue bounce or terminate          Shadow Ray → Direct illumination
    ↓
ReSTIR resampling (spatial + temporal)
    ↓
Neural Denoiser (separates diffuse/specular/shadow)
    ↓
Temporal Accumulation + Upscaling
```

- **ReSTIR GI**: Reservoir-based path reuse for indirect illumination
- **ReSTIR BDPT**: Bidirectional path tracing with reservoirs for caustics
- **Acceleration Structure**: BLAS per mesh, TLAS per frame (rebuilt each frame for dynamic objects)
- **Files**: `engine/renderer/pathtracer/`, `shaders/pathtracing/`

### 4.6 — Neural Radiance Cache (NOVEL)

- Small MLP (Multi-Layer Perceptron) trained at runtime
- Input: surface position + normal + view direction (hashed encoding)
- Output: multi-bounce indirect radiance
- **Training**: After 1-2 path-traced bounces, train network on converged radiance
- **Inference**: Replace expensive bounces 3+ with neural network query
- **Architecture**: Hash grid encoding (Instant NGP style) + 4-layer MLP, runs on tensor cores
- **Files**: `engine/renderer/neural/`

### 4.7 — Spectral Rendering Mode (NOVEL)

- Replace RGB (3 channels) with spectral sampling (hero wavelength + companions)
- **Hero Wavelength Spectral Sampling**: Sample 1 primary wavelength, derive 3 companion wavelengths at fixed offsets
- Enables: dispersion (prisms, diamonds), iridescence (soap bubbles, butterfly wings), fluorescence
- Spectral material data: reflectance stored as SH coefficients over wavelength domain
- Convert final spectral radiance to XYZ → sRGB for display
- **Files**: `engine/renderer/spectral/`, `shaders/pathtracing/spectral_*`

---

## Phase 5: Post-Processing & Atmosphere

### 5.1 — Temporal Super Resolution (Custom TSR)

- Render at lower resolution (50-67% of target)
- Jittered rendering with Halton sequence sub-pixel offsets
- Temporal reprojection using motion vectors
- History rectification: neighborhood clamping to prevent ghosting
- Sharpening pass on upscaled result
- **Target**: Native 4K quality at ~1440p render cost

### 5.2 — Variable Rate Shading

- Content-adaptive: analyze luminance variance + motion from previous frame
- Generate shading rate image (SRI) via compute shader
- Regions with low variance or high motion → coarser shading rate (2x2, 4x4)
- Edges and detail areas → full rate (1x1)
- 20-40% shading cost reduction

### 5.3 — Volumetric Atmosphere

- **Sky Model**: Precomputed atmospheric scattering (Bruneton model)
  - Rayleigh + Mie scattering
  - Transmittance LUT, irradiance LUT, inscatter LUT
- **Volumetric Clouds**: Ray-marched 3D noise volumes
  - Weather map (2D) drives cloud coverage/type
  - Detail noise for shape erosion
  - Temporal reprojection (render 1/16 pixels per frame, accumulate)
  - Multiple scattering approximation via "powder" effect
- **Volumetric Fog**: Froxel-based (frustum-aligned voxel grid)
  - Inject fog density + lighting into 3D texture
  - Ray-march through froxels during material resolve

### 5.4 — Post-Processing Stack

- Bloom: compute-based downsample/upsample chain with energy conservation
- Tone Mapping: AgX or Tony McMapface (modern filmic)
- Color Grading: 3D LUT-based
- Motion Blur: per-pixel velocity-based
- Depth of Field: Bokeh simulation with near/far CoC
- Film Grain, Vignette, Chromatic Aberration (subtle)

---

## Phase 6: Asset Pipeline & Streaming

### 6.1 — Asset Import Pipeline

- **Mesh**: glTF 2.0 (primary), FBX via Assimp
  - Import → Meshlet build → LOD hierarchy → Compress → Pack
- **Textures**: PNG, HDR, EXR → BC7/BC6H compression → Virtual texture pages
- **Materials**: glTF PBR → Engine material definition
- **Audio**: WAV, OGG, FLAC → Engine format with streaming metadata
- Asset database tracking all processed assets with content hash for incremental rebuilds

### 6.2 — Virtual Texture System

- **Feedback Buffer**: Low-res render pass outputs required texture page IDs
- **Page Cache**: LRU cache in VRAM, backed by disk cache
- **Indirection Texture**: Maps virtual page coordinates to physical cache location
- **Transcoding**: BC7 pages streamed directly from disk (no CPU decompression)
- Supports material layering: blend multiple material textures in single virtual texture

### 6.3 — World Streaming

- World divided into cells (configurable grid)
- Cells streamed based on camera position with configurable load radius
- Async loading on dedicated I/O threads
- LOD for distant cells (lower geometry detail, lower texture mips)

<a id="phase-6-4-serialization-versioning"></a>
### 6.4 — Serialization & Versioning Strategy

- Define canonical binary schema for runtime data:
  - Save games, cooked assets, replication payloads, and hot-reload state snapshots.
- Every serialized blob includes:
  - Magic number, schema version, content hash, endianness, compression/encryption flags.
- Backward compatibility policy:
  - N and N-1 loader support for released schema versions.
  - Explicit migration steps per version bump, with upgrade tests.
- Determinism and replay:
  - Stable field ordering and deterministic float handling in network-critical state.
- Tooling:
  - `tools/schema_codegen/` for schema IDs, serializers, and migration stubs.

---

## Phase 7: Physics, Audio, Animation

### 7.1 — Jolt Physics Integration

- Rigid body simulation (static, dynamic, kinematic)
- Shapes: sphere, box, capsule, convex hull, triangle mesh, height field
- Constraints: hinge, slider, cone, fixed, 6DOF
- Ragdoll setup with joint limits
- Collision filtering layers
- Character controller (virtual character)

### 7.2 — GPU Particle System

- Compute shader particle simulation (millions of particles)
- SDF collision for particles (sample global distance field)
- Emitters: point, sphere, mesh surface, volume
- Forces: gravity, wind, turbulence (curl noise), attractors
- Sorting for alpha-blended rendering

### 7.3 — Spatial Audio Engine

- HRTF-based binaural rendering for headphones
- Distance attenuation with customizable curves
- Occlusion: ray cast against scene for muffling
- Reverb zones with convolution reverb
- Audio mixer with DSP chain (EQ, compressor, limiter, delay)
- Streaming audio for music, decode-on-demand for SFX

### 7.4 — Animation System

- Skeletal animation with bone hierarchy
- Animation blending (linear, additive, masked)
- Animation state machine with transitions
- **Motion Matching**: Query-based pose selection from motion database
  - Feature vector: joint positions/velocities, trajectory, phase
  - KD-tree search for best matching pose
  - Inertial blending for smooth transitions
- IK: Two-bone IK, FABRIK for chains, foot IK for terrain adaptation

---

## Phase 8: Editor & Tooling

### 8.1 — ImGui-Based Editor

- Dockable panels using ImGui docking branch
- **Panels**: Scene hierarchy, property inspector, viewport, asset browser, console, profiler
- **Viewport**: Full engine rendering in editor viewport with gizmos
- **Gizmos**: Translate/rotate/scale using GA motors
- **Undo/Redo**: Command pattern with serialization
- **Project Settings**: Graphics quality, physics settings, input mapping

### 8.2 — Visual Profiler

- CPU timeline (job system visualization)
- GPU timeline (render pass durations)
- Memory usage per subsystem
- Draw call / triangle count statistics
- Frame graph visualization

---

## Phase 9: Scripting & Gameplay

### 9.1 — Lua Scripting (sol2 Binding)

- Lua 5.4 embedded with sol2 for automatic C++ binding
- ECS query access from Lua scripts
- Event handling: `on_update`, `on_collision`, `on_trigger`
- Coroutine support for async gameplay logic
- Sandbox: restrict file/network access in scripts

### 9.2 — C++ Hot Reload

- Gameplay code compiled as DLL
- File watcher triggers recompilation
- Swap DLL at runtime, reconnect function pointers
- Serialize/deserialize game state across reload

---

## Phase 10: Networking

### 10.1 — Client-Server Networking

- ENet or custom UDP transport with reliability layer
- Authoritative server model
- State replication: delta compression, priority/relevancy filtering
- Client-side prediction + server reconciliation
- Interest management: only replicate relevant entities

<a id="phase-10-2-network-protocol-rollback"></a>
### 10.2 — Protocol, Rollback, and Determinism Design

- Fixed network tick (e.g., 60 Hz) decoupled from render rate.
- Packet protocol:
  - Versioned message schema IDs.
  - Snapshot delta stream + reliable command channel + unreliable event channel.
  - Sequence numbers, ACK ranges, packet loss and RTT telemetry.
- Rollback model:
  - Client stores input/state history for rollback window (e.g., 100-250 ms).
  - Server reconciliation with deterministic re-sim and correction blending.
- Lag compensation:
  - Server-side rewind for hit validation using historical transforms.
- Anti-cheat and trust boundaries:
  - Server authoritative for damage, movement constraints, inventory, and game rules.
- Verification:
  - Deterministic replay test, packet fuzz test, high-loss/high-latency soak test.

---

## Novel Mathematical Innovations Summary

| Innovation | Traditional Approach | Our Approach | Benefit |
|-----------|---------------------|--------------|---------|
| Transform Math | 4x4 Matrices + Quaternions | Projective Geometric Algebra Motors | 2x less memory, no gimbal lock, unified algebra |
| Light Transport | RGB only | Hero Wavelength Spectral Sampling | Dispersion, iridescence, fluorescence |
| GI Bounces 3+ | More ray tracing | Neural Radiance Cache | Orders of magnitude faster infinite-bounce GI |
| Direct Lighting | Shadow maps or N shadow rays | ReSTIR/RTXDI (1 ray = thousands quality) | Millions of lights, 1 ray/pixel |
| Spatial Hashing | BVH/Octree for all queries | Stochastic hash grid for particles/SPH | Better GPU parallelism, O(1) neighbor lookup |
| Texture Compression | Fixed BC7 blocks | Wavelet decomposition + BC7 | Progressive streaming, better quality/ratio |

---

## Build & Test Verification Plan

### Per-Phase Verification

1. **Phase 1**: Unit tests for allocators, containers, math (GA), ECS queries, job system throughput benchmark
2. **Phase 2**: Triangle rendering test (Vulkan + DX12), shader compilation validation
3. **Phase 3**: Meshlet rendering of Sponza scene, GPU culling correctness (compare CPU reference), visibility buffer validation
4. **Phase 4**: Cornell box path tracer convergence test, GI comparison screenshots, shadow map correctness
5. **Phase 5**: TSR quality metrics (PSNR vs native), volumetric cloud visual test
6. **Phase 6**: Asset round-trip test (import → cook → load → render), streaming stress test
7. **Phase 7**: Physics simulation stability test, audio spatialization test
8. **Phase 8**: Editor launch, scene manipulation, undo/redo
9. **Phase 9**: Lua script execution, hot reload test
10. **Phase 10**: Two-client connection test, state sync verification

### Integration Tests

- Full Sponza scene with path tracing at 60fps (RTX 4080+)
- 1M triangle dynamic scene with GPU-driven pipeline
- Stress test: 10K entities with physics + rendering
- Memory leak detection via custom allocator tracking

### Benchmarks

- Frame time breakdown per render pass
- GPU occupancy and bandwidth utilization
- Comparison screenshots vs UE5 (same scene, same lighting)

<a id="ci-cd-quality-gates"></a>
### CI/CD & Quality Gates

- CI matrix:
  - Windows/MSVC + Vulkan
  - Windows/MSVC + DX12
  - Linux/Clang + Vulkan
- Per-PR gates:
  - Build, unit tests, integration smoke tests, static analysis, format/lint checks.
- Nightly gates:
  - Long-running soak test, perf trend tracking, importer/network fuzz suites.
- Artifact pipeline:
  - Store editor build, sample builds, test reports, perf baselines, and crash dumps.
- Regression rules:
  - Fail if perf regresses above threshold (e.g., >5% p95 frame time) on tracked scenes.

---

## Dependencies (vcpkg)

| Library | Purpose |
|---------|---------|
| volk | Vulkan meta-loader |
| VMA | Vulkan Memory Allocator |
| glm | Fallback math (until GA lib complete) |
| meshoptimizer | Meshlet partitioning |
| JoltPhysics | Physics engine |
| sol2 + lua | Scripting |
| imgui (docking) | Editor UI |
| stb_image/stb_image_write | Image I/O |
| cgltf or tinygltf | glTF loading |
| tracy | Profiler |
| spdlog | Logging (initial, replace later) |
| entt | ECS reference (replace with custom) |
| fmt | String formatting |

---

## Implementation Priority Order

**Start with Phase 1 → 2 → 3 (get triangles on screen with GPU-driven pipeline)**
Then Phase 4 (lighting makes it look real), then Phase 5-10 with an explicit dependency graph (not all subsystems are independent).

**Immediate first steps:**

1. Project skeleton + CMake + vcpkg
2. Core types + memory allocators
3. Platform layer (window + input)
4. Vulkan RHI (device, swapchain, command list, pipeline)
5. First triangle on screen
6. Mesh shader pipeline with meshlets
7. GPU culling + visibility buffer
8. PBR materials + direct lighting
9. Path tracing integration
