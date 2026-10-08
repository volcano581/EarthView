# EarthView 3D — Roadmap for Agent Handoff

> Status, change log, decisions and issues: **[3D_PROGRESS.md](3D_PROGRESS.md)** ·
> Design: **[OSGEARTH_INTEGRATION.md](OSGEARTH_INTEGRATION.md)** ·
> Local Doctrine wiring: **[DOCTRINE_WIRING.md](DOCTRINE_WIRING.md)**

**Direction (2026-10-08):**
- The 3D / stealth view is built on **osgEarth** (D-007) as a reusable, host-independent
  library **`earthview3d`** in this repository, built with **MSVC** (D-008).
- A local integration test inside Doctrine (Conquer superbuild) succeeded (D-010, D-011).
- **Doctrine/Conquer cannot go to GitHub.** Cloud agents therefore work **only in this
  repo**: they finish and polish `earthview3d`, a demo app that simulates a host, packaging,
  data and docs. The final wiring into Doctrine (task **L1**) is done **locally** following
  `DOCTRINE_WIRING.md`.
- Superseded: custom-renderer milestones M2–M10 and the earlier O0–O11 plan.

---

## 0. Ground rules for every agent

1. **Stay in this repo.** Never reference, recreate or guess Doctrine/Conquer source. The
   only Doctrine knowledge you need is the host contract in `DOCTRINE_WIRING.md` §2.
2. **`earthview3d` is host-independent.** No simulation types; hosts push
   `EntityState3D` records and read signals. Public headers include **no osgEarth/OSG
   headers** (pimpl) so hosts never see the vcpkg include tree (D-011).
3. **Do not change EarthView 2D behaviour**, and do not extend the old pseudo-3D path
   (`Camera::terrainMercatorToScreen`, `stealthViewMercatorToScreen`, terrain shaders,
   `isStealthViewEnabled()`/`isTerrain3DView()` branches) — removed in E9.
4. **osgEarth owns 3D rendering**: configure osgEarth; no custom terrain/imagery/sky/LOD.
   Keep the embedded-widget rules in `OSGEARTH_INTEGRATION.md` §3.3 (they were learned the
   hard way — e.g. no logarithmic depth buffer, D-010).
5. **Precision:** positions are geodetic/ECEF `double`; never `float` world positions.
6. **Dependencies** come from the pinned vcpkg manifest at the repo root (E1). No new
   `FetchContent` downloads — the product must build offline.
7. **Tests:** non-GL logic (config, entity diffing, model table matching, camera maths,
   synthetic feed) gets Qt Test unit tests; `ctest` must pass on Linux and Windows CI.
8. **Never commit build output** (`build*/`, `out/`, `x64/`, `vcpkg_installed/`) or large
   data (`*.dt2`, `*.mbtiles`).
9. **PRs:** one task per branch (`3d/<task>-<topic>`), < ~1500 changed lines (vendored
   code excepted); description with summary, files, how to verify visually on Windows,
   limitations, and what you could not verify. Update `3D_PROGRESS.md`, this file's Status,
   and `DOCTRINE_WIRING.md` if the public API changed.
10. **Style:** C++17, Qt naming, `m_` members, `#pragma once` + include guards, Doxygen
    `@brief` on public classes, namespace `earthview3d`.

### Environment
- Cloud agents: Linux, no GPU. They **can** build `earthview3d` on Linux against vcpkg
  osgEarth (first build ~1 h; use the cache) and run unit tests and the demo headless-free
  parts. They **cannot** run MSVC or look at rendering.
- **Windows CI** (`windows-latest`, MSVC, Qt `msvc2022_64`, vcpkg binary cache) proves the
  MSVC build (E1). **Visual checks are human**, using `earthview3d_demo --snapshot` images
  attached to the PR by the reviewer.

---

## 1. Tasks

