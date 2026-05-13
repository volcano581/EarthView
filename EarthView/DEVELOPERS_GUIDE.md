# EarthView Developer Guide

This guide explains the current EarthView source tree, runtime architecture,
data formats, rendering pipeline, and common extension points. It is intended
for developers who need to build, debug, or extend the application.

## 1. Project Overview

EarthView is a Qt 6 desktop GIS/map viewer written in C++17. It renders map
imagery, borders, latitude/longitude grids, city markers, and labels using a
Qt `QOpenGLWidget` with an OpenGL 3.3 core profile.

The application currently supports:

- TMS/XYZ-style raster imagery from URL templates.
- Raster MBTiles imagery and vector MBTiles geometry.
- Map source discovery from `.earth` files.
- Flat Web Mercator rendering.
- Orthographic globe-style rendering.
- Optional horizontal longitude wrapping.
- Dynamic scale bar overlay.
- ESRI shapefile border loading.
- NDJSON city/settlement label loading.
- Batched line rendering and text atlas rendering.

The application starts in `main.cpp`, creates `MainWindow`, and uses
`MapWidget` as the central OpenGL rendering widget.

## 2. Repository Layout

Important top-level files:

| Path | Purpose |
| --- | --- |
| `CMakeLists.txt` | Main build definition. This is the practical source of truth for new builds. |
| `main.cpp` | Application entry point and global OpenGL format setup. |
| `MainWindow.*` | Qt main window, toolbar, map-source discovery, and UI actions. |
| `MapWidget.*` | Central OpenGL widget and render orchestration. |
| `Camera.*` | Camera state, navigation, projections, and visible tile/extent math. |
| `MercatorProjection.*` | Coordinate conversion helpers. |
| `TMSLoader.*` | Tile source state, visible tile selection, network/MBTiles tile loading. |
| `TileRenderer.*` | OpenGL tile drawing. |
| `TextureManager.*` | OpenGL texture creation and cache management. |
| `MbTilesReader.*` | MBTiles metadata, raster tile reading, and vector tile parsing. |
| `VectorTileRenderer.*` | OpenGL vector MBTiles rendering with batched fills, lines, and labels. |
| `BorderRenderer.*` | Shapefile parsing and border line generation/rendering. |
| `GridRenderer.*` | Latitude/longitude grid line and label generation. |
| `CityLoader.*` | City NDJSON discovery and parsing. |
| `CityRenderer.*` | City marker and label generation/rendering. |
| `LineBatchRenderer.*` | Shared colored line batch renderer. |
| `TextRenderer.*` | Text label atlas generation and rendering. |
| `ShaderUtils.*` | Shader file discovery and shader program loading. |
| `Constants.h` | Shared GIS and tile constants. |
| `fpsCounter.h` | Simple frame-rate counter used by the overlay. |
| `shaders/` | GLSL shader programs copied to the build output after build. |
| `Data/` | Local runtime data such as borders, cities, `.earth` files, and MBTiles. |

Generated/build folders such as `build/`, `out/`, and `x64/` should not be
treated as source code.

## 3. Build And Run

### Requirements

- CMake 3.16 or newer.
- C++17 compiler.
- Qt 6 with these modules:
  - Core
  - Widgets
  - Network
  - OpenGL
  - OpenGLWidgets
  - Sql
- SQLite Qt driver for MBTiles support.
- OpenGL 3.3 core profile support, or software OpenGL.

`CMakeLists.txt` looks for Qt in this order:

1. `EARTHVIEW_QT_PREFIX` CMake cache path, if supplied.
2. `C:/Qt/6.11.0/llvm-mingw_64`, if present.
3. `C:/Qtfull/6.11.0/llvm-mingw_64`, if present.
4. Normal CMake Qt discovery.

### Configure

Example:

```powershell
cmake -S . -B build -DEARTHVIEW_QT_PREFIX=C:/Qt/6.11.0/llvm-mingw_64
```

If Qt is already discoverable by CMake, the prefix can be omitted:

```powershell
cmake -S . -B build
```

### Build

```powershell
cmake --build build --parallel 4
```

The build copies `shaders/` to the executable directory. On Windows, the
`EARTHVIEW_DEPLOY_QT` option defaults to `ON`, so `windeployqt` is run after
linking when it can be found.

To disable deployment:

