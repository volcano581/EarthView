# EarthView — Architecture & Open-Source API Reference

Audience: engineers onboarding onto EarthView who need a single map of **how the code is
structured**, **which third-party/open-source libraries it integrates and why**, and **where
to hook in** when extending it. It is a synthesis layer above the existing deep-dive docs —
read it first, then follow the links for the full detail on any subsystem.

Deep-dive docs this document indexes (do not duplicate them, cross-reference instead):

| Doc | Covers |
| --- | ------ |
| [DEVELOPERS_GUIDE.md](DEVELOPERS_GUIDE.md) | 2D source tree, startup flow, build/debug, file-by-file reference |
| [RENDERING_PIPELINE.md](RENDERING_PIPELINE.md) | 2D OpenGL frame pipeline, per-subsystem benefits/costs |
| [OSGEARTH_INTEGRATION.md](OSGEARTH_INTEGRATION.md) | 3D design: threading, GL context, coordinate systems, public API sketch |
| [DOCTRINE_WIRING.md](DOCTRINE_WIRING.md) | The host contract `earthview3d` exposes to any embedding application |
| [ROADMAP_3D.md](ROADMAP_3D.md) / [3D_PROGRESS.md](3D_PROGRESS.md) | Task plan and status/decision/issue log for the 3D work |
| [earthview3d/README.md](earthview3d/README.md) | `earthview3d` library layout, usage snippet, embedded-widget rules |
| [CLAUDE.md](../CLAUDE.md) | Agent/contributor rules for this repo |

---

## 1. System Overview

EarthView is a Qt 6 / OpenGL desktop mapping engine with **two independent rendering
stacks** that share the same process and, in places, the same camera input, but never the
same GPU context:

```
 ┌─────────────────────────────── EarthView.exe ───────────────────────────────┐
 │                                                                              │
 │   MainWindow (Qt widgets, toolbar, source discovery)                        │
 │     └── MapWidget : QOpenGLWidget   ── core-profile GL 4.5 / 3.3 fallback   │
 │           2D engine: Mercator + Orthographic globe, raster/vector tiles,    │
 │           DEM terrain overlay, borders, grid, cities, text — all custom     │
 │           OpenGL code (no 3D-party rendering engine)                        │
 │                                                                              │
 │   earthview3d_demo (separate target; stands in for a host app)              │
 │     └── GlobeView3D : QOpenGLWidget  ── own GL context, compatibility prof. │
 │           3D engine: osgEarth-owned terrain/imagery/sky/entities/cameras    │
 │           (earthview3d/ library — host-independent, reusable by any app)   │
 └──────────────────────────────────────────────────────────────────────────────┘
```

- **2D engine** (`EarthView/*.cpp`, `shaders/`): a hand-written renderer, built and
  polished first; it is **frozen behaviour** — see CLAUDE.md rule 1. Documented in
  DEVELOPERS_GUIDE.md and RENDERING_PIPELINE.md.
- **3D engine** (`earthview3d/`, `scene3d/`): a thin, host-independent Qt widget around
  **osgEarth**, which owns all 3D rendering (terrain, imagery, sky, LOD). This is the
  active development area (roadmap `E1`–`E10` in ROADMAP_3D.md).
- The two stacks never share a GPU context (`OSGEARTH_INTEGRATION.md §3.3`) and the 3D
  library never includes 2D headers or vice versa — the only coupling is that both read the
  same `Data/` directory and some demo code shows them side by side.

---

## 2. 2D Engine — Module Map

| Layer | Classes | Role |
| ----- | ------- | ---- |
| Coordination | `MainWindow`, `MapWidget` | UI shell, source discovery, render-loop orchestration, input |
| Camera / math | `Camera`, `MercatorProjection` | Mercator ⇄ screen ⇄ orthographic conversions, visible-tile math |
| Raster imagery | `TMSLoader`, `TileRenderer`, `TextureManager` | Tile selection/fetch, texture cache, draw |
| Vector imagery | `MbTilesReader`, `VectorTileRenderer` | MBTiles/MVT decode, stencil-based polygon fill, linework, labels |
| Elevation (2D) | `DemLoader`, `TerrainRenderer` | Remote/local GeoTIFF DEM catalog, height-texture terrain tilt view |
| Overlays | `BorderRenderer`, `GridRenderer`, `CityLoader`, `CityRenderer` | Shapefile borders, lat/lon grid, settlement markers/labels |
| Shared rendering | `LineBatchRenderer`, `TextRenderer`, `ShaderUtils` | Batched colored lines, text-atlas labels, shader loading |
| Support | `Constants.h`, `FrameProfiler`, `StreamingBuffer`, `OpenGLRuntime`, `fpsCounter.h` | Tunables, frame timing, buffer streaming helpers, GL context setup |

