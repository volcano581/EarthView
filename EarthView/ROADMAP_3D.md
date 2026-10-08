# EarthView / Doctrine 3D — Roadmap for Agent Handoff

> Execution status, change log, decisions and open issues: **[3D_PROGRESS.md](3D_PROGRESS.md)**.

**Direction (2026-10-08):** the 3D / stealth view is built on **osgEarth** (D-007), embedded
in-process in Doctrine, and Doctrine, cgf-engine and EarthView move to the **MSVC**
toolchain (D-008). The earlier custom-renderer plan (M2–M10) is dropped; M0 and M1 are done
and their results are kept (CI and tests; `scene3d/Geodesy` + `Camera3D` are reused for
coordinate conversion and camera maths).

The roadmap is written for cloud AI coding agents (Claude Code on the web /
`claude --remote`), one task per session, one branch and one PR per task.

---

## 0. Ground rules for every agent

1. **Do not change 2D behaviour.** EarthView's Mercator/Orthographic map (and Doctrine's
   2D GIS map) must look and behave the same after every task. The toolchain move (O1, O2)
   must not change rendering.
2. **Do not extend the old pseudo-3D path** in EarthView (`Camera::terrainMercatorToScreen`,
   `stealthViewMercatorToScreen`, `shaders/terrain_height.vert`, `colored_terrain.vert`,
   `terrain_line.vert`, the `isStealthViewEnabled()`/`isTerrain3DView()` branches). It is
   removed in O10.
3. **osgEarth owns 3D rendering.** Do not write custom terrain, imagery, sky or LOD code;
   configure osgEarth (earth files, layers, `Util` classes) instead. Custom OpenGL is only for
   things osgEarth cannot do, and needs a recorded decision first.
4. **Precision:** entity positions crossing into the 3D view are geodetic/ECEF in `double`
   (`scene3d::Geodesy`). Never pass world positions as `float`.
5. **One GL owner per widget.** osgEarth renders only inside its own `QOpenGLWidget`; never
   mix osgEarth and EarthView 2D renderers in one widget/context.
6. **Dependencies come from the pinned vcpkg manifest** (baseline in `vcpkg.json`). Do not
   download libraries in CMake (`FetchContent` of new deps) — the product must build offline
   (`spikes/osgearth/OFFLINE_BUILD.md`).
7. **Tests:** non-GL logic (conversions, protocol/snapshot mapping, camera controller maths,
   config parsing) gets Qt Test unit tests; `ctest` must pass.
8. **Never commit build output** (`build/`, `out/`, `x64/`, `vcpkg_installed/`, `*.user`).
9. **PRs:** under ~1500 changed lines (vendored code excepted), description with summary,
   files changed, how to verify visually on Windows, known limitations, and what you could
   not verify. Update `3D_PROGRESS.md` and this file's Status column in the same PR.
10. **Style:** match existing code — C++17, Qt naming, `m_` members, `#pragma once` plus
    include guards, Doxygen `@brief` on public classes.

### Environment caveats
- **Cloud agents run on Linux without a GPU.** They can build and unit-test, but cannot run
  the MSVC build or look at rendering. Windows/MSVC builds are checked by **GitHub Actions
  `windows-latest`** jobs (added in O1) and visual checks are done by a human on Windows.
- **osgEarth on CI:** building osgEarth through vcpkg takes 30–60 min; CI must use the vcpkg
  binary cache (GitHub Actions cache) — see O3.
- **Two repos:** EarthView (`volcano581/EarthView`) and Doctrine/cgf-engine
  (`D:\Source\cgf-engine`, **not yet under git** — O0). Tasks name which repo they change.

---

## 1. Milestones

