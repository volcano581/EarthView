# Wiring earthview3d into Doctrine (local, off GitHub)

Doctrine (inside the Conquer superbuild) is **not on GitHub**. Cloud agents finish and
polish everything in this repo; the final wiring into Doctrine is done **on the local
machine** by following this document. Agents keep this document and the
`earthview3d` API in sync — if a task changes the API, it updates the steps below.

Everything Doctrine-specific described here is a **contract**, not code from Doctrine.

---

## 1. What lives where

| Piece | Repo | Who |
| ----- | ---- | --- |
| `earthview3d` library (osgEarth widget, entities, cameras, map config) | EarthView (GitHub) | cloud agents |
| `earthview3d_demo` app with a synthetic entity feed (stands in for Doctrine) | EarthView (GitHub) | cloud agents |
| CMake package / `add_subdirectory` support + runtime deploy helper | EarthView (GitHub) | cloud agents |
| Doctrine adapter (~150 lines): snapshot → `EntityState3D`, window + menu action | Conquer (local) | human / local Claude session |
| Build switch + runtime deploy in Doctrine's CMake | Conquer (local) | human / local Claude session |

A working adapter already exists locally on Conquer branch `earthview-3d-test`
(`doctrine/src/view3d/Doctrine3DWindow.*`, commit `1fd3d89`). After E-tasks change the
API, update that adapter, not a new one.

---

## 2. Host contract (what the adapter must provide)

The adapter runs on the GUI thread inside the widget's pre-frame callback. API as of E1:
`#include <earthview3d/GlobeView3D.h>`, class `earthview3d::GlobeView3D(MapConfig, homeLat,
homeLon)`, records `earthview3d::EntityState3D`, `setPreFrameCallback()`, `setEntities()`.
E3 adds attitude, `kindKey`, `force`, the model table (`MapConfig::modelTable`), picking and
selection (see below).
Entries with `id == 0` are ignored; a repeated id in one snapshot keeps the first entry.

| `EntityState3D` field | Meaning | Doctrine source (Conquer, as of 2026-10-08) |
| --------------------- | ------- | ------------------------------------------- |
| `id` | stable per entity, 64-bit | `RenderEntity::id` (flecs entity id) |
| `latDeg`, `lonDeg` | WGS-84, double | `RenderEntity::lat_rad`, `lon_rad` × 180/π |
| `altM` | height above the **WGS-84 ellipsoid** (HAE, not MSL — D-021) | `RenderEntity::alt_m` (if the sim stores MSL, add `view->geoidHeight(lat, lon)`) |
| `headingDeg` | clockwise from north | `RenderEntity::heading_rad` × 180/π |
| `pitchDeg`, `rollDeg` (E3) | attitude, degrees (nose up / right wing down positive) | not in the snapshot yet — 0 until added |
| `kindKey` (E3) | model-table key, e.g. SISO `"1.1.225.1.1.3"` or `"ground/armour"`; empty = use `shape` | SISO kind when available; empty until then |
| `shape` | placeholder marker when no model matches (0 inf, 1 air, 2 armour, 3 arty) | `RenderEntity::unitType` |
| `force` (E3) | `Force::Friendly/Hostile/Neutral/Unknown` colours the marker and label; `Unspecified` uses `rgba` | force/side of the unit |
| `rgba` | 0xRRGGBBAA, used when `force` is `Unspecified` | `RenderEntity::symbolColor` |
| `label` | short text | `RenderEntity::label` (char[8]) |
| `clampToTerrain` | put on 3D terrain | `true` for non-air units (I-015: sim ground is a flat plane at the origin altitude, ~20 m under DTED near Islamabad) |
| `visible` | hide when false (visual and selection kept) | `RenderEntity::visible` |

Model table (E3): `MapConfig::modelTable` = path to `models.json` (relative to `dataRoot`;
format in `ModelTable.h`, example `Data/models/models.json`). Each entry maps a `kindKey` glob
(`*`, `?`; most specific wins) to a model file, scale, HPR offset, clamp flag and fallback
shape. Missing files, unmatched kinds and models smaller than `iconBelowPixels` on screen are
drawn as placeholder markers. A broken table is reported via `loadFailed` and ignored.

