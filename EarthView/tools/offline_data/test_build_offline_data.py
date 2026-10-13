#!/usr/bin/env python3
"""Tests for build_offline_data.py (E6). GDAL-dependent tests skip when the tools are missing."""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_offline_data as bod  # noqa: E402

HAVE_GDAL = all(shutil.which(t) for t in ("gdal_translate", "gdaldem", "gdaladdo", "gdal_rasterize",
                                         "gdal_create", "ogr2ogr", "gdalinfo"))


def gdalinfo(path):
    return json.loads(subprocess.run(["gdalinfo", "-json", "-stats", path], capture_output=True,
                                     text=True, check=True).stdout)


class PureTests(unittest.TestCase):
    def test_parse_aoi(self):
        self.assertEqual(bod.parse_aoi("70.5,32.5,75.5,35.5"), (70.5, 32.5, 75.5, 35.5))
        with self.assertRaises(ValueError):
            bod.parse_aoi("1,2,3")
        with self.assertRaises(ValueError):
            bod.parse_aoi("75,32,70,35")   # west > east

    def test_style_is_well_formed(self):
        for layer, where, rgb in bod.OMT_STYLE:
            self.assertTrue(layer)
            self.assertEqual(len(rgb), 3)
            self.assertTrue(all(0 <= c <= 255 for c in rgb))
        layers = [l for l, _, _ in bod.OMT_STYLE]
        self.assertLess(layers.index("landcover"), layers.index("transportation"))  # roads on top

    def test_read_places(self):
        with tempfile.TemporaryDirectory() as tmp:
            os.makedirs(os.path.join(tmp, "pk"))
            with open(os.path.join(tmp, "pk", "place_city.ndjson"), "w", encoding="utf-8") as f:
                f.write(json.dumps({"name": "Islamabad", "location": [73.05, 33.69]}) + "\n")
                f.write("\n")
                f.write(json.dumps({"name": "", "location": [73.0, 33.0]}) + "\n")      # no name
                f.write(json.dumps({"name": "Bad", "location": [200.0, 33.0]}) + "\n")  # out of range
            with open(os.path.join(tmp, "pk", "place-village.ndjson"), "w", encoding="utf-8") as f:
                f.write(json.dumps({"name": "گاؤں", "location": [72.9, 33.6]}) + "\n")
            with open(os.path.join(tmp, "pk", "other.ndjson"), "w", encoding="utf-8") as f:
                f.write(json.dumps({"name": "Ignored", "location": [72.9, 33.6]}) + "\n")
            places = sorted(bod.read_places(tmp), key=lambda p: p[3])
            self.assertEqual(places, [("Islamabad", 73.05, 33.69, 1, "pk"), ("گاؤں", 72.9, 33.6, 3, "pk")])
            gj = bod.places_geojson(places)
            self.assertEqual(gj["features"][0]["geometry"]["coordinates"], [73.05, 33.69])
            self.assertEqual(gj["features"][1]["properties"]["rank"], 3)

    def test_missing_tool_is_reported(self):
        with self.assertRaises(bod.ToolError):
            bod.run(["definitely-not-a-gdal-tool"], quiet=True)


