#!/usr/bin/env python3
"""Build EarthView's offline map data for the 3D view (roadmap E6).

Everything here runs without a network. It needs Python 3.8+ and the GDAL command-line
tools on PATH (gdal_translate, gdaldem, gdaladdo, gdal_rasterize, gdal_create, ogr2ogr):
OSGeo4W / QGIS on Windows, `gdal-bin` on Linux, or vcpkg `gdal[tools]`.

Outputs go into the data tree next to the earth file (EarthView/Data), so the relative
paths in Data/maps/earthview.earth find them:

  imagery/world_base.tif   land/sea base for the whole globe (from the borders shapefile)
  imagery/hillshade.tif    shaded relief from the DTED mosaic (modulates the imagery)
  imagery/base.tif         detailed base map rasterised from an OpenMapTiles vector MBTiles
  Cities/places_major.gpkg cities and towns (labels)
  Cities/places_minor.gpkg villages and hamlets (labels, close range only)
  DEM/dted_1arc.vrt.ovr    DTED overviews (faster zoomed-out terrain)

Usage (from the repository root):
  python EarthView/tools/offline_data/build_offline_data.py all \
      --mbtiles D:/Data/osm-2020-02-10-v3.11_asia_pakistan.mbtiles --aoi 70.5,32.5,75.5,35.5
  python EarthView/tools/offline_data/build_offline_data.py places
Run with --help for every step and option.
"""

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DATA = os.path.normpath(os.path.join(HERE, "..", "..", "Data"))

# Background and land/sea colours (light, close to common topographic maps).
SEA_RGB = (170, 196, 220)
LAND_RGB = (236, 233, 222)

# OpenMapTiles v3 schema: (layer, SQL where clause or None, RGB). Drawn in this order, so
# later entries paint over earlier ones (roads over landuse over landcover).
OMT_STYLE = [
    ("landcover", "class IN ('farmland')", (238, 240, 213)),
    ("landcover", "class IN ('grass')", (216, 232, 192)),
    ("landcover", "class IN ('wood')", (198, 220, 170)),
    ("landcover", "class IN ('wetland')", (212, 230, 217)),
    ("landcover", "class IN ('sand')", (242, 232, 196)),
    ("landcover", "class IN ('ice')", (250, 252, 255)),
    ("landuse", "class IN ('residential','suburb','neighbourhood')", (228, 222, 214)),
    ("landuse", "class IN ('industrial','commercial','retail','railway')", (224, 214, 228)),
    ("landuse", "class IN ('military')", (232, 206, 206)),
    ("park", None, (204, 228, 190)),
    ("water", None, SEA_RGB),
    ("waterway", None, (150, 185, 220)),
    ("building", None, (214, 205, 194)),
    ("transportation", "class IN ('minor','service','track','path')", (255, 255, 255)),
    ("transportation", "class IN ('tertiary','secondary','primary')", (246, 214, 140)),
    ("transportation", "class IN ('trunk','motorway')", (236, 160, 90)),
    ("transportation", "class IN ('rail')", (150, 150, 150)),
    ("aeroway", None, (210, 210, 220)),
]

PLACE_RANKS = {"place_city": 1, "place-town": 2, "place-village": 3, "place-hamlet": 4}


class ToolError(RuntimeError):
    pass


def run(args, quiet=False):
    """Run a GDAL tool; raise ToolError with its output on failure."""
    exe = shutil.which(args[0])
    if not exe:
        raise ToolError(f"'{args[0]}' not found on PATH. Install the GDAL command-line tools "
                        "(OSGeo4W/QGIS on Windows, gdal-bin on Linux, or vcpkg gdal[tools]).")
    if not quiet:
        print("  $ " + " ".join(str(a) for a in args), flush=True)
    proc = subprocess.run([exe] + [str(a) for a in args[1:]], capture_output=True, text=True)
    if proc.returncode != 0:
        raise ToolError(f"{args[0]} failed ({proc.returncode}):\n{proc.stdout}\n{proc.stderr}")
    return proc.stdout


def parse_aoi(text):
    """'minLon,minLat,maxLon,maxLat' -> tuple of floats, validated."""
    parts = [float(p) for p in text.split(",")]
    if len(parts) != 4:
        raise ValueError("AOI must be minLon,minLat,maxLon,maxLat")
    w, s, e, n = parts
    if not (-180 <= w < e <= 180 and -90 <= s < n <= 90):
        raise ValueError(f"AOI out of range or empty: {text}")
    return w, s, e, n