```powershell
cmake -S . -B build -DEARTHVIEW_DEPLOY_QT=OFF
```

### Run

The executable is produced at:

```text
build/EarthView.exe
```

For systems with problematic GPU drivers, request software OpenGL:

```powershell
$env:EARTHVIEW_FORCE_SOFTWARE_OPENGL='1'
.\build\EarthView.exe
```

The app also honors `QT_OPENGL=software`.

## 4. Runtime Startup Flow

Startup happens in this order:

1. `main.cpp` selects an OpenGL 3.3 core profile `QSurfaceFormat`.
2. If software OpenGL is requested, Qt attribute `AA_UseSoftwareOpenGL` is set
   before `QApplication` is created.
3. `MainWindow` is constructed.
4. `MainWindow::setupUI()` creates toolbar actions, creates `MapWidget`, and
   discovers available map sources.
5. `MapWidget` constructs `Camera` and `TmsLoader`.
6. When the OpenGL context is ready, `MapWidget::initializeGL()` creates the
   renderers and loads default border/city data.
7. `MapWidget::paintGL()` renders each frame.

The app uses a zero-interval `QTimer` in `MapWidget` to request continuous
repaints.

## 5. Main UI And Source Discovery

`MainWindow` owns the top-level UI:

- Zoom in.
- Zoom out.
- Reset view.
- Map source selection.
- Load borders.
- Toggle textures, borders, grid, and cities.
- Toggle longitude wrapping.
- Toggle globe view.

Map source discovery is implemented in `MainWindow.cpp`.

### MBTiles Sources

`discoverMbTilesSources()` searches these locations:

- `Data/OSM` under the current working directory.
- `Data/OSM` beside the application.
- `../Data/OSM` relative to the application.

Each `*.mbtiles` file is opened through `MbTilesReader::readMetadata()`. A
single `TmsLoader::TileSourceLayer` is created for each valid MBTiles file.
Formats `pbf`, `mvt`, and `vector` are treated as `VectorMbTiles` sources and
are rendered by `VectorTileRenderer` rather than the raster tile path.

The checked-in `.gitignore` ignores `Data/OSM/*.mbtiles`, so large tile
archives can exist locally without being committed.

### Earth File Sources

`discoverEarthSources()` searches for `*.earth` in:

- `Data/EarthFiles` under the current working directory.
- Current working directory.
- `Data/EarthFiles` beside the application.
- Application directory.
- `../Data/EarthFiles` relative to the application.
- Parent of the application directory.

`readEarthLayers()` parses `<image driver="tms">` elements. Non-TMS image
drivers are skipped.

Supported layer fields include:

- `name` attribute.
- `min_level`, `minLevel`, `min_zoom`, `minZoom`.
- `max_level`, `maxLevel`, `max_zoom`, `maxZoom`.
- Nested `<url>...</url>`.
- Nested `<min_level>`, `<minZoom>`, `<min_zoom>`.
- Nested `<max_level>`, `<maxZoom>`, `<max_zoom>`.

The parser wraps the file content in a synthetic root element before parsing,
which allows `.earth` files with multiple top-level entries.

Tile URL templates must eventually contain `{z}`, `{x}`, and `{y}`. If they do
not, `tileTemplateFromUrl()` appends:

```text
/{z}/{x}/{y}.png
```

It also recognizes literal suffixes such as `/z/x/y.png`, `/z/x/y.jpg`, and
`/z/x/y.jpeg`.

### TileMapResource Metadata

If an earth URL ends with `tilemapresource.xml`,
`applyTileMapResourceMetadata()` tries to read metadata. It checks local
candidate paths first and falls back to a short network fetch.

Metadata can provide:

- Tile image extension.
- Minimum tile zoom.
- Maximum tile zoom.
- TMS Y-origin behavior.

This metadata is important for layered TMS sources where a coarse source covers
a large area and higher-resolution sources cover smaller areas.

## 6. Central Rendering Coordinator: MapWidget

`MapWidget` is the central widget and the primary coordinator.

It owns:

- `Camera`
- `TmsLoader`
- `TileRenderer`
- `VectorTileRenderer`
- `BorderRenderer`
- `GridRenderer`
- `CityRenderer`
- `LineBatchRenderer`
- `TextRenderer`
- Cached line vertices and label batches
- Toggle state for textures, borders, grid, and cities