@unittest.skipUnless(HAVE_GDAL, "GDAL command-line tools not installed")
class GdalTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.data = os.path.join(self.tmp, "Data")
        os.makedirs(os.path.join(self.data, "DEM"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def write_dem(self):
        # 0.2° x 0.2° synthetic hill at 1" spacing, saved as the mosaic name the tool expects.
        xyz = os.path.join(self.tmp, "dem.xyz")
        with open(xyz, "w") as f:
            n = 73
            for j in range(n):
                for i in range(n):
                    lon = 73.0 + i * 0.2 / (n - 1)
                    lat = 33.2 - j * 0.2 / (n - 1)
                    h = 500 + 400 * max(0.0, 1 - ((i - 36) ** 2 + (j - 36) ** 2) / 900.0)
                    f.write(f"{lon} {lat} {h}\n")
        tif = os.path.join(self.data, "DEM", "dem.tif")
        bod.run(["gdal_translate", "-q", "-a_srs", "EPSG:4326", xyz, tif], quiet=True)
        bod.run(["gdal_translate", "-q", "-of", "VRT", tif, os.path.join(self.data, "DEM", "dted_1arc.vrt")], quiet=True)

    def test_hillshade_and_dem_overviews(self):
        self.write_dem()
        out = bod.hillshade(self.data, res_arcsec=10.0)
        info = gdalinfo(out)
        self.assertEqual(len(info["bands"]), 4)   # RGBA (issue #11)
        self.assertEqual(info["bands"][3]["colorInterpretation"], "Alpha")
        self.assertTrue(info["bands"][0]["overviews"])
        stats = info["bands"][0]
        self.assertGreater(stats["maximum"], stats["minimum"])   # relief is visible
        ovr = bod.dem_overviews(self.data)
        self.assertTrue(os.path.exists(ovr))

    def test_vector_base_from_mvt(self):
        # A tiny OpenMapTiles-like MBTiles: one water polygon and one primary road.
        src = os.path.join(self.tmp, "src")
        os.makedirs(src)
        water = {"type": "FeatureCollection", "features": [{"type": "Feature", "properties": {"class": "lake"},
                 "geometry": {"type": "Polygon", "coordinates": [[[73.02, 33.02], [73.08, 33.02], [73.08, 33.08],
                                                                   [73.02, 33.08], [73.02, 33.02]]]}}]}
        road = {"type": "FeatureCollection", "features": [{"type": "Feature", "properties": {"class": "primary"},
                "geometry": {"type": "LineString", "coordinates": [[73.0, 33.15], [73.2, 33.15]]}}]}
        for name, fc in (("water", water), ("transportation", road)):
            with open(os.path.join(src, name + ".geojson"), "w") as f:
                json.dump(fc, f)
        gpkg = os.path.join(self.tmp, "src.gpkg")
        for name in ("water", "transportation"):
            bod.run(["ogr2ogr", "-f", "GPKG", "-update", "-append", "-nln", name, gpkg,
                     os.path.join(src, name + ".geojson")], quiet=True)
        mbt = os.path.join(self.tmp, "omt.mbtiles")   # the MBTiles writer cannot append layers
        bod.run(["ogr2ogr", "-f", "MBTILES", "-dsco", "MINZOOM=10", "-dsco", "MAXZOOM=12", mbt, gpkg], quiet=True)
        out = bod.vector_base(self.data, mbt, (73.0, 33.0, 73.2, 33.2), zoom=12, res_deg=0.001)
        info = gdalinfo(out)
        self.assertEqual(info["size"], [200, 200])
        self.assertEqual(len(info["bands"]), 4)   # RGBA (issue #11)
        self.assertEqual(info["bands"][3]["colorInterpretation"], "Alpha")
        # Water pixels: blue band dominates; background land: red band highest.
        px = subprocess.run(["gdallocationinfo", "-valonly", "-wgs84", out, "73.05", "33.05"],
                            capture_output=True, text=True, check=True).stdout.split()
        self.assertEqual([int(v) for v in px], list(bod.SEA_RGB) + [255])
        bg = subprocess.run(["gdallocationinfo", "-valonly", "-wgs84", out, "73.15", "33.05"],
                            capture_output=True, text=True, check=True).stdout.split()
        self.assertEqual([int(v) for v in bg], list(bod.LAND_RGB) + [255])

    def test_world_base_is_rgba(self):
        borders = os.path.join(self.data, "Borders", "World_Countries_Generalized_Shapefile")
        os.makedirs(borders)
        src = os.path.join(bod.DEFAULT_DATA, "Borders", "World_Countries_Generalized_Shapefile")
        for f in os.listdir(src):
            shutil.copy(os.path.join(src, f), borders)
        out = bod.world_base(self.data, res_deg=1.0)
        info = gdalinfo(out)
        self.assertEqual(info["size"], [360, 180])
        self.assertEqual([b["colorInterpretation"] for b in info["bands"]], ["Red", "Green", "Blue", "Alpha"])

    def test_committed_world_base_is_rgba(self):
        info = gdalinfo(os.path.join(bod.DEFAULT_DATA, "imagery", "world_base.tif"))
        self.assertEqual([b["colorInterpretation"] for b in info["bands"]], ["Red", "Green", "Blue", "Alpha"])

    def test_places(self):
        os.makedirs(os.path.join(self.data, "Cities", "pk"))
        with open(os.path.join(self.data, "Cities", "pk", "place_city.ndjson"), "w", encoding="utf-8") as f:
            f.write(json.dumps({"name": "Islamabad", "location": [73.05, 33.69]}) + "\n")
        with open(os.path.join(self.data, "Cities", "pk", "place-hamlet.ndjson"), "w", encoding="utf-8") as f:
            f.write(json.dumps({"name": "Dhok", "location": [73.1, 33.7]}) + "\n")
        major, minor = bod.places(self.data)
        out = subprocess.run(["ogrinfo", "-al", "-so", major], capture_output=True, text=True, check=True).stdout
        self.assertIn("Feature Count: 1", out)
        out = subprocess.run(["ogrinfo", "-al", minor], capture_output=True, text=True, check=True).stdout
        self.assertIn("Dhok", out)


if __name__ == "__main__":
    unittest.main()