def tiff_options():
    return ["-co", "TILED=YES", "-co", "COMPRESS=DEFLATE", "-co", "PREDICTOR=2", "-co", "BIGTIFF=IF_SAFER"]


def add_overviews(path, levels=(2, 4, 8, 16, 32, 64, 128), resampling="average", read_only=False):
    args = ["gdaladdo", "-r", resampling, "--config", "COMPRESS_OVERVIEW", "DEFLATE"]
    if read_only:
        args.append("-ro")
    run(args + [path] + [str(l) for l in levels])


# ── steps ────────────────────────────────────────────────────────────────────

def world_base(data, res_deg=0.05):
    """Land/sea raster of the whole globe from the country borders shapefile."""
    shp = os.path.join(data, "Borders", "World_Countries_Generalized_Shapefile", "World_Countries_Generalized.shp")
    out = os.path.join(data, "imagery", "world_base.tif")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    if os.path.exists(out):
        os.remove(out)
    with tempfile.TemporaryDirectory() as tmp:
        # The shapefile is Web Mercator; gdal_rasterize does not reproject, so convert first.
        geo = os.path.join(tmp, "borders.gpkg")
        run(["ogr2ogr", "-f", "GPKG", "-t_srs", "EPSG:4326", "-nln", "borders", geo, shp])
        args = ["gdal_rasterize", "-l", "borders", "-ot", "Byte", "-a_srs", "EPSG:4326",
                "-te", -180, -90, 180, 90, "-tr", res_deg, res_deg, "-at"]
        for v in LAND_RGB:
            args += ["-burn", v]
        for v in SEA_RGB:
            args += ["-init", v]
        run(args + tiff_options() + [geo, out])
    add_overviews(out, levels=(2, 4, 8, 16))
    return out


def dem_overviews(data):
    """External overviews for the DTED mosaic (gdaladdo -ro writes dted_1arc.vrt.ovr)."""
    vrt = os.path.join(data, "DEM", "dted_1arc.vrt")
    add_overviews(vrt, levels=(2, 4, 8, 16, 32, 64), resampling="average", read_only=True)
    return vrt + ".ovr"


def hillshade(data, res_arcsec=3.0):
    """Shaded relief from the DTED mosaic, resampled to res_arcsec (default 3" ~ 90 m)."""
    vrt = os.path.join(data, "DEM", "dted_1arc.vrt")
    out = os.path.join(data, "imagery", "hillshade.tif")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    res = res_arcsec / 3600.0
    with tempfile.TemporaryDirectory() as tmp:
        dem = os.path.join(tmp, "dem.vrt")
        run(["gdal_translate", "-of", "VRT", "-tr", res, res, "-r", "average", vrt, dem])
        if os.path.exists(out):
            os.remove(out)
        # -s 111120: degrees to metres; multidirectional light reads well under a sky light.
        run(["gdaldem", "hillshade", "-s", 111120, "-multidirectional", "-compute_edges", "-z", 1.5]
            + tiff_options() + [dem, out])
    add_overviews(out)
    return out


def vector_base(data, mbtiles, aoi, zoom=13, res_deg=0.0002):
    """Rasterise an OpenMapTiles vector MBTiles over the AOI into imagery/base.tif.

    Reads a single zoom level (ZOOM_LEVEL open option) and only the AOI, which avoids the
    whole-file scan that made the runtime OGR approach hang (I-007).
    """
    w, s, e, n = aoi
    out = os.path.join(data, "imagery", "base.tif")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    if os.path.exists(out):
        os.remove(out)
    cols = int(round((e - w) / res_deg))
    rows = int(round((n - s) / res_deg))
    args = ["gdal_create", "-of", "GTiff", "-ot", "Byte", "-bands", 3, "-outsize", cols, rows,
            "-a_srs", "EPSG:4326", "-a_ullr", w, n, e, s]
    for v in LAND_RGB:
        args += ["-burn", v]
    run(args + tiff_options() + [out])

    with tempfile.TemporaryDirectory() as tmp:
        gpkg = os.path.join(tmp, "omt.gpkg")
        layers = sorted({layer for layer, _, _ in OMT_STYLE})
        present = []
        for layer in layers:
            try:
                run(["ogr2ogr", "-f", "GPKG", "-update", "-append", "-oo", f"ZOOM_LEVEL={zoom}",
                     "-spat", w, s, e, n, "-spat_srs", "EPSG:4326", "-t_srs", "EPSG:4326", "-nln", layer, gpkg, mbtiles, layer])
                present.append(layer)
            except ToolError as err:
                print(f"  (layer {layer} skipped: {str(err).splitlines()[0]})")
        for layer, where, rgb in OMT_STYLE:
            if layer not in present:
                continue
            args = ["gdal_rasterize", "-l", layer, "-b", 1, "-b", 2, "-b", 3]
            for v in rgb:
                args += ["-burn", v]
            if where:
                args += ["-where", where]
            run(args + [gpkg, out])
    add_overviews(out)
    return out


