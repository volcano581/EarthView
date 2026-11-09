# MBTiles loading and rendering — coding guide

There is no `MbTilesLoader.cpp` in this tree. **Loading** lives in `MbTilesReader.cpp`
(public API in `MbTilesReader.h`). HTTP range reads of the same SQLite file go through
`RemoteMbTiles.cpp`. **Rendering** splits by format:

| Format in `metadata.format` | `TileSourceLayer::SourceType` | Who fetches | Who draws |
| --- | --- | --- | --- |
| `png`, `jpg`, `jpeg`, `webp`, … | `MbTiles` | `TmsLoader::fetchMbTile()` | `TileRenderer` (textured quads) |
| `pbf`, `mvt`, `vector` | `VectorMbTiles` | `VectorTileRenderer::fetchVectorTile()` | `VectorTileRenderer` (meshes + labels) |

Raster MBTiles never enter `VectorTileRenderer`. Vector MBTiles never enter
`TmsLoader::fetchMbTile()` / `readTileImage()`. Mixing the two is an error path
(`readTileImage` refuses vector formats).

This guide is the 2D engine (`earthview_core`). osgEarth 3D reads MBTiles on its own
through `.earth` files (`<MBTilesImage>`); do not wire 2D `MbTilesReader` into
`earthview3d`.

---

## 1. Pipeline at a glance

```text
MapSourceCatalog
  discoverMbTilesSources()          Data/OSM/*.mbtiles
  readEarthLayers()                 Data/EarthFiles/*.earth  driver="mbtiles"
  urlMbTilesLayer()                 http(s) file, TileJSON, tilemapresource.xml, {z}/{x}/{y}
        |
        |  TmsLoader::TileSourceLayer
        v
MapWidget::setTileSourceLayers()
  +-- TmsLoader                 raster only (skips VectorMbTiles)
  +-- VectorTileRenderer        VectorMbTiles as BaseMap
  +-- VectorTileRenderer        VectorMbTiles as Overlay (hybrid over imagery)
        |
        |  MbTilesReader::threadPool()   (4 long-lived threads)
        v
MbTilesReader
  local path  -> QSQLITE immutable URI, one connection per (thread, file)
  http(s) URL -> RemoteMbTiles range reader (never downloads the whole file)
        |
        +-- QImage                  TmsLoader -> TileRenderer -> TextureManager
        +-- MbTilesVectorTile       VectorTileRenderer::buildTileContent()
                                      -> GPU mesh + MapTheme styling
```

Frame order in `MapWidget::paintGL()`:

1. Optional globe backdrop
2. Raster tiles (`TileRenderer`)
3. Optional DEM overlay
4. Vector base map (`VectorTileRenderer`, `Mode::BaseMap`)
5. Vector overlay (`Mode::Overlay`)
6. Borders, grid, cities, labels

---

## 2. Files to know

| File | Role |
| --- | --- |
| `MbTilesReader.h` / `.cpp` | Metadata, tile blob I/O, gzip, minimal MVT protobuf parser, shared `QThreadPool` |
| `RemoteMbTiles.h` / `.cpp` | Read-only SQLite over HTTP Range (64 KB blocks, 64 MB cache) |
| `MapSourceCatalog.cpp` | Discover sources; classify raster vs vector; parse `.earth` `driver="mbtiles"` |
| `TMSLoader.cpp` | Visible raster tiles, background `readTileImage`, retained tiles while zooming |
| `TileRenderer.cpp` | Upload `QImage` → GL texture (4 ms/frame budget), draw Mercator or globe mesh |
| `TextureManager.cpp` | Texture cache (~200 MB), RGBA8, mipmaps, anisotropy |
| `VectorTileRenderer.cpp` | Visible vector tiles, decode + mesh off-thread, stencil fills, instanced lines |
| `MapTheme.cpp` | OpenMapTiles styling (`standard` / `positron` / `dark`; overlay hybrid) |
| `mapcontrols/LayerImages.cpp` | Layer-list thumbnails (same reader pool) |
| `tests/tst_mbtilesreader.cpp` | Layouts, URIs, earth-file entries, remote range reads |
| `tests/tst_mapsourcecatalog.cpp` | Catalog / earth-file wiring |
| `tests/tst_maptheme.cpp` | Theme rules (no GL) |

---

## 3. Source discovery (how a file becomes a layer)

`MapSourceCatalog::discoverMapSources()` concatenates MBTiles files and `.earth` maps.

