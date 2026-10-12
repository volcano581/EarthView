# earthview3d

Host-independent osgEarth 3D / stealth view for EarthView (roadmap E1). A host (Doctrine, or
`earthview3d_demo` from E2) creates a `GlobeView3D`, pushes `EntityState3D` records every
frame and reads signals. Design: `../OSGEARTH_INTEGRATION.md`; host contract:
`../DOCTRINE_WIRING.md`.

## Layout

| Path | What |
| ---- | ---- |
| `include/earthview3d/` | Public headers — Qt/STL types only, no osgEarth/OSG (D-011) |
| `src/GlobeView3D.cpp` | The osgEarth widget (pimpl); started as the widget from the local host test |
| `src/MapConfig.cpp`, `src/EntityDiff.cpp`, `src/ModelTable.cpp`, `src/Picking.cpp`, `src/RuntimePaths.cpp` | GL-free logic, unit tested (`tests/tst_mapconfig`, `tst_entitydiff`, `tst_modeltable`, `tst_runtimepaths`) |
| `../Data/models/models.json` | Example model table (`MapConfig::modelTable`; format in `ModelTable.h`); armour uses `test_armour.osgt` |
| `tools/make_test_armour.cpp` | Generator for the test model (not built) |
| `src/Runtime.cpp` | `configureRuntime()`: OSG plugin path + GDAL/PROJ data |
| `demo/` | `earthview3d_demo`: host simulator — 2D map + "Open 3D View", synthetic 100 Hz entity feed (E2) |
| `../Data/maps/earthview.earth` | Default map; layer paths relative to the earth file |

CMake targets: `earthview3d_core` and `earthview3d_demo_feed` (always built), `earthview3d` and
`earthview3d_demo` (only with `EARTHVIEW_BUILD_3D`, which defaults to ON when osgEarth is found).

## Use

```cpp
#include <earthview3d/GlobeView3D.h>

earthview3d::MapConfig config;
config.dataRoot = "D:/Source/EarthView1/EarthView/Data";
config.earthFile = "maps/earthview.earth";        // relative to dataRoot
config.cacheDir = "cache";                         // optional osgEarth filesystem cache

auto* view = new earthview3d::GlobeView3D(config, originLatDeg, originLonDeg);
view->setPreFrameCallback([&] { view->setEntities(buildEntitiesFromHostSnapshot()); });
```
`MapConfig::fromJsonFile()` reads the same settings from JSON (see `MapConfig.h`).

Runtime files: osgDB reads `OSG_LIBRARY_PATH` when its DLL loads, before `main()`, so
setting it in the application does nothing. Call `earthview3d::configureRuntime(paths)`
(`Runtime.h`) before the first view with the OSG plugin folder and GDAL/PROJ data folders;
otherwise the first `GlobeView3D` uses `RuntimePaths::buildTreeDefaults()` (the vcpkg
folders of this build tree). Explicit `GDAL_DATA`/`PROJ_DATA` variables still win.

Mouse: left-click selects an entity (`entityPicked`); left-drag rotates (orbit) or looks around
(other modes); wheel zooms (orbit) or sets chase range / stealth eye height / free-fly speed.
Keyboard: O orbit, T chase, F stealth, P first person, G free-fly (W/A/S/D, Q/E down/up,
Shift ×4), L chase free-look, J HUD, K ground-offset overlay, N next entity, U untether
(= orbit), H home.

Heights are WGS-84 ellipsoid heights (HAE). The DTED layer is declared `vdatum="egm96"`
(D-021); `geoidHeight()` converts to MSL. `heightAt()` (cached, any thread) and
`sourceHeightAt()` (full resolution) are the terrain query API (E5).

`GlobeView3D::entityPosition(id, &lat, &lon, &alt)` returns where an entity was actually
placed (after terrain clamping) — used by `earthview3d_demo --check-agreement`.

