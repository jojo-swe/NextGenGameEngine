# MASTERPLAN — Work Orders for LLM Workers

This document is the single source of truth for what to work on next in
NextGenGameEngine. It is written to be executed by an LLM coding agent with
**no other context**. Every fact below was verified against the real repository
and real CI history on 2026-07-10.

## How to use this document (read this first)

1. You are assigned **exactly one work package (WP) per session**. Do not start
   a second WP in the same session, even if you finish early.
2. Read the **Global Rules** section, then read **only your assigned WP**.
3. Follow the WP steps in order. Do not improvise extra features.
4. Before claiming "done", run every command in the WP's **Acceptance criteria**
   section and paste the real output in your report.
5. If a step is impossible (file missing, API changed), STOP and report the
   discrepancy instead of guessing. This document may be stale; the code wins.

### Dependency order

```
WP-0 (human only)
  └── WP-1 (CI green)
        ├── WP-2 (transform/PGA fixes)   ─┐
        ├── WP-3 (shader quarantine)      ├── may run in parallel
        └── WP-5 (README honesty)        ─┘
              └── WP-4 (vertical slice: render a glTF mesh)
```

### Status table (update your WP's row in the same PR that completes it)

| WP | Title | Status |
|----|-------|--------|
| WP-0 | Restore GitHub Actions runners (billing) | OPEN — human only |
| WP-1 | Make CI green for the first time | OPEN |
| WP-2 | Fix motor decomposition bug + harden PGA | OPEN |
| WP-3 | Shader inventory honesty pass | OPEN |
| WP-4 | Vertical slice: render one glTF mesh | BLOCKED (needs WP-1, WP-2) |
| WP-5 | Subsystem freeze + honest README | OPEN |

---

## Ground truth (verified 2026-07-10 — do not re-litigate, but do re-verify line numbers)

- CI (`.github/workflows/ci.yml`) has **never been green** on `main`. Two causes:
  - Runs up to 2026-03: every build job failed at the **"Setup vcpkg"** step
    because `actions/checkout@v4` is called **without `submodules: true`**, so
    the `vcpkg/` submodule (pinned at `e99d1b2`) is an empty directory and
    `lukka/run-vcpkg@v11` fails. → fixed by WP-1.
  - Runs from 2026-07 on: all jobs fail in ~3 seconds with `runner_id: 0`
    (no runner ever assigned). This is a GitHub **account billing / spending
    limit** problem. No code change can fix it. → WP-0, human only.
- The PGA math core (`engine/core/math/pga.h`, `pga.cpp`) compiles standalone
  with `clang++ -std=c++20 -mavx2 -mfma` and is numerically correct for basic
  motor rotation (rotating point (1,0,0) by 90° about Z yields (0,1,0)).
- `engine/scene/transform/transform.h` `Transform::SetPosition()` (around line
  25–32) **discards the rotation** of `localMotor` — a known bug marked TODO.
- `engine/core/math/pga.h` has a silently-wrong overload (around line 201):
  `static Motor FromAxisAngle(Line, f32) { return Identity(); }`.
- `Motor::Normalized()` uses `_mm_rsqrt_ps` (≈12-bit precision) — too imprecise
  for a transform that gets composed thousands of times per frame.
- Of 117 HLSL files under `shaders/`, exactly **2 are loaded at runtime**:
  `shaders/mesh/triangle.vert.hlsl` and `shaders/mesh/triangle.frag.hlsl`
  (loaded by `engine/renderer/pipeline/render_pipeline.cpp`, which also probes
  for them to locate the shader root — see its `fs::exists` calls near line 55
  and `createShader` calls near line 538). Both use entry point `main` and
  include no other files. All other shader references in C++ are commented out.
- Subsystems `ai/`, `animation/`, `audio/`, `physics/`, `scripting/` are thin
  stubs (92–315 lines of .cpp each). The real engine is `rhi/` (~28k lines),
  `renderer/` (~8k), `assets/` (~3.6k), `core/` (~1.4k).
- There are ~375 `TODO|FIXME|not implemented|stub` markers in engine code.
- Tests: `tests/CMakeLists.txt` globs `tests/**/*.cpp` into one `EngineTests`
  executable — a new test file is picked up automatically, no CMake edit needed.
