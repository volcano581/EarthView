# EarthView — agent instructions

EarthView is a Qt 6 / OpenGL map rendering engine (C++17) in `EarthView/`. Its 2D modes
(Mercator, Orthographic) are integrated into the Doctrine project and must not regress.
Active work: building a real 3D globe / stealth-view renderer. **The plan is
`EarthView/ROADMAP_3D.md` — read it before starting any 3D task and implement only the
milestone task you were given.**
Progress is tracked in `EarthView/3D_PROGRESS.md` — read its status board before starting
(do not redo `Merged` tasks) and update it in your PR (see rule 9).

Further docs: `EarthView/DEVELOPERS_GUIDE.md`, `EarthView/RENDERING_PIPELINE.md`.

## Build

Windows (developer machine): Qt 6.11 llvm-mingw, MinGW Makefiles.
```
cmake -S EarthView -B EarthView/build -G "MinGW Makefiles"
cmake --build EarthView/build -j
```

Linux (cloud agents, no GPU): install Qt 6 and build with Ninja. `windeployqt` and the
Windows Qt paths in `CMakeLists.txt` must stay optional (see roadmap task M0.2).
```
sudo apt-get install -y cmake ninja-build qt6-base-dev libqt6opengl6-dev libgl1-mesa-dev
cmake -S EarthView -B out -G Ninja -DEARTHVIEW_DEPLOY_QT=OFF
cmake --build out
ctest --test-dir out --output-on-failure
```
Software GL for smoke runs: `QT_QPA_PLATFORM=offscreen EARTHVIEW_FORCE_SOFTWARE_OPENGL=1`.

## Rules

1. **Do not change 2D behaviour.** `Camera`, `TileRenderer`, `VectorTileRenderer`,
   `BorderRenderer`, `GridRenderer`, `CityRenderer`, `TextRenderer` keep working identically.
   New 3D code goes in `EarthView/scene3d/`. Edit existing files only at the small hook points
   a roadmap task names.
2. **Do not extend the old pseudo-3D path** (`Camera::terrainMercatorToScreen`,
   `stealthViewMercatorToScreen`, `shaders/terrain_height.vert`, `colored_terrain.vert`,
   `terrain_line.vert`, the `isStealthViewEnabled()`/`isTerrain3DView()` branches). It is
   removed in milestone M9.
3. **Precision:** world math is WGS84/ECEF in `double` (GLM `dvec3`/`dmat4`). GPU data is
   relative-to-eye `float`. Never upload absolute ECEF as float. Don't use `QMatrix4x4` for
   world transforms.
4. **Depth:** reversed-Z with infinite far plane on GL 4.5; logarithmic depth on the 3.3
   fallback (detect via `OpenGLRuntime`).
5. **Tests:** math, LOD and tile-selection code gets Qt Test unit tests under
   `EarthView/tests/`; `ctest` must pass before opening a PR.
6. **Never commit build output** (`build/`, `out/`, `x64/`, `*.user`).
7. **Style:** match existing code — C++17, Qt naming, `m_` members, `#pragma once` plus
   include guards, Doxygen `@brief` on public classes, shaders as files in `shaders/` (or
   `scene3d/shaders/`) registered in `CMakeLists.txt`.
8. **PRs:** one roadmap task per branch (`3d/<milestone>-<topic>`), under ~1500 changed
   lines. Description includes: summary, files changed, how to verify visually on Windows,
   known limitations. Cloud agents cannot see rendered output — say explicitly what you
   could not verify.
9. **Tracking:** every PR for the 3D work updates `EarthView/3D_PROGRESS.md` in the same
   PR: set your row on the status board, add a change-log entry at the top using the
   template there, record new design decisions and open issues/tech debt, and mark visual
   checks `pending (Windows)` — never claim checks you could not run. Also set your task's
   Status in `ROADMAP_3D.md`.