### 3.1 Loose `*.mbtiles` files

`discoverMbTilesSources()` scans `mbTilesSearchDirectories()` (`Data/OSM` under CWD,
beside the exe, and `../Data/OSM`). Each file is opened once with
`MbTilesReader::readMetadata()`. Large files are gitignored (`Data/OSM/*.mbtiles`).

`mbTilesLayer()` fills `TmsLoader::TileSourceLayer`:

- `sourceType` from `MbTilesReader::isVectorFormat(format)`
- `mbTilesPath` = absolute path or `http(s)` URL (same string is later passed to the reader)
- `mbTilesFormat` = metadata `format`
- `minTileZoom` / `maxTileZoom` = `minzoom` / `maxzoom`
- `tmsYOrigin` = **true unless** `scheme` is `xyz` (default scheme is `tms`)
- `vectorLayers` = ids from the metadata `json` row’s `"vector_layers"` array

### 3.2 `.earth` image entries

```xml
<image name="OSM" driver="mbtiles">
  <url> M:/maps/planet.mbtiles </url>
</image>
```

or a URL (sample: `Data/EarthFiles/osm_mbtiles.earth.sample`):

```xml
<image name="OSM" driver="mbtiles">
  <url> http://host:port/maps/planet.mbtiles </url>
</image>
```

Relative paths are resolved against the earth file’s directory. Unreadable files are
skipped with a `qWarning` (see `tst_mbtilesreader` `earthFileSkipsUnreadableMbTiles`).

`urlMbTilesLayer()` also accepts:

- `{z}/{x}/{y}` or `{-y}` templates (vector vs raster from extension or a probe GET)
- TileJSON / `tilemapresource.xml`
- A URL whose first bytes are `SQLite format 3` (MBTiles under another name)

Vector layers discovered this way with an empty `mbTilesPath` are fetched with
`readVectorTileFromUrl(urlTemplate, …)` instead of SQLite.

---

## 4. Loading: `MbTilesReader`

Public surface (call these; do not open SQLite yourself):

```cpp
MbTilesReader::threadPool();                 // 4 threads, expiry -1, shutdown wait 3 s
MbTilesReader::immutableUri(filePath);       // file:…?mode=ro&immutable=1
MbTilesReader::isVectorFormat(format);
MbTilesReader::vectorLayerIds(json);
MbTilesReader::readMetadata(location, &err);
MbTilesReader::readTileImage(location, z, x, y, tmsYOrigin, format, &err);
MbTilesReader::readVectorTile(location, z, x, y, tmsYOrigin, &err);
MbTilesReader::readVectorTileFromUrl(template, z, x, y, tmsYOrigin, &err);
MbTilesReader::decodeVectorTile(bytes, &err);  // gzip or raw protobuf
```

`location` is either a filesystem path (drive, UNC, Unix) or an `http://` / `https://`
URL. The same functions dispatch to `RemoteMbTiles` when `RemoteMbTiles::isRemoteUrl()`
is true.

### 4.1 Local SQLite

**Tile reads** (`readTileData`) reuse `threadConnection()`:

- Connection name: `earthview_mbtiles_t<threadId>_<hash(path)>`
- Open options: `QSQLITE_OPEN_READONLY;QSQLITE_OPEN_URI` with `immutableUri()`
- Fallback: plain read-only open of the native path if the URI is rejected
- Pragmas: `query_only=1`, `cache_size=-65536` (64 MB), `temp_store=MEMORY`

That is why jobs **must** run on `MbTilesReader::threadPool()`: threads never expire, so
the connection and SQLite page cache stay warm. Opening a tens-of-GB share file per tile
is exactly what this avoids.

**Metadata reads** still use a one-shot UUID connection (`openDatabase`). They run at
catalog time on the UI thread; keep them cheap.

Query used for every local tile:

```sql
SELECT tile_data FROM tiles
 WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?
 LIMIT 1
```

SQLite’s `tiles` **view** (OpenMapTiles `map`+`images`, planetiler, …) is handled by
SQLite itself for local files. The HTTP reader reimplements those layouts (see §5).

### 4.2 TMS vs XYZ row order

EarthView’s `y` is **XYZ / Google** (row 0 at the north). MBTiles traditionally store
TMS (row 0 at the south).

```cpp
const int tileRow = tmsYOrigin ? ((1 << z) - 1 - y) : y;
```

