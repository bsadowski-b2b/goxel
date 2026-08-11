# GPU Upgrade Specification — Goxel XCom Fork

**Document version:** 3.3.1  
**Status:** All five phases and View Frames enhancement passed; final release approved  
**Target:** Goxel XCom Fork (C99 / C++17), Apple M1 primary development machine  
**Last updated:** 2026-07-24  

---

## 1. Purpose

Upgrade the fork incrementally so large voxel scenes edit and render more
smoothly, without risking file compatibility, undo/redo, tool behavior, or the
existing renderer. Each phase is independently buildable, can be disabled at
runtime, and ends with hands-on acceptance testing by the owner. Work does not
advance to the next phase until the owner explicitly approves the current one.

This document replaces the earlier all-at-once OpenGL 4.3/Vulkan/DXR proposal.

## 2. Findings and Changes to the Original Proposal

### 2.1 Repository findings

- The current macOS GLFW setup does not request a modern OpenGL core context.
- macOS does not expose OpenGL 4.3 compute shaders or SSBOs; the previous
  OpenGL 4.3 compute design cannot be the primary path on the Apple M1 target.
- Rendering is already cached per 16×16×16 tile and invalidated using the tile
  plus its 26 neighbors. CPU vertex generation and `glBufferData` happen on a
  cache miss in `src/render.c`.
- The CPU volume is part of the document model used by tools, history,
  serialization, layer composition, selection, and the CPU path tracer. Making
  the GPU copy authoritative immediately would create substantial correctness
  and readback risk.
- Picking already has a GPU-rendered ID-buffer path. It should be measured
  before introducing a replacement DDA picker.
- Marching cubes and block-face rendering are different workloads and require
  separate kernels and acceptance tests.

### 2.2 Revised technical direction

1. Keep the CPU sparse volume authoritative through the initial GPU phases.
2. Add a small backend-neutral acceleration interface, but implement only the
   backends justified by supported platforms:
   - **Metal compute on macOS** (primary GPU acceleration path).
   - **Existing OpenGL renderer / CPU mesher** (mandatory fallback).
   - Other backends are future proposals, not part of this approval.
3. Mirror only dirty tiles and their one-tile halo to GPU memory.
4. Generate per-tile geometry on the GPU and retain the existing visual shader
   path initially, minimizing rendering changes.
5. Add bounds checks, overflow reporting, deterministic validation, telemetry,
   and a one-click/runtime fallback before performance work.
6. Treat GPU tools and path tracing as optional later phases. They must not
   block the core meshing upgrade.

### 2.3 Explicitly removed or deferred claims

- No fixed promise of 10 million voxels, sub-2 ms edits, or 60 FPS path tracing
  until baseline measurements establish meaningful scene-specific targets.
- No “zero-copy” claim on Apple Silicon. Unified memory can reduce transfers,
  but synchronization and ownership costs still exist.
- No Vulkan 1.3, DXR, WebGPU, sparse virtual textures, hardware ray tracing,
  or SVGF in the initial project scope.
- No fixed 128³ page table. Goxel supports sparse coordinates, including
  negative positions; a dense fixed-origin table would waste memory and impose
  an artificial world boundary.
- No direct GPU mutation of the saved document model until undo, serialization,
  scripting, selection, and CPU/GPU reconciliation are designed and proven.

---

## 3. Non-Negotiable Requirements

### 3.1 Correctness and compatibility

- Existing `.gox` files load and save without format changes.
- Undo/redo, layers, selection, symmetry, materials, grid/edge/wireframe modes,
  marching cubes, smoothness, transparency, and exports retain current behavior.
- Grid Lines, Edges, and Frames are independent persisted View effects. Grid
  Lines include the voxel surface grid and fine canvas-volume face grids.
  Frames include an adjustable measurement lattice on the canvas-volume faces
  and matching frame divisions projected onto model surfaces.
- A CPU/OpenGL fallback remains available for every accelerated feature.
- GPU failures never corrupt the document. On initialization, allocation,
  shader compilation, validation, or overflow failure, the affected operation
  falls back to the CPU path and records a diagnostic.
- GPU resources are released and rebuilt safely across context/device changes.

### 3.2 Measurement

Every performance result must record:

- commit/build mode;
- machine, OS, GPU, window resolution, and renderer/backend;
- test scene and active tile/voxel counts;
- median, 95th percentile, and worst-case frame/edit times;
- CPU meshing, upload, GPU compute, synchronization, and draw time separately;
- memory use and generated vertex/index counts.

