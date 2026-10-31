# Enhanced EarthView — Doctrine Integration & Architecture Guide

A single, self-contained reference for two audiences:

1. **Integrators** wiring the `earthview3d` 3D / stealth view into Doctrine (Conquer
   superbuild) on the offline machine — **Part 1**.
2. **Developers** extending EarthView's 2D engine or the osgEarth 3D library — **Parts 2–4**:
   complete class hierarchies, call hierarchies and end-to-end render pipelines for both the
   2D and 3D paths.

> **Read this alongside, not instead of:**
> [`DOCTRINE_WIRING.md`](DOCTRINE_WIRING.md) (the host contract, authoritative for field
> meanings), [`OSGEARTH_INTEGRATION.md`](OSGEARTH_INTEGRATION.md) (design rationale, decisions
> D-00x), [`DEVELOPERS_GUIDE.md`](DEVELOPERS_GUIDE.md) (2D engine deep dive),
> [`OFFLINE_BUILD.md`](OFFLINE_BUILD.md) (air-gapped build + deploy),
> [`3D_PROGRESS.md`](3D_PROGRESS.md) (status, decisions, open issues).
>
> This guide is **descriptive of the code as it stands on `master`**. Where it quotes an API
> signature it was copied from the header; if the header and this guide ever disagree, the
> header wins — fix this guide in the same PR.

---

## Part 0 — Why drift matters, and how to use this guide

Doctrine/Conquer lives **off GitHub** on the offline machine. The `earthview3d` library is
finished and polished **in this repo** by cloud agents, then wired into Doctrine **locally**.
Two interfaces therefore evolve independently and can drift apart between integration passes:

- **The EarthView side** — the `earthview3d` public API (`GlobeView3D`, `EntityState3D`,
  `MapConfig`, the CMake targets). This guide and `DOCTRINE_WIRING.md` describe it; the
  headers under `earthview3d/include/earthview3d/` are the ground truth.
- **The Doctrine side** — `RenderEntity`, the `SimRenderBridge`/snapshot, `CoordBridge`,
  `GeodeticOrigin`, the main window. None of that is in this repo. The old adapter
  (`doctrine/src/view3d/Doctrine3DWindow.*`, Conquer branch `earthview-3d-test`,
  commit `1fd3d89`) was written against an **earlier** shape of both sides and **may not
  compile or behave correctly as-is**.

So treat the previous integration as a **starting sketch, not a drop-in**. Part 1 is written
as a *re-derivation*: verify each interface against the live code on both machines before you
wire them together. §1.3 is the drift checklist that makes this concrete.

---

# Part 1 — Wiring `earthview3d` into Doctrine (step by step)

## 1.1 Prerequisites