`tmsYOrigin == true` (the default) inverts before the SQL/HTTP lookup. Callers pass
camera/XYZ `y`; they do not invert themselves.

### 4.3 Raster decode

`readTileImage`:

1. Rejects vector formats.
2. Loads `tile_data` with `QImage::loadFromData` (PNG/JPEG/WebP as Qt supports).
3. Returns a null image for a missing tile **without** treating it as a hard error
   (empty blob, empty `errorMessage` unless SQLite/network failed).

`TmsLoader` converts to `QImage::Format_RGBA8888` on the worker so the UI thread only
assigns the image.

### 4.4 Vector decode (MVT)

`decodeVectorTile`:

1. Gzip if the blob starts with `1f 8b` (`inflateInit2(..., 16 + MAX_WBITS)`).
2. Walk protobuf: tile field 3 = layer.
3. Layer: name, features, keys, values, extent (default 4096).
4. Feature: id, tags, type (1 point / 2 line / 3 polygon), geometry commands.
5. Geometry: MVT `MoveTo` (1), `LineTo` (2), `ClosePath` (7), zigzag deltas.
6. Coordinates are scaled from layer extent into **256 tile pixels**
   (`kTileImageSize` / `GIS::TILE_SIZE`).

The parser is purpose-built, not a general protobuf library. Unknown fields are skipped
when the wire type is understood.

`readVectorTileFromUrl`: substitutes `{z}` `{x}` `{y}` (TMS invert when asked). HTTP
**404** and **204** are empty tiles, not errors.

### 4.5 Cancelling work: `MbTilesRequestSet`

Loaders share a thread-safe set of tile keys still wanted. Each pool job checks
`wanted->contains(key)` **before** touching the file. `TmsLoader` and
`VectorTileRenderer` bump a priority so newer pan/zoom jobs run first
(`QThreadPool::start(job, ++priority)`).

After `clearCache()` / source change, increment `m_requestGeneration` and ignore
completions whose generation no longer matches.

---

## 5. HTTP files: `RemoteMbTiles`

Used when the imagery server publishes the `.mbtiles` file over HTTP. SQLite cannot
open a URL, so this module walks the SQLite B-tree with Range requests.

Contract:

- Server should return **206** for `Range: bytes=…`. IIS / nginx / Apache static files do.
- If Range is ignored, only files **≤ 64 MB** are accepted (downloaded once).
- Thread-safe. Each reader-pool thread keeps a keep-alive `QNetworkAccessManager`.
- Shared 64 KB blocks, 64 MB cache, parsed schema per URL.

Layouts understood (see `tst_mbtilesreader::remoteReadsEveryLayout`):

1. Physical `tiles` table with an index
2. Deduplicated `map` + `images` behind a `tiles` **view** (OpenMapTiles, tippecanoe, mbutil)
3. Planetiler `tiles_shallow` + `tiles_data`

Tests stand in for the server with a local HTTP listener. Call
`RemoteMbTiles::clearCaches()` between cases.

Do not use `QNetworkAccessManager` from a worker for MBTiles **files**; go through
`RemoteMbTiles::readTile` / `httpGet`. Tile **templates** (`readVectorTileFromUrl`) use
`httpGet` for a whole tile body.

---

## 6. Raster rendering path

### 6.1 Visibility (`TmsLoader::updateVisibleTiles`)

1. Camera tile zoom → `layerIndicesForZoom()` (display band + coarser underlays).
2. Per layer: tile zoom + visible range. Skip updates that would exceed 2048 tiles.
3. Skip `VectorMbTiles` in `fetchTile()`.
4. Keys:
   - render key: `{layerIndex}_{z}_{x}_{y}`
   - texture key: X wrapped with `MercatorProjection::wrapTileX` so wrapping reuses GL textures
5. Tiles that leave the view stay in `m_retainedTiles` until every **active** tile has a
   texture, so zoom does not flash empty.

### 6.2 Fetch

`fetchMbTile` starts a reader-pool job → `readTileImage` → queued
`completeMbTileRead` on the loader’s thread → `applyTileImage` (store `QImage`,
`textureId = 0`) → `tileLoaded`.

### 6.3 Draw (`TileRenderer::render`)

- Draw **retained** tiles first, then active, coarse `tileZoomLevel` first.
- Lazy upload: `TextureManager` from RGBA8 image, **≤ 4 ms** of new uploads per frame.
- Flat mode: one screen-space quad per tile (`textured_quad`).
- Orthographic: 24×24 grid projected with `Camera::projectMercatorToScreen()`.