| #  | Task | Depends on | Size | Visual | Status |
| -- | ---- | ---------- | ---- | ------ | ------ |
| M0 | Repo hygiene, cross-platform build, CI, tests | — | S | no | **Done** |
| M1 | Geodesy + Camera3D maths (`scene3d`) | M0 | M | no | **Done** (#3) |
| S1 | osgEarth spike | — | M | yes | **Done** |
| T1 | Local Doctrine integration test (Conquer) | S1 | M | yes | **Done** (local, `1fd3d89`) |
| E1 | `earthview3d` library from the reference widget + vcpkg manifest + Windows CI | — | M | yes | In review (`3d/e1-earthview3d`, [#4](https://github.com/volcano581/EarthView/pull/4)) |
| E2 | `earthview3d_demo` app with synthetic host feed (replaces the spike app) | E1 | M | yes | |
| E3 | Entity layer: API, model table, labels, LOD, attitude, picking + selection | E1 | L | yes | |
| E4 | Camera controllers (orbit, chase, stealth, first-person, free-fly) + HUD | E1 | M | yes | |
| E5 | Terrain services: height query API, clamping, vertical datum check | E1 | S | yes | |
| E6 | Offline data pipeline (imagery, DEM overviews, overlays, earth template, cache) | E1 | M | yes | |
| E7 | Packaging: CMake package/`add_subdirectory`, `earthview3d_deploy_runtime()`, install, offline build doc | E1 | M | no | |
| E8 | Wiring kit: keep `DOCTRINE_WIRING.md` + host contract test in sync; integration checklist | E3, E4, E7 | S | no | |
| E9 | Cleanup: remove EarthView pseudo-3D; 2D MapViewWindow "Open 3D" hook | E2 | M | yes | |
| E10 | Performance + polish (1k entities, multiple windows, smooth motion) | E3, E4 | M | yes | |
| L1 | **Local:** wire `earthview3d` into Doctrine (Conquer) per `DOCTRINE_WIRING.md` | E7, E8 | M | yes | local only |

Parallel after E1: E3, E4, E5, E6, E7. E2 should land early (it is how every later task is
demonstrated).

---

## 2. Task details

### E1 — `earthview3d` library — IN REVIEW ([#4](https://github.com/volcano581/EarthView/pull/4))
- Move `earthview3d/reference/GlobeView3D.*` to `earthview3d/src/` +
  `earthview3d/include/earthview3d/`; namespace `earthview3d`; **pimpl** so the public
  header has only Qt/STL types; rename `DOCTRINE_3D_*` env vars to `EARTHVIEW3D_*`.
- `vcpkg.json` at the repo root (baseline from `spikes/osgearth/vcpkg.json`):
  `osgearth` + test deps. CMake option `EARTHVIEW_BUILD_3D` (default ON when osgEarth is
  found, OFF otherwise) so 2D-only builds are unaffected.
- `MapConfig` (earth file, data root, cache dir, sky on/off + time) instead of hard-coded
  paths; earth file template moved from the spike.
- Windows CI job: MSVC, Qt `msvc2022_64` via `install-qt-action`, vcpkg with
  `VCPKG_BINARY_SOURCES=clear;x-gha,readwrite`; build + `ctest`. Linux job builds
  `earthview3d` too (vcpkg, cached) once green.
- Tests: `MapConfig` parsing/path resolution; entity diff (add/update/remove) logic split
  into a GL-free class.
- Acceptance: both CI jobs green; `spikes/osgearth` app ported onto the library renders
  home/chase/stealth on Windows (human check).

### E2 — `earthview3d_demo` (host simulator)
- New app `earthview3d_demo`: main window with the 2D EarthView `MapWidget` **and** an
  "Open 3D View" action opening `GlobeView3D` — mirrors how Doctrine uses it.
- `SyntheticFeed`: N entities (ground, air, artillery, infantry) moving on scripted routes
  around a configurable origin (default 33.6844 N, 73.0479 E), published at 100 Hz into a
  thread-safe double buffer, consumed per frame — same pattern as the host contract.
- Options: `--entities N`, `--origin lat,lon`, `--view home|chase|stealth|fp|free`,
  `--snapshot2d/--snapshot3d <png> --after <s>`, `--map <earth file>`.
- Retire `spikes/osgearth` (keep README results as history).
- Acceptance: snapshots show 2D and 3D positions agreeing; tests for the feed.

### E3 — Entity layer
- `EntityState3D`: id, lat/lon/alt (double), heading/pitch/roll, `kindKey`, rgba, label,
  `clampToTerrain`, visible.
- Model table (`models.json`): kindKey pattern → model file (`.osgb`/glTF), scale, HPR
  offset, clamp flag; placeholder shapes as fallback (current behaviour).
- Labels (`LabelNode`/`PlaceNode`, declutter), force colours, LOD (model → icon by pixel
  size), selection highlight.
- Picking: `entityPicked(quint64)` signal; `setSelected(quint64)`.
- Tests: model table matching, diffing, visibility.
- Acceptance (demo): 200 entities readable at all ranges; click-select works.

### E4 — Cameras
- Orbit, chase (tether, heading-locked/free-look), stealth (eye height above terrain
  configurable, default ~10 m), first-person (entity attitude), free-fly (WASD + mouse,
  speed scales with AGL); terrain clearance never below +2 m (query, keep last good).
- Smooth transitions; HUD overlay (heading, pitch, MSL/AGL, speed, target, mode).
- Acceptance (demo): attach to a moving ground and air entity, look around, detach, fly;
  no frame shows the camera below terrain.

### E5 — Terrain services
- `heightAt(lat, lon)` (async-safe, cached), clamping used by E3/E4.
- Vertical datum: decide ellipsoid vs EGM96 for DTED; verify against a known spot height;
  document. Report the ground-plane offset for hosts (I-015) via a debug overlay.

### E6 — Offline data
- Imagery decision + pipeline (raster MBTiles from the OpenMapTiles vector file, or
  satellite) — closes I-007; `gdaladdo` overviews for `dted_1arc.vrt`; borders/cities as
  osgEarth feature layers; graticule; relative-path earth template; filesystem cache.
- Acceptance: network unplugged → demo renders fully; second run uses the cache.

### E7 — Packaging
- `earthview3d` consumable two ways: `add_subdirectory(EarthView/earthview3d)` and
  `find_package(earthview3d CONFIG)` (install + export).
- `earthview3d_deploy_runtime(<target>)` CMake function: copies osgEarth/OSG/GDAL/PROJ
  DLLs, `osgPlugins-*`, GDAL/PROJ data, osgEarth data, MSVC runtime; runtime env set by the
  library (no host code needed).
- `OFFLINE_BUILD.md` generalised from the spike (feeder/offline vcpkg caches, same VS
  version on both machines).
- Acceptance: a tiny external CMake project (in `tests/consumer/`) builds against the
  installed package in CI.

### E8 — Wiring kit
- Keep `DOCTRINE_WIRING.md` §2/§3 aligned with the final API (field names, signals, CMake
  calls). Add a compile-only "host contract" test that fills `EntityState3D` from a
  struct shaped like the contract table, so API drift breaks CI.

### E9 — Cleanup
- Remove the pseudo-3D/stealth path from EarthView 2D (Camera branches, shaders, UI).
- EarthView viewer app: "Open 3D View" action using `earthview3d` when built.

### E10 — Performance + polish
- 1,000 moving entities ≥ 60 fps at 1440p (demo, `--entities 1000`); interpolation between
  host ticks; multiple 3D windows; numbers recorded in `3D_PROGRESS.md`.

### L1 — Local Doctrine wiring (not for cloud agents)
Done on the developer machine in Conquer, following `DOCTRINE_WIRING.md` §3–§4; results
recorded in `3D_PROGRESS.md` (Verification log), which is on GitHub.

---

## 3. Running this with Claude cloud agents

One task per session, e.g.:
> Implement task **E1** from `EarthView/ROADMAP_3D.md`, following `CLAUDE.md`. Branch
> `3d/e1-earthview3d`. Make Linux and Windows CI green, update `3D_PROGRESS.md`, open a PR.

Review loop: CI → `/code-review` → human runs `earthview3d_demo` snapshots on Windows →
merge → tracker updated. Human-only: visual checks, E6 imagery decision, L1.

## 4. Risks

| Risk | Mitigation |
| ---- | ---------- |
| Agents cannot see Doctrine | Host contract + demo host (E2) + contract test (E8) |
| API drift vs local adapter | E8 contract test; adapter is ~150 lines, update in L1 |
| osgEarth CI build time | vcpkg binary cache (E1) |
| Embedded-osgEarth pitfalls regress | rules in design §3.3 + reference README; visual checks per PR |
| Sim ground ≠ DTED | clamp ground units (E3/E5); fix in Doctrine later (I-015) |
| Offline imagery unresolved | E6 decision |