### OpenGL Lifecycle

Renderers are not created in the constructor. They are created in
`initializeGL()` after the OpenGL context exists.

Important rule:

When adding code that creates or deletes OpenGL resources, ensure a valid
current context exists. `MapWidget` already calls `makeCurrent()` in places
such as `setTileSourceLayers()` when it needs to clear texture resources.

### Frame Rendering Order

`MapWidget::paintGL()` renders in this order:

1. Clear color/depth buffers.
2. Update FPS counter.
3. Draw orthographic globe backdrop when in globe mode.
4. Render imagery tiles.
5. Render batched borders and grid lines.
6. Render city markers.
7. Disable depth test.
8. Rebuild labels if dirty.
9. Draw the scale bar overlay.
10. Render grid/city/vector/FPS/scale text labels.
11. Re-enable depth test.

Imagery is rendered before vector overlays. Text is rendered last.

### Dirty Caches

`MapWidget` caches line vertices and text labels:

- `m_lineBatchDirty`
- `m_mapLabelsDirty`

Camera changes invalidate both caches. Toggle changes invalidate only the
affected cache.

## 7. Camera And Coordinate Systems

`Camera` owns:

- Center in internal Mercator coordinates.
- Floating zoom level.
- Viewport size.
- Horizontal wrap state.
- Projection mode:
  - `Mercator`
  - `Orthographic`

### Internal Mercator Units

The application stores Mercator positions as radians, not meters:

- X range is approximately `[-pi, pi]`.
- Y range is constrained by constants in `Constants.h`.
- Pixel conversion uses `GIS::EARTH_RADIUS` where meters are needed.

`MercatorProjection` provides conversion helpers:

- `latLonToMercator()`
- `mercatorToLatLon()`
- `screenToMercator()`
- `mercatorToScreen()`
- `orthographicMercatorToScreen()`
- `orthographicScreenToMercator()`
- `wrapMercatorX()`
- `wrapTileX()`
- `tileToMercatorBounds()`
- `getTileRange()`

### Zoom

`Camera::getZoomLevel()` returns a floating value used for smooth navigation.
`Camera::getTileZoomLevel()` returns:

```cpp
ceil(m_zoomLevel)
```

clamped to `[0, GIS::MAX_TILE_ZOOM]`.

### Resolution

`Camera::getResolution()` returns meters per screen pixel:

```cpp
GIS::EARTH_CIRCUMFERENCE / (GIS::TILE_SIZE * pow(2.0, m_zoomLevel))
```

It is cached until zoom or viewport changes.

### Visible Tile Range

`Camera::getTileRange(int zoomLevel)` computes visible tiles from the visible
Mercator extent. In orthographic mode it adds a preload margin and clamps Y to
valid tile rows.

## 8. Imagery Loading

Imagery loading is split between:

- `TmsLoader`: decides which tiles are visible and loads tile images.
- `TextureManager`: stores OpenGL textures.
- `TileRenderer`: draws active tiles.

### TileSourceLayer

`TmsLoader::TileSourceLayer` describes one imagery layer:

```cpp
struct TileSourceLayer {
    enum class SourceType {
        UrlTemplate,
        MbTiles,
        VectorMbTiles
    };

    QString name;
    QString urlTemplate;
    QString mbTilesPath;
    QString mbTilesFormat;
    SourceType sourceType = SourceType::UrlTemplate;
    int minZoom = 0;
    int maxZoom = GIS::MAX_TILE_ZOOM;
    int minTileZoom = 0;
    int maxTileZoom = GIS::MAX_TILE_ZOOM;
    bool tmsYOrigin = false;
};
```

The distinction between display zoom and tile zoom matters:

- `minZoom`/`maxZoom` describe when the layer is selected for display.
- `minTileZoom`/`maxTileZoom` describe actual tile resources available on disk
  or server.

### Display Zoom Derivation

`deriveDisplayZoomsFromTileResources()` sorts layers by `maxTileZoom` and
assigns display zoom bands. Layers with lower max tile zoom become coarse
sources. Layers with higher max tile zoom become high-resolution sources.

The highest-resolution group is extended to `GIS::MAX_TILE_ZOOM`.

### Underlay Behavior

`layerIndicesForZoom()` returns:

- Layers whose display zoom band matches the camera tile zoom.
- Lower-resolution layers whose display band has ended but whose tile resources
  can still be used as an underlay.

The returned layers are sorted by effective tile zoom, from coarse to detailed.
This lets coarse imagery remain visible while high-resolution imagery is loaded
on top of it.

### Visible Tile Update

`TmsLoader::updateVisibleTiles()`:

1. Reads the current camera tile zoom.
2. Gets active layer indices for that zoom.
3. Computes a tile zoom and tile range for each active layer.
4. Refuses excessive updates over `2048` tiles per layer.
5. Adds new active tiles.
6. Starts network or raster MBTiles tile fetches.
7. Removes no-longer-visible tiles.
8. Aborts pending network requests that are no longer visible.

Active tiles are stored in:

```cpp
QMap<QString, TileInfo> m_activeTiles;
```

Tile keys include layer index, zoom, X, and Y:

```text
{layerIndex}_{z}_{x}_{y}
```

Texture cache keys wrap X so longitude wrapping can reuse textures:

```cpp
MercatorProjection::wrapTileX(x, z)
```

### Network Tiles

For URL sources, `fetchTile()`:

1. Builds a URL by replacing `{z}`, `{x}`, and `{y}`.
2. Uses TMS Y inversion when `tmsYOrigin` is true.
3. Sends a Qt network request.
4. Tracks pending requests by texture cache key.
5. On completion, decodes the image with `QImage::loadFromData()`.

Loaded images are stored on the matching active tile. Texture creation is
deferred to the render path so it happens with a current OpenGL context.

### MBTiles Tiles

For raster MBTiles sources, `fetchMbTile()` uses
`MbTilesReader::readTileImage()`. The tile data path is synchronous.

Raster MBTiles are decoded with `QImage::loadFromData()`.

Vector MBTiles formats (`pbf`, `mvt`, `vector`) do not enter this texture path.
They are parsed by `MbTilesReader::readVectorTile()` and rendered as geometry by
`VectorTileRenderer`.

### Tile Failures

`handleTileFailure()` removes failed active tiles unless the tile has
`fallbackLayerIndices`. The current layered-underlay path usually does not
populate `fallbackLayerIndices` because lower-resolution layers are active as
separate tiles.

## 9. Tile Rendering

`TileRenderer` draws all active tiles from `TmsLoader`.

Important behavior:

- Active tile keys are sorted before drawing.
- Coarser `tileZoomLevel` values are drawn first.
- Higher-resolution tiles are drawn later, which places them above lower
  resolution underlays.
- Texture creation from `QImage` happens lazily during rendering.

### Flat Mercator Mode

In flat mode each tile is drawn as one screen-space quad.

### Orthographic Mode

In globe mode each tile is subdivided into a 24x24 mesh. Each grid cell is
projected through `Camera::projectMercatorToScreen()`. Cells that cannot be
projected are skipped. This curves imagery onto the orthographic globe.

## 10. Texture Management

`TextureManager` owns OpenGL texture IDs and a simple size-limited cache.

Default cache size:

```cpp
GIS::TEXTURE_CACHE_SIZE_MB // 200 MB
```

Texture upload behavior:

- Images are converted to `QImage::Format_RGBA8888`.
- `GL_LINEAR_MIPMAP_LINEAR` minification is used.
- `GL_LINEAR` magnification is used.
- `GL_CLAMP_TO_EDGE` wrapping is used.
- Mipmaps are generated after upload.
- Anisotropic filtering is enabled when `GL_EXT_texture_filter_anisotropic` is
  available.

Eviction is based on insertion order stored in `m_textureUsageOrder`.

## 11. MBTiles Reader And Vector Rendering

`MbTilesReader` uses Qt SQL with the SQLite driver.

It reads:

- `metadata` table for source metadata.
- `tiles` table for tile blobs.

The reader creates a unique SQLite connection name per operation, closes the
database, and removes the connection afterward.

### Metadata

`MbTilesMetadata` contains:

- `valid`
- `name`
- `format`
- `scheme`
- `minZoom`
- `maxZoom`

The scheme defaults to `tms`.

### Raster Tiles

Raster tile blobs are loaded directly into a `QImage`.

### Vector Tiles

Vector tiles are decoded using a small internal Mapbox Vector Tile parser:

- Handles protobuf varints.
- Handles gzip-compressed tile payloads.
- Decodes layers, features, tags, and geometry.
- Exposes decoded layers/features/paths to `VectorTileRenderer`.

The vector renderer has a deliberately simple style:

- Landcover/landuse/parks.
- Water.
- Buildings at zoom 13 and above.
- Boundaries.
- Waterways.
- Transportation lines with class-based color/width.
- Place labels and transportation names at appropriate zooms.

This is not a full Mapbox style engine. It is a lightweight OpenGL renderer
that keeps vector MBTiles as geometry instead of converting them into images.

## 12. Borders

`BorderRenderer` loads ESRI shapefiles directly. It does not depend on GDAL or
other GIS libraries.

Supported shape types:

- PolyLine: `3`
- Polygon: `5`
- PolyLineZ: `13`
- PolygonZ: `15`
- PolyLineM: `23`
- PolygonM: `25`

The loader:

1. Validates the shapefile magic number.
2. Detects coordinate mode from the `.prj` file when available.
3. Falls back to coordinate heuristics based on bounds.
4. Converts all points to internal Mercator radians.
5. Stores each part as a `BorderPolygon`.

Recognized coordinate modes:

- Geographic degrees.
- Web Mercator meters.
- Mercator radians.

`BorderRenderer` still contains direct OpenGL render methods, but the main
`MapWidget` path uses `appendMercatorLines()` and `appendScreenLines()` to feed
`LineBatchRenderer`.

## 13. Grid

`GridRenderer` generates latitude/longitude grid lines and labels.

Grid step by zoom:

| Zoom | Step |
| --- | --- |
| `< 2` | 45 degrees |
| `< 4` | 30 degrees |
| `< 6` | 15 degrees |
| `< 8` | 5 degrees |
| `>= 8` | 1 degree |

The grid supports:

- Flat Mercator rendering.
- Orthographic rendering.
- Horizontal wrap in flat mode.
- Dynamic labels near the current center latitude/longitude.

Like borders, the normal frame path appends grid lines to the shared
`LineBatchRenderer`.

## 14. City Data

City data is discovered and loaded by `CityLoader`, then rendered by
`CityRenderer`.

### Directory Structure

City files are expected under:

```text
Data/Cities/{country-code}/*.ndjson
```

Examples:

```text
Data/Cities/pk/place_city.ndjson
Data/Cities/pk/place-town.ndjson
Data/Cities/pk/place-village.ndjson
Data/Cities/pk/place-hamlet.ndjson
```

Files are grouped by rank based on filename:

- `city`
- `town`
- `village`
- `hamlet`

### City JSON Format

Each NDJSON line should be a JSON object. The loader uses:

- `location`: array where index 0 is longitude and index 1 is latitude.
- `name`: preferred label.
- `display_name`: fallback label; only the first comma-separated part is used.

Latitude must be between `-85` and `85`.

### Zoom Bands

| Rank | Zoom Range |
| --- | --- |
| City | 0-7 |
| Town | 8-10 |
| Village | 11-13 |
| Hamlet | 14-18 |

Layers are loaded lazily the first time their zoom range is visible.

### Labels And Markers

`CityRenderer::renderMarkers()` renders point markers with
`point_marker.vert/frag`.

`CityRenderer::appendLabels()` creates `TextRenderer::Label` entries and avoids
overlap with a simple rectangle collision list.

Label/marker limits depend on zoom:

| Zoom | Max labels |
| --- | --- |
| `< 8` | 140 |
| `< 11` | 300 |
| `< 14` | 650 |
| `>= 14` | 900 |

Markers are capped at twice the label cap.

## 15. Text Rendering

`TextRenderer` renders all labels through a generated texture atlas.

For each frame label set:

1. Labels are hashed.
2. If the hash changed, a new atlas image is painted with `QPainter`.
3. One textured quad is generated per packed label.
4. The atlas is uploaded as an OpenGL texture.
5. All label quads are drawn in one batch.

The atlas has a maximum width of 4096 and is clamped to the GPU maximum texture
size.

Label data is defined by:

```cpp
struct Label {
    QString text;
    QRectF rect;
    QFont font;
    QColor textColor;
    QColor backgroundColor;
    QMargins textMargins;
    int radius;
    Qt::Alignment alignment;
};
```

