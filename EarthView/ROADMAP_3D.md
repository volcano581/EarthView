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
| E1 | `earthview3d` library from the reference widget + vcpkg manifest + Windows CI | — | M | yes | **Done** ([#4](https://github.com/volcano581/EarthView/pull/4), `351de51`) |
| E2 | `earthview3d_demo` app with synthetic host feed (replaces the spike app) | E1 | M | yes | Done ([#5](https://github.com/volcano581/EarthView/pull/5), `2242208`) |
| E3 | Entity layer: API, model table, labels, LOD, attitude, picking + selection | E1 | L | yes | Done |
| E4 | Camera controllers (orbit, chase, stealth, first-person, free-fly) + HUD | E1 | M | yes | Done |
| E5 | Terrain services: height query API, clamping, vertical datum check | E1 | S | yes | Done |
| E6 | Offline data pipeline (imagery, DEM overviews, overlays, earth template, cache) | E1 | M | yes | Done |
| E7 | Packaging: CMake package/`add_subdirectory`, `earthview3d_deploy_runtime()`, install, offline build doc | E1 | M | no | Done ([#14](https://github.com/volcano581/EarthView/pull/14), `5f9bc0b`) |
| E8 | Wiring kit: keep `DOCTRINE_WIRING.md` + host contract test in sync; integration checklist | E3, E4, E7 | S | no | Done ([#20](https://github.com/volcano581/EarthView/pull/20), `c3d1158`) |
| E9 | Cleanup: remove EarthView pseudo-3D; 2D MapViewWindow "Open 3D" hook | E2 | M | yes | Done ([#21](https://github.com/volcano581/EarthView/pull/21)) |
| E10 | Performance + polish (1k entities, multiple windows, smooth motion) | E3, E4 | M | yes | |
| E11 | TMS map catalog: EarthView's `Data/EarthFiles` layer-list files → standard earth files, one map per file | E6 | S | yes | Done |
| L1 | **Local:** wire `earthview3d` into Doctrine (Conquer) per `DOCTRINE_WIRING.md` | E7, E8 | M | yes | local only |
| F1 | Fixes from the owner's Windows run: per-frame marker scale (I-037), DTED folder path in the 2D layer lists and the earth files, TMS map by default (I-038) | E11 | S | yes | Done ([#17](https://github.com/volcano581/EarthView/pull/17), `2ceb9ce`) |
| F2 | Demo's 2D pane reads the 2D viewer's own map sources (I-041) | E11 | S | yes | Done ([#18](https://github.com/volcano581/EarthView/pull/18)) |
| F3 | DTED folder indexed in the app: cell headers to a GDAL mosaic, no GDAL tools needed (I-042) | F1 | S | yes | Done ([#19](https://github.com/volcano581/EarthView/pull/19), `99719b8`) |

Parallel after E1: E3, E4, E5, E6, E7. E2 should land early (it is how every later task is
demonstrated).

---

## 2. Task details

### E1 — `earthview3d` library — DONE ([#4](https://github.com/volcano581/EarthView/pull/4))
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

### E3 — Entity layer — DONE ([#6](https://github.com/volcano581/EarthView/pull/6))
- `EntityState3D`: id, lat/lon/alt (double), heading/pitch/roll, `kindKey`, rgba, label,
  `clampToTerrain`, visible.
- Model table (`models.json`): kindKey pattern → model file (`.osgb`/glTF), scale, HPR
  offset, clamp flag; placeholder shapes as fallback (current behaviour).
- Labels (`LabelNode`/`PlaceNode`, declutter), force colours, LOD (model → icon by pixel
  size), selection highlight.
- Picking: `entityPicked(quint64)` signal; `setSelected(quint64)`.
- Tests: model table matching, diffing, visibility.
- Acceptance (demo): 200 entities readable at all ranges; click-select works.

### E4 — Cameras — DONE ([#8](https://github.com/volcano581/EarthView/pull/8))
- Orbit, chase (tether, heading-locked/free-look), stealth (eye height above terrain
  configurable, default ~10 m), first-person (entity attitude), free-fly (WASD + mouse,
  speed scales with AGL); terrain clearance never below +2 m (query, keep last good).
- Smooth transitions; HUD overlay (heading, pitch, MSL/AGL, speed, target, mode).
- Acceptance (demo): attach to a moving ground and air entity, look around, detach, fly;
  no frame shows the camera below terrain.

### E5 — Terrain services — DONE ([#9](https://github.com/volcano581/EarthView/pull/9))
- `heightAt(lat, lon)` (async-safe, cached), clamping used by E3/E4.
- Vertical datum: decide ellipsoid vs EGM96 for DTED; verify against a known spot height;
  document. Report the ground-plane offset for hosts (I-015) via a debug overlay.

### E6 — Offline data — DONE ([#10](https://github.com/volcano581/EarthView/pull/10); local-check fixes: issue #11)
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

### E8 — Wiring kit — IN REVIEW
- Keep `DOCTRINE_WIRING.md` §2/§3 aligned with the final API (field names, signals, CMake
  calls). Add a compile-only "host contract" test that fills `EntityState3D` from a
  struct shaped like the contract table, so API drift breaks CI.
- Done in `tests/tst_hostcontract.cpp`: the adapter of §2 plus `static_assert` over every
  field and signature the document names. It includes `GlobeView3D.h` without linking the
  widget (public headers are osgEarth-free, D-011), so the check also runs on Linux.

### E9 — Cleanup — IN REVIEW
- Remove the pseudo-3D/stealth path from EarthView 2D (Camera branches, shaders, UI).
- EarthView viewer app: "Open 3D View" action using `earthview3d` when built.
- Scope call by the owner (2026-10-15): the DEM overlay **stays**, drawn flat in Mercator
  (`terrain_flat.vert`), so the 2D DTED reader (I-039) remains possible. Only the pitched
  projection and the stealth view go.

### E10 — Performance + polish
- 1,000 moving entities ≥ 60 fps at 1440p (demo, `--entities 1000`); interpolation between
  host ticks; multiple 3D windows; numbers recorded in `3D_PROGRESS.md`.

### E11 — TMS map catalog — DONE
- The host's tile server (local TMS) is described by EarthView's own layer-list earth files
  (`Data/EarthFiles/*.earth`: TMS `<image>`/`<elevation>` lines, no `<map>` root). 2D reads
  them directly and must keep doing so.
- `EarthFileCatalog` (`earthview3d_core`) scans the same folder, offers one map per file (2D
  labels) and writes a standard osgEarth earth file per source by splicing `<TMSImage>` /
  `<TMSElevation>` layers into the `<!--@EARTHVIEW3D_TMS_LAYERS@-->` placeholder of a template
  earth file (default `maps/earthview.earth`, which keeps supplying elevation and overlays).
- Demo: `--earth-files`, `--map-template`, `--list-maps`, `--map-name`, and a **Map** dropdown
  that rebuilds the 3D view on change.
- Acceptance (Windows, with the tile server reachable): the dropdown lists one entry per file
  and each one renders its TMS imagery on the terrain.

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
