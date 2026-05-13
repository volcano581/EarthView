# EarthView Rendering Pipeline

This document describes the technical rendering pipeline used by EarthView. It
focuses on how the application turns map sources, vector geometry, city data,
borders, grids, and overlays into OpenGL draw calls, and it calls out the
benefits and tradeoffs of each major rendering feature.

The current renderer is a Qt 6 desktop renderer built around `QOpenGLWidget`
and an OpenGL 3.3 core profile. The central coordinator is `MapWidget`, while
specialized renderer classes own source-specific batching, shader programs,
and GPU resources.

## High-Level Architecture

The frame pipeline is coordinated by `MapWidget::paintGL()`:

```text
QOpenGLWidget paint event
  -> clear color/depth/stencil buffers
  -> optional orthographic globe backdrop
  -> raster tile renderer
  -> vector MBTiles renderer
  -> shared border/grid line batch renderer
  -> city marker renderer
  -> map label rebuild if dirty
  -> scale bar and FPS overlays
  -> text atlas renderer
```

The major rendering participants are:


| Component            | Responsibility                                                                |
| -------------------- | ----------------------------------------------------------------------------- |
| `MapWidget`          | OpenGL lifecycle, frame order, input, dirty flags, overlays.                  |
| `Camera`             | Zoom, pan, Mercator projection, orthographic projection, visible tile ranges. |
| `TmsLoader`          | Raster URL/MBTiles tile discovery, network loading, active tile cache.        |
| `TileRenderer`       | Draws raster tile textures.                                                   |
| `TextureManager`     | OpenGL texture creation, mipmaps, cache eviction.                             |
| `MbTilesReader`      | SQLite MBTiles reads and minimal MVT protobuf decoding.                       |
| `VectorTileRenderer` | Vector MBTiles fill, line, label, and base land rendering.                    |
| `LineBatchRenderer`  | Shared colored-line rendering for borders, grids, and vector linework.        |
| `BorderRenderer`     | Shapefile parsing and border line generation.                                 |
| `GridRenderer`       | Latitude/longitude grid line and label generation.                            |
| `CityRenderer`       | Settlement marker and label generation.                                       |
| `TextRenderer`       | Label atlas generation and textured quad rendering.                           |


## OpenGL Context And Surface Format

`main.cpp` and `MapWidget` request:

```text
OpenGL profile: 3.3 core
Depth buffer:   24 bits
Stencil buffer: 8 bits
Swap interval:  0
```

The stencil buffer is required for correct vector polygon fills. Vector tiles
use Mapbox Vector Tile ring winding. A center-fan triangulation breaks concave
polygons and holes; the current implementation uses front/back stencil
increment/decrement to preserve non-zero winding behavior.

Benefits:

- OpenGL 3.3 core keeps the renderer widely supported.
- The stencil buffer gives robust polygon fill behavior without pulling in a
tessellation dependency.
- A consistent `QSurfaceFormat` prevents accidental fallback to a surface that
cannot support vector fill correctness.

Costs and risks:

- Stencil use adds extra passes for polygon fills.
- Some systems may provide a lower-quality or software OpenGL implementation.
- `swapInterval(0)` favors responsiveness/FPS but can increase GPU usage.

## Coordinate Systems

EarthView uses several coordinate spaces:


| Space             | Description                                                             |
| ----------------- | ----------------------------------------------------------------------- |
| Geographic        | Latitude/longitude degrees, mostly used by input data and labels.       |
| Internal Mercator | Radian-based Web Mercator-like coordinates. X wraps around `[-pi, pi]`. |
| Tile pixel        | Vector tile coordinates scaled to `0..256`.                             |
| Screen            | Pixel coordinates in the current viewport.                              |
| NDC               | Normalized device coordinates produced by shaders.                      |


`Camera` is the authority for projection:

- Flat mode uses `MercatorProjection::mercatorToScreen()`.
- Globe mode uses orthographic projection through
`MercatorProjection::orthographicMercatorToScreen()`.
- `Camera::projectMercatorToScreen()` returns false for points outside the
visible hemisphere in orthographic mode.

