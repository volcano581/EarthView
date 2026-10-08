# EarthView — agent instructions

EarthView is a Qt 6 / OpenGL map rendering engine (C++17) in `EarthView/`. Its 2D modes
(Mercator, Orthographic) are integrated into the Doctrine project and must not regress.

Active work: a 3D / stealth view built on **osgEarth**, embedded in Doctrine, with the
projects moving to the **MSVC** toolchain (decisions D-007, D-008). Read before any 3D task:
- `EarthView/ROADMAP_3D.md` — tasks O0–O11; implement only the task you were given.
- `EarthView/OSGEARTH_INTEGRATION.md` — the design every task must follow.
- `EarthView/3D_PROGRESS.md` — status board (do not redo `Merged` tasks), decisions, issues;
  update it in your PR (rule 9).

The earlier custom-renderer milestones M2–M10 are **dropped**; do not implement them.
`EarthView/scene3d/` (M1: Geodesy, Camera3D) stays and is reused for conversions.

Further docs: `EarthView/DEVELOPERS_GUIDE.md`, `EarthView/RENDERING_PIPELINE.md`,
`spikes/osgearth/` (working osgEarth-in-Qt reference, `OFFLINE_BUILD.md`).

## Build

Windows, current (llvm-mingw, kept until roadmap O10):
```
cmake -S EarthView -B EarthView/build -G "MinGW Makefiles"
cmake --build EarthView/build -j
```

Windows, target (MSVC, VS 2026 + Qt `msvc2022_64`, from roadmap O1; osgEarth via vcpkg):
```
cmake -S EarthView -B build-msvc -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build-msvc --config Release
```

Linux (cloud agents, no GPU, no osgEarth): Qt 6 + Ninja; builds 2D and unit tests only.
```
sudo apt-get install -y cmake ninja-build qt6-base-dev libqt6opengl6-dev libgl1-mesa-dev
cmake -S EarthView -B out -G Ninja -DEARTHVIEW_DEPLOY_QT=OFF
cmake --build out
ctest --test-dir out --output-on-failure
```
Software GL for 2D smoke runs: `QT_QPA_PLATFORM=offscreen EARTHVIEW_FORCE_SOFTWARE_OPENGL=1`.
MSVC/osgEarth code is verified by the Windows CI job (added in O1) and by a human.

## Rules

1. **Do not change 2D behaviour.** `Camera`, `TileRenderer`, `VectorTileRenderer`,
   `BorderRenderer`, `GridRenderer`, `CityRenderer`, `TextRenderer` keep working identically,
   including after the MSVC move.
2. **Do not extend the old pseudo-3D path** (`Camera::terrainMercatorToScreen`,
   `stealthViewMercatorToScreen`, `shaders/terrain_height.vert`, `colored_terrain.vert`,
   `terrain_line.vert`, the `isStealthViewEnabled()`/`isTerrain3DView()` branches). It is
   removed in roadmap O10.
3. **osgEarth owns 3D rendering.** No custom terrain/imagery/sky/LOD code; configure
   osgEarth. 3D code lives in `EarthView/earthview3d/` and never includes Doctrine headers.
   Follow `OSGEARTH_INTEGRATION.md` (embedded-widget requirements in §3.3).
4. **Precision:** positions crossing into 3D are geodetic/ECEF `double` (`scene3d::Geodesy`);
   never `float` world positions.
5. **Dependencies** come from the pinned vcpkg manifest; no new `FetchContent` downloads (the
   product must build offline).
6. **Tests:** non-GL logic gets Qt Test unit tests under `EarthView/tests/`; `ctest` must pass.
7. **Never commit build output** (`build/`, `build-*/`, `out/`, `x64/`, `vcpkg_installed/`,
   `*.user`) or large data (`*.dt2`, `*.mbtiles`).
8. **Style and PRs:** match existing code (C++17, Qt naming, `m_` members, `#pragma once`
   plus include guards, Doxygen `@brief`). One roadmap task per branch (`3d/<task>-<topic>`),
   under ~1500 changed lines. Description: summary, files changed, how to verify visually on
   Windows, known limitations, and what you could not verify.
9. **Tracking:** every PR for the 3D work updates `EarthView/3D_PROGRESS.md` in the same
   PR (status row, change-log entry from the template, new decisions and open issues; visual
   checks `pending (Windows)` unless actually run) and the task's Status in `ROADMAP_3D.md`.