The FPS overlay is also rendered through `TextRenderer`.

## 16. Line Rendering

`LineBatchRenderer` is the shared renderer for colored line segments.

It supports two coordinate modes:

- `Screen`: vertices are already in screen pixels.
- `Mercator`: vertices are in internal Mercator radians and are transformed in
  the shader using camera center and pixels-per-Mercator.

Each vertex contains:

```cpp
float position[2];
float color[4];
```

Current users:

- Borders.
- Grid lines.

## 17. Shaders

Shaders are stored in `shaders/` and loaded at runtime by `ShaderUtils`.

Shader lookup roots include:

- `shaders` beside the application.
- `../shaders` relative to the application.
- `shaders` in the current working directory.
- `../shaders` relative to the current working directory.
- `shaders` beside `ShaderUtils.cpp`.

Main shader pairs:

| Files | Purpose |
| --- | --- |
| `textured_quad.vert`, `textured_quad.frag` | Tiles and text atlas quads. |
| `colored_2d.vert`, `colored_line.frag` | Screen-space colored line batches. |
| `colored_mercator.vert`, `colored_line.frag` | Mercator-space colored line batches. |
| `solid_2d.vert`, `solid_2d.frag` | Globe backdrop and older solid line paths. |
| `mercator_line.vert`, `solid_2d.frag` | Older border rendering path. |
| `point_marker.vert`, `point_marker.frag` | City marker points. |

When adding a shader:

1. Add the file under `shaders/`.
2. Add it to the `SHADERS` list in `CMakeLists.txt`.
3. Load it through `ShaderUtils::loadProgram()`.
4. Keep vertex attribute locations aligned with the renderer struct layout.

## 18. Data Locations

Runtime data is discovered from current working directory and application
directory locations. Typical local layout:

```text
Data/
  Borders/
    World_Countries_Generalized_Shapefile/
      World_Countries_Generalized.shp
      World_Countries_Generalized.shx
      World_Countries_Generalized.dbf
      World_Countries_Generalized.prj
  Cities/
    pk/
      place_city.ndjson
      place-town.ndjson
      place-village.ndjson
      place-hamlet.ndjson
  EarthFiles/
    *.earth
  OSM/
    *.mbtiles
```

The current source tree includes city and border data. `Data/EarthFiles` may be
empty in a fresh checkout. MBTiles files are intentionally ignored by Git.

## 19. Projection Modes

### Mercator Mode

This is the default flat map. Tiles are drawn as flat quads. Mercator overlays
can be transformed directly by shaders.

### Orthographic Mode

Enabled by the "Globe View" action.

Behavior:

- The map is drawn as a globe-like orthographic projection.
- `MapWidget::drawGlobeBackdrop()` draws a dark disk and outline.
- Tiles are subdivided before rendering so texture quads curve with the globe.
- Grid and border lines are generated in screen coordinates because visibility
  changes across the globe horizon.
- Camera panning adjusts latitude/longitude rather than flat Mercator offsets.

## 20. Longitude Wrapping

Horizontal wrapping is enabled by the "Wrap Longitude" action.

Key implementation points:

- `Camera::setHorizontalWrapEnabled()` controls the mode.
- `MercatorProjection::wrapMercatorX()` wraps the camera center.
- Tile cache keys wrap X through `wrapTileX()`.
- Grid and border generation compute visible world copy ranges.
- City rendering checks world copies so labels/markers can repeat horizontally.

## 21. Common Development Tasks

### Add A New Toolbar Action

1. Add a `QAction` in `MainWindow::setupUI()`.
2. Add it to the toolbar and, if appropriate, the menu.
3. Connect it to `MapWidget`, `Camera`, or a renderer method.
4. Show a short status message through `statusBar()->showMessage()`.

### Add A New Overlay Renderer

1. Create a new renderer class with a `Camera*`.
2. Delay OpenGL resource creation until a valid context exists.
3. Instantiate it in `MapWidget::initializeGL()`.
4. Delete it in `MapWidget::~MapWidget()` after `makeCurrent()`.
5. Add toggle state and dirty flags if the overlay generates cached data.
6. Render it in `MapWidget::paintGL()` at the correct layer order.

For line overlays, prefer appending to `LineBatchRenderer` instead of creating a
separate line renderer.