Full file-by-file reference, frame order, and dirty-flag caching rules are in
DEVELOPERS_GUIDE.md §6–§21 and RENDERING_PIPELINE.md.

**Do not extend** the legacy pseudo-3D path still present in this stack
(`Camera::terrainMercatorToScreen`, `stealthViewMercatorToScreen`,
`shaders/terrain_height.*`, `colored_terrain.vert`, `terrain_line.vert`,
`isStealthViewEnabled()`/`isTerrain3DView()`); it is superseded by `earthview3d` and removed
in roadmap task E9 (CLAUDE.md rule 2).

---

## 3. 3D Engine — Module Map

| Layer | Classes / files | Role |
| ----- | ---------------- | ---- |
| Geodesy math (Qt-free) | `scene3d::Geodesy`, `scene3d::Camera3D` | WGS84 ⇄ ECEF, ENU frames, double-precision camera maths; vendors GLM |
| Public widget | `earthview3d::GlobeView3D` (pimpl) | osgEarth-backed `QOpenGLWidget`; the only class a host touches directly |
| Entity model | `EntityState3D.h`, `ModelTable`, `EntityDiff`, `Picking` | Per-entity pose/kind/force record, model-table matching, add/update/remove diffing, screen-space picking |
| Cameras | `CameraControl` | Orbit/chase/stealth/first-person/free-fly pose maths, terrain clearance, HUD text (GL-free) |
| Terrain services | `TerrainServices` | Cached drawn-terrain height, full-resolution spot sampling, EGM96 geoid conversion |
| Config / runtime | `MapConfig`, `RuntimePaths`, `Runtime.h`/`.cpp` | Earth file + data root + cache settings; OSG plugin / GDAL / PROJ path wiring at startup |
| Demo host | `earthview3d/demo/` (`DemoWindow`, `SyntheticFeed`, `main.cpp`) | Stands in for a real host (Doctrine): synthetic 100 Hz entity feed, 2D+3D side-by-side window |

`earthview3d_core` (GL-free: `MapConfig`, `EntityDiff`, `ModelTable`, `Picking`,
`RuntimePaths`, `TerrainServices`, `CameraControl`) builds and unit-tests on any platform.
`earthview3d` (the actual `GlobeView3D` widget) only builds when osgEarth is found
(`EARTHVIEW_BUILD_3D`), keeping 2D-only builds unaffected. See
[earthview3d/CMakeLists.txt](earthview3d/CMakeLists.txt).

**Design invariants** (CLAUDE.md rule 3, OSGEARTH_INTEGRATION.md §3):
- `earthview3d` never includes Doctrine/host headers; a host only sees
  `earthview3d/include/earthview3d/*.h`, which contain **no osgEarth/OSG types** (pimpl).
- osgEarth owns all 3D rendering — no custom terrain/imagery/sky/LOD code is written here.
- World positions crossing into 3D are geodetic/ECEF `double`, never `float`.

---

## 4. Open-Source / Third-Party APIs

### 4.1 Summary table

