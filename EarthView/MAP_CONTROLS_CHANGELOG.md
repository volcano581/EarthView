# Map Controls — Change Log (for offline machines)

This file is the hand-over for the dev team working on the offline machine. Every map-controls PR adds
a section below that lists **all** files added, changed and removed, the build and deploy steps,
and how to verify. Apply the changes in order; each section can be checked off on its own.

Background: decision **D-034** in `3D_PROGRESS.md`. 3D work is paused, and the 2D view gets
Google-Maps-style on-map controls in QML over the existing OpenGL map (`QQuickWidget` overlay,
"option A"). `MapWidget` and the 2D renderers are unchanged.

Planned PRs:

| PR | Branch | Content | Status |
|---|---|---|---|
| 1 | `2d/ui1-map-controls` | Overlay foundation, side bar (compass, 3D, measure, settings), projection switch, Globe View removed from toolbar | done |
| 2 | `2d/ui2-layers-panel` | Mini-map layers button + panel: OSM / Imagery base maps, Hillshade toggle (row 1); Cities / Borders / Grid overlays | **done** |
| 3 | `2d/ui3-measure-smooth-zoom` | Right-side layout, base map tiles from discovery, measure tool, smooth zoom + inertial pan | **done** |
| 4 | `2d/ui4-mbtiles-share-minzoom` | MBTiles on the imagery server's share, fast large-file loading, fill-the-window minimum zoom | **done** |
| — | later | Raster parent-tile fallback while zooming | planned |

---

## PR 4 — MBTiles on the imagery server, large-file loading, minimum zoom (2026-10-23, branch `2d/ui4-mbtiles-share-minzoom`, on top of PR 3)

### What the user sees

- **No zooming out past a full-height map**, as in Google Maps: the floor is the level where the
  world (or the globe, in Orthographic) is exactly as tall as the map area, `log2(height / 256)`.
  That is about 1.75 for a maximised 1080p window and 2.0 for a 1024 px tall map area. Making the
  window taller raises the floor and zooms in with it. Wheel, buttons and the eased zoom all respect it.
- **MBTiles from the imagery server:** an earth file in `Data/EarthFiles` (next to the imagery and
  CardG files) can name the MBTiles file on the server's **network share**:
  ```
  <image name="OSM" driver="mbtiles"> <url> \\imagery-server\maps\planet.mbtiles </url></image>
  ```
  UNC path, mapped drive, or a path relative to the earth file. It appears in the dropdown and as the
  **OSM Map** tile in the Layers panel. Sample: `Data/EarthFiles/osm_mbtiles.earth.sample`.
- **The 73 GB file stays smooth:**
  - only tiles **in view** at the **current zoom** are read; the file is never copied or scanned.
    Zoomed out means low-zoom tiles: few and small;
  - **vector** tiles are read one level coarser (they are made for 512 px), so there are 4× fewer
    tiles to read and decode, with styling at the displayed zoom (no visual change in colours/widths);
  - after a pan or zoom, reads for tiles that left the view are **dropped before they reach the
    share**, and the newest requests are read first;
  - while new vector tiles load, the previous view's tiles **stay drawn** underneath, so there are no blank flashes.

### Why it was slow

`MbTilesReader` opened and closed the SQLite database for **every tile**. On a 73 GB file over a
share, each open re-reads the schema across the network and starts with an empty cache, and SMB
file locking adds round trips. In addition, reads could not be cancelled, so after every pan or
zoom the shared thread pool worked through all outdated tiles first.

### How it works now

- One **persistent read-only connection per reader thread and file**, opened as
  `file:...?mode=ro&immutable=1` (no locking; falls back to a plain read-only open), with
  `query_only`, a 64 MB page cache and in-memory temp storage. SQLite reads only the B-tree index pages and the tile's bytes.
- A dedicated reader pool, `MbTilesReader::threadPool()`: 4 threads that never expire, so their
  connections stay warm.
