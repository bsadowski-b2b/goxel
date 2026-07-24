# GPU Upgrade Phase 3 — Owner Test Guide

Phase 3 uses Metal to generate complete block-renderer vertices in **Auto**.
**Validation** generates both CPU and Metal geometry, compares every vertex
byte independent of emission order, and renders the CPU result. Marching cubes
remains CPU-only.

## Validation parity test

1. Open Settings → GPU Acceleration and select **Validation**.
2. Open the Debug panel and reset GPU counters.
3. Test normal blocks with grid, edges, wireframe, lighting, smoothness,
   transparency, and shadows.
4. Paint, subtract, replace, and use symmetry across several tile boundaries.
5. Edit around negative coordinates and along thin or isolated features.
6. Rotate the camera and verify picking/selection on every visible face.
7. Toggle layers, undo/redo rapidly, save, close, and reopen.

The Debug panel must show:

- Metal block attempts and successes increasing on changed block tiles.
- `Block fallback/mismatch/overflow` ending with mismatch and overflow at zero.
- Mirror checksum errors remaining zero.

## Auto rendering test

1. Change GPU Acceleration to **Auto**.
2. Repeat the visual modes and editing operations above.
3. Compare several camera views against **Off** mode.
4. Confirm there are no missing faces, incorrect colors, lighting seams,
   broken borders, picking errors, flicker, or crashes.
5. Confirm Marching Cubes still works normally.

## Performance and stability

1. Run a cold-cache benchmark in **Off**, then restart and repeat in **Auto**.
2. Run continuous edits in Auto for at least ten minutes.
3. Log a GPU baseline report after each run.
4. Confirm memory settles and the application remains responsive.

Stop and provide the log if any checksum or vertex mismatch appears, if the
scene differs between Off and Auto, or if Auto crashes or hangs. Do not approve
Phase 4 until all Phase 3 tests pass.