For text overlays, append `TextRenderer::Label` entries and let
`TextRenderer` batch them.

### Add A New Tile Source Type

1. Extend `TmsLoader::TileSourceLayer::SourceType`.
2. Add source discovery/parsing where appropriate, usually in `MainWindow.cpp`.
3. Add a fetch method in `TmsLoader`.
4. Keep decoded tile images as `QImage` until render time.
5. Let `TextureManager` create OpenGL textures with an active context.
6. Include source type in cache keys if it can collide with existing keys.

### Add Or Change Earth File Parsing

Most `.earth` parsing is in these functions:

- `readEarthLayers()`
- `tileTemplateFromUrl()`
- `applyTileMapResourceMetadata()`

When adding fields, parse both attribute-style and nested-element style if
possible, since the current loader already supports both for zoom bounds.

### Change City Styling

Use these methods in `CityRenderer`:

- `markerSizeForRank()`
- `markerColorForRank()`
- `textColorForRank()`
- `maxLabelsForZoom()`

Use `CityLoader` only for data discovery and parsing behavior.

### Change Grid Density

Update `GridRenderer::gridStepDegrees()`.

Remember that dense grid lines affect both line rendering and label generation.

### Change Texture Cache Size

Update `GIS::TEXTURE_CACHE_SIZE_MB` in `Constants.h`, or add a runtime setter
that calls `TextureManager::setMaxCacheSize()`.

## 22. Debugging Notes

### Useful Logs

The app logs with `qDebug()` and `qWarning()`.

Important messages include:

- Requested and actual OpenGL format.
- TMS layer display/tile zoom ranges.
- MBTiles discovery failures.
- City layer load counts.
- Shapefile load status.
- Shader load/link failures.
- Tile update skips caused by excessive tile ranges.

### Common Problems

#### Blank Window Or No Rendering

Check:

- OpenGL 3.3 support.
- `EARTHVIEW_FORCE_SOFTWARE_OPENGL=1`.
- Shader files were copied to `build/shaders`.
- Shader load errors in debug output.

#### Missing Imagery

Check:

- Selected source in the toolbar.
- `.earth` file paths and URL templates.
- Whether `tilemapresource.xml` was found.
- Network availability for URL sources.
- MBTiles SQLite driver availability.
- Zoom bands logged by `deriveDisplayZoomsFromTileResources()`.

#### Missing MBTiles

Check:

- File exists under `Data/OSM`.
- `.gitignore` excludes local MBTiles, so they may need to be added manually.
- Qt SQLite driver is deployed.
- Metadata table contains `format`, `minzoom`, and `maxzoom` where possible.

#### Missing Borders

Check:

- Shapefile path under `Data/Borders`.
- `.shp` file is present.
- Shape type is supported.
- Projection in `.prj` is recognizable or bounds heuristic is correct.

#### Missing City Labels

Check:

- NDJSON files exist under `Data/Cities/{country}`.
- Filenames include `city`, `town`, `village`, or `hamlet`.
- JSON lines have `location` and either `name` or `display_name`.
- Current zoom falls into the rank's zoom band.

## 23. Performance Considerations

- `MapWidget` repaints continuously with a zero-interval timer.
- `TmsLoader` caps tile requests at 2048 tiles per active layer per update.
- Network tile fetches are asynchronous.
- MBTiles reads are synchronous.
- Texture creation is deferred to the render thread.
- Text labels are batched into a single atlas when the label hash changes.
- Border and grid lines are batched through `LineBatchRenderer`.
- Orthographic tile rendering is more expensive because each tile is subdivided
  into a 24x24 mesh.
- City layers are loaded lazily by zoom band, but once loaded they remain in
  memory.

Potential future optimization areas:

- Asynchronous MBTiles reads.
- LRU update on texture access rather than insertion-only eviction.
- Spatial indexing for city points.
- More selective label invalidation.
- Persistent tile meshes for orthographic mode.

## 24. Threading And Object Ownership

Most code runs on the Qt GUI thread.

Ownership patterns:

- `MainWindow` owns `MapWidget` through Qt parent/central-widget ownership.
- `MapWidget` manually owns renderers and deletes them with a current context.
- `Camera` is parented to `MapWidget`.
- `TmsLoader` is parented to `MapWidget`.
- `TmsLoader` owns `QNetworkAccessManager`.
- `TmsLoader` manually owns `TextureManager`.
- Renderers are `QObject`s but are deleted manually by `MapWidget`.