Warm-cache and cold-cache results must not be mixed.

### 3.3 Validation mode

A developer toggle runs CPU and GPU meshing for the same dirty tiles and
compares canonicalized output:

- triangle/quad count;
- bounds;
- position, normal, color/material, and required effect attributes;
- optional order-independent hash, because parallel GPU output order may vary.

On mismatch, the application logs the tile coordinate and effect flags, uses
the CPU result, and can export a compact reproduction fixture.

### 3.4 Resource safety

- All GPU buffers have explicit capacities and checked allocations.
- Kernels never write beyond capacity.
- Overflow sets a flag/counter, discards incomplete GPU output, and falls back
  to CPU meshing for the affected tile.
- Buffer growth uses measured high-water marks and a documented maximum budget.
- Synchronization points and buffer ownership are explicit.

---

## 4. Proposed Architecture

### 4.1 Acceleration boundary

Add a narrow internal interface; avoid presenting unsupported functions as
general engine capabilities.

```c
typedef enum {
    GPU_BACKEND_NONE = 0,
    GPU_BACKEND_METAL,
} gpu_backend_t;

typedef struct gpu_accel gpu_accel_t;

typedef struct {
    bool available;
    bool mesh_blocks;
    bool mesh_marching_cubes;
    bool tool_preview;
    bool pathtrace_preview;
    const char *device_name;
} gpu_capabilities_t;

gpu_accel_t *gpu_accel_create(void);
void gpu_accel_destroy(gpu_accel_t *accel);
gpu_capabilities_t gpu_accel_get_capabilities(const gpu_accel_t *accel);

bool gpu_accel_upload_tiles(
    gpu_accel_t *accel, const volume_t *volume,
    const int (*tile_positions)[3], int tile_count);

bool gpu_accel_mesh_tiles(
    gpu_accel_t *accel, const gpu_mesh_request_t *request,
    gpu_mesh_result_t *result);
```

Exact names may change during Phase 1, but the boundary must remain small and
the existing CPU functions must remain callable.

### 4.2 Sparse tile mirror

- Map signed tile coordinates to compact GPU pool slots on the CPU.
- Store RGBA8 voxel payloads using the current `TILE_SIZE == 16`.
- Maintain a compact coordinate-to-slot lookup table for the active dispatch,
  rather than a global dense page table.
- Upload changed tiles plus the 26-neighbor halo required for face visibility,
  ambient occlusion, smoothing, and marching-cubes boundaries.
- Track tile generation IDs so unchanged data is not uploaded.
- Evict pool slots with an explicit policy when the memory budget is reached.

### 4.3 Meshing

Implement separate Metal compute pipelines:

1. **Block-face mesher:** expose faces and emit the exact attributes required by
   `voxel_vertex_t`. First target behavioral parity, not greedy merging.
2. **Compaction/count pass:** count or prefix-sum output before final emission,
   avoiding unsafe blind atomics into a fixed buffer.
3. **Marching-cubes mesher:** separate optional pipeline after block meshing is
   validated. Tables are immutable GPU buffers; neighbor/halo behavior must
   match the CPU implementation.

Greedy quad merging is an optimization after parity. It must be benchmarked
because 16³ tiles are small and scan/merge overhead may exceed its savings.

### 4.4 Render integration

Phase 3 may copy Metal-generated vertices into an OpenGL VBO if required by the
current renderer. A direct Metal renderer is out of initial scope. The measured
cost of interop/copy determines whether that architecture remains worthwhile.
No indirect-draw requirement is imposed until profiling shows draw-call
submission is a significant bottleneck.

### 4.5 Tools and document ownership

The CPU sparse volume remains authoritative. GPU tool work initially accelerates
preview/evaluation only. The existing CPU operation commits the final edit so
undo/redo, save/load, scripting, selection, and exports remain correct.

Moving document authority to GPU would require a separate approved
specification covering command logging, dirty readback, history snapshots,
memory pressure, and recovery.

---

## 5. Phased Implementation and Owner Test Gates

The owner performs the listed hands-on tests at each gate. The implementer
supplies a build, exact test instructions, baseline/result table, known issues,
and a fallback/revert toggle. **No next phase begins without explicit owner
approval.**

