# GPU Upgrade Phase 1 — Owner Test Guide

Phase 1 adds measurement and a disabled feature scaffold. It does not replace
the CPU mesher or OpenGL renderer.

## Prepare benchmark fixtures

From the repository:

```sh
python3 tools/generate_gpu_benchmark_scenes.py
```

This creates deterministic small, medium, and stress TXT scenes under
`benchmarks/gpu/`. Import a fixture with Goxel's TXT importer, then save a
working copy as `.gox` if desired.

## Capture a baseline

1. Launch the debug build and open the Debug panel.
2. Press **Reset GPU baseline counters**.
3. Open or import a test scene.
4. Rotate the view for 15 seconds, then make edits across several tile
   boundaries.
5. Press **Log GPU baseline report**.
6. Save the terminal line beginning with `GPU_BASELINE`.
7. Repeat once for a cold cache (restart Goxel) and once for a warm cache.

The report includes backend/mode, FPS, volume and tile counts, cache
hits/misses, meshed tiles, CPU meshing time, buffer upload time/bytes, draw
calls, and CPU submission time.

## Functional acceptance

- Open, edit, undo/redo, save, reopen, and export a known project.
- Exercise block, marching-cubes, grid, edge, wireframe, transparency, and
  smoothness modes.
- Confirm Debug reports the Metal device but states that Phase 1 acceleration
  features are disabled.
- Test `off`, `auto`, and `validation` by changing `mode` under
  `[gpu_acceleration]` in the user `settings.ini`, restarting, and confirming
  identical output.
- Complete a normal editing session without a crash or visible regression.

Do not approve Phase 2 if file round-tripping, rendering, or tool behavior has
changed.
