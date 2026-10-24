# NextGenGameEngine Megaplan

This is the execution roadmap for turning NextGenGameEngine from a broad engine
prototype into a reliable, demonstrable game engine. The long-term vision stays
ambitious, but near-term work is judged by integrated, observable results.

G0 build/test evidence refreshed: **2026-10-24**. Broader feature inventory last
grounded against the repository: **2026-07-11**.

## North star

Ship a small, repeatable 3D experience made entirely through the engine:

1. Open the editor.
2. Import a glTF scene with meshes, materials, and textures.
3. Create and edit entities, lights, and a camera.
4. Save, close, reload, and reproduce the scene exactly.
5. Enter play mode and move a character through the scene.
6. Render stable frames through the Vulkan backend without validation errors.
7. Package and run the result without the source tree.

Until this works, isolated advanced systems are research prototypes rather than
shipped engine features.

## Current state

### Strengths

- A substantial C++20 codebase with clear subsystem boundaries.
- A broad RHI and renderer foundation, including render-graph and GPU-resource
  abstractions.
- A large automated test corpus with more than 1,000 test cases.
- Vulkan, CMake, vcpkg, editor, samples, and cross-platform CI scaffolding.
- PGA-based transforms and a distinctive technical direction.

### Immediate risks

- Important paths remain stubbed or partial: glTF ingestion, GPU uploads,
  scripting, audio, physics integration, serialization, prefabs, and parts of
  Vulkan descriptor handling.
- The README describes more capability than has been demonstrated end to end.
- The old `build/` cache discovers no CTest tests. Fresh G0 build directories
  restore registration; evidence and remaining gate checks are recorded below.
- Hosted baseline and full Windows/Linux CI evidence is recorded in WP-G0-001;
  later source or workflow changes must rerun those checks.
- Runtime logs and diagnostic output at repository root obscure normal state.
- Breadth is growing faster than integration confidence.

## Definition of engine maturity

Every feature is assigned one of these states. Documentation must use these
terms consistently.

| State | Meaning |
|---|---|
| Sketch | Interface, shader, or algorithm exists but is not integrated. |
| Prototype | Works in isolation or under a mock/stub implementation. |
| Integrated | Runs through the real engine path in a sample or editor flow. |
| Verified | Has automated coverage and relevant runtime validation. |
| Shippable | Survives clean build, packaging, documentation, and soak tests. |

Files and class names alone do not advance a feature beyond **Sketch**.

## Program gates

Work proceeds through gates in order. A later phase may be researched, but it
must not consume implementation priority before the current gate passes.

```text
G0 Trustworthy repository
  -> G1 Stable editor triangle
    -> G2 Imported textured scene
      -> G3 Editable persistent world
        -> G4 Playable vertical slice
          -> G5 GPU-driven renderer
            -> G6 Production hardening
              -> G7 Advanced rendering research
```

## Phase 0 — Restore trust in the repository

**Goal:** a fresh clone configures, builds, and tests predictably.

### Work

- Repair repo-sanity drift: either restore the intentionally maintained design
  document or remove the obsolete CI requirement. Do not add a dummy file just
  to satisfy CI.
- Perform a clean Windows debug configure with Vulkan disabled, build all core
  targets, and confirm CTest discovery.
- Perform a full Windows configure with rendering/gameplay manifest features.
- Verify the Linux configuration in CI.
- Diagnose why the current `build/` tree reports no tests; treat regeneration
  as a local action, and fix CMake only if a clean build reproduces the issue.
- Add a small smoke-test label for fast pre-commit checks.
- Ensure generated logs, traces, caches, layouts, and binaries are ignored or
  placed under `build/`/a dedicated runtime-data directory.
- Make README requirements agree with `cmake_minimum_required` and presets.
- Record known-good compiler, Vulkan SDK, and vcpkg baseline versions.

### Exit gate G0