### Phase 1 — Baseline, Capability Layer, and Safety Harness

**Goal:** Establish reliable measurements and a disabled-by-default GPU
foundation without changing rendered output.

**Implementation**

- Create repeatable small, medium, and stress `.gox` benchmark scenes or a
  deterministic scene generator.
- Instrument cache hit/miss, CPU meshing, buffer upload, draw, edit-to-frame
  latency, frame time, memory, tile count, and generated geometry count.
- Log actual OpenGL context/version and Metal device/capabilities.
- Add the narrow `gpu_accel` lifecycle/capability layer and runtime setting:
  `Off`, `Auto`, and `Validation`.
- Add allocation/error/overflow diagnostics and graceful fallback hooks.
- Record reference screenshots and geometry statistics for all render effects.
- Add automated unit tests for coordinate-to-slot mapping, dirty-tile halo
  expansion, capacity arithmetic, and fallback state transitions.

**Owner test gate**

1. Open, edit, undo/redo, save, reopen, and export a known project.
2. Exercise block, marching-cubes, grid, edge, wireframe, transparent, and
   smoothness modes.
3. Run each benchmark scene and confirm the report is produced.
4. Switch `Off`, `Auto`, and `Validation`; output must remain visually identical.
5. Confirm no crashes or new visible regressions during a normal editing session.

**Pass criteria**

- Clean debug and release builds.
- Existing automated tests pass.
- Reference files round-trip successfully.
- Baseline report is complete and repeatable within a documented tolerance.
- GPU-off behavior matches the pre-phase reference.
- Owner explicitly approves Phase 1.

### Phase 2 — Dirty-Tile Tracking and Transfer/CPU Pipeline Optimization

**Goal:** Reduce unnecessary CPU work and uploads, and prove the sparse mirror
before writing a GPU mesher.

**Implementation**

- Make dirty-tile and neighbor invalidation explicit and observable.
- Retain the existing shared CPU staging buffer and generation-keyed render
  cache; avoid redundant mirror uploads when tile generations are unchanged.
- Implement the Metal sparse tile mirror, generation tracking, memory budget,
  and eviction, but do not use it to render.
- Add CPU/GPU mirror checksum sampling in Validation mode.
- Add stress tests for negative coordinates, distant sparse tiles, tile
  boundaries, rapid undo/redo, layer changes, and allocation failure.

**Implemented 2026-07-24**

- A frame-level observer synchronizes the authoritative merged render volume
  without changing document ownership or the CPU/OpenGL rendering path.
- The Metal mirror stores 16×16×16 RGBA tiles in a bounded 64 MiB shared
  buffer, maps sparse signed tile origins to slots, and evicts least-recently
  used slots at capacity.
- Tile generation IDs identify new or changed content. Deleted tiles are
  removed, and every dirty origin expands to a deduplicated 3×3×3 tile region
  so all 26 neighbors are explicitly observed and refreshed when present.
- `Validation` hashes each uploaded CPU tile and its Metal-buffer copy and
  reports mismatches without modifying the document or rendered result.
- The Debug panel and `GPU_BASELINE` report expose source/mirrored tile counts,
  dirty/halo counts, upload volume, capacity/use, removals, evictions, and
  checksum failures.
- Settings exposes `Off`, `Auto`, and `Validation` in a dedicated GPU
  Acceleration section and applies mode changes immediately.
- Automated tests cover halo deduplication, boundary overlap, negative tile
  coordinates, undersized output capacity, and checked allocation arithmetic.
- Phase 2 deliberately does not add GPU meshing or route rendering through
  Metal; those remain gated behind Phase 3 acceptance.

**Owner test gate**

1. Repeat Phase 1 functional tests.
2. Paint continuously across tile boundaries and at negative coordinates.
3. Edit several layers, toggle visibility, undo/redo rapidly, save, and reopen.
4. Load the stress scene and edit for at least ten minutes in `Auto` and `Off`.
5. Compare the supplied before/after latency and memory report.

**Pass criteria**

- No checksum mismatch or document/visual regression.
- No unbounded memory growth over the stress session.
- No statistically meaningful performance regression in `Off` mode.
- At least one measured bottleneck is improved, or results justify proceeding
  without speculative claims.
- Owner explicitly approves Phase 2.

### Phase 3 — Metal Block Meshing with CPU Validation and Fallback