OpenGL resource cleanup relies on having a current context in destructors.
`MapWidget::~MapWidget()` calls `makeCurrent()` before deleting renderers.

## 25. Coding Conventions In This Codebase

Existing style:

- C++17.
- Qt containers and types are used heavily.
- Headers use include guards and `#pragma once`.
- Class names are PascalCase.
- Member variables use `m_` prefix.
- Helper functions are often placed in anonymous namespaces in `.cpp` files.
- OpenGL vertex structs are local to renderers.
- Renderer resource setup is usually lazy and guarded by a boolean.
- Debug/status strings are plain Qt strings.

When modifying the code:

- Keep OpenGL object creation inside a valid context.
- Prefer using existing helpers in `MercatorProjection` and `ShaderUtils`.
- Keep decoded tile imagery as `QImage` until the render path creates textures.
- Avoid touching generated build files.
- Add shader files to `CMakeLists.txt` so they are copied/deployed.
- Keep data discovery paths consistent between current directory and app
  directory.

## 26. Source File Reference

### `main.cpp`

Sets global OpenGL format, handles software OpenGL environment flags, creates
`QApplication`, constructs `MainWindow`, and starts the Qt event loop.

### `MainWindow.*`

Creates the user interface and discovers map sources. Also contains helper code
for parsing `.earth` files and reading `tilemapresource.xml` metadata.

### `MapWidget.*`

Owns the render loop and all visual subsystems. Handles mouse, wheel, and
keyboard navigation, toggles visual layers, and coordinates dirty caches.

### `Camera.*`

Stores viewport, zoom, center, projection mode, and horizontal wrap state.
Converts between screen and Mercator space and computes visible tile ranges.

### `MercatorProjection.*`

Standalone projection math for Web Mercator, orthographic globe projection,
tile range calculation, and horizontal wrapping.

### `TMSLoader.*`

Maintains tile source layers, active visible tiles, pending network requests,
and image loading. It supports URL templates and MBTiles sources. It keeps
lower-resolution layers available as underlays when higher-resolution layer
coverage is partial.

### `TileRenderer.*`

Consumes active tiles from `TmsLoader`, creates textures through
`TextureManager`, and draws imagery. It sorts tiles so coarser layers render
before detailed layers.

### `TextureManager.*`

Creates, caches, evicts, and deletes OpenGL textures.

### `MbTilesReader.*`

Reads MBTiles metadata/tile data through SQLite. Supports raster tile decoding
and vector tile parsing.

### `VectorTileRenderer.*`

Loads visible vector MBTiles, caches parsed MVT geometry, and renders polygon
fills, linework, and labels through OpenGL batches.

### `BorderRenderer.*`

Loads shapefiles and converts geometry into internal Mercator coordinates.
Provides border line vertices for batched rendering.

### `GridRenderer.*`

Generates grid line vertices and grid labels based on zoom and projection mode.

### `CityLoader.*`

Discovers and lazily loads city NDJSON files grouped by rank.

### `CityRenderer.*`

Renders city markers and contributes city labels to the text renderer.

### `LineBatchRenderer.*`

Shared renderer for colored line segments in screen or Mercator coordinates.

### `TextRenderer.*`

Packs labels into a texture atlas and renders them as quads.

### `ShaderUtils.*`

Finds shader files and compiles/links shader programs.

### `Constants.h`

Contains Earth, Mercator, zoom, tile, and texture cache constants.

### `fpsCounter.h`

Small utility for computing an FPS value every half second.

## 27. Suggested Verification Checklist

After a code change, verify:

1. Project builds with `cmake --build build --parallel 4`.
2. App starts and logs actual OpenGL context.
3. Default map source appears.
4. Zooming and panning update imagery.
5. High zoom imagery retains lower-resolution underlay where detailed tiles are
   missing.
6. Borders toggle on/off.
7. Grid toggle on/off.
8. City markers and labels appear at appropriate zooms.
9. Globe view renders a nonblank globe with imagery and overlays.
10. Longitude wrapping repeats imagery and overlays.
11. Loading a shapefile through the UI still works.
12. No shader load errors appear in logs.
