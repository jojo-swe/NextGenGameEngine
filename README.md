# NextGenGameEngine

[![CI](https://img.shields.io/badge/CI-not%20configured-lightgrey)](#roadmap)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/)
[![CMake](https://img.shields.io/badge/CMake-3.28%2B-blue)](https://cmake.org/)

NextGenGameEngine is a from-scratch C++20 3D engine project focused on modern rendering and engine architecture:

- Vulkan-first renderer (DX12 planned)
- GPU-driven pipeline (meshlets, visibility, indirect submission)
- Real-time and reference path tracing tracks
- Custom core systems (memory, containers, math, jobs, ECS)

## Project Status

This repository is in early pre-alpha.

Implemented now:
- Core foundation headers and utilities (`engine/core/*`)
- Logging and memory allocator scaffolding
- CMake + vcpkg build setup
- Sample target: `SampleTriangle`

In progress:
- Platform layer completion
- RHI bring-up and first rendered frame path
- Phase-based execution plan and delivery gates

Master implementation and roadmap document:
- `frolicking-tumbling-pond.md`

## Repository Layout

```text
.
├── engine/                 # Engine library
├── samples/triangle/       # Minimal sample app target
├── tests/                  # GoogleTest target scaffold
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
└── frolicking-tumbling-pond.md
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

Managed by `vcpkg.json`, including:
- Vulkan loader + VMA
- fmt + spdlog
- GTest
- meshoptimizer
- Jolt Physics
- Lua + sol2
- ImGui (docking)

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

## License

Licensed under the MIT License.
See `LICENSE`.
