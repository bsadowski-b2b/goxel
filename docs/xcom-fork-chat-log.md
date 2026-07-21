# Goxel XCom Fork Chat Log

Date prepared: 2026-07-21

This is a structured project chat log, not a verbatim transcript. It records the major requests, decisions, and implementation outcomes from the Goxel XCom fork work.

## Repository Context

- Workspace: `/Users/Laura/Documents/Goxel XCom Fork`
- Active branch during this work: `codex/phase-2-grid-input-brushes`
- Remote: `https://github.com/bsadowski-b2b/goxel.git`
- Latest implementation commit before this documentation pass: `018c1d19 Make toolbar swatches editable`

## Conversation Timeline

### Fork Setup And Initial Scope

- The project started with locating Goxel on GitHub, pulling it down, and setting up a custom fork.
- The core fork direction was established: add XCom-oriented palette/material workflows on top of Goxel.
- The user wanted a first pass where desired changes were listed and difficulty was assessed before implementation.

### Initial Feature List

Requested early improvements:

- After using the color picker, return to the previous active tool.
- Add camera reset to the top button bar.
- Add one-click grid and edge visibility toggle.
- Default new model size: `64x64x80`.
- Add custom grid lines for 64x64 work:
  - strong center lines at `x=32` and `y=32`
  - lighter guide lines at `16` and `48`
- Remove the mouse button hint text/icons from the menu bar.
- Add a noise brush, like pencil/paint but scattered.
- Add multiple quick color swatches, eventually set to 8 swatches.
- Save swatch colors with `.gox` data and allow palette save/load.
- Begin the XCom tool concept: a new navigation panel merging materials, layers, and custom palette UI for model/material assignment.
- Investigate key remapping, especially Space, WASD, Mouse 4, and Mouse 5.

### Phase Planning

- Phase 1 was defined as quick, low-risk UI and workflow improvements.
- Later phases were reserved for the deeper XCom fork goals:
  - material assignment per voxel/selection
  - possible `.gox` internal format extensions
  - physics/export workflows
  - deeper palette/material/layer integration

### Phase 1 Implementation And Performance

Implemented and validated:

- XCom fork naming and app identity.
- Release build workflow, because the debug version was too slow.
- Launcher script for easier local running.
- Default new size `64x64x80`.
- Color picker returns to the previous tool after picking.
- Camera reset button was initially added to the top toolbar.
- Grid and edge toggle button was added.
- XCom guide grid lines were added.
- Menu bar mouse-hint text was removed.
- Noise brush was added.
- 8 XCom toolbar swatches were added.
- Swatches were saved into `.gox` through an `XSWC` chunk and covered by a file roundtrip test.
- Palette save/load was added.

### Grid And View Iteration

Follow-up grid/view work:

- Grid lines were moved so they only appear on intended background faces instead of all volume sides.
- Incorrect grid visibility behavior was corrected.
- Floor guide occlusion was improved.
- The custom bottom grid was hidden when the camera is below it.
- Grid line colors were made adaptive so grid visibility is better on dark voxel colors.
- Side/background grid behavior was refined for the `64x64x80` volume.

### XCom Panel And Palette UI

XCom UI iteration:

- Added a new XCom navigation panel in the same vertical navigation layer as View and Camera.
- Moved color swatches into the XCom panel.
- Added save/load palette controls.
- Added XCom settings/options including an XCom Gox Repository path.
- Removed the text `Swatch`.
- Removed old Active controls.
- Replaced active swatch state with a brighter border highlight.
- Collapsible Save section was added.
- Swatches were displayed in one row, 8 across.
- Fixed errors opening the XCom tool panel.

### Icon Work

Icon changes and follow-up:

- Custom icons were applied for view toggle, camera reset, XCom tool, noise brush, paint, subtract, undo, redo, and add.
- Later, several original icons were restored:
  - top toolbar icons
  - undo/redo
  - camera reset
  - view options
  - paint toolbar icons
- Paint toolbar was renamed to Main.
- Color swatches were removed from the Main toolbar.

### Movable Toolbars

Movable/foldable toolbar work:

- Added draggable toolbar positioning.
- Added horizontal/vertical orientation settings.
- Split paint/mode controls and swatches into separate movable toolbars.
- Added toolbar handle controls.
- Changed fold behavior so double-clicking the handle folds/unfolds.
- Refined handle styling:
  - removed secondary dark button background
  - made grip/arrow smaller
  - vertically centered the icon
- Added `Select Bar`, a movable selection toolbar.
- Added `MyTools`, a movable toolbar containing:
  - Shape tool
  - Color Picker tool
  - Camera Reset