- `MbTilesRequestSet`: the keys a loader still wants, shared with its jobs (thread-safe). Jobs check it first.

### Files added

| File | Purpose |
|---|---|
| `EarthView/tests/tst_mbtilesreader.cpp` | Builds a small MBTiles file and tests reads, 32 parallel pool reads, immutable URIs (UNC, drive, escaping), earth-file `mbtiles` entries, and the request set |
| `EarthView/Data/EarthFiles/osm_mbtiles.earth.sample` | Template for the share entry |

### Files changed

| File | Change |
|---|---|
| `EarthView/MbTilesReader.h/.cpp` | Persistent per-thread connections, `immutableUri()`, `threadPool()`, `MbTilesRequestSet` |
| `EarthView/TMSLoader.h/.cpp` | Raster MBTiles reads go to the reader pool, newest first. Stale reads are dropped (`abortRequestsExcept` now covers them). |
| `EarthView/VectorTileRenderer.h/.cpp` | Same for vector reads. Tile zoom is one level coarser (`kVectorTileZoomOffset`). Previous tiles are kept until the new view is loaded (`m_retainedKeys`, max 384). |
| `EarthView/MapSourceCatalog.h/.cpp` | `driver="mbtiles"` entries in layer-list earth files; `mbTilesLayer()` and `isMbTilesSource()` helpers (the `Data/OSM` discovery uses the same helper) |
| `EarthView/Camera.h/.cpp` | Opt-in `setFitHeightMinimumZoom()` / `minimumZoomLevel()`, **off by default** |
| `EarthView/mapcontrols/MapControlsOverlay.cpp` | Turns the minimum zoom on |
| `EarthView/mapcontrols/SmoothNavigator.cpp` | Eased zoom stops at the floor (re-checked every frame) |
| `EarthView/mapcontrols/BaseMapSources.cpp` | Earth files with an MBTiles layer get the OSM Map tile |
| `EarthView/Data/EarthFiles/README.md` | The `mbtiles` entry |
| `EarthView/tests/tst_smoothnavigation.cpp`, `EarthView/tests/CMakeLists.txt` | Minimum-zoom tests; new test target |

### Behaviour changes to know (2D, D-034)

- Vector MBTiles are drawn from one zoom level coarser tiles (the same look at 512 px design size).
- Without `MapControlsOverlay`, the minimum zoom stays 0 as before. Every other change applies to all hosts:
  they are loading fixes, not visual changes.
- The 3D view ignores `mbtiles` entries.

### Offline machine setup

1. Copy `Data/EarthFiles/osm_mbtiles.earth.sample` to `osm_mbtiles.earth` and put the share path of
   the file in `<url>`. The account running EarthView needs **read** access to the share.
2. Remove the local copy from `Data/OSM` if you had one. Otherwise both appear: two OSM Map tiles, named.
3. Start EarthView. Choose the **OSM Map** tile (or the dropdown entry). The first view reads a few
   index pages; later reads hit the 64 MB per-thread cache.

### Verify

1. `ctest --test-dir build-msvc -C Release --output-on-failure` (Qt `bin` on `PATH`): **22/22**.
2. On the offline machine, with the share entry:
   - The OSM Map tile shows the planet file. Zoom in fast to a city: tiles appear within moments, with no freeze.
   - Pan quickly back and forth: the map follows, with no growing backlog. Zoom out: no blank flashes; the old tiles stay until the new ones are in.
   - Zoom out fully: it stops where the map fills the window's height. Maximise or restore: the floor follows the height.

### Verification status

- MSVC build: **OK**. `ctest`: **22/22 passed** (MBTiles test against a real SQLite MBTiles file, including 32 parallel reads).
- Against the 73 GB file on the real share: **pending (offline machine)**. Not available on the dev PC.

---

## PR 3 — Right-side layout, discovered base maps, measure tool, smooth zoom (2026-10-23, branch `2d/ui3-measure-smooth-zoom`, on top of PR 2)

### What the user sees

