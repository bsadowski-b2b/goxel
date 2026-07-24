# GPU Upgrade Phase 4 — Owner Test Guide

Phase 4 adds Metal geometry generation for the **Smooth** Marching Cubes mode.
The non-smooth Marching Cubes variant remains on its established CPU path
because it uses different flat-color topology. Picking retains the existing
cached GPU ID buffer and now reports its timing. Tool-preview volumes render
through the accelerated tile mesher, while the final document edit, undo
history, save data, and exports remain CPU-authoritative.

## 1. Validation parity

1. Start `goxel` from Terminal so its log remains visible.
2. Open Settings → GPU Acceleration and select **Validation**.
3. Open View, enable **Marching Cubes**, then enable **Smooth**.
4. Create or load geometry that crosses several 16-voxel tile boundaries.
5. Paint, subtract, replace, extrude, undo, and redo on both sides of a seam.
6. Try isolated voxels, thin features, transparent colors, several materials,
   negative coordinates, symmetry, clipping, and selections.
7. Rotate and zoom while editing.

Expected:

- No cracks, missing triangles, color changes, or unstable seams.
- No `Metal marching-cubes mismatch` log entry.
- Debug → `Mesh fallback/mismatch/overflow` ends with mismatch and overflow at
  zero. A fallback count is acceptable only if accompanied by a clear
  allocation/capacity diagnostic and correct CPU rendering.

## 2. Auto versus Off

1. Save the scene.
2. Select **Off**, inspect several camera angles, and take reference screenshots.
3. Select **Auto**, revisit the same views, and repeat the same edits.
4. Toggle **Smooth** off. Confirm the flat Marching Cubes appearance is still
   correct; this mode intentionally uses the CPU.
5. Toggle Marching Cubes off and confirm normal blocks remain correct.

Expected:

- Smooth Marching Cubes in Auto matches Off visually.
- Flat Marching Cubes and normal blocks remain unchanged.
- Switching modes immediately rebuilds the visible tile geometry.

## 3. Picking and tool previews

1. With **Auto** selected, use brush, shape/box, sphere, line, paint, subtract,
   replace, extrude, and symmetry previews where available.
2. Hover and click empty space, tile seams, thin features, transparent voxels,
   and the edges of a large scene.
3. Before committing each tool operation, compare the preview with the final
   result. Undo and redo it.
4. Open Debug and confirm:
   - `Picking` and `Pick readback` counters increase during interaction.
   - cached picks increase when the scene/camera is unchanged.
   - `GPU-assisted tool preview frames` increases while a preview is visible.

Expected:

- Picking remains stable and lands on the intended voxel/face.
- Every preview matches its committed result.
- Undo/redo remains exact.

## 4. Save, export, and log

1. Save, close, and reopen the test scene.
2. Export one normally supported format and confirm it remains correct.
3. In Debug, click **Log GPU baseline report**.
4. Quit normally and send the complete Terminal log.

The `GPU_BASELINE` line should include `mesh_attempts`, `mesh_successes`,
`mesh_fallbacks`, `mesh_mismatches`, `mesh_overflows`, picking timing, and
`tool_preview_frames`.

Stop testing and send the log immediately if the app crashes or hangs, a
marching-cubes mismatch appears, mismatch/overflow is nonzero, geometry differs
between Off and Auto, picking becomes unstable, or a preview differs from its
committed edit.