Selection and picking (E3): left-click on an entity selects it and emits
`entityPicked(quint64 id)`; `setSelected(id)` selects from the host (0 clears) and emits
`selectionChanged(label)`. `setLabelsVisible(bool)` toggles the decluttered labels.

Terrain services (E5, `TerrainServices.h`): `heightAt(lat, lon, &hae)` returns the drawn
terrain height (cached; callable from the sim thread — a miss returns false and is resolved
on the next frame, ask again). `sourceHeightAt()` samples the elevation source at full
resolution (slow; tools/spot checks). `geoidHeight(lat, lon)` gives EGM96 N (MSL = HAE − N).
`setGroundOffsetOverlay(true)` / key K shows terrain − host altitude for clamped entities on
the HUD and `groundOffsetStats()` returns it — use it in L1 to measure I-015.

Map data (E6): the default earth file `maps/earthview.earth` is fully offline; build its
local layers once with `tools/offline_data/build_offline_data.py` (see its README) and set
`MapConfig::cacheDir` for the osgEarth tile cache. `maps/earthview_online.earth` adds OSM.
`setLayerVisible(name, bool)`, `layerVisible()`, `layerNames()` toggle earth-file layers
(e.g. a host menu for "Graticule", "Hillshade", "Places (cities, towns)").

Map sources from EarthView's earth files (`EarthFileCatalog.h`): Doctrine's data tree already
carries the 2D viewer's layer-list earth files (`Data/EarthFiles/*.earth`, one TMS layer per
line, no `<map>` root). `EarthFileCatalog::build(directories, templatePath, outputDir,
&warnings)` returns one `MapCatalogEntry` per file — `label` for a host dropdown (same labels
as 2D), `earthFile` for `MapConfig::earthFile` — writing a standard earth file per source by
splicing the TMS layers into the template's `<!--@EARTHVIEW3D_TMS_LAYERS@-->` placeholder.
`defaultSearchDirectories(dataRoot)` gives the folders to scan. The host switches map by
rebuilding the view with the new `MapConfig` (osgEarth loads the map when the view is built).
2D code is untouched: it still reads the original files.

Cameras (E4, `CameraControl.h`): `setCameraMode(CameraMode)` with `Orbit`, `Chase`,
`Stealth`, `FirstPerson`, `FreeFly` (smooth 1 s transition; chase/stealth/first person follow
the selected entity), `cameraMode()`, signal `cameraModeChanged(CameraMode)`.
`chaseSelected()`, `stealthSelected()`, `firstPersonSelected()`, `freeFly()` and `untether()`
(= orbit) are shortcuts. `setStealthEyeHeight(m)` (default 10 m), `setChaseFreeLook(bool)`,
`setHudVisible(bool)` (HUD: mode, heading, pitch, altitude, AGL, speed, target). Every mode
but orbit keeps the eye at least 2 m above terrain. The widget handles its own keys (O, T, F,
P, G, L, J, N, H; WASD/Q/E/Shift in free-fly); a host menu can call the same methods.

Rules for the adapter:
- Read the snapshot the same way the 2D map does (copy on new tick only:
  compare the latest tick count before copying).
- No coordinate maths: Doctrine already produces geodetic positions (CoordBridge).
- Home viewpoint = the scenario `GeodeticOrigin`.
- Never include osgEarth headers in Doctrine files; include only `earthview3d` public
  headers (D-011).

---

## 3. Local wiring steps (Conquer)

Prerequisites: Conquer's `msvc` preset builds; osgEarth available (vcpkg tree or the
`earthview3d` package from E7); EarthView checked out locally.

1. **Bring in earthview3d.** In Conquer's root `CMakeLists.txt`, after EarthView 2D:
   ```cmake
   option(CONQUER_ENABLE_3D "Build the osgEarth 3D view" OFF)
   if(CONQUER_ENABLE_3D)
       add_subdirectory(${EARTHVIEW_REPO}/EarthView/earthview3d earthview3d)   # or find_package(earthview3d 0.7 CONFIG REQUIRED)
   endif()
   ```