- The long-term design document is `frolicking-tumbling-pond.md` (do not delete
  or rename it — CI's repo-sanity job checks it exists). Reality is at its
  Phase 2.2–2.3 (Vulkan backend bring-up), regardless of what else is scaffolded.

---

## Global Rules (apply to every WP)

- **Branch/PR:** one WP = one branch = one PR. Branch off the latest default
  branch. Never push to `main` directly. Never force-push shared branches.
- **Scope:** touch only the files listed in your WP's "Files you may touch".
  If you believe another file must change, report it; do not change it.
- **No new subsystems, no new dependencies, no new third-party libraries.**
- **TODO ratchet:** this command must not return a number larger than before
  your change:
  ```bash
  grep -rE "TODO|FIXME|not implemented|stub" --include="*.cpp" --include="*.h" engine/ editor/ samples/ | wc -l
  ```
- **Conventions** (from CLAUDE.md): namespace `nge`; type aliases `nge::u32`,
  `nge::f32`, etc. from `engine/core/types.h`; PascalCase types, camelCase
  methods/variables; headers use `.h`; C++20, no extensions; match the comment
  style of surrounding code.
- **Build check where possible:** full build needs vcpkg (`VCPKG_ROOT` set,
  `git submodule update --init vcpkg`, then
  `cmake --preset linux-debug -DNGE_ENABLE_VULKAN=OFF -DNGE_BUILD_SAMPLES=OFF`
  and `cmake --build build --config Debug`). If your environment cannot
  bootstrap vcpkg (network-restricted), you MUST at minimum smoke-compile the
  files you touched, e.g.:
  ```bash
  clang++ -std=c++20 -mavx2 -mfma -fsyntax-only -I. <changed .cpp/.h files>
  ```
  and say clearly in your report which level of verification you achieved.
- **Report format:** end with (1) what changed, file by file; (2) acceptance
  criteria commands + their real output; (3) anything you could not verify.

---

## WP-0 — Restore GitHub Actions runners (HUMAN ONLY — not for LLM workers)

Since July 2026, all CI jobs fail in ~3 seconds with no runner assigned
(`runner_id: 0` in the Actions API). This means GitHub is refusing to start
runners for the account — almost always a spending limit hit or a failed
payment.

**Owner action:** github.com → avatar → **Settings → Billing and plans →
Plans and usage → Actions**. Check spending limit and payment method. Then
re-run the latest failed workflow. Done when a workflow run shows runners
actually executing steps (pass or fail).

---

## WP-1 — Make CI green for the first time

**Goal:** all four CI jobs pass on a PR.
**Precondition:** WP-0 done (runners start at all).

**Files you may touch:** `.github/workflows/ci.yml` only.

**Context:** The workflow uses `lukka/run-vcpkg@v11`, which expects the vcpkg
git submodule at `./vcpkg` to be checked out. Every `actions/checkout@v4` step
in the workflow is currently missing `submodules: true`, so the submodule is
empty and "Setup vcpkg" fails. This was the failure on every pre-July run.

**Steps:**
1. Open `.github/workflows/ci.yml`. There are three jobs with a Checkout step:
   `repo-sanity`, `build-and-test` (matrix), `build-and-test-full-windows`.
2. In the two build jobs, change the checkout step to:
   ```yaml
   - name: Checkout
     uses: actions/checkout@v4
     with:
       submodules: true
   ```
   (`repo-sanity` does not build, so it may keep a plain checkout.)
3. Commit, push, open a PR, and watch the workflow run on the PR.
4. If "Setup vcpkg" now passes but Configure/Build/Test fails, fix ONLY
   build-system-level breakage (CMake flags, missing `test -f` files). If the
   failure is a C++ compile error in engine code, report it with the full error
   text instead of attempting a drive-by code fix — that becomes its own WP.

**Acceptance criteria:**
- Screenshot-equivalent evidence (job list + conclusions) that all four jobs
  are green on your PR.

**Forbidden:** editing any C++ file; changing CMake options that alter what
gets built (e.g. do not silently disable tests to get green).

---

## WP-2 — Fix motor decomposition bug + harden PGA

**Goal:** `Transform` setters preserve position AND rotation; remove two
known-wrong Motor APIs; add a real unit-test file for PGA transforms.

**Files you may touch:**
- `engine/core/math/pga.h`, `engine/core/math/pga.cpp`
- `engine/scene/transform/transform.h`, `engine/scene/transform/transform.cpp`
- new file `tests/unit/test_pga_transform.cpp` (picked up automatically by the
  test glob — do not edit CMake)

**Context — the bug** (`engine/scene/transform/transform.h`, ~line 25):
```cpp
void SetPosition(math::Vec3 pos) {
    // Extract current rotation, combine with new translation
    pga::Motor rot = pga::Motor::Rotation({0,0,1}, 0); // TODO: extract rotation from current motor
    // For now, rebuild motor from scratch
    localMotor = pga::Motor::Translation(pos);
    ...
```
Setting position silently deletes the entity's rotation.

**Relevant existing Motor API** (`engine/core/math/pga.h`): layout is
`p1 = (s, e23, e31, e12)` (rotor part) and `p2 = (e0123, e01, e02, e03)`
(ideal part). Available: `Rotation(axis, angle)`, `Translation(v)`,
`Multiply(a, b)` (= b-then-a? — read the comment at its declaration:
"M_combined = M_b * M_a means apply M_a first"), `Apply(Point)`, `Reverse()`,
`Normalized()`, `Log()`, `Exp(Line)`, `Slerp(a,b,t)`, `GetTranslation()`,
`ToMat4()`.

**Steps (test-first):**
1. Create `tests/unit/test_pga_transform.cpp` using Google Test
   (`#include <gtest/gtest.h>`, include `engine/core/math/pga.h` and
   `engine/scene/transform/transform.h`). Write these tests with tolerance
   `1e-4f` before touching any engine code, and confirm the relevant ones FAIL:
   - `RotationOnly`: motor `Rotation({0,0,1}, π/2)` applied to point (1,0,0)
     gives (0,1,0).
   - `TranslationRoundTrip`: for several vectors v, `Translation(v)` applied to
     the origin gives v, and `GetTranslation()` on that motor also gives v.
   - `GetTranslationMatchesApplyOrigin`: for 10 random combined motors
     `Multiply(Translation(t), Rotation(axis, ang))`, `GetTranslation()` equals
     `Apply(Point::Origin()).ToVec3()`. **`Apply(Origin)` is the authoritative
     definition of a motor's translation** — if `GetTranslation()` disagrees,
     `GetTranslation()` is the one that's wrong (its hand-derived formula has
     suspicious sign comments).
   - `SetPositionPreservesRotation`: build a `scene::Transform`; call
     `SetRotation({0,1,0}, π/2)` then `SetPosition({5,0,0})`. Assert
     `GetLocalPosition()` ≈ (5,0,0) AND `GetForward()`… (forward uses
     worldMotor; instead assert via `localMotor.Apply(Point(0,0,-1))` relative
     to `Apply(Origin)`) still points where a 90° yaw sends it, i.e. rotation
     survived. This test MUST fail before your fix.
   - `MotorNormalizationPrecision`: normalize `Rotation({0,0,1}, 0.3f)`
     composed with itself 10,000 times (re-normalizing each iteration); the
     rotor norm `s²+e23²+e31²+e12²` stays within 1e-4 of 1.
   - `SlerpEndpoints`: `Slerp(a, b, 0) ≈ a`, `Slerp(a, b, 1) ≈ b` componentwise
     for a mixed rotation+translation pair.
   - `HierarchyComposition`: parent motor `Translation({10,0,0})`, child local
     `Rotation({0,0,1}, π/2)`; world = `Multiply(parent, childLocal)` applied to
     (1,0,0) gives (10,1,0) — adjust expected value to match `Multiply`'s
     documented operand order if it differs; derive by hand first.
