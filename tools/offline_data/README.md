# Offline map data (roadmap E6)

`build_offline_data.py` turns local source data into the layers that
`Data/maps/earthview.earth` (the offline default map) reads. Nothing needs a network.

Requirements: Python 3.8+ and the GDAL command-line tools on `PATH`
(`gdal_translate`, `gdaldem`, `gdaladdo`, `gdal_rasterize`, `gdal_create`, `ogr2ogr`) —
OSGeo4W/QGIS on Windows, `gdal-bin` on Linux, or vcpkg `gdal[tools]`.

| Step | Input | Output (under `EarthView/Data`) |
| ---- | ----- | ------------------------------- |
| `world-base` | `Borders/…/World_Countries_Generalized.shp` | `imagery/world_base.tif` (in git; 0.05°, land/sea) |
| `dem-overviews` | `DEM/dted_1arc.vrt` + `.dt2` tiles | `DEM/dted_1arc.vrt.ovr` |
| `hillshade` | `DEM/dted_1arc.vrt` | `imagery/hillshade.tif` (3″ default, `--hillshade-res`) |
| `vector-base` | OpenMapTiles vector `.mbtiles` (`--mbtiles`, `--aoi`, `--zoom`) | `imagery/base.tif` (~20 m, `--base-res`) |
| `places` | `Cities/<cc>/place*.ndjson` | `Cities/places_major.gpkg`, `places_minor.gpkg` |
| `all` | everything above (`vector-base` only with `--mbtiles`) | |

```
python EarthView/tools/offline_data/build_offline_data.py all ^
    --mbtiles D:/Data/osm-2020-02-10-v3.11_asia_pakistan.mbtiles --aoi 70.5,32.5,75.5,35.5
```

Notes:
- `vector-base` reads one zoom level (`-oo ZOOM_LEVEL`) inside the AOI only; the runtime
  OGR/MapBoxGL approaches failed on the whole Pakistan file (I-007, D-023). A larger AOI or
  finer `--base-res` grows the output quickly (5°×3° at 0.0002° ≈ 25000×15000 px).
- Colours follow the OpenMapTiles v3 schema (`OMT_STYLE` in the script); edit there.
- Generated files except `world_base.tif` are git-ignored (rule 7).
- Tests: `test_build_offline_data.py` (run by `ctest`; GDAL cases skip without the tools).