| Library | License | Used by | Role | Integration point |
| ------- | ------- | ------- | ---- | ------------------ |
| **Qt 6** (Core, Widgets, Network, OpenGL, OpenGLWidgets, Sql, Test) | LGPL/commercial | Both engines | Application framework, GUI, GL context (`QOpenGLWidget`), networking, SQLite access, unit tests | `find_package(Qt6 …)` in `CMakeLists.txt`; `CMAKE_AUTOMOC`/`AUTOUIC`/`AUTORCC` |
| **OpenGL** (driver-provided) | — | Both engines | GPU rendering API | `Qt6::OpenGL`, `opengl32` (Win32); core profile 4.5/3.3 negotiated in `main.cpp`/`OpenGLRuntime` |
| **SQLite** (via Qt Sql driver) | Public domain | 2D engine | MBTiles raster/vector tile storage | `MbTilesReader` uses `QSqlDatabase` with the `QSQLITE` driver |
| **GLM** 1.0.1 (vendored, `third_party/glm`) | MIT | 2D & 3D geodesy math | Vector/matrix/quaternion math for `scene3d::Geodesy`/`Camera3D` | Header-only, vendored for offline builds (D-004) |
| **osgEarth** 3.8.1 | LGPL-3 with exceptions | 3D engine | Owns terrain, imagery, sky, feature/graticule layers, LOD, picking, `EarthManipulator` | `find_package(osgEarth CONFIG REQUIRED)`; linked `PRIVATE` into `earthview3d` only |
| **OpenSceneGraph (OSG)** 3.6.5 | OSGPL (LGPL-like) | 3D engine (via osgEarth) | Scene-graph runtime osgEarth is built on (`osgDB`, `osgGA`, `osgUtil`, `osgViewer`) | `find_package(OpenSceneGraph REQUIRED COMPONENTS …)` |
| **GDAL** | MIT | 3D engine (via osgEarth) | Raster/vector geodata I/O (DTED/GeoTIFF, shapefiles, vector MBTiles for the offline pipeline) | Pulled by the osgEarth vcpkg port; `find_package(GDAL CONFIG)` used directly by `earthview3d` to set `GDAL_DATA` |
| **PROJ** | MIT | 3D engine (via osgEarth) | Coordinate reference system transforms, EGM96 vertical datum | Pulled by osgEarth; `RuntimePaths`/`Runtime.cpp` sets `PROJ_DATA`/`PROJ_LIB` |
| **GEOS** | LGPL | 3D engine (via osgEarth) | Geometry predicates used internally by OGR/osgEarth feature layers | Transitive vcpkg dependency, not called directly |
| **protobuf, blend2d, curl, sqlite3** | BSD-3 / Zlib / MIT / Public domain | 3D engine (transitive) | osgEarth's own feature/raster backends; `curl` stays linked but unused at runtime (fully offline product) | Transitive vcpkg dependencies of the `osgearth` port (see `vcpkg depend-info osgearth`) |
| **vcpkg** | MIT | 3D build | Pinned, offline-capable dependency manager for the osgEarth stack | `vcpkg.json` at the repo root, `builtin-baseline` pinned |
| **GDAL CLI tools** (`gdal_translate`, `gdalwarp`, `gdaladdo`, `ogr2ogr`) | MIT | Offline data pipeline | Pre-render raster base maps/hillshade/DEM overviews/place GeoPackages | Invoked as subprocesses from `tools/offline_data/build_offline_data.py` |

### 4.2 Notable "no external library" decisions

Two subsystems deliberately avoid a third-party dependency, which matters when extending
them:

- **Vector tiles (MVT/protobuf):** `MbTilesReader` contains a small, purpose-built
  protobuf-varint and gzip-aware decoder for Mapbox Vector Tile payloads rather than linking
  a general protobuf runtime into the 2D engine. See RENDERING_PIPELINE.md "MBTiles And MVT
  Decoding". Do not replace this with a full protobuf dependency without a clear reason —
  the 2D engine is explicitly kept dependency-light (CLAUDE.md applies the same "no new
  `FetchContent`" offline rule to both engines).
- **Shapefiles:** `BorderRenderer` parses ESRI `.shp`/`.dbf`/`.prj` directly; it does not
  depend on GDAL/OGR, unlike the 3D offline pipeline, which does use GDAL for the same data
  format. This keeps the 2D engine's dependency footprint independent of the 3D vcpkg stack.

### 4.3 Why osgEarth (D-007)

An earlier plan (superseded milestones M2–M10, O0–O11) attempted a custom 3D
terrain/imagery renderer on top of the 2D engine's OpenGL code
(`terrain_height.vert`, `stealthViewMercatorToScreen`, etc.). It was dropped in favor of
osgEarth because terrain LOD, imagery draping, vertical datum handling, and camera
manipulation are a large, mature surface area that osgEarth already solves; EarthView's
value-add is the thin, host-independent integration layer (`earthview3d`), not a
competing terrain engine. See `3D_PROGRESS.md` decision `D-007` and
`OSGEARTH_INTEGRATION.md §1`.

---

## 5. Integration Details (Build Wiring)

### 5.1 2D engine → Qt

`CMakeLists.txt` resolves a Qt 6 kit (explicit `EARTHVIEW_QT_PREFIX`, then llvm-mingw
defaults, then an MSVC `msvc2022_64` fallback), then:

```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network OpenGL OpenGLWidgets Sql)
```

`Sql` is required specifically for the `QSQLITE` driver that `MbTilesReader` depends on.
`EARTHVIEW_DEPLOY_QT` (default ON on Windows) runs `windeployqt` as a post-build step.

### 5.2 3D engine → osgEarth / OSG / GDAL / PROJ

All osgEarth-stack dependencies come from the **pinned vcpkg manifest at the repo root**
(`vcpkg.json`, baseline `2cfff9c…`) — never `FetchContent`, so the product builds fully
offline (CLAUDE.md rule 5):

```cmake
find_package(OpenSceneGraph QUIET COMPONENTS osgDB osgGA osgUtil osgViewer)
find_package(osgEarth CONFIG QUIET)
option(EARTHVIEW_BUILD_3D "Build the osgEarth-based earthview3d library" ${_earthview3d_default})
...
target_link_libraries(earthview3d PUBLIC earthview3d_core Qt6::Widgets Qt6::OpenGLWidgets
                                   PRIVATE ${OPENSCENEGRAPH_LIBRARIES} osgEarth::osgEarth)
```

Key points for anyone touching this wiring (`earthview3d/CMakeLists.txt`):
- osgEarth/OSG are linked `PRIVATE`; GDAL is looked up separately only to resolve its
  `share/gdal` data directory at runtime (`EARTHVIEW3D_HAVE_GDAL`).
- `earthview3d_core` has **no** osgEarth dependency at all — it is pure Qt Core — so CI and
  2D-only consumers never need the vcpkg toolchain to build and test it.
- Runtime plugin/data discovery (`osgDB` plugin folder, `GDAL_DATA`, `PROJ_DATA`) is handled
  by `earthview3d::configureRuntime()` (`Runtime.h`/`.cpp`), **not** by environment variables
  set in `main()` — `osgDB` reads `OSG_LIBRARY_PATH` when its DLL loads, which is before any
  application code runs (decision `D-017`).

### 5.3 Offline data pipeline → GDAL CLI

`tools/offline_data/build_offline_data.py` shells out to GDAL command-line tools (not the
GDAL C/C++ API) to pre-render the default offline map: world land/sea base, AOI raster base
map from an OpenMapTiles vector MBTiles, DTED hillshade/overviews, and a places GeoPackage.
This keeps `earthview3d`'s own runtime dependency surface (osgEarth reading finished
rasters/feature layers) separate from the one-time authoring step. See
`tools/offline_data/README.md`.

---

## 6. Extension & Enhancement Guide

### 6.1 Extending the 2D engine

| Task | Where to start | Notes |
| ---- | --------------- | ----- |
| New toolbar action | `MainWindow::setupUI()` | Wire to `MapWidget`/`Camera`/a renderer |
| New overlay renderer | New class taking a `Camera*`; instantiate in `MapWidget::initializeGL()`, delete (with `makeCurrent()`) in `~MapWidget()` | Prefer appending to `LineBatchRenderer`/`TextRenderer` over a bespoke draw call |
| New tile source type | `TmsLoader::TileSourceLayer::SourceType` + a fetch method + cache-key update | Keep decoded images as `QImage` until render time; let `TextureManager` own GL textures |
| New `.earth`/map-source parsing | `readEarthLayers()`, `tileTemplateFromUrl()`, `applyTileMapResourceMetadata()` in `MainWindow.cpp` | Support both attribute- and nested-element XML forms |
| New shader | Add to `shaders/`, list it in `CMakeLists.txt`'s `SHADERS`, load via `ShaderUtils::loadProgram()` | Keep vertex attribute locations aligned with the renderer's vertex struct |

Full checklists: DEVELOPERS_GUIDE.md §21 ("Common Development Tasks").

### 6.2 Extending the 3D engine (`earthview3d`)

The supported extension surface is the **public API** in
`earthview3d/include/earthview3d/*.h` — primarily `GlobeView3D`
([GlobeView3D.h](earthview3d/include/earthview3d/GlobeView3D.h)), `MapConfig`,
`EntityState3D`, `CameraControl`'s `CameraMode`, and `TerrainServices`. A host (or a new demo
feature) should only need these headers plus Qt/STL — never an osgEarth/OSG include, which
would violate D-011.

