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
| M2 | Scene3D skeleton in MapWidget | Not started | | | | |
| M3 | Globe quadtree + frustum/SSE LOD | Not started | | | | |
| M4 | DEM terrain (skirts, normals, DTED via GDAL) | Not started | | | | |
| M5 | Imagery draping | Not started | | | | |
| M6 | Vectors, borders, grid, cities, labels | Not started | | | | |
| M7 | Lighting, sky, fog | Not started | | | | |
| M8 | Entities (glTF), picking | Not started | | | | |
| M9 | Stealth-view controllers + remove old pseudo-3D | Not started | | | | |
| M10 | Performance + Doctrine integration API | Not started | | | | |
| S1 | osgEarth evaluation spike (`spikes/osgearth`) | In progress (not built yet) | master | — | `241c108`, `21e600c` | pending (Windows) |

**Go/no-go gate:** decide custom renderer vs osgEarth **before M3 starts**, using the spike
checklist in `spikes/osgearth/README.md`. Record the outcome under Decisions (D-007).

---

## Change log (newest first)

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
| D-007 | Custom renderer vs osgEarth — **pending**, decide before M3 from spike results. | — | S1 |

---

## Open issues / tech debt

| ID | Item | Owner / task | Status |
| -- | ---- | ------------ | ------ |
| I-001 | `Camera3D::frustumContains()` rebuilds all planes on every call; M3 tile culling must compute planes once per frame and reuse them. | M3 | Open |
| I-002 | ~~M1 did not set its roadmap status marker; reviewer updates on merge.~~ Marker set in #3. | M1 merge | Closed (#3) |
| I-003 | `DemLoader` only scans `*.tif/*.tiff`; DTED (`.dt2`) / VRT support via GDAL needed for the custom terrain path (osgEarth reads them already). | M4 | Open |
| I-004 | osgEarth spike unbuilt; MapBoxGL rendering of vector MBTiles (path syntax, blend2d/protobuf features) unverified. | S1 | Open |
| I-005 | ~~M0.3 snapshot tool not yet run on Windows.~~ Run on Windows 2026-10-08 (GPU, not llvmpipe): both modes render correctly (grid, borders, city labels/dots; orthographic shows curved graticule). | M0.3 | Closed (2026-10-08) |
| I-006 | `spike.earth` / `osm_style.json` use absolute `D:/Source/...` data paths; offline machines need the same layout or edited paths. | S1 | Open |

---

## Verification log (Windows, human)

Record each manual Windows check (build + visual) here; cloud agents cannot do these.

| Date | Commit / branch | Check | Result | By |
| ---- | --------------- | ----- | ------ | -- |
| 2026-10-07 | `3d/m1-geodesy` `1ad4ce8` | llvm-mingw build of app + ctest (3 suites) | Pass | review |
| 2026-10-08 | `3d/m1-geodesy` `5529c47` (post-master-merge) | llvm-mingw build of app + ctest (3 suites: `tst_mercatorprojection`, `tst_geodesy`, `tst_camera3d`) | Pass | Claude Code |
| 2026-10-08 | `3d/m1-geodesy` `5529c47` | `earthview_snapshot` run on Windows (real GPU), `--mode mercator` and `--mode orthographic` | Pass — both PNGs render grid/borders/city labels correctly; closes I-005 | Claude Code |