Missing tiles: `handleTileFailure` drops the active entry unless `fallbackLayerIndices`
is populated (the underlay path usually uses separate coarser tiles instead).

---

## 7. Vector rendering path

`TmsLoader` does not load vector tiles. `MapWidget` constructs:

- `m_vectorTileRenderer` — `Mode::BaseMap` (land, areas, casings, names)
- `m_featureRenderer` — `Mode::Overlay` (roads/labels over satellite; no area fills)

Both receive the same layer list; each ignores non-`VectorMbTiles` layers.

### 7.1 Visibility and fetch

`updateVisibleTiles()` uses camera tile zoom **minus** `kVectorTileZoomOffset` (1):
vector tiles are requested one zoom coarser than raster at the same camera zoom.

`fetchVectorTile` on the reader pool:

1. `readVectorTile` or `readVectorTileFromUrl`
2. `buildTileContent()` — style + tessellate **on the worker**
3. Queued `completeVectorTileRead` — insert `m_tileCache`, `contentChanged()` for labels

Retained keys work like raster: previous view stays until pending reads drain.

### 7.2 `buildTileContent` (CPU mesh)

- Overlay off: paint the tile square with `MapTheme::background()` (OpenMapTiles has no
  full land polygon).
- Per feature: `MapTheme::featureStyle(layer, tags, type, zoom, overlay)`.
- Polygons → winding fans + optional hairline outline (stencil-correct holes later).
- Lines → instanced thick-line records (`LineInstance`: Mercator segment, width, dash, color).
- Label candidates (places, road names) stored for `appendLabels()`.

Geometry is converted from 256-px tile space to Mercator with the tile’s
`mercatorBounds`. Long segments are subdivided for globe curvature.

### 7.3 GPU draw

- Fills: stencil increment/decrement for MVT non-zero winding, then cover quads.
  Needs the 8-bit stencil requested in `main.cpp` / `MapWidget`.
- Lines: `vector_line.vert` / `.frag`, instanced quads, pixel widths, round joins, dashes.
- Globe: draw the **same** Mercator buffers into an azimuthal-equidistant FBO, then
  `globe_composite.frag` onto the orthographic sphere (no CPU reproject while spinning).
- Themes: `setTheme()` restyles cached tiles on the reader pool without re-reading SQLite.

`pick()` hits labels, then points, lines, smallest area; pieces sharing MVT feature `id`
across tile edges are gathered.

Hidden layers: `setHiddenLayers()` (UI layer list). `seenLayers()` grows as tiles arrive
when metadata omitted `vector_layers`.

---

## 8. Threading rules (easy to get wrong)