| #   | Milestone | Repo | Depends on | Size | Visual check | Status |
| --- | --------- | ---- | ---------- | ---- | ------------ | ------ |
| M0  | Repo hygiene, cross-platform build, CI, tests | EarthView | — | S | no | **Done** (`2cda9e8`, `5682f87`) |
| M1  | Geodesy + Camera3D + RTE math | EarthView | M0 | M | no | **Done** ([#3](https://github.com/volcano581/EarthView/pull/3), `23dcfe4`) |
| S1  | osgEarth evaluation spike | EarthView | — | M | yes | **Done** — decision D-007 (open items moved to O5/O7/O8) |
| O0  | Put Doctrine/cgf-engine under git + GitHub, add CLAUDE.md | Doctrine | — | S | no | |
| O1  | EarthView builds with MSVC (Qt `msvc2022_64`), Windows CI | EarthView | — | S | yes (2D unchanged) | |
| O2  | cgf-engine + Doctrine build with MSVC, Windows CI | Doctrine | O0 | M | yes (2D unchanged) | |
| O3  | osgEarth via pinned vcpkg manifest in Doctrine, CI binary cache | Doctrine | O2 | M | no | |
| O4  | `earthview3d` library: production osgEarth Qt widget | EarthView | O1 | M | yes | |
| O5  | Doctrine 3D window fed by `SimRenderBridge` | Doctrine | O3, O4 | M | yes | |
| O6  | Entity visualisation: models, symbols, labels, picking | EarthView + Doctrine | O5 | L | yes | |
| O7  | Stealth camera controllers + HUD | EarthView | O4 | M | yes | |
| O8  | Offline data pipeline (imagery, DEM, overlays, cache) | EarthView | O4 | M | yes | |
| O9  | Offline packaging + air-gapped build for Doctrine | Doctrine | O3, O5 | M | yes | |
| O10 | Remove EarthView pseudo-3D path; unify Doctrine's GIS copy | both | O5 | M | yes | |
| O11 | Performance + polish (1k entities, multi-window) | both | O6, O7 | M | yes | |
| M2–M10 | Custom renderer milestones | EarthView | — | — | — | **Dropped** (D-007) |

O1 and O0 can run in parallel; O4, O7 and O8 can run in parallel once O1 is merged.

---

## 2. Tasks

Agents: trust the Status column / DONE markers; do not redo finished tasks.

### O0 — Doctrine under version control (human-led)
- `git init` in `D:\Source\cgf-engine`; `.gitignore` for `build*/`, `out/`, large data
  (`*.mbtiles`, `*.dt?`, SISO CSV if licensed), push to a private GitHub repo.
- Add `CLAUDE.md` (rules from section 0 + Doctrine build commands) and link this roadmap.
- Record in `3D_PROGRESS.md` where Doctrine's copy of EarthView GIS (`doctrine/src/GIS/`,
  `cgf-engine/EarthView/`) came from and how far it has drifted (input for O10).

### O1 — EarthView on MSVC
- Make `CMakeLists.txt` work with the Qt `msvc2022_64` kit and VS 2026 (keep llvm-mingw and
  Linux working until O10): MSVC warning flags, `/utf-8`, `/permissive-`, `NOMINMAX`,
  `_USE_MATH_DEFINES` where needed; zlib (`Qt6::ZlibPrivate` or vcpkg `zlib`).
- Add a GitHub Actions `windows-latest` job: install Qt via `jurplel/install-qt-action`
  (msvc2022_64), configure, build, `ctest`.
- Acceptance: CI green on Linux + Windows; human compares 2D screenshots (`earthview_snapshot`
  Mercator + Orthographic) llvm-mingw vs MSVC — identical.

### O2 — cgf-engine + Doctrine on MSVC
- Same as O1 for the Doctrine repo: `DOCTRINE_QT_ROOT` defaults to `msvc2022_64`; replace
  `FetchContent` GLM with the vcpkg port (rule 6); fix MSVC warnings/errors (`/W4`).
- Windows CI job; all existing tests pass.
- Acceptance: Doctrine runs with MSVC, 2D map and simulation behave as before (human check).

### O3 — osgEarth dependency in Doctrine
- `vcpkg.json` manifest at the Doctrine root with `osgearth` (same `builtin-baseline` as
  `spikes/osgearth/vcpkg.json`), `glm`, `zlib`.
- CMake: `find_package(osgEarth CONFIG)`, OpenSceneGraph; install rules from the spike
  (DLLs, `osgPlugins-*`, `share/gdal`, `share/proj`, **plus MSVC runtime** via
  `InstallRequiredSystemLibraries`, **plus osgEarth data folder** for sky textures).
- CI: cache vcpkg binaries (`VCPKG_BINARY_SOURCES` with `x-gha` or `files` + actions/cache).
- Acceptance: CI builds and links a trivial osgEarth call; second CI run restores from cache.

### O4 — `earthview3d` library (osgEarth Qt widget)
Start from `spikes/osgearth/OsgWidget.*` (already debugged on Windows). New target
`earthview3d` in EarthView under `earthview3d/`, MSVC-only (`if(TARGET osgEarth::osgEarth)`).
- `GlobeView3D : QOpenGLWidget` — embedded viewer with the spike's fixes (null-terrain guard,
  vertex-attribute aliasing, home viewpoint via manipulator, per-frame FBO id, DPI).
- Config struct: earth file path, initial viewpoint, sky on/off + date/time, log depth.
- Public API: `setViewpoint`, `flyTo`, `viewpoint()`, `snapshot(QString)`, signal
  `viewpointChanged`. No Doctrine types in this library.
- Fix spike issue I-008 (chase view under terrain): enable `EarthManipulator` terrain
  avoidance / min pitch, re-test.
- Tests: config parsing, viewpoint ↔ `scene3d::Geodesy` conversions.
- Acceptance: example app (spike rewritten on the library) renders home/chase/stealth
  snapshots correctly on Windows.

