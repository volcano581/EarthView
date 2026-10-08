# EarthView 3D — Progress Tracker

Single source of truth for **what has actually changed** in EarthView for 3D rendering.
The plan lives in [ROADMAP_3D.md](ROADMAP_3D.md); this file records execution: who did
what, in which PR/commit, how it was verified, and what is still open.

Last updated: 2026-10-08

---

## How to update this file (humans and cloud agents)

**Every PR that changes EarthView for the 3D work must update this file in the same PR.**

1. **Status board** — set your task's row: status, branch, PR link, merge commit (fill the
   merge commit after merge, or leave `—` and the reviewer fills it).
2. **Change log** — add one entry at the **top** of the log using the template below.
   Keep it factual: what changed, files/modules touched, tests added, how verified.
3. **Decisions** — if you made a design choice another task depends on (coordinate
   convention, library, file layout, API shape), add a row. Never silently change a
   recorded decision; propose it in your PR description and update the row once approved.
4. **Open issues / tech debt** — add anything you deferred, noticed but did not fix, or
   could not verify. Close items (strike through + PR ref) when fixed.
5. **Verification** — state honestly what was verified where. Cloud agents run on Linux
   without a GPU: mark visual checks as `pending (Windows)` — never as done.
6. Do not rewrite other entries except to fix factual errors; this is an append-only log.

Status values: `Not started` · `In progress` · `In review` · `Changes requested` · `Merged` · `Blocked` · `Dropped`

### Change-log entry template
```
### YYYY-MM-DD — <Task id>: <short title>  (<Status>)
- **Branch / PR:** `3d/...` / <PR link> — merge `<sha>`
- **Author:** <cloud agent | name>
- **Changes:** <bullets: new modules, modified files and why>
- **Existing files touched:** <list, or "none"> (2D code must stay unchanged in behaviour)
- **Tests:** <new tests; ctest result + platform>
- **Verified:** Linux CI <✓/✗> · Windows build <✓/pending> · Visual check <✓/pending/n.a.>
- **Follow-ups:** <items added to Open issues, or "none">
```

---

## Status board