- **Layout:** the side bar sits at the **right edge, vertically centred**. The **scale bar** moves to the
  **bottom-right**, and the **Layers mini-map** sits right above it. Panels open to the left.
- **Base map tiles = the Map Source dropdown.** The tiles come from the same discovery
  (`MapSourceCatalog::discoverMapSources()`) in the same order, so tile *i* is dropdown item *i*:
  - every discovered **earth file** gets a tile with its dropdown name and an icon for its kind:
    satellite/imagery → imagery picture, CardG/topo → topographic picture, anything else → generic map;
  - **MBTiles** files get an **"OSM Map"** tile (with several MBTiles: "OSM Map <name>");
  - no map sources at all: one **"OSM (online)"** tile, which is the dropdown's "OpenStreetMap fallback".
  Hillshade stays in row 1 after a divider. With more than three sources the row wraps.
- **Measure (ruler button, now enabled):** the panel opens left of the bar. It has **Line | Path**, units
  **km / mi / nmi / m / ft**, the total in large type, the last leg (Path), a hint line and **Clear**.
  While it is open:
  - a click on the map adds a point. Dragging still pans, because a click is a press and release within 4 px;
  - Line: the 2nd click finishes, and a 3rd click starts a new line. Path: every click adds a leg.
    **Right-click removes the last point**;
  - the line is drawn along the **great circle** (blue, with a white halo), with a dashed rubber band
    to the cursor and the total next to the last point. The cursor is a crosshair;
  - distances are **WGS-84 ellipsoidal** (Vincenty), with a spherical fallback for near-antipodal points;
  - clicks off the globe (Orthographic) are ignored. A map click does **not** close the measure panel.
    Closing it (ruler, ✕, Settings or Layers) clears the line.
- **Smooth zoom:** the mouse wheel eases toward the target level (time constant 70 ms), anchored at the
  cursor; consecutive notches add up. 0.5 level per notch, as before. The toolbar **Zoom In / Zoom Out**
  buttons now zoom **one level**, eased.
- **Inertial pan:** releasing a fast drag (> 150 px/s over the last 80 ms) lets the map glide on and slow
  down (time constant 0.32 s). A drag that stopped before release does not glide. Any new press stops a glide.

### Files added

| File | Purpose |
|---|---|
| `EarthView/mapcontrols/GeoMeasure.h/.cpp` | WGS-84 distance (Vincenty), path length, units, formatting, great-circle points |
| `EarthView/mapcontrols/MeasureModel.h/.cpp` | Measure points and result, exposed to QML as `measure` |
| `EarthView/mapcontrols/SmoothNavigation.h/.cpp` | Pure easing / glide maths (constants at the top of the header) |
| `EarthView/mapcontrols/SmoothNavigator.h/.cpp` | Drives the `Camera` each frame through its public `zoom()` / `pan()` |
| `EarthView/mapcontrols/qml/icons/thumb_topo.svg`, `thumb_map.svg` | Tile pictures for CardG/topographic and other earth files |
| `EarthView/tests/tst_geomeasure.cpp`, `tst_smoothnavigation.cpp` | 10 + 8 test functions |

### Files changed