def read_places(cities_dir):
    """Yield (name, lon, lat, rank, country) from the Cities/<cc>/place*.ndjson files."""
    for path in sorted(glob.glob(os.path.join(cities_dir, "*", "*.ndjson"))):
        stem = os.path.splitext(os.path.basename(path))[0]
        rank = PLACE_RANKS.get(stem)
        if rank is None:
            continue
        country = os.path.basename(os.path.dirname(path))
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                rec = json.loads(line)
                loc = rec.get("location") or []
                name = rec.get("name")
                if not name or len(loc) != 2:
                    continue
                lon, lat = float(loc[0]), float(loc[1])
                if -180 <= lon <= 180 and -90 <= lat <= 90:
                    yield name, lon, lat, rank, country


def places_geojson(places):
    return {
        "type": "FeatureCollection",
        "features": [
            {"type": "Feature",
             "geometry": {"type": "Point", "coordinates": [lon, lat]},
             "properties": {"name": name, "rank": rank, "country": country}}
            for name, lon, lat, rank, country in places
        ],
    }


def places(data):
    """Place-name points for labels: major (city/town) and minor (village/hamlet)."""
    cities = os.path.join(data, "Cities")
    all_places = list(read_places(cities))
    outputs = []
    for suffix, ranks in (("major", (1, 2)), ("minor", (3, 4))):
        subset = [p for p in all_places if p[3] in ranks]
        out = os.path.join(cities, f"places_{suffix}.gpkg")
        with tempfile.TemporaryDirectory() as tmp:
            gj = os.path.join(tmp, "places.geojson")
            with open(gj, "w", encoding="utf-8") as f:
                json.dump(places_geojson(subset), f, ensure_ascii=False)
            if os.path.exists(out):
                os.remove(out)
            run(["ogr2ogr", "-f", "GPKG", "-nln", "places", out, gj])
        print(f"  {len(subset)} places -> {out}")
        outputs.append(out)
    return outputs


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("step", choices=["all", "world-base", "dem-overviews", "hillshade", "vector-base", "places"])
    p.add_argument("--data", default=DEFAULT_DATA, help="data root (default: EarthView/Data)")
    p.add_argument("--mbtiles", help="OpenMapTiles vector MBTiles (vector-base; optional for all)")
    p.add_argument("--aoi", default="70.5,32.5,75.5,35.5",
                   help="vector-base extent minLon,minLat,maxLon,maxLat (default: Islamabad region)")
    p.add_argument("--zoom", type=int, default=13, help="vector tile zoom level to read (default 13)")
    p.add_argument("--base-res", type=float, default=0.0002, help="vector-base pixel size in degrees (~20 m)")
    p.add_argument("--hillshade-res", type=float, default=3.0, help="hillshade pixel size in arcsec (default 3)")
    p.add_argument("--world-res", type=float, default=0.05, help="world-base pixel size in degrees")
    a = p.parse_args(argv)

    data = os.path.abspath(a.data)
    try:
        if a.step in ("all", "world-base"):
            print("world-base:"); world_base(data, a.world_res)
        if a.step in ("all", "dem-overviews"):
            print("dem-overviews:"); dem_overviews(data)
        if a.step in ("all", "hillshade"):
            print("hillshade:"); hillshade(data, a.hillshade_res)
        if a.step == "vector-base" or (a.step == "all" and a.mbtiles):
            if not a.mbtiles:
                p.error("vector-base needs --mbtiles")
            print("vector-base:"); vector_base(data, a.mbtiles, parse_aoi(a.aoi), a.zoom, a.base_res)
        if a.step in ("all", "places"):
            print("places:"); places(data)
    except (ToolError, ValueError, OSError) as err:
        print(f"error: {err}", file=sys.stderr)
        return 1
    print("done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
