# Goxel XCom Fork

This repository is a fork workspace for custom Goxel work.

## Repository Remotes

- `origin`: `https://github.com/bsadowski-b2b/goxel.git`
- `upstream`: `https://github.com/guillaumechereau/goxel.git`

Keep `upstream` pointed at the original Goxel project. Put fork-specific work
on topic branches and push those branches to `origin`.

## Current Branch

The material palette work starts on:

```sh
git switch codex/material-palettes
```

The branch was created from `upstream/master`.

## Build Setup

Goxel uses SCons. On macOS, the local baseline build needs:

```sh
brew install scons pkg-config glfw
```

Build a debug binary:

```sh
scons -j 8
```

Build a release binary:

```sh
make release
```

Run the built app:

```sh
./goxel
```

The debug build runs the built-in unit tests during app startup.

## Fork Goal

This fork will add palette material binding: a workflow where colors in a
palette can be assigned named materials, so voxel colors can carry rendering or
export semantics such as metal, plastic, glass, cloth, armor, trim, or emissive
surfaces.

The implementation notes are in `docs/material-palettes.md`.