| File | Change |
|---|---|
| `EarthView/MapWidget.h/.cpp` | **Two opt-in hooks; default output unchanged:** `setScaleBarCorner(Qt::Corner)` (default bottom-left; the overlay sets bottom-right) and `setOverlayPainter(std::function<void(QPainter&)>)`, a QPainter callback after the GL frame, empty by default. |
| `EarthView/mapcontrols/BaseMapSources.h/.cpp` | Rewritten: `entries(sources)` returns one `{label, icon}` per discovered source (the OSM/Imagery rule from PR 2 is gone). |
| `EarthView/mapcontrols/MapControlsModel.h/.cpp` | `baseMaps` (list) + `currentBaseMap` (index) replace `baseMap` / `osmAvailable` / `imageryAvailable`. New `measureOpen` (+ `toggleMeasure()`), exclusive with Settings/Layers, and not closed by `closePanels()`. |
| `EarthView/mapcontrols/MapControlsOverlay.h/.cpp` | New positions. Takes over the map's **wheel** (eased zoom), feeds drags to the navigator (glide), handles measure clicks and right-click, draws the measure line through the overlay painter, sets the scale bar to the bottom-right. Exposes `measure()` and `navigator()`. |
| `EarthView/mapcontrols/qml/MapSideBar.qml` | Measure panel. Panels are centred on the bar. |
| `EarthView/mapcontrols/qml/LayersButton.qml` | Button at the right, panel opens left, one tile per `baseMaps` entry. |
| `EarthView/mapcontrols/CMakeLists.txt` | New sources and icons. |
| `EarthView/MainWindow.h/.cpp`, `EarthView/MapViewWindow.h/.cpp` | Keep their overlay. Toolbar Zoom In/Out go through the navigator. Tiles ↔ dropdown by index. |
| `EarthView/tests/*` | Model tests updated (base map list, measure panel rules). `tst_basemapsources` rewritten. Two new tests. |

### Not changed (on purpose)

`Camera`, `MapSourceCatalog`, `TMSLoader`, `MbTilesReader` and all the renderers. Without
`MapControlsOverlay` (e.g. a host that does not opt in), the map zooms, pans and draws exactly as before.

### Build / deploy

Same as PR 1. No new Qt modules, no new deploy files.

### Verify

1. `ctest --test-dir build-msvc -C Release --output-on-failure` (Qt `bin` on `PATH`): **21/21**.
2. Visual check (Windows, by a person):
   - The side bar is right-middle, the Layers mini-map is bottom-right above the scale bar, and the scale bar is bottom-right.
   - Layers panel: one tile per dropdown entry, with matching names. Picking a tile changes the dropdown and the map, and vice versa.
     With an `.mbtiles` in `Data/OSM` there is an "OSM Map" tile.
   - Wheel: zoom glides and stays under the cursor. Fast scrolling adds up. Zoom In/Out buttons glide one level.
   - Throw the map with a quick drag: it glides and slows down. A slow drag stops dead.
   - Ruler → Line: click two points to get the total in the panel and on the map. A 3rd click starts over. Change units.
   - Path: click several points; right-click removes the last one; Clear empties it. Dragging still pans while measuring.
   - Orthographic: clicks off the globe add nothing, and lines follow the globe.

### Verification status

- MSVC build: **OK**. `ctest`: **21/21 passed** (including the new `tst_geomeasure` and `tst_smoothnavigation`).
- On screen: the new layout renders as described (side bar right-middle, mini-map above the bottom-right
  scale bar), and the app starts at zoom 2 as before.
- Clicking through measure / glide / tiles: **pending (Windows, human)**. Automated input was blocked on the dev PC.

### Known limitations

- While zooming, tiles of the new level pop in as they load; showing the parent tile until then is planned.
- The measure line is not kept after the panel closes, and the result cannot yet be copied or exported.
- Keyboard zoom (+/−) still steps without easing.

---

## PR 2 — Mini-map layers button and panel (2026-10-23, branch `2d/ui2-layers-panel`, on top of PR 1)

### What the user sees

- A **mini-map button** labelled "Layers" at the **bottom-left** of the map, above the scale bar.
  It previews the current base map (OSM or Imagery), with a hillshade tint when Hillshade is on.
- Clicking it opens a panel to the right:
  - **MAP TYPE** (row 1): **OSM** | **Imagery**, one at a time, with a blue outline on the active
    one. After a divider comes **Hillshade**, an on/off tile (blue check badge when on) that blends
    the DEM terrain over whichever base map is active.
  - separator
  - **OVERLAYS**: **Cities**, **Borders**, **Grid**, each on/off with a check badge.
- A base map tile with no map source behind it is greyed out, and hovering it explains why
  ("No imagery map source found").