| May run on UI thread | Must run on `MbTilesReader::threadPool()` |
| --- | --- |
| `readMetadata` at catalog time | `readTileImage`, `readVectorTile`, `readVectorTileFromUrl` |
| Completing jobs via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` | `RemoteMbTiles::readTile` / heavy `httpGet` |
| GL upload / `paintGL` | `buildTileContent` / thumbnail `LayerImages` |

- `QSqlDatabase` connections are **per thread**. Never share a connection across threads.
- Capture `QPointer<TmsLoader>` / `QPointer<VectorTileRenderer>` in lambdas; the widget
  can die while a read is in flight.
- Do not `waitForDone()` on the pool from the UI thread except process shutdown
  (`qAddPostRoutine`, 3 s).
- Convert images to RGBA8888 on the worker.

---

## 9. Convert a custom `.earth` file to a standard earth file

The 2D viewer reads **custom layer-list** files (`Data/EarthFiles/*.earth`): a bare list of
`<image>` / `<elevation>` lines, **no `<map>` root**. osgEarth will not load those. A
**standard** earth file is an osgEarth map whose first element is `<map version="3">`
(examples: `Data/maps/earthview.earth`).

Do **not** rewrite the 2D files in place. Conversion is already implemented in
`earthview3d::EarthFileCatalog` (`earthview3d/include/earthview3d/EarthFileCatalog.h`,
`earthview3d/src/EarthFileCatalog.cpp`): it splices TMS/DTED layers into a **template** at
`<!--@EARTHVIEW3D_TMS_LAYERS@-->` and writes `maps/tms_<source>.earth` (git-ignored). The
2D path (`MapSourceCatalog::readEarthLayers`) is unchanged.

```text
custom Data/EarthFiles/satellite.earth     (no <map>)
        |
        |  EarthFileCatalog::parse / parseFile
        |    looksLikeStandardEarthFile() → first element is <map>?
        |      yes  → use the file as-is (earthFile = sourcePath)
        |      no   → wrap in <earth>, keep driver=tms / driver=dted
        v
TmsLayerSpec list
        |
        |  EarthFileCatalog::generate
        |    copy Data/maps/earthview.earth
        |    replace <!--@EARTHVIEW3D_TMS_LAYERS@-->
        |    image     → <TMSImage>
        |    elevation → <TMSElevation>
        |    dted      → <GDALElevation> + dted_mosaic.vrt
        v
standard maps/tms_satellite.earth   (has <map>; osgEarth loads this)
```

### 9.1 Call the existing converter (no code change)

From a host or `earthview3d_demo`:

```cpp
QStringList warnings;
const auto maps = earthview3d::EarthFileCatalog::build(
    earthview3d::EarthFileCatalog::defaultSearchDirectories(config.dataRoot),
    config.resolvedEarthFile(),   // template: maps/earthview.earth
    QString(),                    // empty = write next to the template
    &warnings);
config.earthFile = maps.first().earthFile;   // dropdown: maps[i].label
```

Single file:

```cpp
earthview3d::MapCatalogEntry entry;
QString error;
EarthFileCatalog::parseFile(customPath, &entry, &error);
EarthFileCatalog::generate(&entry, templatePath, QString(), &error, &warnings);
// entry.earthFile is the standard map (or customPath if it already had <map>)
```

CLI check: `earthview3d_demo --list-maps`. Copy `*.earth.sample` → `*.earth` and put real
URLs in before expecting a map.

### 9.2 Element mapping (what `layersXml` emits)

| Custom (2D layer list) | `TmsLayerSpec::Kind` | Standard (osgEarth) |
| --- | --- | --- |
| `<image driver="tms">` or no driver | `Image` | `<TMSImage name="…"><url>…</url></TMSImage>` |
| `<elevation>` / `<heightfield>` (TMS) | `Elevation` | `<TMSElevation>` |
| `<elevation driver="dted"><url>folder</url>` | `DtedElevation` | `<GDALElevation>` with `dtedMosaicUrl()` and `vdatum` `egm96` |
| `<image driver="mbtiles">` | **skipped today** | would be `<MBTilesImage>` (see §9.4) |
| any other `driver=` | skipped | — |

`min_level` / `max_level` (and `minZoom` aliases) copy through. Layer names are uniquified.
`driver="mbtiles"` is valid in 2D (`MapSourceCatalog`) and **ignored** in 3D (I-038).

### 9.3 Code-change steps (extend or re-implement conversion)

Touch only `earthview3d` for 3D conversion. Do not change 2D behaviour.

1. **Decide custom vs standard**  
   Keep `looksLikeStandardEarthFile()` in `EarthFileCatalog.cpp`: the first XML start
   element is `<map>` (case-insensitive). If you add another custom root, that test is
   the gate. `generate()` must still no-op when `entry.standardEarthFile` is true
   (`generateLeavesStandardFilesAlone` in `tests/tst_earthfilecatalog.cpp`).

2. **Parse the custom file into `TmsLayerSpec` (or a new spec)**  
   In `EarthFileCatalog::parse()`:
   - Wrap the layer list in `<earth>…</earth>` (no root in the file).
   - On `<image>` / `<elevation>` / `<heightfield>`, read `driver`, `name`, zoom attrs.
   - Skip unknown drivers (`skipCurrent`); keep `isTmsDriver()` / `isDtedDriver()`.
   - On child `<url>`, store `current.url`.
   - On the end tag, append if `!skipCurrent && !current.url.isEmpty()`.
   - Fail if `tmsLayers` is empty (`"No TMS layer with a URL found"`).
   - Call `makeNamesUnique()`.

   Mirror any new field on `TmsLayerSpec` in `EarthFileCatalog.h` (Kind, url, levels,
   vdatum). Keep the header osgEarth-free.

3. **Emit osgEarth XML**  
   In `EarthFileCatalog::layersXml()` map `Kind` → element name (`TMSImage`,
   `TMSElevation`, `GDALElevation`). Escape names and URLs with `xmlEscaped()`. For DTED,
   run `dtedMosaicUrl()` (folder → `…/dted_mosaic.vrt`). Default DTED `vdatum` to `egm96`
   (D-021). Indent to match the template (default 4 spaces).

4. **Splice into the template, do not invent a map from scratch**  
   In `EarthFileCatalog::generate()`:
   - Require `layerPlaceholder()` (`<!--@EARTHVIEW3D_TMS_LAYERS@-->`) in the template.
   - Write **next to the template** unless `outputDir` is set (osgEarth resolves relative
     paths against the generated file).
   - For each `DtedElevation`, `DtedMosaic::ensureMosaic()` then put the mosaic path in
     the layer URL.
   - `replace(placeholder, provenanceComment + layersXml(layers))`.
   - Output name: `generatedFileName()` → `tms_<sanitized source base>.earth`.
   - Set `entry->earthFile` to that path.

   Keep the placeholder in `Data/maps/earthview.earth` (and `earthview_online.earth`).
   osgEarth ignores the comment, so the template stays loadable alone.

5. **Discover + generate for every file**  
   `build()` = `discover()` then `generate()` per entry; drop failures into `warnings`.
   `discover()` skips `tms_*.earth` so generated maps are not listed twice.

6. **Wire the host**  
   `earthview3d/demo/main.cpp` already calls `build()` and puts `--map` first. A Doctrine
   host should do the same: `MapConfig::earthFile = entry.earthFile` (generated path),
   never the custom layer list.

7. **Tests (required)**  
   Extend `tests/tst_earthfilecatalog.cpp`:
   - parse the new custom snippet → expected `Kind` / url / levels
   - `generateSplicesIntoTemplate`-style: output contains the new osgEarth element, keeps
     template layers (`world_base.tif`), has no leftover placeholder
   - standard `<map>` files still not rewritten
   - empty / foreign-driver files still skipped

   Run `tst_earthfilecatalog`.

### 9.4 Adding `driver="mbtiles"` (not implemented)

3D currently skips MBTiles layer-list lines. To convert them the same way as TMS:

1. Add `TmsLayerSpec::Kind::MbTilesImage` (raster) — and only emit it when
   `MbTilesReader::isVectorFormat` is false. osgEarth’s `<MBTilesImage>` is raster;
   vector MBTiles stay 2D-only unless a separate design lands.
2. In `parse()`, treat `driver` `mbtiles` like TMS (do not set `skipCurrent`). Resolve a
   relative `<url>` against `entry.sourcePath` the way `MapSourceCatalog::readEarthLayers`
   does. Leave `http(s)` URLs as-is.
3. In `layersXml()`, emit:
   ```xml
   <MBTilesImage name="OSM">
       <url>…resolved path or URL…</url>
   </MBTilesImage>
   ```
   Paths in generated files must still resolve against `maps/` (the output folder). A
   share path (`M:/maps/planet.mbtiles`) can stay absolute.
4. Do **not** call 2D `MbTilesReader` from `earthview3d` for rendering; osgEarth opens the
   file. Metadata-only classification (raster vs vector) may use Qt Sql from
   `earthview3d_core` only if you keep osgEarth out of the public headers.
5. Tests: `osm_mbtiles.earth.sample` → generated file contains `<MBTilesImage>` and the
   URL; a `format=pbf` file is skipped or warned, not emitted as `MBTilesImage`.
6. Until this ships, uncomment the `<MBTilesImage>` block already in
   `Data/maps/earthview.earth` and point it at `Data/OSM/*.mbtiles` for a one-off 3D map.

### 9.5 Do not

- Hand-edit `tms_*.earth` — regenerate from the layer list (`generate()` writes a
  “edit that file, not this one” comment).
- Put `<map>` into `Data/EarthFiles` layer lists just to “make them standard”; 2D still
  wraps them in `<earth>` and would then see a nested map. Drop a real osgEarth file in
  `Data/maps/` instead (`standardEarthFile` stays true).
- Change `MapSourceCatalog` / `MbTilesReader` as part of this conversion. Custom files
  remain the 2D source of truth.

---

## 10. How to extend (recipes)

**Add a raster MBTiles source in code**

```cpp
TmsLoader::TileSourceLayer layer;
if (!MapSourceCatalog::mbTilesLayer(path, &layer, &error))
    return;
loader->setTileSourceLayers({ layer });
```

Prefer catalog discovery so `.earth` and `Data/OSM` stay consistent.

**Add a vector overlay on top of satellite**

Keep raster layers on `TmsLoader`. Put `VectorMbTiles` layers on
`m_featureRenderer` (`Mode::Overlay`). That is how hybrid maps work.

**Support another vector schema**

Do not grow `MbTilesReader` unless the **protobuf** shape changes. Styling belongs in
`MapTheme` (`featureStyle`, label layers). Add tests in `tst_maptheme.cpp`.

**Support another remote SQLite layout**

Extend schema detection in `RemoteMbTiles.cpp` (`tiles` table vs `tiles` view join).
Add a fixture layout in `tst_mbtilesreader.cpp`. Local SQLite already accepts any view
named `tiles`.

**Read tiles from your own code**

```cpp
MbTilesReader::threadPool()->start([path, z, x, y, tms]() {
    QString err;
    const QImage img = MbTilesReader::readTileImage(path, z, x, y, tms, QStringLiteral("png"), &err);
    // marshal back with QMetaObject::invokeMethod
});
```

---

## 11. Tests and verification

```text
ctest --test-dir <build> -R "tst_mbtilesreader|tst_mapsourcecatalog|tst_maptheme|tst_earthfilecatalog" --output-on-failure
```

`tst_mbtilesreader` covers:

- Immutable URIs for UNC, drive, Unix, and escaped characters
- Missing vs present raster tiles
- Remote range reads of every supported layout; fetch-only-needed pages
- Jobs on `threadPool()`
- Server without Range (small file only)
- Earth-file `driver="mbtiles"` path and URL entries
- TileJSON / `tilemapresource.xml` / `{z}/{x}/{-y}` templates

Visual checks (Windows):

1. Drop a raster `*.mbtiles` in `Data/OSM` — it appears as “Offline Raster MBTiles”.
2. Vector OpenMapTiles file — “Offline Vector MBTiles”; themes standard / positron / dark.
3. Pan/zoom: no empty flash (retained tiles); underlays remain where high-zoom tiles miss.
4. Globe: vector areas that cross the horizon still fill (AE composite path).
5. `.earth` `driver="mbtiles"` with a share path and with `http://…/file.mbtiles`.

---

## 12. Pitfalls

- **Wrong Y origin** — upside-down map. Default is TMS; only `scheme=xyz` disables invert.
- **Calling `readTileImage` on a vector file** — explicit error; use `readVectorTile`.
- **Opening SQLite on the UI thread per tile** — UI stalls and re-opens huge files.
- **New `QThreadPool` instead of `MbTilesReader::threadPool()`** — cold connections,
  extra HTTP managers, shutdown races with Qt.
- **Assuming a physical `tiles` table on HTTP** — many production files use a view;
  `RemoteMbTiles` must parse it. Local Qt SQL does not care.
- **Range-unaware HTTP server for a multi-GB file** — open fails; serve static files
  with Range or copy locally.
- **Vector zoom mismatch** — renderer asks for `cameraTileZoom - 1`. Changing the
  offset without restyling `MapTheme` widths will look wrong.
- **No stencil buffer** — polygon holes and overlapping lakes fill incorrectly.
- **3D path** — do not port this reader into osgEarth. Convert a custom layer list with
  `EarthFileCatalog` (§9) or uncomment `<MBTilesImage>` in `maps/earthview.earth`.
- **Feeding osgEarth a `Data/EarthFiles` layer list** — no `<map>` root; it will fail.
  Use `entry.earthFile` after `generate()` / `build()`.

---

## 13. Data structures (cheat sheet)

```cpp
struct MbTilesMetadata {
    bool valid;
    QString name, format, scheme;  // scheme default "tms"
    int minZoom, maxZoom;
    QStringList vectorLayers;
};

struct MbTilesVectorFeature {
    quint64 id;                    // 0 = none; shared across tile edges
    int type;                      // 1 point, 2 line, 3 polygon
    QHash<QString, QVariant> tags;
    QVector<QVector<QPointF>> paths;  // 256-px tile space
};

struct TmsLoader::TileSourceLayer {
    SourceType sourceType;         // UrlTemplate | MbTiles | VectorMbTiles
    QString mbTilesPath;           // path or http(s) to the .mbtiles file
    QString urlTemplate;           // {z}/{x}/{y} when not a file
    QString mbTilesFormat;
    bool tmsYOrigin;
    int minZoom, maxZoom;          // display band
    int minTileZoom, maxTileZoom;  // resources in the file
    QStringList vectorLayers;
};
```