**Goal:** Accelerate the normal block renderer on Apple Silicon while retaining
the existing CPU renderer as a correctness oracle and fallback.

**Implementation**

- Implement count/compaction and block-face emission kernels.
- Emit all attributes needed by current lighting, occlusion, materials, grid,
  edges, and picking.
- Integrate per-tile GPU results with the existing render cache.
- Add overflow detection, timeout/error handling, and per-tile CPU fallback.
- Validation mode compares CPU and GPU geometry for every dirty tile.
- After block parity, prototype greedy merging only if profiling predicts a
  net benefit.

**Authorized 2026-07-24**

- Phase 3 implementation was explicitly authorized after all Phase 2 owner
  tests passed.
- The initial audit confirmed that parity requires reproducing the complete
  36-byte `voxel_vertex_t` record, including gradient, border/occlusion atlas
  coordinates, picking data, color, normal, and tangent—not merely face
  positions.
- Because Metal thread scheduling does not guarantee CPU traversal order,
  Validation will compare canonicalized per-quad records rather than raw
  output-buffer order.
- The first implementation checkpoint adds a 4,096-thread Metal face-count
  pass over each 16³ block tile, including its 18³ neighbor input. Validation
  compares the GPU count with the existing CPU mesher and records mismatch,
  overflow, fallback, attempt, success, and timing diagnostics. Rendering still
  uses CPU vertices until the full attribute-emission pass reaches parity.
- The second checkpoint emits bounded packed face descriptors
  `(x, y, z, face)` directly from Metal. Validation sorts the nondeterministic
  GPU stream and compares it with the CPU mesher's picking descriptors, so
  missing, duplicate, or incorrect faces are detected independently of thread
  order. Output beyond the 24,576-face physical tile maximum is rejected.
- The completed Metal kernel emits the full 36-byte vertex record for each
  visible block face: position, normal, tangent, gradient, color, picking data,
  UV, occlusion-atlas coordinates, and border/bump-atlas coordinates.
- `Auto` renders block tiles from Metal output. `Validation` renders the CPU
  result, canonicalizes CPU and Metal quads by picking descriptor, and compares
  every byte. Marching cubes remains on its unchanged CPU path.
- Metal initialization, command, capacity, or validation failure falls back to
  the existing CPU mesher for the affected tile. A runtime parity check on the
  Apple M1 completed without mismatches after correcting atlas texel scaling.
- Changing GPU mode now clears cached tile vertex buffers so the selected
  backend rebuilds tiles immediately; this prevents CPU-built cache entries
  from masking an `Auto` or `Validation` mode change.

**Owner test gate**

1. Repeat all Phase 1 visual modes on the reference scenes.
2. Paint/add/subtract/replace across tile boundaries with symmetry enabled.
3. Test selection and picking at scene edges and on rapidly edited geometry.
4. Compare `Off` and `Auto` screenshots from several camera angles.
5. Run cold-cache and continuous-edit benchmarks; observe responsiveness.
6. Force the documented overflow/failure test and confirm seamless CPU fallback.

**Pass criteria**

- No unresolved validation mismatch.
- Pixel differences are zero or documented and explicitly accepted.
- No save/history/tool behavior changes.
- `Auto` shows a meaningful measured improvement on at least the medium and
  stress scenes without making the small scene materially worse.
- Forced failures recover without crash or document corruption.
- Owner explicitly approves Phase 3.

### Phase 4 — Marching Cubes, Picking, and GPU Tool Preview

**Goal:** Extend acceleration to expensive optional workflows without changing
document authority.

**Implementation**

- Add the separate Metal marching-cubes pipeline with boundary validation.
- Profile existing ID-buffer picking; change it only if it is a demonstrated
  bottleneck or correctness problem.
- Add GPU preview/evaluation for the highest-cost brush/tool operations found
  by Phase 1 metrics. Final document commits remain on CPU.
- Ensure previews and committed results use the same parameters and semantics.
- Validate selection, clipping, symmetry, alpha/blend modes, and materials.

**Implemented 2026-07-24**

- A separate bounded Metal kernel now emits the standard smooth
  marching-cubes triangle stream in the existing 36-byte vertex format.
  `Auto` uses the Metal output; `Validation` canonicalizes CPU and GPU
  triangles and compares positions, colors, and normals before retaining the
  CPU result.
- The kernel reads an 18³ voxel neighborhood for each 16³ tile, preserving the
  CPU algorithm's one-voxel boundary halo. Atomic capacity checks reject
  overflow and route that tile through the CPU mesher.
