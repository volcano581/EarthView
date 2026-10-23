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
| 3 | `2d/ui3-measure` | Measure tool: Line / Path, km / mi / nmi / m / ft, geodesic distance | planned |
| — | later | Inertial zoom / pan, parent-tile fallback | planned |

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