Common extension points:

| Task | Where | Notes |
| ---- | ----- | ----- |
| Add an entity visual/model | `Data/models/models.json` (`ModelTable` format) | Glob `kindKey` pattern → model file, scale, HPR offset, clamp flag, fallback shape; most-specific pattern wins |
| Add a new camera mode | `CameraControl.h/.cpp` (`CameraMode` enum + pose maths) then wire into `GlobeView3D::Private` | Keep pose maths GL-free and unit-tested (pattern used by orbit/chase/stealth/FP/free-fly) |
| Add a map layer (imagery/feature/graticule) | `Data/maps/earthview.earth` (osgEarth earth-file XML) | Configure osgEarth; do not write custom rendering code (CLAUDE.md rule 3) |
| Toggle a layer from host UI | `GlobeView3D::setLayerVisible()/layerVisible()/layerNames()` | Layer names come from the earth file |
| Consume terrain height | `GlobeView3D::heightAt()` (cached, any thread) or `sourceHeightAt()` (full-res, spot checks only) | Never call osgEarth's `ElevationPool` directly from host code — it is private |
| Add a field to the entity contract | `EntityState3D.h`, update `DOCTRINE_WIRING.md §2` in the same change | The host-contract table in that doc is the single source of truth for field meaning |

Before changing the public API, read `OSGEARTH_INTEGRATION.md §7` (API design intent) and
update `DOCTRINE_WIRING.md` in the same PR — CLAUDE.md rule 9 and roadmap task `E8` make this
a hard requirement because the real host (Doctrine/Conquer) is off GitHub and can only track
API drift through that document.

### 6.3 Embedded-OpenGL-widget rules (do not undo)

`earthview3d/README.md` "Rules baked into the widget" lists eleven hard-won constraints
(compat-profile surface format, `setDefaultFboId` every frame, no logarithmic depth buffer,
heap-allocated `osg::ref_ptr` nodes, explicit label alignment boxes, etc.). Any change to
`GlobeView3D.cpp` that touches OSG/osgEarth state should be checked against that list first —
several were regressions found and fixed during E3/E4 review (`3D_PROGRESS.md` review notes).

### 6.4 Adding a new open-source dependency

- **3D stack:** add it to `vcpkg.json` at the repo root and pin via the existing
  `builtin-baseline`; never add `FetchContent` (CLAUDE.md rule 5 — the product must build
  offline). Keep new include directories `PRIVATE` to `earthview3d` unless the public API
  genuinely needs to expose a type (it should not, per D-011).
- **2D stack:** avoid adding a dependency where the codebase already favors a small
  purpose-built decoder (see §4.2) unless the maintenance cost of the custom code clearly
  exceeds the cost of the new dependency.
- **Licensing:** osgEarth (LGPL-3 with exceptions) and OSG (OSGPL) are linked dynamically and
  unmodified; GDAL/PROJ are MIT; GEOS is LGPL. Any new dependency to a product build must be
  reviewed against the "ship licence texts, dynamic linking only" policy in
  `OSGEARTH_INTEGRATION.md §10` before release.

---

## 7. Build & Test Matrix

| Target | Platform | Toolchain | What builds |
| ------ | -------- | --------- | ------------ |
| 2D app + snapshot tool | Windows | llvm-mingw, MinGW Makefiles | `EarthView.exe`, `earthview_snapshot` |
| 2D app | Linux (CI, no GPU) | Qt 6 + Ninja | Same, offscreen (`QT_QPA_PLATFORM=offscreen`) |
| `earthview3d_core` + unit tests | Linux & Windows | Any Qt 6 kit | GL-free logic: `MapConfig`, `EntityDiff`, `ModelTable`, `Picking`, `RuntimePaths`, `TerrainServices`, `CameraControl` |
| `earthview3d` (osgEarth widget) + `earthview3d_demo` | Windows (MSVC) | VS 2026, Qt `msvc2022_64`, vcpkg manifest | Full 3D stack; "visual checks are human" per roadmap — no GPU in CI |

Non-GL logic for both engines is covered by Qt Test suites under `tests/` and
`earthview3d`'s own `tst_*` targets; `ctest` must pass on Linux CI for everything that does
not require osgEarth/a GPU. See CLAUDE.md "Build" and ROADMAP_3D.md §0 "Environment" for the
exact commands and the Linux/Windows split rationale.