2. Add rotor extraction to `Motor` in `pga.h`: a method
   `Motor RotorPart() const` that returns a motor with the same `p1` and a
   zeroed `p2`, normalized. (The rotor of M = T·R is its p1 part for this
   layout; verify with the tests, not with faith.)
3. Fix `Transform::SetPosition` to
   `localMotor = pga::Motor::Multiply(pga::Motor::Translation(pos), localMotor.RotorPart());`
   and remove the TODO comment. Re-check `SetRotation` for the mirrored bug
   (it rebuilds translation via `GetLocalPosition()` — that one is acceptable).
4. Delete the overload `static Motor FromAxisAngle(Line, f32) { return Identity(); }`
   in `pga.h`. First run `grep -rn "FromAxisAngle" engine/ tests/ samples/ editor/`
   — if any call site passes a `Line`, implement it properly via
   `Exp(line * (angle/2))` semantics instead of deleting; if no call site uses
   it, delete it.
5. Fix `Motor::Normalized()` precision: replace `_mm_rsqrt_ps(dp)` with an
   exact form, e.g. `_mm_div_ps(_mm_set1_ps(1.0f), _mm_sqrt_ps(dp))`.
6. If `GetTranslationMatchesApplyOrigin` fails, reimplement `GetTranslation()`
   as `return Apply(Point::Origin()).ToVec3();` (correct by definition; the
   optimized formula can come back later with tests to guard it).