- Opening the layers panel closes the Settings panel and vice versa. A click on the map closes both.
- **Toolbar (both windows):** Textures, Borders, Grid, Cities and Terrain are **removed**; the panel replaces them.
  The main window keeps all five in the **View** menu, and they stay in sync with the panel both ways.
  The Map Source dropdown stays on the toolbar and stays in sync with the OSM / Imagery tiles.
  The Secondary Window has no menu, so its **Textures** (hide all imagery) switch is no longer
  reachable there.
- The base map choice applies to **both windows**, as the Map Source dropdown always has.
  Hillshade and the overlays are **per window**.

### Which file is "OSM" and which is "Imagery"

Rule in `BaseMapSources.cpp` (unit-tested):

- **Imagery** = the first map source whose file name or label contains `satellite` or `imagery`
  (e.g. `satellite_imagery.earth` → "Satellite Imagery").
- **OSM** = the first other source, **MBTiles first** (`Data/OSM/*.mbtiles`), otherwise e.g.
  `cardg_topographic.earth` ("CardG Maps").
- No map sources at all: OSM = the online OpenStreetMap fallback, as before; Imagery greyed out.
- Only imagery sources: OSM greyed out.

To use different files, rename them so they match the rule, or change `BaseMapSources::isImagery()`.

### Files added

| File | Purpose |
|---|---|
| `EarthView/mapcontrols/BaseMapSources.h/.cpp` | The OSM / Imagery rule above; maps a source index to a base map and back |
| `EarthView/mapcontrols/qml/LayersButton.qml` | Mini-map button and layers panel |
| `EarthView/mapcontrols/qml/LayerTile.qml` | Shared square tile (picture, label, selected outline, check badge, greyed-out state); also used by the Settings panel now |
| `EarthView/mapcontrols/qml/icons/thumb_osm.svg`, `thumb_imagery.svg`, `thumb_hillshade.svg`, `overlay_cities.svg`, `overlay_borders.svg`, `overlay_grid.svg` | Tile pictures (64×64 viewBox). Replace freely with the same names. |
| `EarthView/tests/tst_basemapsources.cpp` | 6 test functions for the OSM / Imagery rule |

### Files changed

| File | Change |
|---|---|
| `EarthView/mapcontrols/MapControlsModel.h/.cpp` | New properties: `layersOpen`, `baseMap` (enum `OsmBaseMap` / `ImageryBaseMap`), `osmAvailable`, `imageryAvailable`, `hillshadeVisible`, `citiesVisible`, `bordersVisible`, `gridVisible`; `toggleLayers()`, `closePanels()`. The two panels are exclusive. Selecting an unavailable base map is ignored. |
| `EarthView/mapcontrols/MapControlsOverlay.h/.cpp` | Creates a second `QQuickWidget` (the layers button) anchored bottom-left, 12 px from the left and 92 px from the bottom, clearing the scale bar. Starts the layer toggles from the map's current state and applies their changes to `MapWidget` (`setTerrainVisible`, `setCitiesVisible`, `setBordersVisible`, `setGridVisible`). `view()` is renamed `sideBarView()`, and `layersView()` is added. |
| `EarthView/mapcontrols/qml/MapSideBar.qml` | The Settings panel uses the shared `LayerTile` (no visible change). |
| `EarthView/mapcontrols/CMakeLists.txt` | Adds the new sources, QML files and icons. |
| `EarthView/MainWindow.cpp` | The five layer actions are left off the toolbar when map controls are built (`#ifndef EARTHVIEW_HAS_MAP_CONTROLS`). View menu ↔ panel sync. OSM / Imagery tiles ↔ Map Source dropdown, applied to both windows. |
| `EarthView/MapViewWindow.h/.cpp` | `mapControls()` accessor; the five layer actions are left off its toolbar. |
| `EarthView/tests/tst_mapcontrolsmodel.cpp`, `EarthView/tests/CMakeLists.txt` | 3 more model test functions; adds `tst_basemapsources`. |
| `EarthView/DOCTRINE_WIRING.md` | §5: base maps are the host's job, with an example. |

