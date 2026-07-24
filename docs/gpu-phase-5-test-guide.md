# GPU Upgrade Phase 5 — Owner Test Guide

Phase 5 adds an optional **Metal Direct Preview** to the Render panel. It
ray-traverses the bounded sparse voxel mirror, progressively antialiases the
image, and applies ambient, direct, and hard-shadow lighting. It is intended
for fast composition and lighting feedback.

The existing **CPU (Yocto)** renderer remains the final-quality renderer. The
Metal preview does not yet reproduce Yocto materials, emissive surfaces, sky
model, floor geometry, marching-cubes surfaces, indirect bounces, or denoising.
Do not expect the two renderers to be pixel-identical.

## 1. CPU reference

1. Start `goxel` from Terminal.
2. Open a small or medium scene and open the Render panel.
3. Select **CPU (Yocto)**.
4. Use a moderate output size such as 640×480 and 32 samples.
5. Click **Start**, allow it to finish, and save or capture the result.
6. Record approximately how long the first recognizable image and the finished
   result take.

Verify the established Yocto renderer still handles materials, emissive
surfaces, world/sky, floor, and final saving normally.

## 2. Metal direct preview

1. Set Settings → GPU Acceleration to **Auto**.
2. In Render, select **Metal Direct Preview**.
3. Start with:
   - Resolution: 50%
   - Samples: 32
   - Max ray steps: 1024
4. Click **Start**.
5. Compare camera framing, voxel occupancy, colors, light direction, and major
   shadows against the CPU reference.

Expected:

- A recognizable image appears quickly and refines without flicker.
- Geometry is complete at positive and negative coordinates.
- Colors and camera framing are sensible.
- Hard shadows respond to the Light controls.
- Differences involving materials, floor, sky, emissive light, marching
  cubes, or indirect illumination are expected limitations.

## 3. Reset and cancellation

While Metal preview is running, repeatedly:

1. Rotate, pan, and zoom the camera.
2. Change light direction and intensity.
3. Change world color/intensity.
4. Change resolution, samples, and maximum ray steps.
5. Edit voxels and toggle layer visibility.
6. Click **Stop**, **Start**, and **Restart** several times.
7. Leave the Render panel and return to normal editing.

Expected:

- Every change immediately discards stale accumulation and starts at sample 1.
- No old camera view or removed voxel remains blended into the image.
- Stop/Restart remains responsive.
- Normal editing, undo/redo, saving, and exports remain unchanged.

## 4. Bounds and fallback

1. Test Resolution at 25%, 50%, and 100%.
2. Test Max ray steps at 64, 1024, and 4096. Low values may deliberately clip
   rays through a very large scene.
3. Select GPU Acceleration **Off**, then start Metal Direct Preview.

Expected:

- Metal output is capped at 2048×2048.
- Preview allocation remains bounded (about 80 MiB maximum, plus a small hash
  table, independent of the existing 64 MiB tile-mirror cap).
- With acceleration Off or after a Metal failure, the Render panel reports a
  Yocto CPU fallback and produces a CPU render rather than crashing.

## 5. Diagnostics

1. Open Debug and inspect:
   - `Metal path preview` frames and attempts
   - reset/fallback/time
   - path-preview memory
2. Click **Log GPU baseline report**.
3. Quit normally and retain the Terminal output.

The `GPU_BASELINE` line should contain `path_backend`, `path_size`,
`path_samples`, `path_max_steps`, attempts, frames, resets, fallbacks, GPU
time, and memory.

Stop and send the log if the preview is blank, inverted, incorrectly framed,
missing ordinary block voxels, retains stale accumulation, hangs, grows memory
without bound, or crashes.

## Approval

Passed and approved by the owner on 2026-07-24. Final release packaging is
authorized.