- A clean clone can run configure, build, and CTest using documented commands.
- CI is green on Windows and Linux for Vulkan-off builds.
- The full Windows dependency configuration compiles.
- Repository root remains clean after building and running tests.
- No required CI file is missing.

### WP-G0-001 — Reliable Vulkan-off build and test chain

Outcome: matching baseline CI configurations and CTest runs that fail when no
tests are registered. **G0 remains open.**

In scope: CI-required files, explicit backend/feature/test options, separate
Windows build directories, and blockers reproduced by their test suites.

Reproduction and implementation:

- The historical design document was deliberately deleted. Repo sanity now
  checks the maintained `MASTERPLAN.md` instead.
- `ctest --test-dir build -C Debug --output-on-failure --no-tests=error`
  reproduced zero discovery with exit code 8. Fresh configuration generated
  root test registration without another CMake change.
- The initial baseline run failed `JobSystemTest.ManyJobs` (1,001 executions
  for 1,000 submissions). After the job system moved to owner-thread-only
  Chase-Lev deques, submissions are limited to the main thread or a job. The
  batch regression checks exactly-once execution under that supported path.
- Pending work is counted before a job becomes visible to a worker and reset
  during initialization. The 4,096-job batch regression and `ManyJobs` passed
  in the hosted baseline lanes.
- `AppSmoke.InitUpdateShutdown` expected a Vulkan application in a build with
  Vulkan disabled. It now reports a capability skip before starting the
  application, consistent with the existing RHI smoke test.
- Both glTF fixture tests previously skipped because `tests/fixtures` was
  resolved from the build tree. Their source fixture directory is now supplied
  by the test target, and a missing checked-in fixture fails the test. All three
  importer tests passed in the full dependency configuration; the baseline
  explicitly skips the two tests requiring cgltf.

Verification commands: the G0 commands in `README.md`, with `--parallel 8`
for the local Debug builds. Full dependency verification uses
`build/g0-full`, samples ON, and `rendering;gameplay`, while Vulkan/editor stay
OFF and tests stay ON.

