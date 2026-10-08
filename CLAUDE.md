# EarthView — agent instructions

EarthView is a Qt 6 / OpenGL map rendering engine (C++17) in `EarthView/`. Its 2D modes
(Mercator, Orthographic) are integrated into the Doctrine project and must not regress.

Active work: the **`earthview3d`** library — a host-independent osgEarth 3D / stealth view
built with **MSVC** (D-007, D-008). It is finished and polished **in this repo only**; the
host application (Doctrine, Conquer superbuild) is **not on GitHub** and is wired locally
(roadmap task L1). Read before any 3D task:
- `EarthView/ROADMAP_3D.md` — tasks E1–E10; implement only the task you were given.
- `EarthView/OSGEARTH_INTEGRATION.md` — design (embedded-widget rules in §3.3).
- `EarthView/DOCTRINE_WIRING.md` — the host contract; keep it in sync with the API.
- `EarthView/3D_PROGRESS.md` — status board, decisions, issues; update it in your PR.
- `EarthView/earthview3d/` — the library (E1); its README lists the embedded-widget rules.

Never reference, recreate or guess Doctrine/Conquer source. Superseded plans (custom
renderer M2–M10, O0–O11) must not be implemented. `EarthView/scene3d/` (M1) stays.

Further docs: `EarthView/DEVELOPERS_GUIDE.md`, `EarthView/RENDERING_PIPELINE.md`,
`spikes/osgearth/` (working osgEarth-in-Qt reference, `OFFLINE_BUILD.md`).

## Build

Windows, 2D only (llvm-mingw, still supported):
```
cmake -S EarthView -B EarthView/build -G "MinGW Makefiles"
cmake --build EarthView/build -j
```

Windows, 3D (MSVC + Qt `msvc2022_64`; osgEarth via the repo vcpkg manifest from roadmap E1):
```
cmake -S EarthView -B build-msvc -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build-msvc --config Release
```

Linux (cloud agents, no GPU): Qt 6 + Ninja; 2D + unit tests. After E1 also `earthview3d`
via vcpkg osgEarth (slow first build — use the binary cache).
```
sudo apt-get install -y cmake ninja-build qt6-base-dev libqt6opengl6-dev libgl1-mesa-dev
cmake -S EarthView -B out -G Ninja -DEARTHVIEW_DEPLOY_QT=OFF
cmake --build out
ctest --test-dir out --output-on-failure
```
Software GL for 2D smoke runs: `QT_QPA_PLATFORM=offscreen EARTHVIEW_FORCE_SOFTWARE_OPENGL=1`.
MSVC/osgEarth code is verified by the Windows CI job (added in E1) and by a human.

## Rules

1. **Do not change 2D behaviour.** `Camera`, `TileRenderer`, `VectorTileRenderer`,
   `BorderRenderer`, `GridRenderer`, `CityRenderer`, `TextRenderer` keep working identically,
   including after the MSVC move.
2. **Do not extend the old pseudo-3D path** (`Camera::terrainMercatorToScreen`,
   `stealthViewMercatorToScreen`, `shaders/terrain_height.vert`, `colored_terrain.vert`,
   `terrain_line.vert`, the `isStealthViewEnabled()`/`isTerrain3DView()` branches). It is
   removed in roadmap E9.
3. **osgEarth owns 3D rendering.** No custom terrain/imagery/sky/LOD code; configure
   osgEarth. 3D code lives in `EarthView/earthview3d/`, is host-independent, and its public
   headers include no osgEarth/OSG headers (pimpl). Follow `OSGEARTH_INTEGRATION.md` §3.3.
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
   checks `pending (Windows)` unless actually run), the task's Status in `ROADMAP_3D.md`, and
   `DOCTRINE_WIRING.md` whenever the public API changes.
