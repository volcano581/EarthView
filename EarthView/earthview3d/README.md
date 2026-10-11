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
| `src/MapConfig.cpp`, `src/EntityDiff.cpp`, `src/RuntimePaths.cpp` | GL-free logic, unit tested (`tests/tst_mapconfig`, `tst_entitydiff`, `tst_runtimepaths`) |
| `src/Runtime.cpp` | `configureRuntime()`: OSG plugin path + GDAL/PROJ data |
| `examples/earthview3d_viewer.cpp` | The `spikes/osgearth` app ported onto the library |
| `../Data/maps/earthview.earth` | Default map; layer paths relative to the earth file |

CMake targets: `earthview3d_core` (always built), `earthview3d` and `earthview3d_viewer`
(only with `EARTHVIEW_BUILD_3D`, which defaults to ON when osgEarth is found).

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

Keyboard: N next entity, T chase, F stealth, U untether, H home.

## Build (Windows, MSVC)

osgEarth comes from the vcpkg manifest at the repo root (`vcpkg.json`); see `CLAUDE.md`.
```
cmake -S EarthView -B build-msvc -G "Visual Studio 18 2026" -A x64 ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_MANIFEST_DIR=D:/Source/EarthView1
cmake --build build-msvc --config Release
build-msvc\earthview3d\Release\earthview3d_viewer.exe --view stealth --snapshot stealth.png --after 30
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
   with pre-scale + minimum scale).
