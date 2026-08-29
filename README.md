# MakeHuman — C++/Qt6

A native C++23 port of [MakeHuman](http://www.makehumancommunity.org), the
parametric 3D human generator. Targets macOS (arm64 first), with a Qt6 UI and a
Metal-backed renderer replacing the original Python/PyQt5/legacy-OpenGL stack.

**Status: core complete, UI not started.** The port loads MakeHuman's data,
builds a parameterised character with byte-level fidelity to the original, fits
proxies, and exports to OBJ, glTF/GLB and FBX.

## What works today

| Area | State |
|---|---|
| Mesh, OBJ read, normals, tangents, GPU unweld | ✅ parity-tested |
| Catmull-Clark subdivision | ✅ exact parity — 75,008 verts / 73,944 faces |
| Targets, macro factors, modifiers, `Human` | ✅ parity-tested end to end |
| `.mhm` saved models | ✅ round-trip parity |
| Proxies (`.mhclo`), materials (`.mhmat`), asset index | ✅ parity-tested |
| **Export** OBJ · glTF 2.0 / GLB · FBX 7500 binary · Collada · STL · 3MF | ✅ |
| **Import** FBX · glTF · OBJ · Collada · STL | ✅ new capability |
| Skeleton, skinning, pose | ⬜ not started |
| Renderer, UI | ⬜ not started |

Every ported subsystem is checked against the original Python implementation,
which is retained in `legacy/python/` purely as a test oracle and is **not part
of the build**. CI enforces that.

## Performance

Measured against the Python original on the same machine and data
(`benchmarks/`, release build):

| Operation | C++ | Python | |
|---|---|---|---|
| Load base mesh | 5.2 ms | 211.8 ms | **40×** |
| Catmull-Clark build | 6.3 ms | 202.3 ms | **32×** |
| Subdivided refresh | 0.46 ms | 28.2 ms | **61×** |
| Full character rebuild | 0.07 ms | — | |
| Load all 1,280 targets | 465 ms | 3,226 ms | **6.9×** |

The subdivided-refresh figure is the one that matters: the original needs
28.2 ms per update, which exceeds a 16.6 ms frame budget before anything is
drawn. That is why subdivided editing cannot hold 60 fps today.

## Design

The interface is specified in [`memory/design.md`](memory/design.md), with a
rendered visual reference:
**[MakeHuman Interface Kit](https://claude.ai/code/artifact/fddacb8c-5f35-4619-81cc-6e0f45d1a070)**
— the dark dockable workspace, its tokens, controls and icons, built in the tokens
it documents.

## Building

Requires CMake ≥ 3.28, Ninja, a C++23 compiler, and assimp.

```bash
brew install cmake ninja assimp
cmake --preset macos-arm64-release
cmake --build --preset macos-arm64-release -j
ctest --preset macos-arm64-debug --output-on-failure
```

Presets: `macos-arm64-debug`, `macos-arm64-release`, `macos-arm64-asan`.

## Layout

```
src/{core,io}      C++ implementation   include/makehuman/  public headers
data/              CC0 assets           tests/              unit · golden · regression
benchmarks/        vs. the Python baseline
memory/            project documentation — start with memory/session_start.md
legacy/            the Python original: test oracle only, never built
```

## Licensing

Two licences, deliberately separated so downstream users get real reuse rights.
See **[LICENSING.md](LICENSING.md)** — it is the authoritative document.

- **`mh-core` (AGPL-3.0-or-later)** — ported from MakeHuman, so copyleft is
  permanent and cannot be removed.
- **`mh-io` (Apache-2.0)** — written from published format specifications, never
  translated from the original, so it is separately reusable.

Dependency arrows point one way only: AGPL modules may use Apache-2.0 modules,
never the reverse. CI checks the SPDX header on every source file.

**Assets are CC0.** Anything you create with this software is yours, with no
conditions. The application's *source* stays open; a closed-source fork is not
permitted.

The Autodesk FBX SDK is **not** used, despite being free of charge — its licence
and AGPL impose contradictory distribution obligations. FBX support comes from
assimp (BSD-3), which writes newer FBX than the original did. Reasoning with
clause references is in LICENSING.md §5.3.