### O5 — Doctrine 3D window
- Determine what `RenderEntity::position` (`glm::vec3`) represents in Doctrine and add a
  double-precision geodetic position (lat, lon, alt) + heading/pitch/roll + entity kind
  (SISO enumeration) + force to the snapshot. Keep the 2D map's fields unchanged.
- Secondary window (`MainWindow` action "3D View") hosting `GlobeView3D`; consumes
  `SimRenderBridge::consume()` each frame; creates/updates/removes osgEarth nodes per
  entity (`GeoTransform` + placeholder model). Entity map keyed by EntityID.
- Tests: snapshot → geodetic conversion, add/update/remove diffing.
- Acceptance: entities move in 3D in sync with the 2D map; closing/reopening the window works.

### O6 — Entity visualisation
- Models: per-kind model table (osgb/glTF via OSG plugins; config file mapping SISO kind →
  model, scale, orientation offset); fallback icon/billboard when far (LOD by pixel size).
- Labels and force colours (osgEarth `LabelNode`/`PlaceNode`); optional MIL-STD-2525 icons
  reusing Doctrine's symbol atlas.
- Picking (osgEarth `ObjectIndex`/`RTTPicker`) → selection shared with the 2D map.
- Acceptance: 200 mixed entities readable at all ranges; click-select syncs both views.

### O7 — Stealth camera controllers
- Modes: orbit (EarthManipulator), tether/chase (follow selected entity, smoothed),
  first-person (entity attitude), free-fly (WASD + mouse look). Terrain clearance clamp.
- Smooth transitions; HUD overlay (heading, pitch, altitude AGL/MSL, entity name, mode).
- Acceptance: VR-Vantage-like stealth: attach to a moving entity, look around, detach, fly.

### O8 — Offline data pipeline
- Imagery: produce raster MBTiles offline (decision needed: pre-render OpenMapTiles vector
  tiles with a renderer, or source satellite/raster imagery) — closes I-007.
- DEM: `dted_1arc.vrt` + overviews (`gdaladdo`), document adding tiles; drop irrelevant
  `N18.tif` from the default map.
- Overlays: borders, cities as osgEarth feature layers; grid (`GraticuleLayer`).
- Earth file template with **relative paths** and an osgEarth cache (filesystem/sqlite).
- Acceptance: unplug network → full map renders; second launch loads from cache.

### O9 — Offline packaging for Doctrine
- `cmake --install` produces a self-contained folder (Qt, osgEarth, GDAL/PROJ data, MSVC
  runtime, osgEarth data, earth files); data package layout documented.
- Update `OFFLINE_BUILD.md` for Doctrine (feeder/offline vcpkg caches, VS 2026 on both).
- Acceptance: install on a clean offline Windows VM, Doctrine 2D + 3D work.

### O10 — Cleanup and unification
- Delete EarthView's pseudo-3D / stealth path (Camera branches, shaders, UI actions) —
  replaced by `earthview3d`.
- Make Doctrine consume EarthView's 2D GIS code from one place (submodule or vcpkg overlay
  port) instead of the copied `doctrine/src/GIS/`.
- Acceptance: both repos build; 2D unchanged; no duplicate GIS sources.

### O11 — Performance and polish
- Frame-time budget with osgEarth stats; 1,000 moving entities at 60 fps on the target GPU.
- Multiple 3D windows / viewpoints; shared-context behaviour with the 2D map window.
- Acceptance: documented numbers in `3D_PROGRESS.md`.

---

## 3. Running this with Claude cloud agents

1. Each repo needs a `CLAUDE.md` with section 0 and its build commands (EarthView has one;
   Doctrine gets one in O0).
2. One task per session, e.g.:
   > Implement task **O1** from `EarthView/ROADMAP_3D.md`, following `CLAUDE.md`. Branch
   > `3d/o1-msvc`. Make Linux and Windows CI green, update `3D_PROGRESS.md`, open a PR.
3. Review loop: CI (Linux + Windows) → `/code-review` → human Windows visual check → merge →
   tracker updated.
4. Tasks needing a human: O0 (repo creation), all visual checks, O8 imagery decision, O9
   clean-VM test.

## 4. Risks

| Risk | Mitigation |
| ---- | ---------- |
| MSVC move breaks Doctrine/2D | O1/O2 isolated, screenshot comparison, keep llvm-mingw until O10 |
| osgEarth CI build time | vcpkg binary cache in CI (O3) |
| osgEarth GL state vs Qt | separate widget/context per renderer (rule 5); spike already proves embedding |
| Offline imagery source | explicit decision in O8; vector-MBTiles rendering in osgEarth failed (I-007) |
| Doctrine not in git | O0 first; agents cannot touch Doctrine before it |
| LGPL obligations (osgEarth/OSG) | dynamic linking via vcpkg DLLs; ship licences in the install folder (O9) |