**Acceptance criteria:**
```bash
# full build available:
ctest --test-dir build -C Debug --output-on-failure -R Pga
# environment without vcpkg — minimum bar, must compile clean:
clang++ -std=c++20 -mavx2 -mfma -fsyntax-only -I. \
  engine/core/math/pga.cpp engine/scene/transform/transform.cpp \
  tests/unit/test_pga_transform.cpp   # (gtest include may fail; then check pga+transform only)
```
- All new tests pass (or, without a test runner, you show the failing-then-fixed
  reasoning and the syntax-only compile passes — say which).
- The TODO at `transform.h` SetPosition is gone; TODO ratchet count decreased.

**Forbidden:** changing the Motor memory layout; touching renderer/RHI code;
"optimizing" anything not listed above.

---

## WP-3 — Shader inventory honesty pass

**Goal:** stop pretending 117 shaders exist. Quarantine the 115 dead ones;
keep the 2 live ones exactly where the engine looks for them.

**Files you may touch:** everything under `shaders/`, plus `README.md`
(shader-count claims only).

**Context:** `engine/renderer/pipeline/render_pipeline.cpp` locates the shader
root by probing `fs::exists(candidate / "mesh" / "triangle.vert.hlsl")` and
loads exactly `shaders/mesh/triangle.vert.hlsl` and
`shaders/mesh/triangle.frag.hlsl` (entry point `main`, no `#include`s). Every
other `.hlsl` reference in C++ is inside a comment. Moving the two live files
breaks the engine; moving the rest breaks nothing.

**Steps:**
1. Verify the live-set claim yourself (the code may have changed):
   ```bash
   grep -rn '\.hlsl"' engine/ samples/ editor/ --include="*.cpp" --include="*.h" | grep -v "^\s*//" 
   ```
   Any shader referenced by live (non-comment) code, plus any file it
   `#include`s (check with `grep -n "#include" <file>`), is LIVE.
2. Create `shaders/experimental/` and `git mv` every non-live shader into it,
   preserving the subdirectory structure (e.g.
   `shaders/lighting/foo.hlsl` → `shaders/experimental/lighting/foo.hlsl`).
3. Add `shaders/experimental/README.md` (5 lines): these files are unreviewed,
   uncompiled design sketches; nothing loads them; promote one out of
   experimental only together with the C++ change that loads it and a CI
   compile check.
4. Update `README.md`: replace any "117 shaders" style claim with the honest
   split (N live under `shaders/`, M sketches under `shaders/experimental/`).
5. Do NOT edit any C++ file. If step 1 reveals more live shaders than the two
   listed, keep them (and their includes) in place and report the difference.

**Acceptance criteria:**
- `grep -rln "experimental" engine/ --include="*.cpp"` returns nothing (no code
  points into the quarantine).
- The two live shaders still exist at their original paths:
  `test -f shaders/mesh/triangle.vert.hlsl && test -f shaders/mesh/triangle.frag.hlsl && echo OK`
- Full build (or, minimum bar, CI on your PR) still passes — shaders are not
  compiled by CMake today, so this mainly guards against accidental file moves.

**Forbidden:** deleting any shader; editing shader contents; touching C++.

---

## WP-4 — Vertical slice: render one glTF mesh (the milestone that matters)

**Goal:** replace the hardcoded demo triangle with one glTF mesh loaded through
the engine's own asset pipeline, rendered with depth, with an orbiting camera
driven by PGA motors. This is the first end-to-end proof of
assets → RHI → renderer → scene.

**Preconditions:** WP-1 merged (CI green) and WP-2 merged (transforms
trustworthy). **This WP needs a machine with a Vulkan 1.3 GPU/driver** — it
cannot be fully verified in CI (CI builds with `NGE_ENABLE_VULKAN=OFF`). If you
have no GPU, do the code work, keep it compiling, and mark the runtime check
as unverified in your report.

