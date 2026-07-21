# Goxel XCom Fork Project Breakdown

Date prepared: 2026-07-21

## Current State

The fork is set up as a working macOS Goxel XCom editor branch with a Release build path, custom app identity, XCom palette storage, multiple movable toolbars, reference image utilities, custom grid/hover behavior, and early XCom UI scaffolding.

Current branch:

```text
codex/phase-2-grid-input-brushes
```

Latest implementation commit before this documentation pass:

```text
018c1d19 Make toolbar swatches editable
```

## How To Run

Primary local launcher:

```sh
tools/run_goxel_xcom_fork.sh
```

Release build command used during development:

```sh
xcodebuild -project osx/goxel/goxel.xcodeproj -scheme goxel -configuration Release CODE_SIGN_IDENTITY=- CODE_SIGNING_REQUIRED=NO CODE_SIGNING_ALLOWED=NO build
```

Release app path:

```text
/Users/Laura/Library/Developer/Xcode/DerivedData/Build/Products/Release/Goxel XCom Fork.app
```

## Accomplished

### Fork Setup

- Pulled down and configured a custom Goxel fork.
- Added XCom fork identity/name so it is distinguishable from normal Goxel.
- Added launcher script support for easier local running.
- Switched practical testing to Release builds because Debug was too slow.

### Default Project Setup

- Added default new canvas/model size of `64x64x80`.
- Added XCom grid guides for `64x64` workflows:
  - center lines at `32`
  - lighter guide lines at `16` and `48`

### XCom Swatches And Palettes

- Added 8 XCom swatches.
- Added a swatch toolbar.
- Added XCom panel swatch UI in one row.
- Saved XCom swatches inside `.gox` using an `XSWC` chunk.
- Added `.gox` roundtrip test coverage for swatches.
- Added palette save/load support.
- Made toolbar swatches directly editable by clicking them.
- Added brighter active swatch highlight.

### XCom Panel

- Added XCom navigation panel alongside the existing main panel navigation.
- Added palette controls to the XCom panel.
- Added Save section with collapsible behavior.
- Added Load Palette.
- Added XCom Options path for XCom Gox Repository.
- Fixed earlier XCom panel opening errors.

### Tool And Toolbar Workflow

- Color picker now returns to the previous active tool after picking.
- Added noise brush.
- Added one-click Grid+Edges view toggle.
- Removed annoying mouse-button hint text from the menu bar.
- Added movable/foldable toolbars with saved positions and orientation settings.
- Split mode tools and swatches into separate toolbars.
- Added Main toolbar.
- Added Swatches toolbar.
- Added Select Bar with:
  - Selection
  - Fuzzy Select
  - Rectangle Select
- Removed Set/Add/Sub mode buttons from Select Bar after review.
- Added MyTools toolbar with:
  - Shape tool
  - Color Picker tool
  - Camera Reset
- Moved Camera Reset from Top toolbar to MyTools.
- Refined toolbar handle/fold behavior and styling.

### Selection Tool State

- Shared selection mode state across selection tools so the tool panel controls do not fight separate per-tool states.
- Preserved Set/Add/Sub selection behavior in the tools themselves.

### View, Grid, And Rendering

- Limited XCom side grids to intended background faces.
- Corrected grid line visibility issues.
- Added depth occlusion for XCom floor guides.
- Hid the XCom floor grid from below the volume.
- Made voxel grid line color adaptive so grid lines remain visible on dark colors.
- Improved default color-accurate viewport shading.
- Added voxel hover target outlines.
- Changed hover preview toward face-only highlighting.

### Reference Image And Utility Windows

- Added drag/drop reference image loading.
- Added reference image window with pan/zoom.
- Moved reference image pan to middle mouse.
- Saved reference image path, position, size, pan, zoom, alpha, and visibility.
- Added titlebar alpha slider for the reference image window.
- Added reset reference image position.
- Clamped reference image titlebar position so it can be recovered if moved near an edge.
- Added hover-only utility chrome behavior.
- Stabilized image hover so the image does not shift on mouse-over.
- Added View panel toggles for:
  - Image Ref
  - View Cube
  - Axis widget
- Added separate utility windows for:
  - view cube
  - axis widget

### UI Polish

- Shrunk color swatches by 33%.
- Added panel translucency.
- Improved active toolbar highlights.
- Reverted several experimental icon changes back to original icons.
- Restored expected top toolbar, undo/redo, camera reset, view option, paint, and subtract icon behavior after review.

## Left To Do

### High Priority

- **XCom material/layer/palette system**
  - Build the full XCom tool as the central reason for the fork.
  - Merge material and layer workflows into an XCom-focused UI.
  - Assign material data to selected voxels and/or newly painted voxels.
  - Decide how material metadata should be stored in `.gox`.
  - Define export/physics model requirements.

- **Improve swatch color editing**
  - Current swatches are editable, but precise setting is still not easy enough.
  - Add Color Picker `(C)`/eyedropper behavior into the RGB/rainbow color picker UI.
  - Goal: when editing a swatch, make sampling and precise color selection faster.

- **Mouse 4 and Mouse 5 bindings**
  - Current state: exposed/attempted input support is not enough.
  - User reports Mouse 4 and Mouse 5 still do not work.
  - Needs deeper investigation in platform event handling and shortcut binding serialization.

- **Reference image opacity**
  - Current slider affects the window/background behavior.
  - Requested behavior: slider should affect the entire Image Ref window, including the image/content, not just the background.

### Medium Priority

- **Residual yellow/warm color tint**
  - Color-accurate default shading was improved.
  - User still saw a slight yellow tint.
  - Needs visual validation with pure white/gray test voxels and comparison against expected RGB output.

- **Image Ref outside app window**
  - User asked if the Image Ref window can live outside the Goxel app window.
  - Needs platform/UI feasibility review.

- **macOS file preview / thumbnail**
  - User asked what it would take for macOS to display `.gox` file contents as an icon/preview.
  - Likely needs Quick Look thumbnail extension or storing preview image data alongside/inside `.gox`.

- **Toolbar customization**
  - Current solution is hardcoded toolbars with movable/foldable/orientation settings.
  - Earlier question remains: should toolbars become fully customizable, or continue with hardcoded curated toolbars?

- **XCom palette/material file format**
  - Palette persistence exists.
  - Full material persistence and backwards-compatible `.gox` schema still need design.

### Lower Priority / Polish

- Review whether Set/Add/Sub selection controls should stay only in the tool panel or appear somewhere else.
- Tune active swatch highlight if it becomes too visually heavy.
- Validate all toolbar default positions on small screens.
- Add a visible reset for all utility windows if not already sufficient through Settings/View controls.
- Add more tests around `.gox` XCom chunks as material metadata is added.

## Suggested Next Implementation Order

1. Fix Mouse 4 / Mouse 5 input handling.
2. Improve swatch editor by integrating eyedropper/color picker behavior into the RGB/rainbow UI.
3. Make reference image alpha affect the entire window/image content.
4. Validate and finish residual yellow tint work.
5. Start the real XCom material model:
   - data structure
   - `.gox` storage
   - selection assignment
   - paint-time assignment
6. Expand XCom panel into the material/layer/palette command center.

## Known Verification

- Release builds have repeatedly succeeded through Xcode.
- The app has been relaunched after each recent UI change.
- Swatch `.gox` persistence has test coverage in `src/tests.c`.
- Worktree was clean before these documentation files were added.