- Automated parity checks cover colored and transparent voxels, negative
  coordinates, and surfaces crossing X, Y, and Z tile boundaries.
- The existing non-smooth marching-cubes mode intentionally remains on its CPU
  path. It uses a separate flat-color topology-splitting algorithm and is not
  treated as equivalent to standard smooth marching cubes.
- The existing ID-buffer picker was retained after audit: it already renders
  position/face/tile data on the GPU and caches the buffer until volume,
  camera, render settings, or viewport changes. Diagnostics now measure cache
  hits, ID-buffer render time, one-pixel readback count, and readback time so a
  future replacement requires evidence.
- Interactive tool volumes already pass through the same render-layer and
  accelerated tile-meshing path as ordinary geometry. Phase 4 records active
  preview frames and advertises this capability without moving final edits,
  history, serialization, selection, or exports away from the CPU.
- The Debug panel and `GPU_BASELINE` log now report aggregate Metal mesh
  attempts/successes/fallbacks/mismatches/overflows/time, picking measurements,
  and GPU-assisted tool-preview frames.
- macOS now uses its Retina framebuffer and requests 4× multisample
  antialiasing. Runtime display scaling is derived from the window's actual
  framebuffer-to-window ratio rather than the primary monitor's nominal scale,
  keeping grid lines physically consistent between displays and avoiding
  doubled line thickness in a non-Retina framebuffer.
- CPU fallback remains per tile for Metal initialization, command, allocation,
  validation, or capacity failure.

**Owner test gate**

1. Compare block and marching-cubes results across all smoothness settings.
2. Inspect seams at tile boundaries and after edits on either side of a boundary.
3. Test brush, box, sphere, paint, subtract, replace, extrude, noise, symmetry,
   clipping, selection, and undo/redo where available.
4. Verify the preview exactly matches the committed result.
5. Test picking on empty space, thin features, transparent voxels, and large scenes.
6. Repeat save/reopen/export checks.

**Pass criteria**

- No cracks, missing surfaces, unstable picking, or preview/commit mismatch.
- CPU/GPU validation passes for block and marching-cubes modes.
- Tool-preview rendering uses the accelerated mesher without changing the
  preview/commit result; diagnostics show whether scene-specific latency merits
  deeper per-tool compute work.
- CPU fallback remains complete.
- Owner explicitly approves Phase 4.

### Phase 5 — Optional GPU Path-Traced Preview and Release Hardening

**Goal:** Add a bounded Metal compute path-traced preview only after editing and
meshing are stable, then harden the feature for normal use.

**Implementation**

- First benchmark the existing Yocto CPU path tracer and define accepted visual
  reference scenes.
- Prototype sparse-grid DDA in Metal compute using the tile mirror.
- Implement progressive accumulation reset rules for camera, lighting, material,
  layer, and voxel changes.
- Start with direct lighting and a small bounded bounce count. Add denoising
  only after the undenoised reference is correct and profiling justifies it.
- Add cancellation, resolution scaling, sample/bounce controls, memory limits,
  and CPU renderer fallback.
- Complete diagnostics, settings UI, documentation, and a cross-feature
  regression matrix.

**Authorized and implemented for owner testing 2026-07-24**

- The existing final renderer remains the asynchronous Yocto CPU path tracer.
  Its audit confirmed cancellation and camera/scene reset support, but exposed
  missing material-change invalidation; material hashes now participate in
  scene rebuild decisions.
- The Render panel now offers an explicit **Metal Direct Preview** alongside
  **CPU (Yocto)**. It uses sparse voxel DDA over the existing bounded tile
  mirror, an open-addressed signed-coordinate tile table, progressive
  sub-pixel sampling, ambient/direct lighting, and a bounded hard-shadow ray.
- Metal preview controls expose 25–100% resolution and 64–4096 maximum ray
  steps. Output is capped at 2048². Accumulation plus output memory is bounded
  to about 80 MiB at the cap, in addition to the existing 64 MiB sparse mirror.
- Camera, viewport, voxel/layer, material, world, light, renderer, resolution,
  sample, ray-step, and GPU-mode changes reset accumulation. CPU work is
  cancelled when switching to Metal; Stop/Restart remains available.
- Metal command, allocation, capability, mirror, or bound failure activates
  Yocto CPU fallback without modifying the document.