Benefits:

- Renderers can share one projection abstraction.
- Flat mode can keep many vertices in Mercator space and let shaders transform
them every frame.
- Orthographic mode can reject hidden geometry before upload.

Costs and risks:

- Globe mode needs CPU projection for many paths because visibility can change
per vertex and per segment.
- The internal Mercator Y constants in `Constants.h` are narrower than full
Web Mercator, so data outside expected map bounds needs care.
- Horizontal wrapping affects X, tile selection, and label placement.

## Frame Lifecycle

### Initialization

`MapWidget::initializeGL()` creates the renderer objects after the OpenGL
context is current:

- `TileRenderer`
- `BorderRenderer`
- `GridRenderer`
- `CityRenderer`
- `VectorTileRenderer`
- `LineBatchRenderer`
- `TextRenderer`

Most renderers lazily initialize their VAOs, VBOs, textures, and shader
programs the first time they render. This avoids creating OpenGL resources
before a valid context exists.

### Per-Frame Work

`MapWidget::paintGL()` performs:

1. Clear color, depth, and stencil buffers.
2. Update FPS counter.
3. Draw the orthographic globe backdrop when enabled.
4. Render raster imagery tiles.
5. Render vector MBTiles.
6. Render border and grid lines through a shared line batch.
7. Render city markers.
8. Rebuild cached labels when dirty.
9. Draw scale bar geometry.
10. Render all text labels through one text atlas.

Benefits:

- Draw order is simple and predictable.
- Text is always last, so labels remain readable.
- Dirty flags avoid rebuilding expensive line and text batches every frame.

Costs and risks:

- Some individual renderers still use dynamic uploads during rendering.
- OpenGL state is shared, so each renderer must restore the important state it
changes.

## Raster Tile Pipeline

Raster sources are represented by `TmsLoader::TileSourceLayer`. Layers can be:

- URL template tiles.
- Raster MBTiles.
- Vector MBTiles.

`TmsLoader` only handles URL and raster MBTiles imagery. Vector MBTiles are
skipped here and handled by `VectorTileRenderer`.

### Tile Selection

`TmsLoader::updateVisibleTiles()`:

1. Reads the camera tile zoom: `ceil(camera zoom)`.
2. Selects active source layers for that zoom.
3. Computes the tile range from `Camera::getTileRange()`.
4. Rejects excessive updates over 2048 requested tiles.
5. Adds missing active tiles.
6. Starts URL or MBTiles fetches.
7. Removes no-longer-visible tiles.
8. Aborts network requests for tiles that left the view.

The loader keeps two key types:


| Key         | Purpose                                                              |
| ----------- | -------------------------------------------------------------------- |
| Render key  | Includes original X so wrapped world copies can be drawn separately. |
| Texture key | Wraps X so repeated world copies can share one texture.              |


Benefits:

- Texture reuse works across horizontal world wrapping.
- Large camera jumps cancel obsolete network requests.
- Layer underlay behavior lets lower-resolution imagery remain visible while
detailed tiles load.

Costs and risks:

- Raster MBTiles reads are synchronous.
- URL requests depend on network availability and response latency.
- Excessive tile ranges are skipped rather than progressively degraded.

### Texture Creation

`TextureManager::createTexture()`:

- Converts `QImage` to `RGBA8888`.
- Uploads with `glTexImage2D()`.
- Uses `GL_LINEAR_MIPMAP_LINEAR` minification.
- Uses `GL_LINEAR` magnification.
- Uses `GL_CLAMP_TO_EDGE`.
- Generates mipmaps.
- Enables anisotropic filtering when `GL_EXT_texture_filter_anisotropic`
exists.
- Evicts older textures when the cache limit is exceeded.

Benefits:

- Mipmaps reduce shimmer and improve zoomed-out quality.
- Anisotropic filtering improves oblique/globe appearance on supporting GPUs.
- Cache size limits bound GPU memory use.

