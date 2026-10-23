# Wiring earthview3d into Doctrine (local, off GitHub)

Doctrine (inside the Conquer superbuild) is **not on GitHub**. Cloud agents finish and
polish everything in this repo; the final wiring into Doctrine is done **on the local
machine** by following this document. Agents keep this document and the
`earthview3d` API in sync — if a task changes the API, it updates the steps below.

Everything Doctrine-specific described here is a **contract**, not code from Doctrine.

> For a step-by-step wiring walkthrough with a drift-reconciliation checklist (the Doctrine
> interface may have changed on the offline machine), plus class/call hierarchies and the
> end-to-end 2D and 3D render pipelines, see
> **[INTEGRATION_AND_ARCHITECTURE.md](INTEGRATION_AND_ARCHITECTURE.md)**. This file stays the
> authoritative source for the `EntityState3D` field-mapping table.

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

> **This contract is tested.** `EarthView/tests/tst_hostcontract.cpp` (E8) carries a struct
> shaped like Doctrine's `RenderEntity`, converts it exactly as the table below says, and pins
> every signature named in this section with `static_assert`. Change the API and that test
> stops compiling — which is the point: CI says so here, not the next time someone opens the
> Conquer tree. Update the test and this section together.

The adapter runs on the GUI thread inside the widget's pre-frame callback:
`#include <earthview3d/GlobeView3D.h>`, class `earthview3d::GlobeView3D(MapConfig, homeLat,
homeLon, parent)`, records `earthview3d::EntityState3D`,
`setPreFrameCallback(std::function<void()>)`, `setEntities(const std::vector<EntityState3D>&)`.
Attitude, `kindKey`, `force`, the model table (`MapConfig::modelTable`), picking and selection
came with E3; cameras and the HUD with E4; terrain services with E5.

Entries with `id == 0` are ignored; a repeated id in one snapshot keeps the first entry.
`entityCount()` reports how many the view holds, and `entityColor(state)`
(`EntityState3D.h`) gives the colour the view will use, for a host legend.

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
`selectionChanged(QString label)`. `selectedId()` and `selectedLabel()` read it back,
`selectNextEntity()` steps through them (key N) and `setLabelsVisible(bool)` toggles the
decluttered labels. `loadFailed(QString reason)` carries every non-fatal load problem (a
missing model file, a broken model table, no EGM96 plugin): connect it to the host's log
rather than letting it pass silently.

Terrain services (E5, `TerrainServices.h`): `heightAt(lat, lon, &hae)` returns the drawn
terrain height (cached; callable from the sim thread — a miss returns false and is resolved
on the next frame, ask again). `sourceHeightAt()` samples the elevation source at full
resolution (slow; tools/spot checks). `geoidHeight(lat, lon)` gives EGM96 N (MSL = HAE − N).
`setGroundOffsetOverlay(true)` / key K shows terrain − host altitude for clamped entities on
the HUD and `groundOffsetStats()` returns it (`GroundOffsetStats`: count, min, max, mean) —
use it in L1 to measure I-015. `haveGeoid()` says whether the EGM96 plugin was found at all;
when it is false `geoidHeight()` returns 0 and the MSL readouts are really HAE.

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
`defaultSearchDirectories(dataRoot)` gives the folders to scan — the data root's own, then the
working directory and the executable's folder with `Data/EarthFiles`, which is every folder the
2D viewer reads, so a layer list that feeds the 2D map is never invisible to the 3D one
(I-046), plus `<dataRoot>/maps` so the standard osgEarth maps shipped in the data tree are
offered too — generated `tms_*.earth` files are skipped there, each being already the
`earthFile` of its own source entry (I-051). It returns cleaned, de-duplicated absolute paths;
`build()`'s `warnings` names a folder holding only `*.earth.sample` templates. An entry with no
`tmsLayers` is a standard map rather than a tile-server one, which is how a host should pick a
default. The host switches map by
rebuilding the view with the new `MapConfig` (osgEarth loads the map when the view is built).
2D code is untouched: it still reads the original files.

Those layer-list files may also carry the offline machine's DTED folder, as the 2D viewer
reads it:

```
<elevation name="DTED" driver="dted"> <url> D:/EarthViewData/DTED </url></elevation>
```