- Automated runtime tests compile and execute the Metal kernel, exercise a
  colored voxel at a negative coordinate, and verify nonempty RGBA output.
- Debug and `GPU_BASELINE` diagnostics report attempts, completed frames,
  resets, fallbacks, GPU time, allocation size, backend, dimensions, samples,
  and ray-step limit.
- The initial prototype deliberately does not claim final-render parity.
  Materials, emissive surfaces, sky synthesis, floor geometry, marching-cubes
  surfaces, indirect bounces, and denoising remain Yocto-only. Bounce controls
  and denoising are deferred until owner testing shows the direct preview is
  useful enough to justify expanding it.

**Owner test gate**

1. Compare reference scenes against the CPU path tracer for geometry, materials,
   lighting, shadows, and emissive voxels.
2. Move the camera and edit while previewing; accumulation must reset correctly.
3. Change layers, materials, lighting, resolution, samples, and bounce count.
4. Cancel and restart repeatedly; return to normal editing.
5. Run an extended stability session and compare performance/memory reports.
6. Disable all acceleration and confirm the original workflows still operate.

**Pass criteria**

- No stale accumulation, missing updates, crashes, or unbounded memory growth.
- Accepted visual quality at explicitly recorded settings.
- Performance targets are based on measured hardware results, not fixed promises.
- Documentation and known limitations are complete.
- Owner explicitly approves final release candidate.

### Post-Phase View Enhancement — MagicaVoxel-Style Frames

**Goal:** Separate the canvas/model measurement frame from ordinary voxel grid
lines and make both effects useful for reading the fixed editing volume.

**Implemented for owner testing 2026-07-24**

- View → Effects now exposes independent **Grid Lines**, **Edges**, and
  **Frames** checkboxes.
- Grid Lines retain the per-voxel grid projected over model surfaces and now
  add a fine one-voxel grid to the camera-visible faces of the canvas volume.
  Every eighth canvas line is slightly stronger for orientation.
- Frames render the canvas-volume outline and a stronger measurement lattice
  on its camera-visible faces. The same frame divisions are projected over
  model surfaces, aligned to the canvas-volume origin.
- **Frame Spacing** is adjustable from 1–64 voxels and defaults to 8.
- All three toggles and frame spacing persist in `settings.ini`.
- The implementation uses the existing OpenGL presentation path, so it is
  independent of Off, Auto, or Validation GPU acceleration mode and does not
  alter voxel data, `.gox` files, undo/redo, selection, or exports.
- The shader variant compiled and launched successfully on Apple M1 with
  Frames enabled. Final visual acceptance remains an owner test.

**Owner test gate**

1. Toggle Grid Lines, Edges, and Frames individually and in every combination.
2. Confirm Grid Lines appear on both the model and visible canvas-volume faces.
3. Confirm Frames appear on the canvas-volume faces and align across the model.
4. Change Frame Spacing to 1, 4, 8, 16, 32, and 64; check alignment after
   rotating, zooming, editing, and moving voxels across the canvas origin.
5. Quit and reopen; confirm the three toggles and spacing are restored.
6. Repeat with GPU acceleration Off and Validation and confirm identical output.

**Pass criteria**

- Each toggle changes only its own effect.
- Canvas grids remain aligned with voxel coordinates on all visible faces.
- Projected frame divisions remain aligned with the canvas lattice.
- No flicker, severe z-fighting, crashes, or unexpected editing/file changes.
- Owner explicitly approves the enhancement.

---

## 6. Benchmark and Acceptance Matrix

Phase 1 fills numerical targets after baselining.

| Scenario | Primary measure | Baseline | Phase target | Result |
|---|---:|---:|---:|---:|
| Small scene, warm cache | p95 frame time | TBD | No regression > agreed tolerance | TBD |
| Medium scene, cold cache | Time to first complete frame | TBD | Set after Phase 1 | TBD |
| Medium continuous brush | p95 edit-to-frame latency | TBD | Set after Phase 1 | TBD |
| Stress scene rotation | p95 frame time | TBD | Set after Phase 1 | TBD |
| Marching cubes edit | p95 remesh latency | TBD | Set after Phase 1 | TBD |
| GPU direct preview | Time to recognizable composition/lighting preview | Owner test pending | Compare at 640×480-equivalent, 32 samples | TBD |
| Ten-minute edit session | Peak/stable memory | TBD | Bounded; no upward leak trend | TBD |