Costs and risks:

- Mipmap generation has upload cost.
- Cache eviction is insertion-order based, not true access-order LRU.
- Texture uploads occur on the render thread.

### Raster Drawing

`TileRenderer::render()`:

- Sorts active tiles by tile zoom and layer index.
- Lazily creates textures from loaded images.
- Draws each raster tile as one textured quad in flat mode.
- Subdivides each tile into a 24x24 mesh in orthographic mode.

Flat mode passes screen-space tile vertices to `textured_quad.vert`.
Orthographic mode projects each subdivision corner through the camera and skips
cells that fall behind the globe.

Benefits:

- Flat mode is simple and cheap.
- Globe mode curves raster tiles onto the orthographic surface.
- Higher-resolution layers draw after lower-resolution layers.

Costs and risks:

- Raster tile rendering still issues one draw call per visible tile.
- Globe mode creates CPU-side subdivision geometry per tile per frame.
- Texture state changes can dominate when many raster tiles are visible.

## Vector MBTiles Pipeline

Vector MBTiles are loaded and rendered separately from raster tiles.

### MBTiles And MVT Decoding

`MbTilesReader`:

- Opens SQLite MBTiles files using a unique Qt SQL connection per operation.
- Reads `metadata` to discover format and zoom range.
- Reads `tile_data` from the `tiles` table.
- Applies TMS Y inversion when required.
- Decompresses gzip payloads.
- Parses a small subset of protobuf sufficient for MVT layers, features, tags,
values, and geometry commands.
- Decodes geometry commands:
  - `MoveTo`
  - `LineTo`
  - `ClosePath`
- Converts MVT extent coordinates into 256 tile pixel units.

Benefits:

- No external vector tile parser is required.
- Tags are preserved as `QVariant` values for styling and labels.
- Geometry remains vector data and can scale cleanly.

Costs and risks:

- The parser is intentionally minimal, not a full protobuf framework.
- Reads are synchronous.
- Styling is handcrafted and does not implement full MapLibre style JSON.

### Vector Tile Cache

`VectorTileRenderer::updateVisibleTiles()` mirrors raster selection:

- Selects vector MBTiles layers active for the camera zoom.
- Computes tile zoom and visible tile range.
- Reads missing vector tiles from MBTiles.
- Stores parsed tiles in `m_tileCache`.
- Maintains `m_visibleKeys`.
- Marks batches dirty when visibility or tile content changes.

Benefits:

- Vector data is parsed once per visible tile.
- Rebuilds happen only when tile visibility or projection inputs change.

Costs and risks:

- There is no background worker thread for vector decode.
- Large tile changes can still cause a visible CPU spike.

### Base Land Pass

OpenMapTiles-style data often does not include a full land polygon. MapLibre
styles normally paint land with a background layer, then draw water and thematic
features on top.

EarthView follows that model:

- In flat Mercator mode, one instanced quad is drawn per visible vector tile.
- In globe mode, each visible tile background is subdivided and projected on
the CPU, then drawn as normal triangles.

Flat mode uses:

- `instanced_rect_mercator.vert`
- `glDrawArraysInstanced()`
- A static six-vertex unit quad.
- Per-instance rectangle and color attributes.

Benefits:

- Correctly fills country interiors that have no explicit land polygon.
- Flat mode reduces many tile background quads to one instanced draw call.
- Keeps background drawing independent of feature density.

Costs and risks:

- It is a simple background color, not a complete style engine.
- Globe mode cannot use the same simple instancing because projection and
visibility are non-linear over each tile.

### Polygon Fill Correctness

MVT polygon paths rely on ring winding to distinguish filled areas from holes.
The renderer builds "winding edge fans" and uses the stencil buffer:

```text
for each style/color fill batch:
  clear stencil
  disable color writes
  draw winding triangles
    front faces increment stencil
    back faces decrement stencil
  enable color writes
  draw a screen-cover quad wherever stencil != 0
```

This implements a non-zero winding fill rule.