`EarthFileCatalog` turns that entry into an osgEarth `<GDALElevation>` layer
(`TmsLayerSpec::Kind::DtedElevation`, `vdatum` defaulting to `egm96` because DTED is
orthometric, D-021). osgEarth opens a GDAL dataset rather than a folder, so `generate()`
indexes the folder itself through `DtedMosaic` (`DtedMosaic.h`): it walks the sub-folders,
reads each cell's DTED header and writes `<folder>/dted_mosaic.vrt`, which the generated map
names. No GDAL tools are needed on the machine, and the mosaic is rebuilt only when a cell is
newer than it. `DtedMosaic::ensureMosaic()` is that whole rule and is callable on its own, for
a host that configures DTED outside a layer list.

A host that loads `maps/earthview*.earth` directly, without the catalog, still names the
mosaic in the earth file (I-043): nothing in that path can resolve a folder. There is no 3D
API for the folder itself — it is map configuration, not host state.

Cameras (E4, `CameraControl.h`): `setCameraMode(CameraMode)` with `Orbit`, `Chase`,
`Stealth`, `FirstPerson`, `FreeFly` (smooth 1 s transition; chase/stealth/first person follow
the selected entity), `cameraMode()`, signal `cameraModeChanged(CameraMode)`.
`chaseSelected()`, `stealthSelected()`, `firstPersonSelected()`, `freeFly()`, `untether()`
(= orbit) and `goHome()` are shortcuts. `cameraModeName(mode)` and
`parseCameraMode(name, &mode)` (`CameraControl.h`) turn a mode into text and back, for a host
menu or a stored setting — `CameraMode`'s order is part of this contract, so a stored value
keeps its meaning. `setStealthEyeHeight(m)` (default 10 m), `setChaseFreeLook(bool)`,
`setHudVisible(bool)` (HUD: mode, heading, pitch, altitude, AGL, speed, target). Every mode
but orbit keeps the eye at least 2 m above terrain. The widget handles its own keys (O, T, F,
P, G, L, J, N, H; WASD/Q/E/Shift in free-fly); a host menu can call the same methods.

Diagnostics: `viewpointDescription()` is a one-line eye/mode/frame-time summary for a status
bar or a log, and `saveSnapshot(pngPath)` writes the current frame; both are what the demo's
`--snapshot3d` and its agreement check use.

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
   constructor takes an `earthview3d::MapConfig` (earth file, data root, cache dir, model
   table, sky and sky time; `validate()` reports a bad one and `MapConfig::fromJsonFile()`
   reads it from a host settings file; `MapConfig::discoverDataRoot(appDir, workingDir,
   compiledDefault)` finds a deployed `Data` folder beside the executable, so a host needs no
   launcher script and no build-time path to locate its data — D-033)
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
- [ ] DTED from the elevation server loads: the folder is named in the map source and the
      terrain follows the cells (`--spot` against a known elevation).
- [ ] Record the result in `3D_PROGRESS.md` (Verification log) — that file is on GitHub.

If the adapter needed a change this document does not describe, the contract moved: update §2,
`tests/tst_hostcontract.cpp` and the adapter together, so the next person inherits one story.

## 5. Optional: 2D on-map controls (D-034)

Independent of `earthview3d`. A host that embeds the 2D `MapWidget` can add the on-map side
bar (compass, disabled 3D button, measure, settings with Mercator/Orthographic) and the layers mini-map:

```cmake
target_link_libraries(doctrine PRIVATE earthview::mapcontrols)   # needs Qt Qml, Quick, QuickWidgets, Svg
```
```cpp
#include "MapControlsOverlay.h"
auto* overlay = new MapControlsOverlay(mapWidget);   // owned by mapWidget
overlay->model()->setGlobe3DAvailable(false);         // default; true once a 3D view exists
```

The overlay also adds the **layers** mini-map (bottom-right, above the scale bar, which it moves
there), the **measure** tool, **eased wheel zoom** and **inertial pan**. Hillshade, Cities, Borders
and Grid are applied to the `MapWidget` by the overlay itself. The base map tiles are the host's
job, because the host owns the map sources. Give the tiles the same list as your source picker:

```cpp
const auto sources = MapSourceCatalog::discoverMapSources();
MapControlsModel* controls = overlay->model();
controls->setBaseMaps(BaseMapSources::entries(sources));   // tile i == sources[i]
controls->setCurrentBaseMap(0);
QObject::connect(controls, &MapControlsModel::currentBaseMapChanged, [=](int index) {
    if (index >= 0 && index < sources.size())
        mapWidget->setTileSourceLayers(sources.at(index).layers);
});
// Optional: route your own zoom buttons through the eased zoom.
overlay->navigator()->zoomBy(+1.0);
```

`earthview_core` itself does not depend on Qt Quick. Deployment: run `windeployqt` with
`--qmldir EarthView/mapcontrols/qml` so the QtQuick QML modules and the SVG image plugin ship.