| # | Prerequisite | How to check |
| - | ------------ | ------------ |
| 1 | Conquer's MSVC preset builds today (VS 2026, Qt `msvc2022_64`). | `cmake --build` the existing superbuild clean. |
| 2 | osgEarth is available via the **same vcpkg toolchain/triplet** Conquer will use, *or* an installed `earthview3d` package from E7. | `vcpkg list osgearth`, or a `find_package(earthview3d CONFIG)` probe. |
| 3 | EarthView is checked out locally at a known commit. | note the SHA; the adapter is pinned to it. |
| 4 | The offline data package exists (earth file, DTED, imagery, cache). | §1.6; `MapConfig::validate()` will tell you what is missing. |
| 5 | CMake ≥ 4.2 if you use the VS 2026 generator (use VS 2026's bundled CMake if `PATH` has an older one). | `cmake --version`. |

## 1.2 The contract at a glance (as the code is now)

Everything the host touches is in three headers. **No osgEarth/OSG headers are involved** —
public headers carry only Qt/STL types (D-011), so Doctrine never sees the vcpkg include tree.

### The widget — `earthview3d/GlobeView3D.h`

```cpp
namespace earthview3d {
class GlobeView3D : public QOpenGLWidget {
public:
    GlobeView3D(const MapConfig& config, double homeLatDeg, double homeLonDeg,
                QWidget* parent = nullptr);

    void setPreFrameCallback(std::function<void()> callback); // runs on GUI thread each frame
    void setEntities(const std::vector<EntityState3D>& entities); // add/update/remove by id

    // selection / picking
    void     setSelected(quint64 id);          // 0 clears; highlights, emits selectionChanged
    quint64  selectedId() const;
    // signals: entityPicked(quint64), selectionChanged(QString), cameraModeChanged(CameraMode),
    //          loadFailed(QString)

    // cameras (see CameraControl.h: Orbit, Chase, Stealth, FirstPerson, FreeFly)
    void setCameraMode(CameraMode); void chaseSelected(); void stealthSelected();
    void firstPersonSelected(); void freeFly(); void untether(); void goHome();

    // terrain services (HAE metres)
    bool heightAt(double lat, double lon, double* altM) const; // cached; any thread
    bool sourceHeightAt(double lat, double lon, double* altM, double* resM = nullptr) const;
    double geoidHeight(double lat, double lon) const;          // EGM96 N; MSL = HAE − N

    // map layers (names from the earth file)
    bool setLayerVisible(const QString& name, bool); QStringList layerNames() const;

    bool entityPosition(quint64 id, double* lat, double* lon, double* alt) const; // after clamp
    bool saveSnapshot(const QString& pngPath);
};
}
```

### One entity — `earthview3d/EntityState3D.h`

```cpp
struct EntityState3D {
    std::uint64_t id = 0;        // stable; 0 ignored. A repeated id in one snapshot: first wins.
    double latDeg, lonDeg;       // WGS-84, double
    double altM;                 // height above the WGS-84 ellipsoid (HAE, not MSL — D-021)
    double headingDeg;           // clockwise from north
    double pitchDeg, rollDeg;    // nose-up +, right-wing-down +
    QString kindKey;             // model-table key; empty/unmatched -> placeholder `shape`
    int shape = 0;               // 0 infantry, 1 air, 2 armour, 3 artillery
    std::uint32_t rgba = 0xFFFFFFFFu; // used only when force == Unspecified
    Force force = Force::Unspecified; // Friendly/Hostile/Neutral/Unknown colour the marker
    QString label;
    bool clampToTerrain = false; // place on 3D terrain, ignore altM (ground units; I-015)
    bool visible = true;
};
enum class Force : std::uint8_t { Unspecified = 0, Friendly, Hostile, Neutral, Unknown };
```

### What to load — `earthview3d/MapConfig.h`

```cpp
struct MapConfig {
    QString earthFile;   // absolute, or relative to dataRoot
    QString dataRoot;    // root for relative paths; empty = cwd
    QString cacheDir;    // osgEarth filesystem cache; relative to dataRoot
    QString modelTable;  // models.json (ModelTable); relative to dataRoot
    bool sky = true; QDateTime skyTime;
    bool validate(QString* error = nullptr) const;         // earth file set+present, data root present
    static bool fromJsonFile(const QString& path, MapConfig*, QString* error = nullptr);
};
```

**Field-mapping table** (which `RenderEntity` field feeds which `EntityState3D` field) lives in
`DOCTRINE_WIRING.md` §2 and is authoritative — do not duplicate it; reconcile against it (§1.3).

## 1.3 Reconcile drift BEFORE writing code

Run these two checklists first. They take minutes and save a day of mis-mapped fields.

### A. EarthView side (this repo — ground truth is the headers)

```bash
# From the EarthView checkout:
sed -n '1,60p'  earthview3d/include/earthview3d/EntityState3D.h   # field names & order
sed -n '45,135p' earthview3d/include/earthview3d/GlobeView3D.h    # ctor, setEntities, signals
sed -n '26,55p' earthview3d/include/earthview3d/MapConfig.h       # config fields
grep -n earthview3d_deploy_runtime earthview3d/cmake/earthview3dDeploy.cmake
grep -n 'find_package(earthview3d' tests/consumer/*/CMakeLists.txt # a known-good consumer
```
Confirm, specifically:
- Constructor arity: **`(MapConfig, homeLatDeg, homeLonDeg, parent)`** — NOT the old
  earth-file-path constructor and NOT the `(MapConfig, parent)` sketch in
  `OSGEARTH_INTEGRATION.md` §7 (that §7 header is an early design draft; the real header wins).
- Entities are pushed with **`setEntities(std::vector<EntityState3D>)`** from a
  **`setPreFrameCallback`**; there is no per-entity `add/update/remove` host call and no
  `EntityLayer3D` class (that too was a design sketch — the diff is internal).
- Namespace is **`earthview3d`**; env vars are **`EARTHVIEW3D_*`** (old `DOCTRINE_3D_*` are gone).
- CMake targets are **`earthview3d::core`** and **`earthview3d::earthview3d`**; package
  version **0.7**.

### B. Doctrine side (offline machine — ground truth is the live code)

Open the current Conquer tree and confirm each of these still exists and still means what the
`DOCTRINE_WIRING.md` §2 table claims. Any mismatch is drift to resolve in the adapter:

| Assumption the adapter makes | Verify in Conquer | If it drifted |
| ---------------------------- | ----------------- | ------------- |
| `RenderEntity` has `id`, `lat_rad`, `lon_rad`, `alt_m`, `heading_rad`, `unitType`, `symbolColor`, `label`, `visible`. | the `RenderEntity` struct definition. | re-point the field mapping; a renamed/removed field is a code change in the adapter only. |
| `alt_m` is HAE (ellipsoid). | how the sim stores altitude. | if MSL, add `view->geoidHeight(lat,lon)` to get HAE (MSL = HAE − N ⇒ HAE = MSL + N). |
| Snapshot is published copy-on-tick; read by comparing a tick counter. | `SimRenderBridge`/snapshot API. | mirror the current consume pattern (see §1.5 loop). |
| `pitch`/`roll`/`kindKey`/`force` may be absent. | identity/attitude components. | leave them 0 / empty / `Unspecified` until the sim provides them — the view degrades gracefully. |
| A geodetic origin (`GeodeticOrigin`) exists for the scenario. | scenario loader. | use it for the `homeLat/homeLon` ctor args. |
| Selection is routed via an entity id equal to `RenderEntity::id`. | Doctrine selection model. | map both ways in the adapter (§1.5 step 4). |

> **Do not** reconstruct any Doctrine type from this guide or from the old adapter if the live
> code disagrees — the live code is authoritative for the Doctrine side.

## 1.4 Build wiring (Conquer CMake)

1. **Bring in `earthview3d`.** In Conquer's root `CMakeLists.txt`, after EarthView 2D:
   ```cmake
   option(CONQUER_ENABLE_3D "Build the osgEarth 3D view" OFF)
   if(CONQUER_ENABLE_3D)
       add_subdirectory(${EARTHVIEW_REPO}/EarthView/earthview3d earthview3d)
       # or, against an installed package (same vcpkg toolchain/triplet, static):
       # find_package(earthview3d 0.7 CONFIG REQUIRED)
   endif()
   ```
2. **Link + define.** In `doctrine/CMakeLists.txt`:
   ```cmake
   target_link_libraries(doctrine PRIVATE earthview3d::earthview3d)
   target_compile_definitions(doctrine PRIVATE DOCTRINE_HAVE_OSGEARTH=1)
   earthview3d_deploy_runtime(doctrine INSTALL)   # copies OSG plugins, GDAL/PROJ, DLLs; see below
   ```
   `earthview3d_deploy_runtime(<target> [INSTALL] [DESTINATION <dir>] [NO_QT])` (from
   `cmake/earthview3dDeploy.cmake`) copies, next to the executable after each build:
   the vcpkg `*.dll`, `osgPlugins-<ver>/` and `share/gdal` + `share/proj`. `INSTALL` adds the
   same plus the Qt runtime (`qt_generate_deploy_app_script`) and the MSVC runtime to
   `cmake --install`. The runtime layout is found by `RuntimePaths::defaults()` at startup, so
   **no host code and no environment variables are needed** — see §4.3.

> **Acceptance gate 1:** Conquer with `CONQUER_ENABLE_3D=OFF` builds exactly as before — no new
> dependencies reach the default build. Verify this first.

## 1.5 The adapter (`doctrine/src/view3d/`)

Port `Doctrine3DWindow.*` from branch `earthview-3d-test`, but treat it as pseudocode and
rebuild it against §1.2/§1.3. It is ~150 lines and does five things:

**1. Construct the view** with the scenario geodetic origin as home:
```cpp
earthview3d::MapConfig cfg;
cfg.earthFile  = ".../maps/earthview.earth";   // or from the E11 map catalog
cfg.dataRoot   = dataRootPath;
cfg.cacheDir   = "cache";
cfg.modelTable = "models/models.json";
QString err;
if (!cfg.validate(&err)) { /* show err, abort opening the view */ }
auto* view = new earthview3d::GlobeView3D(cfg, origin.latDeg, origin.lonDeg, window);
```

**2. Map the snapshot** — the single place that knows both sides. Read the snapshot the same
way the 2D map does (copy only on a new tick), convert each `RenderEntity` to an
`EntityState3D` per the §1.2 / `DOCTRINE_WIRING.md` §2 table, then push:
```cpp
view->setPreFrameCallback([view, bridge, lastTick, buf]{
    if (!bridge->consumeIfNew(lastTick, buf)) return;      // mirror the sim->render bridge API
    std::vector<earthview3d::EntityState3D> out;
    out.reserve(buf->size());
    for (const RenderEntity& e : *buf) {
        earthview3d::EntityState3D s;
        s.id         = e.id;
        s.latDeg     = e.lat_rad * 180.0 / M_PI;
        s.lonDeg     = e.lon_rad * 180.0 / M_PI;
        s.altM       = e.alt_m;                 // HAE; if the sim is MSL: + view->geoidHeight(lat,lon)
        s.headingDeg = e.heading_rad * 180.0 / M_PI;
        s.shape      = toShape(e.unitType);
        s.rgba       = e.symbolColor;
        s.label      = QString::fromUtf8(e.label, qstrnlen(e.label, 8));
        s.clampToTerrain = !isAir(e.unitType);  // I-015: sim ground is a flat plane
        s.visible    = e.visible;
        // pitchDeg/rollDeg/kindKey/force: fill when the sim exposes them, else leave defaults
        out.push_back(s);
    }
    view->setEntities(out);
});
```
Rules (from `DOCTRINE_WIRING.md` §2): **no coordinate maths** in the adapter beyond
rad→deg (Doctrine already produces geodetic positions via `CoordBridge`); `id == 0` entries are
ignored by the view; a repeated id in one snapshot keeps the first.

**3. Model table (optional).** Point `cfg.modelTable` at a `models.json` mapping `kindKey`
globs to model files (format in `ModelTable.h`). Entities whose `kindKey` is empty or unmatched
fall back to the placeholder `shape`. A broken table is reported via `loadFailed` and ignored.

**4. Two-way selection.** 3D pick → Doctrine selection; Doctrine selection → 3D highlight:
```cpp
connect(view, &earthview3d::GlobeView3D::entityPicked, this, &Doctrine3DWindow::selectInDoctrine);
// when Doctrine's selection changes:
view->setSelected(selectedDoctrineId);   // 0 clears; does not move the camera
```

**5. UI.** Keep the "3D View" toolbar/menu action. Camera modes are driven by the widget's own
keys (O/T/F/P/G/L/J/K/R/N/H) or by calling `chaseSelected()/stealthSelected()/…` from a menu.

> **Runtime files caveat (D-025):** the OSG plugin folder must be correct even if the map
> looks fine without it — without `osgdb_freetype` no `.ttf` loads and osgEarth's own labels
> silently lose non-ASCII glyphs. `earthview3d_deploy_runtime` + `RuntimePaths::defaults()`
> handle this. Only if Doctrine keeps the runtime elsewhere, call
> `earthview3d::configureRuntime(paths)` (`<earthview3d/Runtime.h>`) **before** the first view;
> setting `OSG_LIBRARY_PATH` in `main()` is too late (osgDB reads it when the DLLs load).

## 1.6 Data

Point `MapConfig` at the E6 offline data package (relative paths under `dataRoot`):
- `maps/earthview.earth` — fully offline earth file (world base + base map + hillshade + DTED +
  borders + places + graticule). `maps/earthview_online.earth` adds OSM on top.
- Build the local layers once with `tools/offline_data/build_offline_data.py` (see its README).
- Set `cacheDir` so osgEarth caches tiles between runs.
- **DTED folder:** the offline server holds DTED as a folder tree. A 2D-style layer-list earth
  file names the folder in an `<elevation driver="dted">` entry; `EarthFileCatalog::generate()`
  indexes it via `DtedMosaic` into `<folder>/dted_mosaic.vrt` (no GDAL tools needed). A map
  loaded **directly** by osgEarth must name the mosaic `.vrt`, not the folder (I-043).
- **Map catalog (E11):** `EarthFileCatalog::build(dirs, templatePath, outputDir, &warnings)`
  turns Doctrine's existing `Data/EarthFiles/*.earth` layer lists into standard earth files
  (one `MapCatalogEntry` per source; `label` for a host dropdown, `earthFile` for
  `MapConfig::earthFile`). Switching map = rebuild the view with the new `MapConfig`.

## 1.7 Verify (local, Windows)

Test hooks on the test branch: `DOCTRINE_AUTOSTART_DEMO=1`, `DOCTRINE_OPEN_3D=1`,
`DOCTRINE_2D_SNAPSHOT`, `DOCTRINE_3D_SNAPSHOT`, `DOCTRINE_3D_VIEW=chase|stealth`.

**Acceptance checklist** (record results in `3D_PROGRESS.md` → Verification log):
- [ ] `CONQUER_ENABLE_3D=OFF` builds exactly as before (no new deps).
- [ ] With it ON: "3D View" opens; entities appear once the scenario starts.
- [ ] 2D and 3D positions agree (snapshots at the same tick).
- [ ] Ground units sit on terrain; air units at sim altitude.
- [ ] Chase / stealth / first-person / free-fly track a moving entity.
- [ ] 2D selection ↔ 3D selection both ways.
- [ ] Offline: network unplugged, map and entities still render.
- [ ] Close + reopen the 3D window; app exit is clean.

---

# Part 2 — 2D engine internals (for extenders)

The 2D engine is a Qt `QOpenGLWidget` with a core-profile context (GL 4.5, 3.3 fallback). It is
integrated into Doctrine's 2D views and **must not regress** (CLAUDE.md rule 1).
`DEVELOPERS_GUIDE.md` is the chapter-by-chapter deep dive; this part gives the hierarchies and
the end-to-end pipeline in one place.

## 2.1 Class hierarchy

```
QApplication (main.cpp — sets global QSurfaceFormat: GL 4.5 core, or 3.3 software/compat)
 └─ MainWindow : QMainWindow                 (UI, toolbar, map-source discovery)
     └─ MapWidget : QOpenGLWidget, QOpenGLFunctions   (central widget, render coordinator)
         ├─ Camera : QObject                 (viewport, zoom, pan, projection, tile math)
         ├─ TmsLoader : QObject              (visible-tile selection + tile loading)
         │   └─ TextureManager               (GL texture cache, ~200 MB, insertion-order evict)
         ├─ TileRenderer                     (draws active tiles; coarse→fine order)
         ├─ VectorTileRenderer               (MVT geometry: fills, lines, labels)
         ├─ BorderRenderer                   (shapefile parse → Mercator polylines)
         ├─ GridRenderer                     (lat/lon graticule lines + labels)
         ├─ CityRenderer  (+ CityLoader)     (NDJSON markers + labels)
         ├─ LineBatchRenderer                (shared colored-line batch: borders, grid)
         ├─ TextRenderer                     (QPainter atlas → one textured-quad batch)
         ├─ DemLoader / TerrainRenderer      (DTED/DEM; feeds the DEPRECATED pseudo-3D path)
         └─ ShaderUtils (free functions)     (shader discovery + QOpenGLShaderProgram load)

Standalone helpers: MercatorProjection (coord math), MbTilesReader (SQLite + MVT parser),
Constants.h (GIS/tile/cache constants), FpsCounter.
```

Ownership: `MainWindow` owns `MapWidget` (Qt central widget); `MapWidget` manually owns every
renderer and deletes them with a **current GL context** (`makeCurrent()` in `~MapWidget`).
`Camera` and `TmsLoader` are Qt children of `MapWidget`; `TmsLoader` owns the
`QNetworkAccessManager` and the `TextureManager`.

> **Deprecated pseudo-3D path (CLAUDE.md rule 2, removed in roadmap E9):** `Camera`'s
> `setTerrain3DEnabled`, `setStealthViewEnabled`, `terrainMercatorToScreen`,
> `stealthViewMercatorToScreen`, `isTerrain3DView`/`isStealthViewEnabled`, plus
> `DemLoader`/`TerrainRenderer` and `shaders/terrain_*.vert`. **Do not extend it.** All real 3D
> is `earthview3d` (Part 3).

## 2.2 Call hierarchy — the 2D frame

A zero-interval `QTimer` in `MapWidget` drives continuous repaints:

```
QTimer(0) ─timeout─▶ MapWidget::update() ─▶ paintGL()
 MapWidget::paintGL():
   1. glClear(color|depth)
   2. m_fpsCounter.update()
   3. if globe mode: drawGlobeBackdrop()                     (dark disk + outline)
   4. TmsLoader::updateVisibleTiles()  ◀── Camera::getTileRange(), layerIndicesForZoom()
        ├─ add newly visible tiles, start fetchTile()/fetchMbTile()
        └─ remove off-screen tiles, abort stale network requests
      TileRenderer::render(camera, tmsLoader)                (lazy QImage→GL texture upload)
   5. rebuildLineBatchIfNeeded():                            (if m_lineBatchDirty)
        BorderRenderer::appendMercatorLines() + GridRenderer::append… ▶ LineBatchRenderer
      LineBatchRenderer::render(camera)                      (Screen or Mercator vertex mode)
   6. CityRenderer::renderMarkers(camera)                    (point_marker.vert/frag)
   7. glDisable(DEPTH_TEST)
   8. rebuildMapLabelsIfNeeded():                            (if m_mapLabelsDirty)
        GridRenderer + CityRenderer + VectorTileRenderer → TextRenderer::Label list
        + appendScaleBarOverlay() + appendFpsOverlay()
   9. TextRenderer::render()                                 (atlas rebuilt only if label hash changed)
  10. glEnable(DEPTH_TEST)

 Input: mouse/wheel/key events → Camera::pan/zoom/setProjectionMode/…
         → Camera emits cameraChanged() → MapWidget::onCameraChanged()
         → invalidateLineBatch() + invalidateMapLabels()     (caches recomputed next frame)
```

## 2.3 End-to-end 2D render pipeline

**Coordinates.** Internal positions are **Mercator radians** (x ∈ [−π, π]); `MercatorProjection`
converts lat/lon ↔ Mercator ↔ screen and computes tile ranges. `Camera::getResolution()` is
metres/pixel = `EARTH_CIRCUMFERENCE / (TILE_SIZE · 2^zoom)`; `getTileZoomLevel() =
clamp(ceil(zoom), 0, MAX_TILE_ZOOM)`.

**Imagery.** `TmsLoader` chooses visible tiles per active layer for the current tile zoom
(`layerIndicesForZoom()` keeps coarse layers as **underlays** so low-res imagery stays visible
under loading high-res tiles), fetches them (async network for URL templates, synchronous SQLite
for raster MBTiles), and keeps decoded images as `QImage` until the render path uploads them
with a current context. `TileRenderer` sorts active tiles so coarser `tileZoomLevel` draws
first. **Flat Mercator:** one screen quad per tile. **Orthographic globe:** each tile is a 24×24
mesh projected cell-by-cell through `Camera::projectMercatorToScreen()`.

**Vectors.** Vector MBTiles (`pbf`/`mvt`/`vector`) skip the texture path; `MbTilesReader`
decodes MVT (varints, gzip, layers/features/geometry) and `VectorTileRenderer` draws
landcover/water/buildings/boundaries/transport with a deliberately simple built-in style.

**Overlays.** Borders (shapefile → Mercator polylines) and grid lines batch through the shared
`LineBatchRenderer`; cities (NDJSON, lazy per zoom band) render markers and contribute labels;
everything textual (grid/city/vector/scale/FPS) goes through one `TextRenderer` atlas drawn
last. Caches (`m_lineBatchDirty`, `m_mapLabelsDirty`) are invalidated on camera change.

## 2.4 2D extension points

| To add… | Do this |
| ------- | ------- |
| A toolbar action | `QAction` in `MainWindow::setupUI()`, connect to `MapWidget`/`Camera`/renderer, show a `statusBar()` message. |
| An overlay renderer | new class holding a `Camera*`; create GL resources in `MapWidget::initializeGL()` (not the ctor); delete in `~MapWidget` after `makeCurrent()`; render at the right point in `paintGL()`. Prefer `LineBatchRenderer`/`TextRenderer` over a new line/text renderer. |
| A tile-source type | extend `TmsLoader::TileSourceLayer::SourceType`, add discovery in `MainWindow.cpp`, a fetch method in `TmsLoader`, keep images as `QImage` until render, include the type in cache keys. |
| Earth-file fields | `readEarthLayers()` / `tileTemplateFromUrl()` / `applyTileMapResourceMetadata()` in `MainWindow.cpp`; parse both attribute and nested-element forms. |
| A shader | add under `shaders/`, add to the `SHADERS` list in `CMakeLists.txt` (it is copied/deployed), load via `ShaderUtils::loadProgram()`, keep attribute locations aligned with the renderer's vertex struct. |

**Invariant:** every GL object is created/destroyed with a current context; keep decoded tiles
as `QImage` until the render path; keep data-discovery paths consistent between cwd and app dir.

---

# Part 3 — osgEarth 3D internals (`earthview3d`)

`earthview3d` is a **host-independent** osgEarth 3D / stealth view (CLAUDE.md rule 3: osgEarth
owns all terrain/imagery/sky/LOD; no custom renderer). Positions crossing into 3D are geodetic
`double` (rule 4). The widget runs **GUI-thread only**.

## 3.1 Library layout and CMake targets

```
earthview3d/
  include/earthview3d/*.h     public headers — Qt/STL types only (no osgEarth in public API)
  src/*.cpp                   implementation (GlobeView3D.cpp holds all osgEarth usage)
  demo/                       earthview3d_demo — a synthetic host (SyntheticFeed, DemoWindow)
  cmake/earthview3dDeploy.cmake    earthview3d_deploy_runtime(), earthview3dConfig.cmake.in
  tests/ (…/tests at repo)    Qt Test unit tests for every GL-free class

Targets (CMakeLists.txt, version 0.7.0):
  earthview3d_core  (alias earthview3d::core)  STATIC, Qt6::Core only — ALWAYS built.
      CameraControl, DtedMosaic, EarthFileCatalog, EntityState3D, EntityDiff, MapConfig,
      ModelTable, Picking, RuntimePaths, ScreenScale, TerrainServices  (all GL-free, unit tested)
  earthview3d       (alias earthview3d::earthview3d)  STATIC — built when EARTHVIEW_BUILD_3D
      (default ON iff OpenSceneGraph + osgEarth are found).
      GlobeView3D, Runtime. Links Qt6::Widgets/OpenGLWidgets PUBLIC; OSG + osgEarth::osgEarth
      PRIVATE; OSG/osgEarth include dirs PRIVATE so vcpkg's tree never reaches hosts (D-011).
```

The split is deliberate: all the testable logic (diffing, coordinate/camera math, model-table
matching, config parsing, DTED indexing, terrain cache) is in `earthview3d_core` with **no
osgEarth dependency**, so CI on Linux without a GPU exercises it. `GlobeView3D.cpp` is the only
translation unit that includes osgEarth/OSG.

## 3.2 Class hierarchy

```
earthview3d::GlobeView3D : QOpenGLWidget                 (public; pimpl)
 └─ GlobeView3D::Private                                 (all osgEarth state; hidden)
     ├─ osg::ref_ptr<osgViewer::Viewer>                  SingleThreaded, embedded
     ├─ osg::ref_ptr<osgViewer::GraphicsWindowEmbedded>  Qt owns the GL context
     ├─ osg::ref_ptr<osgEarth::MapNode>                  the map (from the earth file)
     ├─ osg::ref_ptr<osgEarth::Util::EarthManipulator>   orbit camera + tether
     ├─ osg::ref_ptr<osg::Group> entityRoot              parent of all entity visuals
     ├─ std::unordered_map<uint64, EntityVisual> entities
     │     EntityVisual = { GeoTransform geo, MatrixTransform attitude,
     │                      MatrixTransform markerScale, osg::Node modelNode,
     │                      AutoTransform highlight (ring), Geode/Text label, ResolvedStyle }
     ├─ earthview3d::EntityDiff   diff                   (core; add/update/remove by id)
     ├─ earthview3d::ModelTable   modelTable             (core; kindKey → model/style)
     ├─ earthview3d::HeightCache  heights                (core; thread-safe terrain cache)
     ├─ earthview3d::TerrainClearance eyeGround          (core; last-good ground under eye)
     ├─ CameraMode mode + per-mode state (chase/stealth/fly poses, blend, look offsets)
     ├─ const osgEarth::VerticalDatum* egm96             (MSL readouts)
     ├─ osg::ref_ptr<osgEarth::TerrainCallback> tileCallback  (invalidates heights per tile)
     ├─ QLabel* hud                                      (HUD, composited by Qt over GL)
     └─ QTimer frameTimer (interval 0) ─▶ QWidget::update()

Core value/logic types (earthview3d::, GL-free, in headers):
  EntityState3D, Force, entityColor()      — the host-facing record
  MapConfig                                — what to load
  CameraMode, CameraPose, CameraBasis, cameraBasis(), blendPose(), clampEyeAltitude(),
    TerrainClearance, freeFlySpeed(), offsetLatLon(), lookAtAngles(), HudData, formatHud()
  ModelEntry, ModelTable, ResolvedStyle, resolveStyle()
  EntityDiff(::Changes)                    — snapshot diffing
  HeightCache, GroundOffsetStats           — terrain services
  PickCandidate, pickNearest()             — screen-space picking
  screenscale:: metresPerPixel/markerScale/pixelSize   — per-frame marker sizing (I-037)
  RuntimePaths, configureRuntime()         — offline runtime wiring
  TmsLayerSpec, MapCatalogEntry, EarthFileCatalog, DtedMosaic  — map catalog from 2D earth files
```

## 3.3 The osgEarth scene graph

Built once in `Private::buildScene()` (called from `initializeGL()`):

```
osgViewer::Viewer
 └─ sceneData root  = SkyNode  (if MapConfig::sky and not EARTHVIEW3D_NO_SKY)   else osg::Group
     ├─ <earth file node>                       (osgDB::readRefNodeFile → contains the MapNode)
     │    └─ osgEarth::MapNode                   (terrain, imagery, overlays, graticule…)
     │         └─ entityRoot : osg::Group        (added as a child of the MapNode; lighting OFF)
     │              └─ per entity: GeoTransform  (places the entity at lat/lon/alt, ALTMODE_ABSOLUTE)
     │                   ├─ attitude : MatrixTransform   (heading/pitch/roll, D-005 order)
     │                   │    └─ content:
     │                   │         • markerScale : MatrixTransform → aligned shape (placeholder)
     │                   │         • modelNode   : scaled model  (when the kind has a model)
     │                   │        (updateMarkerScales() shows exactly one, per frame)
     │                   ├─ highlight : AutoTransform (ROTATE_TO_SCREEN) → selection ring
     │                   └─ labelGeode : Geode → osgText::Text (shared TextSymbol, ScreenSpaceLayout)
     └─ (SkyNode attaches sun/atmosphere to the viewer)
```

Camera: `viewer->getCamera()` with `setNearFarRatio(2e-5)` normally, `2e-6` for low-eye modes
(first-person/stealth) to stop ground clipping (issue #11). **No logarithmic depth buffer** by
default (D-010; it blacked out close terrain in the embedded widget) — opt in with
`EARTHVIEW3D_LOGDEPTH=1`.

**Embedded-widget requirements** (OSGEARTH_INTEGRATION.md §3.3, learned the hard way, enforced
in code): per-widget **compatibility** surface format (GL 4.6, depth 24, stencil 8, 4× MSAA);
`State::setUseVertexAttributeAliasing(true)` + `setUseModelViewAndProjectionUniforms(true)`
before the first frame; `window->setDefaultFboId(defaultFramebufferObject())` **every** frame
(Qt recreates the FBO on resize); `MapNode::getTerrain()` is null until the first frame; home
viewpoint applied **after** frame 1; viewport/projection in **device pixels**.

## 3.4 Call hierarchy — the 3D frame

```
QTimer(0) ─timeout─▶ QOpenGLWidget::update() ─▶ GlobeView3D::paintGL()
 paintGL():
   1. window->setDefaultFboId(defaultFramebufferObject())         (Qt FBO may have changed)
   2. preFrame()  ◀── HOST CALLBACK: consume snapshot, view->setEntities(...)
        setEntities() → EntityDiff::apply() → createEntity()/updateEntity()/removeEntity()
          updateEntity(): resolveStyle(state, modelTable) → (re)build content if kind changed;
                          clampToTerrain ⇒ terrainHeight() lookup; set GeoTransform position +
                          attitude matrix; accumulate GroundOffsetStats (I-015)
   3. dt = elapsed since last frame (clamped 0.25 s)
   4. installTileCallback()        (once terrain exists: TileInvalidator → HeightCache::invalidate)
   5. resolveHeightRequests()      (answer off-thread heightAt() misses queued in HeightCache)
   6. updateCamera(dt):
        hideOwnEntity() in first person; set near/far ratio for low eye;
        Orbit → EarthManipulator drives the view (return early);
        else targetPose(dt,&pose)  (chase/stealth/first-person/free-fly geometry, core math);
             blendPose() during 1 s transitions; groundAt() → eyeGround.update()/clamp (≥ +2 m);
             applyPose(pose)  (CameraPose → ECEF view matrix via cameraBasis(), right-handed)
   7. updateMarkerScales()         (per entity: screenscale:: from THIS camera's fov/viewport/
                                    eye distance; pick model-vs-icon; scale the selection ring)
   8. viewer->frame()              ◀── osgEarth renders: terrain, imagery, sky, entities, labels
   9. first frame only: apply home viewpoint; else apply any pending camera mode
  10. updateHud(dt)                (eyePose(), AGL from eyeGround, MSL via egm96, speed → QLabel)

 Input (Qt events on the widget):
   left click (no drag)  → pickAt() → pickNearest() → setSelected() + emit entityPicked()
   left drag             → look around (free-fly turns eye; chase free-look/stealth/fp offsets)
   wheel                 → orbit zoom | chase range | stealth eye height | free-fly speed
   keys O/T/F/P/G/L/J/K/R/N/H  → camera modes, HUD, ground-offset overlay, graticule, next, home
   (in Orbit, mouse/keys are forwarded to osgEarth's EventQueue for the EarthManipulator)
```

## 3.5 End-to-end 3D render pipeline (by concern)

**Map load.** `buildScene()` validates `MapConfig`, `osgDB::readRefNodeFile(earthFile)`,
extracts the `MapNode`, optionally wraps it in a `SkyNode`, installs the entity root, loads the
model table and the EGM96 datum, and attaches an `EarthManipulator` with the home viewpoint.
A failure at any step emits `loadFailed(reason)` (surface it in the host UI).

**Entity lifecycle.** The host pushes a full snapshot each frame; `EntityDiff` turns it into
add/update/remove by id. **Add:** `GeoTransform → attitude → content (+ ring + label)`.
**Update:** `resolveStyle()` decides model vs placeholder (most-specific `kindKey` glob wins,
`Force` colours override `rgba`); rebuild content only when the kind/colour/scale changed; set
the `GeoTransform` position (clamped to terrain when asked) and the attitude matrix (model HPR
offset, then entity heading/pitch/roll, D-005). **Remove:** detach the `GeoTransform`; models
stay in `modelCache`.

**Marker sizing (I-037).** Scale is computed per frame from *this* view's camera
(`screenscale::markerScale`), never from `osg::AutoTransform`'s cached per-traversal scale
(which smeared markers across the globe in multi-camera traversals). A placeholder never shrinks
below ~14 px and never grows past 50 km; the model shows once it exceeds
`modelTable.iconBelowPixels`, the icon below that.

**Cameras.** Orbit is osgEarth's `EarthManipulator` (tether mode
`TETHER_CENTER_AND_HEADING`, terrain avoidance on). Chase/stealth/first-person/free-fly are
driven by `GlobeView3D` itself with the manipulator detached, so the eye is clamped
≥ terrain + 2 m every frame (`TerrainClearance` keeps the last good height while tiles load).
All the geometry is GL-free math in `CameraControl` (`cameraBasis` is right-handed at every
pitch incl. ±90°, unit tested — fixes the free-fly mirroring, issue #11).

**Terrain services.** `heightAt()` returns the drawn-terrain height from `HeightCache`
(~1 m cells); a GUI-thread miss queries `Terrain::getHeight` at once, an off-thread miss is
queued and resolved next frame. A `TerrainCallback` invalidates cells under every loaded/refined
tile. `sourceHeightAt()` samples the `ElevationPool` at full resolution (slow; tools/spot
checks). `geoidHeight()` gives EGM96 N for MSL↔HAE. `GroundOffsetStats` (terrain − host
altitude over clamped entities) backs the I-015 debug overlay (key K).

**Picking.** On a non-drag left click, every entity's world position is projected to screen
(`PickCandidate`), and `pickNearest()` returns the closest within 12 px, skipping hidden or
behind-camera entities.

## 3.6 3D extension points

| To add… | Do this (and keep the rules) |
| ------- | ---- |
| A new host-facing field | add to `EntityState3D`; consume it in `GlobeView3D::Private::updateEntity`; update `DOCTRINE_WIRING.md` §2 and the E8 contract test. Keep public headers osgEarth-free. |
| A new camera mode | add to `CameraMode`; geometry as a GL-free function in `CameraControl` (unit test it); wire `targetPose()`/`enterMode()`; expose a method + key. |
| A placeholder shape | extend `createShape()` (size + centreZ); add the `shape` int to the contract. |
| A model format | ensure the osgDB plugin is deployed (`earthview3d_deploy_runtime`); map it in `models.json`. No code change — `loadModel()` uses `osgDB::readRefNodeFile`. |
| A map layer toggle | layers come from the earth file; `setLayerVisible(name,…)` already toggles any `VisibleLayer` by name. |
| New GL-free logic | put it in `earthview3d_core` with a Qt Test; only `GlobeView3D.cpp` may include osgEarth. |

**Rules that gate every 3D change** (CLAUDE.md): osgEarth owns rendering (no custom
terrain/imagery/sky/LOD); public headers include no osgEarth/OSG headers (pimpl); geodetic/ECEF
`double`, never `float` world positions; dependencies only from the pinned vcpkg manifest (no
`FetchContent`); GL-free logic gets Qt Test unit tests; never extend the deprecated 2D pseudo-3D
path.

---

# Part 4 — Cross-cutting concerns

## 4.1 Coordinates

| Frame | Where | Units |
| ----- | ----- | ----- |
| Sim local | Doctrine `TransformComp` | metres, x=east z=north y=alt MSL |
| Geodetic | the boundary + osgEarth `GeoPoint` | lat/lon deg, height m, **double** |
| ECEF | osgEarth world / `scene3d::Geodesy` | metres, double |
| 2D Mercator | EarthView `Camera`/`MercatorProjection` | radians |

Doctrine's `CoordBridge` already delivers geodetic lat/lon/alt in `RenderEntity`, so the adapter
does **only** rad→deg (open question Q1 is resolved for Conquer). Heights crossing into
`EntityState3D.altM` are **HAE** (ellipsoid); DTED is MSL/EGM96 and the earth file declares
`vdatum="egm96"` (D-021), so the terrain and HAE entity heights agree.

## 4.2 Threading

- **Sim thread** only writes the snapshot (unchanged). The adapter's `preFrameCallback` reads it
  on the **GUI thread**, copying only on a new tick.
- **GUI thread** owns everything osgEarth — the viewer, the scene graph, all node changes.
- osgEarth's own worker threads (tile/GDAL) are internal; the code never touches the scene graph
  from them. `heightAt()` is the one method safe to call from another thread (cache + queue).
- Viewer threading model is `SingleThreaded` (required for an embedded Qt widget).

## 4.3 Offline runtime & deployment

osgDB reads `OSG_LIBRARY_PATH` once when the OSG DLLs load (before `main()`), so setting it from
the app is too late. `earthview3d::configureRuntime(RuntimePaths)` (`Runtime.h`) instead adds
the plugin folder to osgDB's path list directly and sets `GDAL_DATA`/`PROJ_DATA` before first
use. The first `GlobeView3D` calls it with `RuntimePaths::defaults()` if the host has not — so a
deployed layout needs **zero** host code. `RuntimePaths::defaults()` finds, next to the
executable: `osgPlugins-<ver>/`, `share/gdal`, `share/proj` (build tree) or their `../share`
installed equivalents, falling back to the build-tree vcpkg folders.
`earthview3d_deploy_runtime(<target> [INSTALL])` produces exactly that layout. See
`OFFLINE_BUILD.md` for the air-gapped build (feeder/offline vcpkg caches, matching VS update).

## 4.4 Keeping the docs honest

Any PR that changes the `earthview3d` public API updates, in the same PR: `DOCTRINE_WIRING.md`
§2/§3, this guide's Part 1 signatures and Part 3 hierarchies, and the E8 compile-only host
contract test (so drift breaks CI). Status/decisions/issues go in `3D_PROGRESS.md`.