| Task | Title | Status | Branch | PR | Merge commit | Visual check |
| ---- | ----- | ------ | ------ | -- | ------------ | ------------ |
| M0.1 | Repo hygiene (untrack build output) | Merged | — (direct) | — | `2cda9e8` | n.a. |
| M0.2 | Cross-platform CMake, Qt Test, Linux CI | Merged | `3d/m0-build-ci` | — | `5682f87` | n.a. |
| M0.3 | Offscreen snapshot tool | Merged | `3d/m0-snapshot` | — | `5682f87` | pending (Windows) |
| M1 | Geodesy + Camera3D + RTE math | Merged | `3d/m1-geodesy` | [#3](https://github.com/volcano581/EarthView/pull/3) | `23dcfe4` | n.a. |
| M2–M10 | Custom renderer milestones | Dropped (D-007) | | | | |
| T1 | Local Doctrine integration test (Conquer, branch `earthview-3d-test`) | Done (local) | — | — | `1fd3d89` (Conquer) | ✓ (2026-10-08) |
| O0–O11 | Earlier osgEarth plan | Superseded by E1–E10 + L1 (D-012) | | | | |
| E1 | `earthview3d` library + vcpkg manifest + Windows CI | In review | `3d/e1-earthview3d` | [#4](https://github.com/volcano581/EarthView/pull/4) | — | pending (Windows) |
| E2 | `earthview3d_demo` with synthetic host feed | Not started | | | | |
| E3 | Entity layer (models, labels, LOD, picking) | Not started | | | | |
| E4 | Camera controllers + HUD | Not started | | | | |
| E5 | Terrain services | Not started | | | | |
| E6 | Offline data pipeline | Not started | | | | |
| E7 | Packaging + runtime deploy | Not started | | | | |
| E8 | Wiring kit / host contract test | Not started | | | | |
| E9 | Cleanup pseudo-3D | Not started | | | | |
| E10 | Performance + polish | Not started | | | | |
| L1 | Local wiring into Doctrine (off GitHub) | Not started | local | — | | |
| S1 | osgEarth evaluation spike (`spikes/osgearth`) | Done — led to D-007 | master | — | `241c108`…`0b5f729` | partial ✓ (2026-10-08) |

**Direction:** host-independent `earthview3d` (osgEarth, MSVC) finished on GitHub by agents; Doctrine wired locally (D-007, D-008, D-012). Design: [OSGEARTH_INTEGRATION.md](OSGEARTH_INTEGRATION.md) · Local wiring: [DOCTRINE_WIRING.md](DOCTRINE_WIRING.md).

---

## Change log (newest first)

### 2026-10-08 — E1: `earthview3d` library, vcpkg manifest, Windows CI  (In review)
- **Branch / PR:** `3d/e1-earthview3d` / [#4](https://github.com/volcano581/EarthView/pull/4) — merge —
- **Author:** cloud agent
- **Changes:**
  - `earthview3d/reference/GlobeView3D.*` moved to `earthview3d/src/GlobeView3D.cpp` +
    `include/earthview3d/GlobeView3D.h`; namespace `earthview3d`; **pimpl** — public headers
    include only Qt/STL (`EntityState3D.h`, `MapConfig.h`, `EntityDiff.h`, `GlobeView3D.h`).
    All eight embedded-widget rules kept (listed in `earthview3d/README.md`).
  - `DOCTRINE_3D_LOGDEPTH`/`DOCTRINE_3D_NO_SKY` → `EARTHVIEW3D_LOGDEPTH`/`EARTHVIEW3D_NO_SKY`.
  - `MapConfig` (earth file, data root, cache dir, sky on/off + time; JSON loading,
    path resolution, validation) replaces the hard-coded earth-file path and sky date.
  - `EntityDiff`: GL-free add/update/remove logic split out of `setEntities()`; id 0
    ignored, first entry wins on duplicate ids. `EntityState3D` gains `visible` (contract §2).
  - The widget calls `osgEarth::initialize()` itself; `cacheDir` → `OSGEARTH_CACHE_PATH`.
  - CMake: `earthview3d_core` (Qt Core, always built) + `earthview3d` and
    `earthview3d_viewer` when `EARTHVIEW_BUILD_3D` (default ON only if osgEarth is found).
  - `earthview3d_viewer`: the `spikes/osgearth` app ported onto the library (home/chase/
    stealth, `--snapshot`); the spike itself stays until E2 retires it.
  - Earth file template `Data/maps/earthview.earth` (from `spike.earth`): paths relative to
    the earth file, no `D:/` paths; dropped the off-area `DEM90TIF/N18.tif` layer (I-010).
  - `vcpkg.json` at the repo root (osgEarth, baseline `2cfff9c`, same as the spike).
  - CI: new `windows` job — MSVC (Ninja), Qt 6.8.3 `msvc2022_64`, vcpkg at the baseline,
    binary cache in `actions/cache` (full on success, partial on failure); build + `ctest`.
- **Existing files touched:** `EarthView/CMakeLists.txt` (`add_subdirectory(earthview3d)`
  only), `tests/CMakeLists.txt`, `.github/workflows/ci.yml`, `CLAUDE.md` (reference path),
  `DOCTRINE_WIRING.md` (API changes), `ROADMAP_3D.md` (status). No 2D code changed.
- **Tests:** `tst_mapconfig` (10 cases), `tst_entitydiff` (7 cases); Linux ctest 5/5 pass.
  `GlobeView3D.cpp`, the viewer and the moc output were syntax-checked on Linux against
  the osgEarth 3.8.1 source headers and OSG 3.6.5 (no errors/warnings); not linked on Linux.
- **Verified:** Linux CI ✓ · Windows CI ✓ (VS 2026 / MSVC 14.51, osgEarth 3.8.1 via vcpkg; all targets link, ctest 5/5 with Qt 6.8.3; after review round 1: Qt **6.11.0** + C++20, all targets link, ctest 6/6) ·
  Visual check pending (Windows): run `earthview3d_viewer --view home|chase|stealth --snapshot`.
- **Review round 1 (owner, Windows run with Qt 6.11):** fixed
  1. Qt kit selection: llvm-mingw fallbacks only `if(MINGW)` and only without an explicit
     `CMAKE_PREFIX_PATH`; MSVC appends `msvc2022_64` as a low-priority fallback;
     `EARTHVIEW_QT_PREFIX` still wins.
  2. C++20 when `MSVC` (Qt 6.11 moc output fails in C++17 with MSVC); Windows CI pinned to
     **Qt 6.11.0** to match developer machines and the host (D-015, D-016). aqtinstall 3.3.0
     cannot install Qt ≥ 6.11 on Windows, so CI uses aqtinstall master (pinned commit) until
     3.4 is released.
  3. Maps did not load from the build tree: osgDB reads `OSG_LIBRARY_PATH` when its DLL
     loads, before `main()`. New `earthview3d::configureRuntime(RuntimePaths)` adds the
     plugin folder to `osgDB::Registry`'s library path list and sets GDAL/PROJ data
     (environment when unset + `CPLSetConfigOption`/`OSRSetPROJSearchPaths`). The first
     `GlobeView3D` calls it with `RuntimePaths::buildTreeDefaults()` (vcpkg folders from
     CMake) if the host did not; the viewer's own env setup is gone (D-017).
  - Minor: `~GlobeView3D` releases all OSG objects while the context is current; the data
    root is added to osgDB's data path list only once. Test `tst_runtimepaths` (3 cases).
- **Follow-ups:** I-017, I-018, I-019.

### 2026-10-08 — Roadmap re-scoped to EarthView-only agent work (D-012)
- Doctrine cannot be pushed to GitHub. `ROADMAP_3D.md` rewritten: tasks E1–E10 finish the
  host-independent `earthview3d` library in this repo; L1 wires it into Doctrine locally.
- Added `DOCTRINE_WIRING.md` (host contract, local steps, acceptance checklist) and
  `earthview3d/reference/` (the Doctrine-free `GlobeView3D` widget from the local test,
  not built yet — E1 turns it into the library). `CLAUDE.md` updated.

### 2026-10-08 — Conquer: 2D + osgEarth 3D integration test  (local branch)
- **Where:** `D:\Source\conquer-3d`, worktree of Conquer on branch `earthview-3d-test`,
  commit `1fd3d89` (no git remote; not pushed). Based on the WIP commit `2d77598`.
- **Author:** Claude Code (local Windows session)
- **What:** new opt-in `doctrine_view3d` library (`GlobeView3D` osgEarth widget +
  `Doctrine3DWindow` adapter), "3D View" action in Doctrine's main window, fed from the
  same `SimRenderBridge` snapshot as the 2D `TacticalMapWidget`. Built with Conquer's
  existing `msvc` preset (VS 2022) + `-DDOCTRINE_ENABLE_OSGEARTH=ON
  -DDOCTRINE_OSGEARTH_PREFIX=<spike vcpkg_installed/x64-windows>`; osgEarth runtime deployed
  next to `doctrine.exe` by a post-build step.
- **Results (demo scenario, 8 entities, Islamabad):** 2D map unchanged; 3D window shows DTED
  terrain + OSM imagery; entity positions match the 2D map; chase and ground-level stealth
  views work after two fixes below.
- **Findings:**
  - Conquer already builds with MSVC (DIS vendor library requires it) → roadmap O1/O2 are
    largely done there; osgEarth from vcpkg (VS 2022) links cleanly.
  - `RenderEntity` already carries EntityID, double lat/lon, ellipsoid altitude, heading
    (→ I-013 mostly resolved); `CoordBridge` answers Q1 (tangent-plane ENU at a
    `GeodeticOrigin`, ECEF hub).
  - Sim ground units sit on a flat plane at the origin altitude (500.7 m); DTED there is
    520–525 m → units ~20–24 m underground (I-015). Test clamps non-air units to terrain.
  - osgEarth `LogarithmicDepthBuffer` drops terrain near the camera in the embedded widget
    (black + red/yellow bands below the horizon) — cause of spike issue I-008. Now off by
    default with `setNearFarRatio(2e-5)` (D-010).
  - Adding OSG's include dir to the whole `doctrine` target pulled vcpkg's Boost/glm into
    every TU and broke `osg/Math` (macro clash) → 3D code isolated in its own library.
- **Verified:** Windows MSVC build ✓ · Visual check ✓ (2D + 3D home/chase/stealth snapshots)
- **Follow-ups:** I-015, I-016.

### 2026-10-08 — Decision: osgEarth in-process, MSVC; roadmap and design rewritten
- **Author:** Claude Code with the project owner
- **Decisions:** D-007 (osgEarth), D-008 (in-process, MSVC), D-009 (proposed coordinate
  conversion).
- **Docs:** `ROADMAP_3D.md` rewritten (tasks O0–O11, M2–M10 dropped);
  new `OSGEARTH_INTEGRATION.md` (architecture, threading, GL context, build, coordinates,
  snapshot contract, API, cameras, data, deployment, tests, risks, open questions);
  `CLAUDE.md` rules updated.
- **Findings in Doctrine (`D:\Source\cgf-engine`):** not under git; builds with Qt
  llvm-mingw; no compiler-specific code found; `RenderEntity` lacks EntityID/orientation/
  kind; no geodetic scenario origin; main window uses `DoctrineViewport`, and
  `DoctineGISMap` entity drawing is a TODO.
- **Follow-ups:** I-012, I-013, I-014.

### 2026-10-08 — S1: osgEarth spike first build and run  (In progress)
- **Branch / PR:** master (direct)
- **Author:** Claude Code (local Windows session)
- **Environment:** osgEarth 3.8.1, OSG 3.6.5, GDAL 3.12.4 from vcpkg; built with VS 2022
  Enterprise (the plan says VS 2026 — see I-009); Qt 6.11 `msvc2022_64`; NVIDIA RTX 3050 Ti,
  GL 4.6 compatibility profile.
- **Fixes:** crash from null `MapNode::getTerrain()` before the first frame; enabled
  vertex-attribute aliasing + matrix uniforms on the embedded window (osgEarth shaders);
  registered the home viewpoint with `EarthManipulator` (camera was framing the sky dome);
  corrected the borders shapefile path. Added `--view home|chase|stealth`,
  `--snapshot <png> --after <sec>` and a viewpoint printout for automated checks.
- **Results:** install step produces a working self-contained folder; DTED terrain, imagery
  draping (online OSM), sky and the stealth view (`F`) render correctly. Offline imagery
  from the vector MBTiles does not work (I-007); chase view (`T`) is broken (I-008).
- **Verified:** Windows build ✓ · Visual check partial ✓ (snapshots of home/chase/stealth)
- **Follow-ups:** I-007, I-008, I-009, I-010, I-011.

### 2026-10-07 — M1: WGS84 geodesy, Camera3D, RTE math  (Merged 2026-10-08, `23dcfe4`)
- **Branch / PR:** `3d/m1-geodesy` / [#3](https://github.com/volcano581/EarthView/pull/3) — commit `1ad4ce8`
- **Author:** cloud agent
- **Changes:** new static lib `earthview_scene3d` (no Qt dependency):
  `scene3d/Geodesy.*` (WGS84 geodetic↔ECEF with iterative Bowring, ENU frame, surface
  normal, ray–ellipsoid intersection); `scene3d/Camera3D.*` (ECEF position + quaternion,
  heading/pitch/roll placement, lookAt, rotation-only RTE view matrix, reversed-Z
  infinite-far projection, near + 4 side frustum planes, screen rays). GLM 1.0.1 vendored
  in `third_party/glm` (MIT, `copying.txt` included).
- **Existing files touched:** `CMakeLists.txt` (adds the library only; not yet linked into
  the app), `tests/CMakeLists.txt`.
- **Tests:** `tst_geodesy` (9 cases), `tst_camera3d` (10 cases). Windows llvm-mingw:
  3/3 suites pass; EarthView app still builds.
- **Verified:** Linux CI ✓ (PR run, 3/3 suites) · Windows build ✓ (review, 2026-10-07) · Visual n.a.
- **Review notes:** maths and sign conventions checked by hand (ECEF, Bowring, ENU,
  heading/pitch/roll matrices, reversed-Z, frustum plane normals). PR exceeds the ~1500-line
  guideline only because of vendored GLM — accepted. Agent did not update the roadmap
  status marker. Follow-ups: I-001, I-002.

### 2026-10-07 — DTED2 elevation data + VRT mosaic
- 34 SRTM 1″ DTED2 tiles (N30–N34, E066–E077, ~849 MB) placed in `Data/DEM/` (git-ignored).
- `Data/DEM/dted_1arc.vrt` mosaics them (relative paths). EarthView's `DemLoader` does not
  read DTED yet → I-003. Commit `241c108`.

### 2026-10-07 — S1: osgEarth evaluation spike
- `spikes/osgearth/`: Qt6 `QOpenGLWidget` hosting osgEarth; DTED VRT terrain; offline
  imagery rendered from the OpenMapTiles vector MBTiles via MapBoxGL style
  (`osm_style.json`); borders shapefile; sky; log depth; moving test entity with chase/
  stealth tether cameras. vcpkg manifest pinned (baseline `2cfff9c`), `cmake --install`
  produces a self-contained offline folder, `OFFLINE_BUILD.md` documents air-gapped builds.
- Built with VS 2026 + Qt `msvc2022_64` (separate from EarthView's llvm-mingw build).
- **Not compiled yet** — osgEarth not installed on the dev machine at time of writing.
  Commits `241c108`, `21e600c`, plus the MBTiles switch.

### 2026-10-07 — M0.2 + M0.3 merged  (Merged `5682f87`)
- Optional Qt Test, zlib fallback for distro Qt, `EARTHVIEW_BUILD_TESTS`, GitHub Actions
  Linux CI (`.github/workflows/ci.yml`), `tst_mercatorprojection`;
  `tools/earthview_snapshot.cpp` offscreen render tool.
- Existing files touched: `CMakeLists.txt`, one-line Qt6 string-compare fix in
  `MainWindow.cpp` (no behaviour change).

### 2026-10-07 — M0.1 + planning docs  (`2cda9e8`)
- Untracked 159 generated files under `EarthView/build/`; ignore rules in repo-root
  `.gitignore`. Added `ROADMAP_3D.md` and root `CLAUDE.md` (agent rules).

---

## Decisions

| ID | Decision | Date | Ref |
| -- | -------- | ---- | --- |
| D-001 | Keep the 2D engine (Mercator/Orthographic) unchanged; build 3D as a separate `scene3d/` path. Old pseudo-3D/stealth path is frozen and removed in M9. | 2026-10-07 | ROADMAP §0 |
| D-002 | World coordinates: WGS84 ECEF in `double`; GPU data relative-to-eye `float`. | 2026-10-07 | ROADMAP §0.3 |
| D-003 | Depth: reversed-Z, infinite far, `[0,1]` clip (GL 4.5); log depth on GL 3.3 fallback. | 2026-10-07 | ROADMAP §0.4 |
| D-004 | Math library: GLM 1.0.1, vendored in `EarthView/third_party/glm` (offline-friendly). | 2026-10-07 | M1 |
| D-005 | Camera conventions: camera space +X right, +Y up, −Z forward; heading clockwise from north, pitch negative = down, roll positive = bank right. | 2026-10-07 | M1 `Camera3D.h` |
| D-006 | `earthview_scene3d` is a Qt-free static library; Qt/GL integration lives in later layers. | 2026-10-07 | M1 |
| D-007 | **osgEarth** provides the 3D / stealth view; the custom renderer (M2–M10) is dropped. `scene3d` (M1) is kept for conversions. | 2026-10-08 | S1, `OSGEARTH_INTEGRATION.md` |
| D-008 | osgEarth is embedded **in-process** in Doctrine; Doctrine, cgf-engine and EarthView move to **MSVC** (VS 2026, Qt `msvc2022_64`). llvm-mingw kept for EarthView 2D until O10. | 2026-10-08 | ROADMAP O1/O2 |
| D-009 | ~~aeqd projection proposal~~ Superseded: Conquer's `CoordBridge` (tangent-plane ENU at a `GeodeticOrigin`, ECEF hub) already provides lat/lon/alt in `RenderEntity`; the 3D view uses those directly. | 2026-10-08 | Conquer test |
| D-010 | osgEarth logarithmic depth buffer is **off** by default in the embedded widget (drops near-camera terrain); use `Camera::setNearFarRatio(2e-5)`. | 2026-10-08 | Conquer test |
| D-012 | Doctrine/Conquer stays **off GitHub**. Agents finish `earthview3d` + demo host + packaging + wiring kit in EarthView (E1–E10); final Doctrine wiring is local (L1). Only the Doctrine-free widget from the test is published (`earthview3d/reference/`). | 2026-10-08 | ROADMAP, DOCTRINE_WIRING.md |
| D-013 | `earthview3d` = `earthview3d_core` (GL-free, Qt Core, always built and tested) + `earthview3d` (osgEarth widget, built only with `EARTHVIEW_BUILD_3D`, default ON when osgEarth is found). osgEarth/OSG include dirs and libraries are PRIVATE to `earthview3d`. | 2026-10-08 | E1 |
| D-014 | Default map lives in the data tree: `EarthView/Data/maps/earthview.earth` with paths relative to the earth file; `MapConfig.dataRoot` = `EarthView/Data` in development. | 2026-10-08 | E1 |
| D-015 | Windows CI: `windows-latest`, MSVC (runner has VS 2026 / MSVC 14.51) via Ninja, Qt **6.11.0** `msvc2022_64` from `install-qt-action` (same as developer machines and the host; was 6.8.3 before review round 1), vcpkg checked out at the manifest baseline, `files` binary cache stored with `actions/cache` (vcpkg no longer supports `x-gha`). | 2026-10-08 | E1 |
| D-016 | MSVC builds of EarthView use **C++20** (Qt 6.11 moc output does not compile with MSVC in C++17); MinGW/GCC builds stay C++17. | 2026-10-08 | E1 review |
| D-017 | `earthview3d` sets its own runtime paths: `configureRuntime(RuntimePaths)` adds the OSG plugin folder to `osgDB::Registry` and sets GDAL/PROJ data before first use (env vars set in `main()` are too late for osgDB). Build-tree defaults come from CMake; E7's deploy helper supplies install paths. | 2026-10-08 | E1 review |
| D-011 | The integration target is **Conquer** (`D:\Source\Conquer`: Doctrine + cgf-engine + EarthView superbuild, already MSVC), not the standalone `D:\Source\cgf-engine`. 3D code lives in an isolated library so osgEarth includes never reach other TUs. | 2026-10-08 | Conquer test |

---

## Open issues / tech debt

| ID | Item | Owner / task | Status |
| -- | ---- | ------------ | ------ |
| I-001 | ~~`Camera3D::frustumContains()` rebuilds planes per call.~~ Moot: custom tile culling dropped (D-007). | M3 | Closed (D-007) |
| I-002 | ~~M1 did not set its roadmap status marker; reviewer updates on merge.~~ Marker set in #3. | M1 merge | Closed (#3) |
| I-003 | ~~`DemLoader` cannot read DTED for the custom terrain.~~ Moot for 3D: osgEarth reads DTED/VRT (D-007). 2D DEM overlay still TIFF-only. | — | Closed (D-007) |
| I-004 | ~~osgEarth spike unbuilt.~~ Built and run 2026-10-08; MapBoxGL result tracked in I-007. | S1 | Closed (2026-10-08) |
| I-005 | ~~M0.3 snapshot tool not yet run on Windows.~~ Run on Windows 2026-10-08 (GPU, not llvmpipe): both modes render correctly (grid, borders, city labels/dots; orthographic shows curved graticule). | M0.3 | Closed (2026-10-08) |
| I-007 | Offline imagery: osgEarth `MapBoxGLImage` renders nothing (even a background-only style); `OGRFeatures` on the vector MBTiles hangs (GDAL scans the whole file). Options: pre-render a raster MBTiles from the vector tiles, obtain satellite/raster imagery, or debug the MapBoxGL layer in osgEarth source. | S1 | Open |
| I-008 | ~~Spike chase view black with red/yellow band.~~ Cause: osgEarth log depth buffer in the embedded widget (D-010). | S1 | Closed (Conquer test) |
| I-009 | Spike was configured with VS 2022 Enterprise, not VS 2026 as in `OFFLINE_BUILD.md`; the vcpkg binary cache is keyed by compiler, so feeder and offline machines must use the same one. | S1 | Open |
| I-010 | Spike deploy folder lacks the MSVC runtime DLLs (vcruntime/msvcp) and osgEarth's data folder (moon texture); `DEM90TIF/N18.tif` covers 55–56°E, 18–19°N, not the area of interest. | S1 | Open |
| I-011 | osgEarth logs "GDAL_DATA environment variable is not set" before `main()` sets it; check GDAL/PROJ actually find the deployed `share/` data. E1 sets GDAL/PROJ paths through the GDAL API as well (D-017); confirm on Windows. | S1 / E1 | Partly resolved (E1) |
| I-012 | Doctrine/cgf-engine is not under version control; agents cannot work on it until O0. | O0 | Open |
| I-013 | In Conquer `RenderEntity` already has EntityID, lat/lon (double), alt, heading; still missing pitch/roll and SISO kind (for models). | O5/O6 | Partly resolved |
| I-014 | ~~No geodetic origin in Doctrine.~~ Conquer has `GeodeticOrigin` + `CoordBridge`. | O5 | Closed (Conquer) |
| I-015 | Conquer ground units ride a flat plane at the origin altitude, ~20–24 m below the DTED surface near Islamabad; sim terrain must use the same DEM (or the 3D view keeps clamping ground units). | O5 | Open |
| I-016 | Conquer test uses the spike's vcpkg tree via `DOCTRINE_OSGEARTH_PREFIX` and online OSM imagery; replace with a pinned manifest (O3) and offline imagery (O8). Pinned manifest now at the repo root (`vcpkg.json`, E1); offline imagery still open (E6). | E1/E6 | Partly resolved (E1) |
| I-006 | `spike.earth` / `osm_style.json` use absolute `D:/Source/...` data paths. The library's map (`Data/maps/earthview.earth`, E1) uses relative paths; the spike files go away in E2. | S1 / E2 | Partly resolved (E1) |
| I-017 | Linux CI does not build `earthview3d` yet (roadmap: "once green"). Cloud containers cannot build osgEarth through vcpkg either: the environment's network policy blocks `sqlite.org` (sqlite3 source). Add a cached Linux vcpkg job after E1. | E1 follow-up | Open |
| I-019 | **VS 2022** + Qt 6.11 + C++20: QtTest headers hit an MSVC internal compiler error (`qrangemodel_impl.h(991): C1001`). Windows CI with **VS 2026 (MSVC 14.51)** compiles all six test suites fine, so the minimum compiler for Qt 6.11 builds is VS 2026. Developer machines on VS 2022 must upgrade (and rebuild vcpkg packages, I-009). | E1 | Open (documented) |
| I-018 | `MapConfig.cacheDir` is applied through `OSGEARTH_CACHE_PATH` before the widget's first `osgEarth::initialize()`; it is ignored if the host initialised osgEarth earlier or set the variable itself. Verify the cache fills on Windows (E6 owns caching). | E6 | Open |

---

## Verification log (Windows, human)

Record each manual Windows check (build + visual) here; cloud agents cannot do these.

| Date | Commit / branch | Check | Result | By |
| ---- | --------------- | ----- | ------ | -- |
| 2026-10-07 | `3d/m1-geodesy` `1ad4ce8` | llvm-mingw build of app + ctest (3 suites) | Pass | review |
| 2026-10-08 | master (S1 spike) | MSVC build, `cmake --install`, home/chase/stealth snapshots with DTED + online OSM | Partial — stealth ✓, chase ✗, offline vector imagery ✗ | Claude Code |
| 2026-10-08 | `3d/m1-geodesy` `5529c47` (post-master-merge) | llvm-mingw build of app + ctest (3 suites: `tst_mercatorprojection`, `tst_geodesy`, `tst_camera3d`) | Pass | Claude Code |
| 2026-10-08 | `3d/m1-geodesy` `5529c47` | `earthview_snapshot` run on Windows (real GPU), `--mode mercator` and `--mode orthographic` | Pass — both PNGs render grid/borders/city labels correctly; closes I-005 | Claude Code |