### Not changed (on purpose)

`MapWidget`, `MapSourceCatalog`, `Camera` and all the renderers are untouched. No new Qt modules or deploy files beyond PR 1.

### Build / deploy

Same as PR 1. No new requirements.

### Verify

1. `ctest --test-dir build-msvc -C Release --output-on-failure` (with Qt `bin` on `PATH`): all pass, including
   `tst_mapcontrolsmodel` and `tst_basemapsources`.
2. Visual check (Windows, by a person), on a machine **with** map sources in `Data/EarthFiles` / `Data/OSM`:
   - The **Layers** mini-map shows bottom-left, above the scale bar.
   - Click it to open the panel. The OSM / Imagery tiles, Hillshade and the three overlays are shown.
   - Click **Imagery**: the map switches to the satellite source, the Map Source dropdown changes,
     the mini-map preview changes, and the Secondary Window (if open) switches too.
   - Choose a source in the dropdown: the tiles follow.
   - Toggle **Hillshade** / **Cities** / **Borders** / **Grid**: the map changes, the check badge follows,
     and the same View-menu item toggles. Toggling from the View menu updates the panel.
   - Open Settings: the layers panel closes. Click the map: any open panel closes.
   - The toolbar has no Textures / Borders / Grid / Cities / Terrain buttons.

### Verification status

- MSVC build: **OK**. `ctest`: **19/19 passed**.
- On screen (this dev PC, no local map sources, so the online OSM fallback was active): the Layers
  mini-map renders bottom-left above the scale bar, the side bar renders bottom-right, the toolbar is
  trimmed, and the app logged no QML warnings.
- Opening the panel and the tile clicks: **pending (Windows, human)**. Automated clicking was blocked on the dev PC.

### Known limitations

- Previews are drawn icons, not live renders of the map.
- The base map choice is shared by both windows; the overlays are per window.
- In the Secondary Window, Textures (hide all imagery) is no longer reachable.

---

## PR 1 — Overlay foundation and side bar (2026-10-23, branch `2d/ui1-map-controls`)

### What the user sees

- A white rounded **side bar at the bottom-right of the map** with four buttons, top to bottom:
  1. **Compass**: red needle points north. Clicking it resets to north-up. The 2D map does not
     rotate yet, so the needle stays north and the click only closes open panels. The needle
     already follows `MapControlsModel::bearingDegrees` for when rotation lands.
  2. **3D globe**: always shown and **disabled** (greyed, tooltip "3D view (not available yet)").
     It stands for a future real 3D view. Enable it with `model()->setGlobe3DAvailable(true)`.
  3. **Ruler (measure)**: shown and **disabled** until PR 3 (tooltip "coming soon").
  4. **Settings (gear)**: opens a panel to the left with **PROJECTION: Mercator | Orthographic**
     tiles. The selected tile is outlined blue. A click anywhere on the map closes the panel.
- Tooltips on hover, on all four buttons (including the disabled ones).
- The same side bar appears in the **Secondary Window**, which now opens only when the user clicks **Secondary Window** (single window at startup).
- The **Globe View** button is **removed from the toolbar** in both windows. In the main window
  it remains in **View ▸ Globe View** and stays in sync with the Settings panel. The secondary
  window has no menu, so its projection is switched from Settings only.
- Unchanged on the toolbar: Zoom In, Zoom Out, Reset View, Map Source, Load Borders, Textures,
  Borders, Grid, Cities, Terrain, Secondary Window, Wrap Longitude. (Textures / Borders / Grid /
  Cities / Terrain move into the layers panel in PR 2.)

### Files added