Correctness gates are absolute unless the owner explicitly approves a documented
exception. Performance gates are hardware- and scene-specific.

---

## 7. Files Expected to Change

Likely initial touch points:

| Area | Expected role |
|---|---|
| `src/render.c`, `src/render.h` | Instrumentation, cache integration, fallback |
| `src/volume.c`, `src/volume.h` | Generation/dirty-tile access without exposing internals unnecessarily |
| `src/volume_to_vertices.c` | CPU reference mesher and validation support |
| `src/marchingcube.c` | CPU reference and immutable tables |
| `src/main.c` | Capability logging and backend lifecycle |
| `SConstruct` | Objective-C++/Metal source and framework integration |
| New `src/gpu_accel.*` | Backend-neutral internal acceleration boundary |
| New macOS backend files | Metal device, buffers, pipelines, synchronization |
| New `.metal` shaders | Block meshing; later marching cubes/tools/path preview |
| `src/tests.c` or focused test files | Mapping, invalidation, overflow, fallback, regression fixtures |

The exact list is refined in Phase 1. Avoid broad changes to tools, formats, or
history until their phase is approved.

---

## 8. Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Metal/OpenGL interop or copy erases compute gains | Measure a minimal Phase 3 prototype before expanding scope; retain CPU path |
| Parallel output differs only in ordering | Compare canonical/order-independent geometry in Validation mode |
| Tile-boundary artifacts | Always include required halo; dedicated seam fixtures |
| Output buffer overflow | Count/scan before emit, explicit capacities, checked fallback |
| GPU/CPU volume divergence | CPU remains authoritative; generation IDs and checksum sampling |
| Unified-memory synchronization stalls | Instrument waits; batch work; avoid unnecessary readback |
| Small scenes become slower | Size threshold selects CPU path; benchmark warm and cold cases |
| Feature combinations regress | Reference matrix covers effects, layers, tools, and file round-trips |
| Backend scope expands uncontrollably | Metal only for this project; other platforms require separate approval |
| Path tracing delays core upgrade | Phase 5 is optional and isolated |

---

## 9. Approval Record

| Phase | Build/commit | Test report | Owner decision/date |
|---|---|---|---|
| 1 — Baseline and harness | Working tree build, 2026-07-24 | `docs/gpu-phase-1-test-guide.md` | Passed and approved by owner 2026-07-24 |
| 2 — Dirty tiles and mirror | Working tree build, 2026-07-24 | All owner tests passed using `docs/gpu-phase-2-test-guide.md` | Passed and approved by owner 2026-07-24 |
| 3 — Metal block meshing | Working tree build, 2026-07-24 | Owner test log: 266 meshed tiles, 0 mirror checksum errors, no vertex mismatch/overflow/crash | Passed and approved by owner 2026-07-24 |
| 4 — MC/picking/tool preview | Working tree build, 2026-07-24 | Automated CPU/Metal parity passed; owner visual, picking, preview, undo/redo, Retina, and save checks passed; Terminal baseline was not retained | Passed and approved by owner 2026-07-24 |
| 5 — Path preview/hardening | Working tree build, 2026-07-24 | Owner completed preview and final visual/stability checks using `docs/gpu-phase-5-test-guide.md` | Passed and approved by owner 2026-07-24 |
| View — Grid/Frames separation | Working tree debug build, 2026-07-24 | Owner visually approved independent Grid Lines, Edges, canvas Frames, and model projection | Passed and approved by owner 2026-07-24 |

Phase 2 implementation was explicitly authorized after Phase 1 passed. Each
later phase received its own owner test and approval. The five-phase GPU
upgrade and follow-on View Frames enhancement are complete.

---

## 10. Final Release Record

- **Owner approval:** 2026-07-24
- **Source commit:** `8ca7793b` — Complete Metal acceleration and view frames upgrade
- **Executable:** Goxel XCom Fork 0.15.2-xcom, optimized arm64 macOS release
- **Executable SHA-256:** `107702fb950f2d8f8ea693aef2f20fbc77a3444e70cc50a593d94073bd76b082`
- **Backup:** `Goxel-XCom-Fork-final-release-source-2026-07-24.zip`
- **Backup contents:** committed source, documentation, this specification,
  and the matching release executable; no Git history, object files, cache
  files, or macOS archive metadata.
