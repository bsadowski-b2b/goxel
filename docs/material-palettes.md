# Palette Material Binding

## Goal

Add a workflow for building palettes where each palette color can optionally be
assigned a material. Artists should be able to paint with fixed colors while
also defining the material behavior for each color.

## Existing Goxel Model

- Palette colors are loaded by `src/palette.c` and displayed in
  `src/gui/palette_panel.c`.
- Materials are image-owned objects in `src/material.h` and `src/image.h`.
- Layers reference one material through `layer_t.material`.
- Voxel data stores color as RGBA bytes in `volume_t`.
- `.gox` files already persist materials in `src/formats/gox.c`.
- glTF export currently exports material per layer in `src/formats/gltf.c`.

This means Goxel already stores voxel colors and material definitions, but it
does not yet store a relationship from a specific color to a material.

## Proposed Data Model

Add an image-owned binding table:

```c
typedef struct material_binding material_binding_t;
struct material_binding {
    material_binding_t *next, *prev;
    uint8_t color[4];
    material_t *material;
};
```

Attach it to `image_t` as `material_binding_t *material_bindings`.

Rules:

- A color can have zero or one material binding.
- A material can be reused by many colors.
- Bindings are stored on the image, not on the built-in palette asset, because
  the same palette may be used differently by different projects.
- Deleting a material should clear or reassign bindings that point to it.
- Undo/redo and image snapshots must copy bindings and remap material pointers.

## UI Plan

Extend `src/gui/palette_panel.c` so a selected palette swatch exposes:

- selected color preview
- assigned material dropdown
- "New Material From Color"
- "Clear Material Binding"

The existing `src/gui/material_panel.c` remains the place to edit material
properties such as metallic, roughness, base color, emission, and opacity.

## File Format Plan

Extend `.gox` persistence in `src/formats/gox.c` with a new chunk, for example
`MBND`, containing:

- color: `uint8_t[4]`
- material: material list index

Older Goxel versions will ignore the unknown chunk. This fork should continue
to load older `.gox` files without bindings.

## Export Plan

glTF needs the first export implementation because material semantics matter
most there.

Current behavior:

- one mesh primitive per layer
- primitive material comes from `layer_t.material`
- voxel color is exported as vertex color or palette texture

Target behavior:

- when bindings exist, split exported geometry by bound material
- unbound colors keep the layer material or default material
- preserve vertex color or palette texture data so base color remains visible

The likely code path is:

- update `volume_generate_mesh` / `fill_mesh` in `src/volume_to_vertices.c` to
  support color-filtered mesh generation or primitive bucketing
- update `save_layer` in `src/formats/gltf.c` to emit one primitive per bound
  material bucket

## Milestones

1. Add binding data structures, copy/delete/hash behavior, and `.gox`
   persistence.
2. Add palette panel UI for assigning selected colors to image materials.
3. Add glTF export support for material buckets.
4. Add tests for binding persistence and export behavior.
5. Optionally add live renderer/pathtracer support for color-bound materials.