| File | Purpose |
|---|---|
| `EarthView/mapcontrols/CMakeLists.txt` | `earthview_mapcontrols` static library (alias `earthview::mapcontrols`); QML and SVG compiled in as Qt resources under `qrc:/earthview/mapcontrols/` |
| `EarthView/mapcontrols/MapControlsModel.h/.cpp` | Control state exposed to QML as `mapControls`: `orthographic`, `bearingDegrees`, `globe3DAvailable`, `measureAvailable`, `settingsOpen`; `resetNorth()`, `toggleSettings()`, tooltip requests. Two-way projection sync with `Camera`. Qt Core only (unit-tested). |
| `EarthView/mapcontrols/MapControlsOverlay.h/.cpp` | Creates the transparent `QQuickWidget` child of a `MapWidget`, sizes it to the QML content, anchors it bottom-right (12 px right, 24 px bottom margin), shows tooltips, closes panels on a map click. Usage: `new MapControlsOverlay(mapWidget);` |
| `EarthView/mapcontrols/qml/MapSideBar.qml` | Side bar and Settings panel. Plain `QtQuick` only, **no Qt Quick Controls** dependency. |
| `EarthView/mapcontrols/qml/icons/compass.svg`, `globe3d.svg`, `ruler.svg`, `settings.svg`, `proj_mercator.svg`, `proj_orthographic.svg` | Icons (24×24 and 40×40 viewBox, colour `#3c4043`). Replace freely with the same file names. |
| `EarthView/tests/tst_mapcontrolsmodel.cpp` | 8 Qt Test functions: defaults, bearing normalisation, reset north, projection sync both ways, camera destroyed |
| `EarthView/MAP_CONTROLS_CHANGELOG.md` | This file |

### Files changed

| File | Change |
|---|---|
| `EarthView/CMakeLists.txt` | New option `EARTHVIEW_BUILD_MAP_CONTROLS` (default ON). It finds `Qt6 Qml Quick QuickWidgets Svg` (QUIET) and switches itself OFF with a status message if any is missing, so a 2D-only Qt still builds. Adds `add_subdirectory(mapcontrols)`. When ON, the `EarthView` app links `earthview::mapcontrols`, defines `EARTHVIEW_HAS_MAP_CONTROLS`, and passes `--qmldir mapcontrols/qml` to `windeployqt`. |
| `EarthView/MainWindow.cpp` | Secondary Window starts hidden (opens on click). Creates `MapControlsOverlay` on the main map (under `#ifdef EARTHVIEW_HAS_MAP_CONTROLS`). Globe View is no longer added to the toolbar; it stays in the View menu. |
| `EarthView/MapViewWindow.cpp` | Creates `MapControlsOverlay` on the secondary map. Globe View action removed (toolbar was its only place); the status-bar message on projection change is kept. |
| `EarthView/tests/CMakeLists.txt` | Adds `tst_mapcontrolsmodel` (links `earthview::core` + `Qt6::Test`; no Qt Quick needed). |
| `EarthView/3D_PROGRESS.md` | Decision **D-034**: 3D paused, 2D on-map controls. |
| `EarthView/ROADMAP_3D.md` | "Paused" note at the top. |
| `EarthView/DOCTRINE_WIRING.md` | §5: how a host adds the on-map controls (optional). |

### Files removed

None.

### Not changed (on purpose)

`MapWidget`, `Camera`, `TileRenderer`, `VectorTileRenderer`, `BorderRenderer`, `GridRenderer`,
`CityRenderer`, `TextRenderer`, shaders, and the `earthview_core` link interface. `earthview_core` still
does **not** depend on Qt Quick. The existing "Stealth" button at the top-right of the map is
left as is (pseudo-3D path, rule 2).

### Offline build requirements (new)

The Qt kit on the offline machine must contain these modules. The standard `msvc2022_64`
installer includes them; check that the folders exist:

- `C:/Qt/6.11.0/msvc2022_64/lib/cmake/Qt6Qml`, `Qt6Quick`, `Qt6QuickWidgets`, `Qt6Svg`
- `C:/Qt/6.11.0/msvc2022_64/qml/QtQuick` (QML runtime modules)
- `C:/Qt/6.11.0/msvc2022_64/plugins/imageformats/qsvg.dll`

