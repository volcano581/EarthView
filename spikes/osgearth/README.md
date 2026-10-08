# osgEarth spike

Throwaway evaluation app: does osgEarth, hosted in a Qt 6 widget, give EarthView an
acceptable 3D / stealth view with our own data? Decide **before roadmap milestone M3**
(see `EarthView/ROADMAP_3D.md`).

It loads `spike.earth`:
- Offline imagery rendered from the OpenMapTiles **vector** MBTiles (Pakistan) through
  osgEarth's MapBoxGL layer and `osm_style.json` (fills and lines only, no labels). The
  .mbtiles path is set in `osm_style.json`; online OSM and raster MBTiles alternatives are
  commented out in `spike.earth`.
- SRTM 1-arc-second DTED2 (34 tiles, N30–N34 / E066–E077) through the mosaic
  `EarthView/Data/DEM/dted_1arc.vrt`, over the 90 m GeoTIFF `DEM90TIF/N18.tif`
- EarthView's country-border shapefile draped on terrain

It also adds a sky with sun lighting, a logarithmic depth buffer, and a test entity (red
cone) circling 300 m above the ground with chase and close tethered cameras.

This is separate from the EarthView build: osgEarth from vcpkg is built with MSVC (VS 2026), so the
spike uses the Qt `msvc2022_64` kit, not llvm-mingw.

## Build (Windows)

One-time vcpkg setup (the first osgEarth build takes 30–60 min: GDAL, OSG, etc.):
```
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
```

Configure and build from a "x64 Native Tools Command Prompt for VS 2026":
```
cd D:\Source\EarthView1\spikes\osgearth
cmake -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build build --config Release
```

Run (Qt and OSG plugin DLLs must be findable):
```
C:\Qt\6.11.0\msvc2022_64\bin\windeployqt.exe build\Release\osgearth_spike.exe
set OSG_LIBRARY_PATH=D:\Source\EarthView1\spikes\osgearth\build\vcpkg_installed\x64-windows\plugins
build\Release\osgearth_spike.exe --lat 33.70 --lon 73.05
```
If the plugin folder differs, find `osgdb_earth.dll` under `build\vcpkg_installed` and point
`OSG_LIBRARY_PATH` at its folder. The default `--lat 33.70 --lon 73.05` lies inside the
DTED coverage (tile `n33_e073`); any point in N30–N34 / E066–E077 works.

For a self-contained folder (offline machines) use `cmake --install`; see
[OFFLINE_BUILD.md](OFFLINE_BUILD.md), which also covers building on an air-gapped machine.

Controls: left-drag rotate, right-drag zoom, middle-drag pan, wheel zoom,
**T** chase entity, **F** close "stealth" tether, **U** untether, **H** home.

Without code you can also try the earth file in osgEarth's own viewer:
`osgearth_viewer spike.earth --sky`

## What to evaluate

| Check | Pass if |
| ----- | ------- |
| DEM | Terrain relief matches the region; no cracks or holes between tiles |
| Imagery | Sharp imagery draped on terrain, no seams, smooth level-of-detail transitions |
| Offline | Works with the network unplugged (vector MBTiles + DTED) |
| Vector imagery | Roads/water/landcover legible when draped; tile build time acceptable while flying |
| Borders | Shapefile lines follow the terrain without flicker |
| Close-up | With **F**, terrain near the camera is stable (no z-fighting or jitter) |
| Entity | Cone moves smoothly; tethered camera follows without wobble |
| Performance | ≥ 60 fps at 1440p on the target GPU (OSG stats: press `s` in osgearth_viewer) |
| Qt integration | Resizing, high-DPI and docking work; acceptable as a secondary window |
| Effort | Build/deploy (DLLs, GDAL data, plugins) is acceptable for Doctrine's installer |

If most checks pass, replace roadmap M3–M7 with "integrate osgEarth" and keep M8 (entities
feed from Doctrine) and M9 (camera controllers) on top of it.

Automated snapshot (saves a frame and exits; prints the camera viewpoint to stderr):
`osgearth_spike.exe [earthfile] --view home|chase|stealth --snapshot out.png --after 30`

## Status (2026-10-08, osgEarth 3.8.1, OSG 3.6.5, GDAL 3.12.4, RTX 3050 Ti)

| Check | Result |
| ----- | ------ |
| Build + `cmake --install` deploy folder | ✓ (built with VS 2022; MSVC runtime DLLs not deployed) |
| DTED 1″ terrain via VRT | ✓ relief of the Margalla Hills visible in stealth view |
| Imagery draping (online OSM XYZ) | ✓ |
| Offline imagery from vector MBTiles (MapBoxGL) | ✗ layer renders nothing, even a background-only style |
| Vector MBTiles via OGR + FeatureImage | ✗ hangs while GDAL scans the file |
| Country borders shapefile | ✓ opens (not in view at the default location) |
| Sky, sun, stars, haze | ✓ (moon texture missing: osgEarth data folder not deployed) |
| Stealth view (`F`) | ✓ |
| Chase view (`T`) | ✗ lower half black with red/yellow band — camera under terrain or mid-transition |

Fixes made during the first run: null terrain at startup (crash), vertex-attribute aliasing
for the embedded window (osgEarth shaders), home viewpoint registered with the
manipulator (camera framed the sky dome), borders shapefile path.
