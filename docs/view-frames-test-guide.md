# View Frames Owner Test

Use the debug executable at:

`/Users/Laura/Documents/Goxel XCom Fork/goxel`

## What changed

View → Effects now has three independent display controls:

- **Grid Lines**: per-voxel lines on the model plus a fine grid on the visible
  faces of the canvas volume.
- **Edges**: model edge emphasis, unchanged.
- **Frames**: a stronger adjustable lattice on the canvas-volume faces,
  projected at matching intervals across model surfaces.

When Frames is enabled, **Frame Spacing** accepts 1–64 voxels and defaults to 8.

## Test

1. Open a model that spans several frame intervals.
2. Toggle Grid Lines alone.
   - Confirm per-voxel model lines.
   - Confirm a fine grid on the visible canvas-volume faces.
3. Toggle Grid Lines off and Frames on.
   - Confirm the stronger canvas frame remains.
   - Confirm its divisions continue across visible model surfaces.
4. Enable Edges, Grid Lines, and Frames together.
   - Confirm all remain distinguishable and no toggle controls another.
5. Try Frame Spacing values 1, 4, 8, 16, 32, and 64.
   - Rotate and zoom at each value.
   - Add and remove voxels on both sides of the canvas origin.
   - Look for misalignment, flicker, or z-fighting.
6. Quit and reopen Goxel.
   - Confirm all three toggles and Frame Spacing were restored.
7. Compare GPU Acceleration Off and Validation.
   - The View effects should look identical.

## Pass report

Please report:

- Grid Lines: pass/fail
- Edges independence: pass/fail
- Frames on canvas faces: pass/fail
- Frames projected on model: pass/fail
- Frame Spacing and persistence: pass/fail
- Any flicker, overly heavy lines, missing faces, or alignment issue

## Approval

Passed and approved by the owner on 2026-07-24. Final release packaging is
authorized.