Benefits:

- Concave polygons fill correctly.
- Holes cancel correctly.
- Broken center-fan artifacts are avoided.
- No third-party tessellation library is required.

Costs and risks:

- Requires an 8-bit stencil buffer.
- Requires at least two draw operations per fill style/color group.
- Stencil clears add cost.
- Correctness depends on preserving original ring order.

### Vector Fill Batching

`VectorTileRenderer::rebuildBatches()`:

1. Builds background instances or background projected vertices.
2. Converts feature tile pixels to Mercator.
3. Groups polygon fills by draw order and color.
4. Sorts fill groups by draw order.
5. Packs winding triangles and cover quads into one upload vector.
6. Uploads the packed fill buffer only when dirty.

Draw order is simplified:


| Order | Layer types              |
| ----- | ------------------------ |
| 10    | landcover, landuse, park |
| 15    | aeroway                  |
| 20    | water                    |
| 25    | fallback                 |
| 30    | building                 |


Benefits:

- Fewer VBO uploads.
- Fewer style/color switches.
- Background is instanced in flat mode.
- Stencil correctness is retained.

Costs and risks:

- Grouping by color can slightly simplify feature order compared with a full
MapLibre layer stack.
- Cover quads are screen-sized, so very many fill groups still cost fill-rate.
- Feature-level ordering within a layer is not fully style-spec compliant.

### Vector Linework

Line features and polygon outlines are appended to `m_lineVertices` and passed
to `LineBatchRenderer`.

Styled line sources include:

- `transportation`
- `transportation_name`
- `waterway`
- `boundary`
- `building` outlines at high zoom
- generic type-2 features

Benefits:

- Shared line renderer avoids one draw call per feature.
- Lines can be drawn in Mercator shader space in flat mode.
- Orthographic mode preprojects visible line segments.

Costs and risks:

- OpenGL core line rendering has platform-dependent width behavior.
- Lines are not joined or capped like a cartographic renderer.
- No dash patterns or casing layers are currently implemented.

### Vector Labels

`VectorTileRenderer::appendLabels()`:

- Selects labels from layers such as `place`, `transportation_name`, and `poi`.
- Chooses text from common name/ref keys.
- Computes an anchor from the feature path average.
- Projects the anchor to screen.
- Performs simple rectangle collision rejection.
- Caps label count by zoom.
- Appends labels into the shared `TextRenderer` list.

Benefits:

- Reuses the same text atlas as grid, city, FPS, and scale labels.
- Keeps vector labels visually integrated with the rest of the UI.

Costs and risks:

- Labels are point-anchored, not line-following.
- Collision is simple rectangle collision in insertion order.
- There is no full symbol placement engine.

## Shared Line Rendering

`LineBatchRenderer` renders colored line vertices in either:

- Screen coordinate mode.
- Mercator coordinate mode.

Each vertex contains:

```text
vec2 position
vec4 color
```

Mercator mode uses `colored_mercator.vert`, which receives:

- `u_viewportSize`
- `u_centerMercator`
- `u_pixelsPerMercator`

Screen mode uses `colored_2d.vert`.

Benefits:

- Borders, grids, and vector linework can share one renderer.
- Flat Mercator mode can avoid CPU reprojection on every frame.
- Color is per vertex, so mixed line colors can share one buffer.

Costs and risks:

- All lines in a batch share one OpenGL line width.
- Wide line rendering is implementation-dependent.
- Advanced cartographic line joins are not supported.

## Border Rendering

`BorderRenderer` loads ESRI shapefiles and stores border parts in Mercator
coordinates. In the current main frame path, `MapWidget` asks it to append
lines into the shared line batch.

Flat mode:

- Uses Mercator vertices.
- Adds world-copy offsets when horizontal wrapping is enabled.
- Lets the line shader transform vertices.

Orthographic mode:

- Projects points to screen.
- Splits segments when visibility changes.

Benefits:

- Shapefile parsing is local and dependency-light.
- Static border geometry can be reused.
- Shared batching reduces draw calls.