### Expected log lines
- `Error reading file ...CascadiaCode.ttf: Could not find plugin` at startup: osgEarth's own
  default font, resolved when its DLL loads (before `configureRuntime()`). Harmless — labels
  load their font explicitly afterwards (E3).
- `earthview3d: tether to entity N (range R m, pitch P)` / `earthview3d: tether ended (...)`:
  camera tether start and end (user input or a new viewpoint). A tether requested before the
  first frame is held until the home viewpoint has been applied (E3 review round 2).
- `Windows Error #2000: ... Unable to restore current OpenGL rendering context` at startup:
  OSG probing contexts while the Qt widget owns the current one. Harmless.
- `GDAL_DATA environment variable is not set` may still appear before `configureRuntime()`
  runs (I-011); GDAL then uses the paths set through its API.

## Build (Windows, MSVC)

osgEarth comes from the vcpkg manifest at the repo root (`vcpkg.json`); see `CLAUDE.md`.
```
cmake -S EarthView -B build-msvc -G "Visual Studio 18 2026" -A x64 ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_MANIFEST_DIR=D:/Source/EarthView1
cmake --build build-msvc --config Release
build-msvc\earthview3d\demo\Release\earthview3d_demo.exe --view stealth --snapshot2d 2d.png --snapshot3d 3d.png --after 30
```
`VCPKG_MANIFEST_DIR` is needed because the CMake source dir (`EarthView/`) is not the repo
root. MSVC builds use C++20 (Qt 6.11 moc output needs it, D-016); use **VS 2026** —
VS 2022 hits an internal compiler error in Qt 6.11's QtTest headers (I-019). The library finds the
vcpkg OSG plugins and GDAL/PROJ data of the build tree by itself; Qt DLLs come from
`windeployqt` or `PATH`. Deployment for other machines is roadmap E7.

## Rules baked into the widget (do not undo)

1. Per-widget compatibility-profile `QSurfaceFormat` (host apps default to core).
2. `setUseVertexAttributeAliasing(true)` + `setUseModelViewAndProjectionUniforms(true)`.
3. `setDefaultFboId(defaultFramebufferObject())` every frame.
4. Home viewpoint set via `EarthManipulator::setHomeViewpoint` and applied after frame 1.
5. `MapNode::getTerrain()` is null before the first frame.
6. **No `LogarithmicDepthBuffer`** (drops near-camera terrain); `setNearFarRatio(2e-5)` (D-010).
   `EARTHVIEW3D_LOGDEPTH=1` turns it on for diagnosis; `EARTHVIEW3D_NO_SKY=1` drops the sky.
7. Ground entities clamped to terrain with a cached last-good height (I-015).
8. Entity markers: real size up close, minimum pixel size far away (`AutoTransform`
   with pre-scale + minimum scale). With a model file they are the far LOD (`osg::LOD`
   by pixel size, `iconBelowPixels`), so entities stay readable at all ranges.
9. **Every `osg::Referenced` object (nodes, state sets, `TextSymbol`, …) is heap-allocated and
   held by `osg::ref_ptr`** — osgEarth/OSG take references and delete them (E3 review: a stack
   `TextSymbol` corrupted the heap).
10. Text alignment boxes are explicit zero boxes, never `osg::BoundingBox()` (invalid →
   labels placed at -FLT_MAX). The label font is loaded after `configureRuntime()`
   (`EARTHVIEW3D_LABEL_FONT` overrides; `EARTHVIEW3D_NO_DECLUTTER=1` for debugging).
11. Labels go through osgEarth's `ScreenSpaceLayout` (declutter); the selected label gets top priority.
12. Cameras other than orbit detach the EarthManipulator and set the view matrix from a
   geodetic `CameraPose` (`CameraControl.h`); the eye is clamped ≥ terrain + 2 m every frame
   using the last good terrain height (D-020). Orbit keeps osgEarth's terrain avoidance.