| Check | Local result, 2026-10-24 |
|---|---|
| Required CI files and JSON parsing | Passed |
| Fresh Windows baseline configure/build | Passed |
| Baseline CTest | 1,366 discovered: 1,362 passed, 4 capability skips, 0 failures |
| Full Windows dependency configure | Passed |
| Full Windows build | Passed, including SampleTriangle compilation/linking |
| Full Windows CTest | 1,366 discovered: 1,364 passed, 2 Vulkan skips, 0 failures |
| Hosted Linux baseline CI | 1,361/1,361 passed; [GitHub Actions run](https://github.com/jojo-swe/NextGenGameEngine/actions/runs/40117717697) |
| Hosted Windows baseline CI | 1,366/1,366 passed; [GitHub Actions run](https://github.com/jojo-swe/NextGenGameEngine/actions/runs/40117717697) |
| Hosted full Windows CI | 1,366/1,366 passed; SampleTriangle built; [GitHub Actions run](https://github.com/jojo-swe/NextGenGameEngine/actions/runs/40117717697) |
| Vulkan validation/runtime | Not performed; Vulkan disabled in all listed configurations |

Toolchain: CMake 4.4.4, Ninja 1.13.2, MSVC 19.51.36260, manifest baseline
`e238b4b4a4239829e1fde2c7db2ab668b65ae1dd`. Diagnostics are under
`build/g0-core/` and `build/g0-full/`; no machine-specific tool paths are needed
in tracked configuration. Final suite logs are `build/g0-core/ctest-final.log`
and `build/g0-full/ctest-final.log`. The baseline skips the RHI/application
Vulkan tests and the two cgltf tests; the full configuration skips only the two
Vulkan tests.

Remaining G0 work: smoke labels, generated runtime artifact hygiene, CMake
requirement alignment, and the broader README maturity audit. Application
cleanup after partial initialization failure remains G1 work; skipping
unavailable GPU tests does not verify that path.

Rollback: revert this package's CI, job queue, regression/smoke, fixture wiring,
and documentation changes together. No gate is promoted solely by local CPU
tests.

## Phase 1 — Stabilize the real editor/render loop

**Goal:** the editor reliably opens, renders the existing triangle, resizes,
and shuts down through the real Vulkan path.

### Work

- Convert the current editor initialization trace into scoped diagnostics with
  clear failure messages; remove temporary trace noise once the fault is fixed.
- Verify subsystem initialization and shutdown order under partial failure.
- Validate swapchain creation, resize, minimize/restore, out-of-date recovery,
  and multi-frame resource ownership.
- Enable Vulkan validation in debug builds and classify all messages.
- Complete the descriptor paths actually used by the triangle; quarantine
  unused descriptor experiments rather than pretending they are operational.
- Add smoke coverage for window lifecycle and renderer initialization where a
  headless/mocked boundary is appropriate.
- Add a manual runtime checklist for behavior that CI cannot execute.

### Exit gate G1

- Editor triangle runs for 30 minutes without crash or resource growth.
- Resize/minimize/restore works repeatedly.
- Clean shutdown reports no live engine-owned GPU objects.
- No validation errors occur in the supported triangle path.
- Initialization failure identifies the failing stage and exits cleanly.

## Phase 2 — Build one honest asset-to-pixel path

**Goal:** import and render a small textured glTF scene using engine APIs.

### Work packages

#### 2.1 CPU asset ingestion

- Implement cgltf parsing for nodes, primitives, indices, positions, normals,
  UVs, transforms, and metallic-roughness materials.
- Implement image decoding through stb for the formats supported by the sample.
- Define explicit ownership and error behavior for partially imported assets.
- Add tiny, redistributable fixtures for valid and malformed glTF files.
- Unit-test coordinate conversion, missing attributes, indices, hierarchy, and
  error reporting.

#### 2.2 GPU upload

- Connect imported mesh and texture data to real staging buffers.
- Define resource states and barriers for upload-to-render transitions.
- Replace placeholder handles only along this vertical path.
- Track upload lifetime and deferred destruction across frames.
- Test upload planning without a GPU and validate execution on a Vulkan device.

#### 2.3 Basic mesh rendering

- Introduce a minimal vertex format and indexed draw path.
- Add model/view/projection data and reversed-Z depth.
- Render normals and one directional light before expanding to PBR.
- Add texture sampling and one metallic-roughness material.
- Keep a deliberate fallback/error material for incomplete assets.

#### 2.4 Camera and transforms

- Harden PGA motor composition, decomposition, normalization, and hierarchy.
- Test position/rotation setters for preservation of the other component.
- Verify CPU matrices against shader conventions and handedness.
- Add an orbit camera and editor camera controls.

### Exit gate G2

- A checked-in sample scene loads through the actual importer.
- It renders indexed textured geometry with depth and a movable camera.
- Missing/corrupt assets produce actionable errors, not silent placeholders.
- Vulkan validation reports no errors introduced by the asset path.
- Automated tests cover CPU import and transform behavior.

## Phase 3 — Make the world editable and persistent

**Goal:** create a scene in the editor and reproduce it after restart.

### Work

- Establish stable entity identity and component registration.
- Finish hierarchy traversal, reparenting, dirty propagation, and cycle checks.
- Implement hierarchy, inspector, viewport selection, and transform gizmos.
- Define a versioned scene format with deterministic ordering.
- Serialize entities, transforms, cameras, lights, mesh renderers, materials,
  and parent relationships.
- Implement save, load, Save As, autosave, and recovery behavior.
- Add undo/redo as commands for entity creation, deletion, reparenting, and
  property edits.
- Implement prefab serialization only after ordinary scene round-tripping is
  proven.
- Add golden round-trip tests and migration tests for every format version.

### Exit gate G3

- A user can assemble the G2 sample scene without editing source code.
- Save/reload preserves hierarchy and relevant component data.
- Undo/redo covers the supported editing operations.
- Malformed and older scene versions fail or migrate deterministically.
- Editor panels show real engine state rather than hardcoded placeholders.

## Phase 4 — Deliver a playable vertical slice

**Goal:** prove the engine can support a tiny game loop, not only a renderer.

### Scope

Use one compact test level: a floor, obstacles, a controllable capsule, a few
lights, one animated or moving object, audio, and a scripted interaction.

### Work

- Choose one physics direction: complete the in-house prototype for the slice
  or integrate Jolt. Remove misleading wrapper claims for the unchosen path.
- Connect the character controller to real collision queries and fixed-step
  simulation.
- Implement input actions and editor/game input focus separation.
- Integrate miniaudio for one streamed/decoded sound and spatial playback.
- Integrate Lua/Sol2 for one component lifecycle and one gameplay interaction.
- Add play/stop mode with deterministic restoration of edit-time state.
- Establish fixed timestep, frame interpolation, pause, and single-step rules.
- Add a vertical-slice integration test and a human playtest checklist.

### Exit gate G4

- The scene can be opened, played, stopped, saved, and reopened.
- A player can move, collide, step up, jump, and trigger an interaction.
- One sound and one script run through real third-party integrations.
- Play mode does not corrupt the edit-time scene.
- A packaged build runs on a second machine.

## Phase 5 — Promote the GPU-driven renderer

**Goal:** replace direct per-object submission with measured GPU-driven work.

Only promote systems that the vertical slice consumes.

### Work order

1. GPU scene buffer and stable instance/material identifiers.
2. Mesh registry and persistent geometry buffers.
3. Frustum culling with indirect draw generation.
4. HZB generation and occlusion culling.
5. Meshlet generation and LOD selection.
6. Bindless textures and residency behavior.
7. Clustered lighting and shadowing needed by the sample.
8. Render-graph scheduling, transient allocation, and barrier verification.
9. Profiling and before/after performance evidence.

Each promotion must include shader compilation, a real C++ call site, a sample
that exercises it, and validation/performance evidence. Unwired shaders remain
experimental.

### Exit gate G5

- The vertical slice uses indirect GPU-driven submission.
- CPU submission cost scales sublinearly with visible object count.
- Culling output is visually/debug verifiable and regression-tested.
- Resource/barrier validators agree with Vulkan validation.
- Performance budgets below are met on a named reference machine.

## Phase 6 — Production hardening

**Goal:** make the engine maintainable, diagnosable, and distributable.

### Work

- Add crash reporting, structured logs, GPU breadcrumbs, and recovery context.
- Run ASan/UBSan on Linux and appropriate Windows memory diagnostics.
- Add deterministic test seeds, flaky-test tracking, and test timeouts.
- Establish asset cache keys, dependency tracking, and incremental rebuilds.
- Add packaging, runtime asset layout, configuration, and license attribution.
- Document supported hardware, drivers, OS versions, and feature tiers.
- Add save-format migration policy and compatibility guarantees.
- Add performance capture workflow and regression thresholds.
- Conduct API ownership, threading, lifetime, and error-handling reviews.

### Exit gate G6

- Reproducible package creation works in CI.
- Sanitizers and validation suites are clean.
- A packaged vertical slice completes a multi-hour soak test.
- Documentation lets a new contributor build and debug without tribal knowledge.

## Phase 7 — Advanced rendering and research

**Goal:** selectively turn the project’s advanced sketches into verified
features after the engine foundation is trustworthy.

Candidate tracks include path tracing/ReSTIR/SVGF, virtual texturing, virtual
geometry, virtual shadow maps, terrain, particles, atmospheric rendering, and
specialized post-processing. Promote one track at a time using this ladder:

1. State the user-visible purpose and performance budget.
2. Compile and test shaders in CI.
3. Integrate behind a capability/feature-tier check.
4. Add a minimal sample and fallback path.
5. Validate correctness and GPU synchronization.
6. Measure GPU time, memory, and quality against a baseline.
7. Document limitations before marking it Integrated or higher.

## Cross-cutting workstreams

### Testing pyramid

- **Unit:** math, containers, allocators, serialization, import conversion.
- **Contract:** RHI validators and mock-device behavior.
- **Integration:** asset-to-scene, scene round-trip, upload planning.
- **GPU smoke:** editor startup, triangle, imported scene, resize, shutdown.
- **Golden image:** a small number of tolerant reference-frame comparisons.
- **Soak:** repeated scene reload, resize, play/stop, and long-running render.

Tests must be registered with labels such as `unit`, `integration`, `gpu`, and
`slow` so local and CI suites can select an appropriate tier.

### Performance budgets

Before G2, record the reference CPU/GPU, resolution, driver, and build type.
Track at minimum:

- Editor startup time and first-frame time.
- CPU frame time and GPU frame time at 1080p.
- Draw/dispatch count, visible instances, and triangles.
- GPU allocations, committed bytes, staging usage, and descriptor usage.
- Scene import time and peak CPU memory.
- Package size and runtime asset-cache size.

Budgets are baselines plus explicit regression thresholds, not aspirational
numbers chosen without measurement.

### Documentation

- README describes demonstrated capability and links here for planned work.
- Architecture decisions with durable tradeoffs get short ADRs under `docs/adr/`.
- Every sample documents what it proves and which maturity state it represents.
- Generated API documentation is secondary to working examples.

## Prioritized backlog

### Now — unblock G0

1. Add a focused smoke label with explicit CPU/GPU capability boundaries.
2. Clean runtime artifacts from repository root.
3. Align CMake requirements between README, root CMake, and preset schema.
4. Rewrite README feature claims using the maturity vocabulary.

### Next — reach G1 and G2

1. Finish editor/Vulkan initialization diagnostics.
2. Stabilize resize and shutdown.
3. Fix and test PGA transform preservation.
4. Implement the minimum cgltf CPU importer.
5. Implement staging upload for one mesh and one texture.
6. Render the checked-in glTF sample with depth and camera controls.

### Later

- Scene editing and persistence.
- Play mode and the gameplay vertical slice.
- GPU-driven promotion.
- Packaging and hardening.
- Advanced rendering tracks.

## Work-package template

Every nontrivial change should be cut into a package using this template:

```markdown
### WP-NNN — Short outcome

Outcome: observable behavior after completion.
Depends on: prior WP or gate.
In scope: exact systems/files or behavior.
Out of scope: tempting adjacent work.
Tests first: failing test or reproduction.
Implementation notes: constraints, not a speculative full design.
Verification: exact configure/build/test/runtime commands.
Evidence: logs, screenshot, trace, or benchmark required.
Rollback: how to disable/revert safely.
Documentation: status/readme/ADR updates required.
```

Keep one primary outcome per work package. A package is complete only when its
verification has actually run, or the report explicitly marks an environmental
check as unverified.

## Decision rules

- Prefer completing a real path over adding another subsystem or shader.
- Fix current-gate defects before implementing later-gate features.
- Do not optimize without a captured baseline and a repeatable workload.
- Do not abstract until two real call sites demonstrate the boundary.
- Do not silently fall back from a requested real backend to a stub.
- Delete or quarantine misleading dead experiments only with provenance and
  after proving they have no live call sites.
- Treat validation errors, data corruption, nondeterminism, and resource leaks
  as release blockers for the path that triggers them.
- When documentation and code disagree, code is the current truth and both the
  documentation and plan must be corrected in the same change.

## Success definition

This plan succeeds when NextGenGameEngine is no longer evaluated by how many
systems it names, but by a small game that reliably exercises its importer,
scene, editor, renderer, physics, audio, scripting, tests, and packaging. The
advanced architecture then becomes a platform for measured evolution rather
than a collection of disconnected promises.
