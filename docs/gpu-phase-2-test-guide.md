# GPU Upgrade Phase 2 — Owner Test Guide

Phase 2 mirrors changed voxel tiles to a bounded Metal buffer. The CPU document
and CPU/OpenGL renderer remain authoritative, so the image should look exactly
as it did after Phase 1.

## Recommended mode

Use **Validation** for the first test. Open Settings, expand
**GPU Acceleration**, and change **Mode** to **Validation**. The change is
applied immediately and saved for the next launch. Validation compares every
uploaded CPU tile with the Metal-buffer copy.

## Short functional test

1. Open the Debug panel and press **Reset GPU baseline counters**.
2. Paint a continuous stroke that crosses several 16-voxel tile boundaries.
3. Paint, subtract, and replace voxels on both sides of the world origin.
4. Undo and redo those edits quickly several times.
5. Add a layer, edit it, toggle its visibility, then delete or hide it.
6. Save the project, close it, reopen it, and confirm the result.
7. Exercise block, marching-cubes, grid, edge, wireframe, transparency, and
   smoothness views.

In the Debug panel, verify:

- **Tile mirror** roughly follows the active rendered tile count.
- **Mirror dirty/halo** rises after edits.
- **Mirror upload** rises after edits but stops rising while the scene is idle.
- **Mirror evict/remove/checksum errors** ends in `/ 0` for checksum errors.

Removals are expected after subtracting whole tiles, hiding layers, or opening a
different scene. Evictions are safe and expected only if the scene exceeds the
64 MiB / 4096-tile mirror budget.

## Ten-minute stability test

1. Load or import the stress benchmark scene.
2. Edit, rotate, change layers, and undo/redo for at least ten minutes in
   **Auto** mode.
3. Repeat a shorter session in **Off** mode.
4. Confirm memory settles rather than growing continuously and that Off still
   behaves normally.
5. Press **Log GPU baseline report** at the end of each run and save the
   terminal line beginning with `GPU_BASELINE`.

## Stop and report

Stop the test and provide the log if any of these occur:

- crash, hang, visual corruption, or save/reopen mismatch;
- a nonzero `mirror_checksum_errors` value;
- mirror uploads rising continuously while the scene is completely idle;
- memory growing continuously beyond the documented 64 MiB mirror allocation;
- a clear responsiveness regression in Off mode.

Phase 3 must not begin until you explicitly approve Phase 2.