Costs and risks:

- It treats shapefile polygon parts as linework, not filled countries.
- Orthographic visibility splitting is approximate at segment granularity.

## Grid Rendering

`GridRenderer` generates latitude/longitude lines and labels.

Grid step by zoom:


| Zoom   | Step       |
| ------ | ---------- |
| `< 2`  | 45 degrees |
| `< 4`  | 30 degrees |
| `< 6`  | 15 degrees |
| `< 8`  | 5 degrees  |
| `>= 8` | 1 degree   |


Flat mode appends straight Mercator latitude/longitude segments. Orthographic
mode samples lines at one-degree intervals and only appends visible segments.

Benefits:

- Grid density tracks zoom.
- Labels appear near the current camera center.
- Horizontal wrap is supported.

Costs and risks:

- Orthographic lines are sampled polylines, not analytic curves.
- Dense grid at high zoom increases line vertex count.

## City Marker And Label Rendering

`CityRenderer` uses `CityLoader` to lazily load city/settlement NDJSON layers by
rank and zoom.

Marker rendering:

- Projects visible points to screen.
- Uses `GL_POINTS` with `GL_PROGRAM_POINT_SIZE`.
- Colors and sizes markers by place rank.

Label generation:

- Uses per-zoom label caps.
- Uses simple rectangle collision.
- Emits labels into `TextRenderer`.

Benefits:

- Marker draw call is compact.
- City data is loaded lazily by zoom-relevant layer.
- Label collision keeps the map readable.

Costs and risks:

- Markers are point sprites, not complex symbols.
- Collision is local to city labels and insertion order.
- Very dense city layers still require CPU filtering each frame.

## Text Rendering

`TextRenderer` receives all labels for the frame:

- Grid labels.
- Vector labels.
- City labels.
- FPS overlay.
- Scale bar text.

It builds a texture atlas with `QPainter`, packs label rectangles row by row,
uploads the atlas to one OpenGL texture, builds textured quads, and renders all
labels with one draw call.

The renderer hashes label content and layout. If the hash matches the previous
frame, the atlas and vertex buffer are reused.

Benefits:

- One texture and one draw call for all text labels.
- Qt text rendering gives good Unicode and font support.
- Hashing avoids unnecessary atlas rebuilds.

Costs and risks:

- Atlas rebuilds are CPU-heavy.
- Row packing is simple and can waste atlas space.
- Text is rasterized at screen size, so zoom/camera changes can trigger new
label positions and atlas uploads.

## Overlays

`MapWidget` draws two main overlays:

- FPS text.
- Scale bar background, ticks, and label.

The scale bar computes a "nice" distance based on camera resolution and target
pixel width. Geometry is drawn with `solid_2d.vert` and `solid_2d.frag`, while
the label is routed through `TextRenderer`.

Benefits:

- Overlay rendering is independent of map data.
- Text and shapes share the same OpenGL frame.

Costs and risks:

- The scale bar is flat-screen logic and does not account for varying scale
across the orthographic globe.

## Shader Programs


| Shader                                               | Use                                            |
| ---------------------------------------------------- | ---------------------------------------------- |
| `textured_quad.vert` / `textured_quad.frag`          | Raster tiles and text atlas quads.             |
| `colored_2d.vert` / `colored_line.frag`              | Screen-space colored lines and fills.          |
| `colored_mercator.vert` / `colored_line.frag`        | Mercator-space colored lines and vector fills. |
| `instanced_rect_mercator.vert` / `colored_line.frag` | Instanced flat-mode vector land background.    |
| `solid_2d.vert` / `solid_2d.frag`                    | Simple overlay and globe backdrop shapes.      |
| `point_marker.vert` / `point_marker.frag`            | City markers.                                  |
| `mercator_line.vert` / `solid_2d.frag`               | Legacy/direct border line path.                |


Benefits:

- Small specialized shaders keep data formats simple.
- Shader-based Mercator transforms reduce CPU work in flat mode.
- Instancing eliminates CPU-expanded background tile quads in flat mode.