No new downloads, no vcpkg changes, no `FetchContent`.

### Build

```
cmake -S EarthView -B build-msvc -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build-msvc --config Release --target EarthView tst_mapcontrolsmodel
```

The configure log must **not** say `map controls disabled`. If it does, a Qt module from the
list above is missing.

### Deploy (copying the app to another offline PC)

`windeployqt` runs after the build with `--qmldir EarthView/mapcontrols/qml`. Next to
`EarthView.exe` it must now place, in addition to the earlier files:

- `Qt6Qml.dll`, `Qt6Quick.dll`, `Qt6QuickWidgets.dll`, `Qt6Svg.dll`, `Qt6QmlModels.dll`,
  `Qt6QmlMeta.dll`, `Qt6QmlWorkerScript.dll`, `Qt6OpenGL.dll`
- `qml/QtQuick/…` folder
- `imageformats/qsvg.dll`

If the side bar is missing or the icons are blank on a target PC, one of these is missing. Run
the app from a console and look for `MapControlsOverlay:` or QML warnings.

### Verify

1. Unit test: `ctest --test-dir build-msvc -C Release -R tst_mapcontrolsmodel --output-on-failure`
   (put `C:/Qt/6.11.0/msvc2022_64/bin` on `PATH` first, as for the other tests). Expect
   all passed.
2. Visual check (Windows, by a person):
   - The side bar is at the bottom-right, with four buttons. 3D and Ruler are greyed out.
   - Hover each button: a tooltip appears.
   - Gear opens the Projection panel. Click **Orthographic**: the globe appears and the tile turns
     blue. **View ▸ Globe View** is now checked. Uncheck it in the menu: the panel switches back
     to Mercator.
   - Click on the map while the panel is open: the panel closes, and panning still works.
   - Panning, wheel zoom and keyboard on the map work everywhere except over the side bar.
   - Resize the window: the side bar stays anchored bottom-right.
   - The Secondary Window has its own side bar, and its toolbar has no Globe View button.
   - The toolbar has no Globe View button. Every other toolbar button is unchanged.

### Verification status

- Built with MSVC (VS 2026, Qt 6.11.0 `msvc2022_64`): **OK**.
- `tst_mapcontrolsmodel`: **all 8 functions passed**. `tst_mercatorprojection`: passed.
- App started offscreen for 8 s: no QML, SVG or overlay warnings.
- Visual check on a real screen: side bar renders (after the fix above). Click-through checklist: **pending (Windows, human)**.

### Fix after first run (2026-10-23)

On Windows the side bar did not appear. The console showed `QQuickWidget: Failed to get a QRhi`
and `'OpenGL' is not compatible with this QQuickWidget`. Cause: Qt Quick uses Direct3D 11 by default,
but a window that contains the OpenGL `MapWidget` composites with OpenGL. Fix in
`MapControlsOverlay.cpp`: `QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL)` is called
before the `QQuickWidget` is created. This setting is **process-wide**. A host that also uses
Qt Quick elsewhere (e.g. Doctrine) must use OpenGL there too, or create the overlay before any other
Qt Quick window. The offscreen smoke test cannot catch this, so always check on a real screen.
Checked on screen afterwards: the side bar renders in the Secondary View (screenshot taken).

### Change: single window at startup (2026-10-23, owner request)

`MainWindow.cpp`: the Secondary Window no longer opens at startup. The **Secondary Window**
toolbar/View action now starts unchecked, and the window opens only when the user clicks it.
The `m_secondaryWindow->show()` call at the end of `setupUI()` was removed. The window is still
created (hidden) and still receives map-source and border changes, so it is current when opened.

### Known limitations

- The 2D map has no rotation, so the compass is static.
- The 3D and Measure buttons are placeholders (disabled).
- Tooltips use the native Qt widget tooltip, positioned below the button.
- The overlay is light-themed only.