**Files you may touch:** `samples/` (new sample or extend the existing one),
`engine/renderer/pipeline/render_pipeline.cpp` (demo-geometry section only),
`engine/assets/gltf_importer.*`, `engine/assets/mesh/mesh_loader.*`,
`shaders/mesh/` (new mesh shader pair is allowed), plus a small test asset.

**Anchors in the existing code:**
- Demo triangle setup: `engine/renderer/pipeline/render_pipeline.cpp` — shader
  loading near line 538 (`createShader(shaderRoot / "mesh" / ...)`), shader-root
  probing near line 55.
- Asset entry points: `engine/assets/gltf_importer.h` (`class GLTFImporter`,
  cgltf-based) and `engine/assets/mesh/mesh_loader.h` (`MeshLoader`,
  `ProcessNode` / `ProcessPrimitive`).
- Transforms/camera: `engine/scene/transform/transform.h` (PGA motors),
  `engine/scene/camera/`.

**Steps (milestone granularity — decompose further yourself):**
1. Add a small CC0/self-made `.glb` (a cube or the glTF-Sample-Models Box) under
   `samples/assets/`. Keep it under 100 KB. Record its provenance in the sample's
   README.
2. Load it via `GLTFImporter`/`MeshLoader` into CPU-side mesh data; log vertex
   and index counts.
3. Create vertex/index buffers through the existing RHI device interface
   (`engine/rhi/common/rhi_device.h`) the same way the demo triangle's
   resources are created; add a `mesh.vert.hlsl`/`mesh.frag.hlsl` pair modeled
   on the triangle shaders (positions + normals, simple N·L shading is enough —
   no PBR yet).
4. Drive the camera from a `scene::Transform` whose `localMotor` is rebuilt
   each frame as orbit = `Multiply(Rotation({0,1,0}, t), Translation({0,0,r}))`;
   upload `ToMat4()` as the view matrix. Keep the existing reversed-Z depth
   setup.
5. Run with Vulkan validation layers enabled; fix every validation error you
   introduced.

**Acceptance criteria:**
- Sample builds in the full-features configuration.
- Runtime (GPU machine): window shows the rotating mesh; zero validation-layer
  errors attributable to the new code; a screenshot is attached to the PR.
- The glTF path is real: deleting the `.glb` makes the sample fail with a clear
  error (no silent fallback to the hardcoded triangle).

**Forbidden:** adding PBR/materials/textures/lighting systems; touching
culling, render graph, or any Phase-3+ feature; adding dependencies.

---

## WP-5 — Subsystem freeze + honest README

**Goal:** the README stops overstating the project, and growth is redirected
into the render core.

**Files you may touch:** `README.md` only.

**Steps:**
1. Add a "Project status" section near the top with a table of subsystems and
   one of three states, based on the ground-truth section above:
   `working` (core/math PGA, RHI Vulkan bring-up, shader hot-reload, demo
   triangle), `bring-up` (renderer, assets, scene), `frozen stub` (ai, network,
   scripting, audio, physics wrapper, animation).
2. State the freeze rule verbatim: "Frozen subsystems accept bug fixes only.
   No new files or features in a frozen subsystem until the vertical slice
   (MASTERPLAN WP-4) has shipped."
3. Remove or reword any claim the ground truth contradicts (shader counts,
   feature-complete phrasing, "rivaling UE5" presented as current fact rather
   than vision).
4. Link to `MASTERPLAN.md` and `frolicking-tumbling-pond.md` as, respectively,
   the near-term work orders and the long-term design.

**Acceptance criteria:** README contains the status table, the freeze rule, and
no claims contradicting the ground-truth section of this file.

**Forbidden:** touching anything except `README.md`.

---

## After WP-4 ships

Do not invent the next phase. The sequence continues from
`frolicking-tumbling-pond.md` Phase 3.3 (GPU-driven culling & draw submission):
promote `frustum_cull.hlsl` / `hiz_build.hlsl` out of `shaders/experimental/`,
wire them into `engine/renderer/pipeline/indirect_cull_pipeline.cpp` (the
loading call is currently commented out at its line ~58), and write the next
MASTERPLAN revision with the same WP structure — ground truth first, one WP per
session, acceptance criteria that a machine can check.