Costs and risks:

- Several renderers own similar VAO/VBO setup code.
- Shader uniform naming must stay consistent with renderer code.

## Dirty Flags And Caching

`MapWidget` has:

- `m_lineBatchDirty`
- `m_mapLabelsDirty`

`VectorTileRenderer` has:

- tile cache visibility keys
- `m_batchesDirty`
- `m_fillBufferDirty`
- `m_backgroundInstanceBufferDirty`
- cached batch camera state for orthographic mode

`TextRenderer` has:

- label hash
- cached texture atlas
- cached vertex count

Benefits:

- Avoids rebuilding stable CPU batches.
- Avoids repeated GPU uploads when tile visibility is unchanged.
- Keeps camera movement cheap in flat Mercator mode because shaders can update
positions from uniforms.

Costs and risks:

- Cache invalidation is spread across multiple classes.
- Missing an invalidation can create stale visual output.
- Over-invalidating can still cause performance spikes.

## Render Order And Visual Layering

The current order is:

```text
clear
globe backdrop
raster imagery
vector land background
vector fills
vector linework
borders and grid
city markers
scale bar geometry
labels
```

Important layering choices:

- Raster imagery is below vector overlays.
- Vector base land is below water, landuse, and buildings.
- Text is last.
- Borders and grid draw after vector tiles, so administrative context remains
visible.

Benefits:

- User-facing information stays legible.
- Vector MBTiles can work as a map source even without raster imagery.

Costs and risks:

- This is a simplified map style, not a faithful MapLibre style stack.
- Some layers may appear above or below where a full cartographic style would
place them.

## Performance Characteristics

Fast paths:

- Flat Mercator line rendering uses shader transforms.
- Flat vector land background uses instancing.
- Vector fill buffers upload only when dirty.
- Text atlas uploads only when label hash changes.
- Raster textures are cached with mipmaps.

Costly paths:

- Raster tiles still draw one tile at a time.
- Vector polygon stencil fill uses multiple passes per style/color group.
- Globe mode projects many vertices on the CPU.
- MBTiles tile reads are synchronous.
- Label collision and city filtering are CPU-side.

Useful future optimization targets:

- Persistent mapped buffers or orphaning strategy for dynamic VBOs.
- Multi-draw indirect for vector fill groups.
- Worker-thread vector tile decode.
- Raster tile draw batching by texture array or atlas.
- Better line rendering with triangle strips for joins, caps, and consistent
width.
- More complete style-layer ordering and MapLibre-style paint properties.

## Correctness Notes

Vector polygon fill correctness depends on:

- Keeping MVT path vertex order intact.
- Closing rings when the MVT command stream emits `ClosePath`.
- Avoiding center-fan triangulation for arbitrary polygons.
- Requesting and clearing the stencil buffer.
- Drawing winding triangles with front/back increment/decrement operations.

Country interiors in OpenMapTiles are generally not country polygon fills. They
are produced by the style background. EarthView therefore paints a base land
tile background and layers water, landcover, landuse, aeroways, buildings, and
linework over it.

## Debugging Checklist

When a layer fails to render:

1. Confirm the source is discovered and classified correctly in `MainWindow`.
2. Check `TmsLoader::TileSourceLayer::sourceType`.
3. Confirm `Camera::getTileRange()` is not returning an excessive range.
4. Confirm raster tiles have a texture ID or pending image.
5. Confirm vector tiles decode to non-empty layers.
6. Confirm the stencil buffer exists for polygon fills.
7. Check shader load messages from `ShaderUtils`.
8. Check dirty flags if stale geometry appears.
9. Check OpenGL state interactions if later passes disappear.

When performance drops:

1. Check visible tile count.
2. Check whether vector batches rebuild every frame.
3. Check text atlas hash churn.
4. Check MBTiles synchronous read volume.
5. Check globe mode subdivision and CPU projection cost.
6. Check draw-call count from raster tiles and stencil fill groups.