2. **Adapter.** Copy `doctrine/src/view3d/Doctrine3DWindow.*` from branch
   `earthview-3d-test`; replace its `GlobeView3D` usage with the `earthview3d` public API.
   Changes since the test (E1): include `<earthview3d/GlobeView3D.h>`; namespace
   `earthview3d`; `GlobeView3D::EntityState3D` is now `earthview3d::EntityState3D`; the
   constructor takes an `earthview3d::MapConfig` (earth file, data root, cache dir, sky)
   instead of an earth-file path; diagnostics `DOCTRINE_3D_LOGDEPTH`/`DOCTRINE_3D_NO_SKY`
   are now `EARTHVIEW3D_LOGDEPTH`/`EARTHVIEW3D_NO_SKY`. The widget calls
   `osgEarth::initialize()` itself. Runtime files (E7): with step 3's
   `earthview3d_deploy_runtime(doctrine)` the OSG plugins and GDAL/PROJ data sit next to
   `doctrine.exe` and the first view finds them itself (`RuntimePaths::defaults()`); no
   host code. Only if Doctrine keeps them elsewhere, call
   `earthview3d::configureRuntime(paths)` (`<earthview3d/Runtime.h>`) before the first view;
   setting `OSG_LIBRARY_PATH` in `main()` is too late. The plugin folder must be right even if the
   map looks fine without it: without `osgdb_freetype` no `.ttf` loads and osgText falls back
   to a built-in ASCII-only font, so osgEarth's own labels silently lose non-ASCII glyphs.
   `configureRuntime()` re-reads osgEarth's default font after registering the folder
   (D-025), which repairs the font osgEarth had already loaded in its Registry constructor.
   Keep the mapping table of §2.
   Optional (E2): `GlobeView3D::entityPosition(id, &lat, &lon, &alt)` reports where an entity
   was placed after clamping — useful for a 2D/3D agreement check like the demo's
   `--check-agreement`.
3. **Link + define.** In `doctrine/CMakeLists.txt`:
   `target_link_libraries(doctrine PRIVATE earthview3d::earthview3d)` and
   `DOCTRINE_HAVE_OSGEARTH=1`; call `earthview3d_deploy_runtime(doctrine INSTALL)` (E7) instead
   of the test's hand-written copy commands. It copies the vcpkg DLLs, `osgPlugins-*` and
   `share/gdal|proj` next to `doctrine.exe` after each build, and with `INSTALL` adds them
   plus the Qt and MSVC runtime to `cmake --install` (see `EarthView/OFFLINE_BUILD.md`).
   With `find_package`, Conquer must use the same vcpkg toolchain/triplet (static library).
4. **UI.** Keep the "3D View" toolbar action and View-menu entry from the test branch
   (`DoctrineMainWindow::open3DView`). Connect `entityPicked(id)` to Doctrine's selection
   (E3) and Doctrine's selection to `setSelected(id)`.
5. **Data.** Point the map config at the data package from E6 (relative paths).
6. **Verify** with the test hooks (`DOCTRINE_AUTOSTART_DEMO=1`, `DOCTRINE_OPEN_3D=1`,
   `DOCTRINE_2D_SNAPSHOT`, `DOCTRINE_3D_SNAPSHOT`, `DOCTRINE_3D_VIEW=chase|stealth`) and the
   checklist below.

## 4. Acceptance checklist (local)

- [ ] Conquer builds with `CONQUER_ENABLE_3D=OFF` exactly as before (no new deps).
- [ ] With it ON: "3D View" opens; entities appear after the scenario starts.
- [ ] 2D and 3D positions agree (compare snapshots taken at the same moment).
- [ ] Ground units sit on the terrain; air units at sim altitude.
- [ ] Chase / stealth / first-person / free-fly work on a moving entity.
- [ ] Selecting in 2D selects in 3D and vice versa.
- [ ] Offline: network unplugged, map and entities still render.
- [ ] Closing and reopening the 3D window works; app exit is clean.
- [ ] Record the result in `3D_PROGRESS.md` (Verification log) — that file is on GitHub.