- Moved Camera Reset from Top toolbar to MyTools.

### Selection Toolbar

Selection toolbar work:

- Added a new movable `Select Bar`.
- Included selection tools:
  - Selection
  - Fuzzy Select
  - Rectangle Select
- Initially included Set/Add/Sub selection modes.
- Later removed Set/Add/Sub from the Select Bar.
- Selection Set/Add/Sub state was made shared with existing selection tool options so tool panel controls and toolbar state did not conflict.

### Reference Image And Utility Windows

Reference/utility window work:

- Added a drag-drop reference image window.
- Added zoom and pan for the reference image.
- Moved reference image pan controls to middle mouse.
- Saved reference image path, position, size, zoom, pan, visibility, and alpha settings.
- Changed reference window styling:
  - titlebar color matched other UI panels
  - blue background changed to gray
  - translucent styling added
  - hover chrome was hidden until mouse-over
- Added opacity slider to the titlebar.
- Added reset position support so an off-screen reference window can be recovered.
- Clamped titlebar position to keep the window retrievable.
- Added View panel toggles for:
  - Image Ref window
  - View Cube window
  - Axis widget window
- Added separate utility windows for:
  - view cube
  - axis widget
- Fixed a hover/click issue where utility chrome appeared on every mouse click.
- Stabilized reference image hover controls so the image does not shift on mouse-over.

### Rendering, Highlighting, And Input

Rendering/input work:

- Investigated yellowish viewport tint.
- Added color-accurate default shading improvements.
- Remaining note: the user still saw a slight warm/yellow cast and this needs further validation.
- Added voxel hover target outlines.
- Changed hover preview to highlight only the active face instead of the entire voxel.
- Improved active toolbar highlight styling.
- Exposed more shortcut/input options.
- Mouse 4 and Mouse 5 were requested for tool mappings, but the user reported they still do not work.

### Latest Record-Only Items

The user then asked to stop implementation and only record future items. Added to the backlog:

- Swatch editing is still not easy enough.
- Add the Color Picker `(C)` behavior into the RGB/rainbow color picker UI.
- Image Ref window opacity slider should affect the entire window, not just the background.
- Mouse 4 and Mouse 5 still do not work and need deeper input handling work.

## Recent Commit Trail

- `018c1d19 Make toolbar swatches editable`
- `eb6e2219 Add MyTools toolbar`
- `cf9a47b2 Clean up select and swatch toolbars`
- `c1e68e76 Refine toolbar handle icon`
- `2f1232d1 Add movable selection toolbar`
- `7b2552ca Combine toolbar handle and add ref reset`
- `1abd50ab Move reference alpha slider to titlebar`
- `8aaa5760 Limit utility chrome to window hover`
- `61e0ef2e Restore toolbar icons and rename paint bar`
- `07550282 Stabilize reference image hover controls`
- `75d42b68 Add foldable toolbars and utility widgets`
- `0b3bf341 Hide XCom floor grid from below`
- `cc06a972 Expose mouse shortcuts and panel transparency`
- `7523b612 Use face-only hover previews`
- `fcbfce95 Improve active toolbar highlights`
- `645d765d Move reference image panning to middle mouse`
- `e1868b58 Refine translucent UI panels`
- `d4a79332 Shrink color swatches`
- `068754a5 Add voxel hover target outlines`
- `be0f2922 Make voxel grid lines adaptive`
- `580f36e7 Default viewport to color accurate shading`
- `2c2d79b3 Add drag-drop reference image viewer`
- `8804d9b4 Fix Goxel XCom launcher default open`
- `cfad55a7 Add Goxel XCom launcher script`
- `1dce11e7 Split paint and swatch toolbars`
- `49c280e7 Add movable toolbar layout controls`
- `98a152ac Replace add tool icon`
- `7366c647 Replace redo button icon`
- `0d697f0d Replace undo button icon`
- `716b76f3 Replace subtract mode icon`
- `2a90102d Replace XCom paint brush icon`
- `e46eeef0 Fix XCom panel section stack`
- `98e29624 Show XCom panel palette in one row`
- `dbadd119 Refine XCom palette UI`
- `7920e16f Depth-occlude XCom floor guides`
- `fb8f33e Add XCom noise brush icon`
- `9fe057e4 Add XCom panel icon`
- `1691adec Update XCom toolbar view icons`
- `9a01112b Fix XCom background grid face selection`
- `ccbe4f29 Add XCom swatch panel`
- `a95a30fc Limit XCom side grids to background faces`
- `9c5ab362 Add XCom grid guides and noise brush`
- `040d205c Add XCom fork phase 1 editor updates`
- `e34fc49c Document fork setup and material palette plan`
