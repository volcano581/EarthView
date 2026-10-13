# Offline build and deployment (earthview3d)

Generalised from the osgEarth spike (`spikes/osgearth/OFFLINE_BUILD.md`, retired) for the
`earthview3d` library, its demo and any host (roadmap E7). Two separate needs:

- **A. Running** on an offline machine → ship the folder `cmake --install` produces.
- **B. Building** on an air-gapped machine → pinned vcpkg + binary cache + asset (source)
  cache carried over from a connected "feeder" machine.

## Toolchain (both machines)

| Item | Version | Note |
|---|---|---|
| Visual Studio | **2026** (C++ desktop workload, toolset v145) | VS 2022 hits I-019 |
| CMake | ≥ 4.2 (VS 2026 bundles one) | needed for the `Visual Studio 18 2026` generator |
| Qt | 6.11 **`msvc2022_64`** kit | binary-compatible with VS 2026; not from vcpkg |
| vcpkg | commit `2cfff9c458d9dcf642e9fa09ba624f9931bb5358` | = `builtin-baseline` in `vcpkg.json` |

> **Same VS update on both machines.** vcpkg's binary-cache key contains the exact compiler
> version (`cl.exe` 19.5x.y, which changes with each VS 18.x update). A different update on
> the offline machine still builds, but misses the cache and rebuilds osgEarth and its
> dependencies from the source cache (offline, ~1–3 h). The v145 toolset name alone is not
> enough: update both machines together, or rebuild the cache on the feeder after updating.

---

## A. Deploy a self-contained app folder

### Host CMake
```cmake
find_package(earthview3d 0.7 CONFIG REQUIRED)        # or add_subdirectory(<repo>/EarthView/earthview3d)
target_link_libraries(myhost PRIVATE earthview3d::earthview3d)
earthview3d_deploy_runtime(myhost INSTALL)           # see earthview3d/cmake/earthview3dDeploy.cmake
```
`earthview3d_deploy_runtime()` copies, after every build, next to the executable:
the vcpkg DLLs (osgEarth, OSG, GDAL, PROJ, …), `osgPlugins-<version>/` (all OSG and
osgEarth plugins — `osgdb_earth`, `osgdb_osgearth_vdatum_egm96` for the DTED datum,
`osgdb_freetype` for labels, …) and `share/gdal`, `share/proj`. With `INSTALL` it adds the
same files, the Qt runtime (Qt's deploy script) and the MSVC runtime to `cmake --install`.

`GlobeView3D` finds this layout by itself (`RuntimePaths::defaults()`): no environment
variables, launcher script or host code. A host that keeps the files elsewhere calls
`earthview3d::configureRuntime(paths)` before the first view. Explicit `GDAL_DATA` /
`PROJ_DATA` variables still win.

### Make the folder
```
cmake --build build-msvc --config Release
cmake --install build-msvc --config Release --prefix D:\deploy\myhost
```
Result:
```
D:\deploy\myhost\
  bin\myhost.exe
  bin\Qt6*.dll, platforms\, imageformats\, …    (Qt deploy script)
  bin\osg*.dll, osgEarth.dll, gdal*.dll, proj*.dll, …   (vcpkg runtime)
  bin\osgPlugins-3.6.5\                          (OSG + osgEarth plugins)
  bin\msvcp140.dll, vcruntime140*.dll, …         (MSVC runtime)
  share\gdal\                                    (GDAL_DATA)
  share\proj\proj.db                             (PROJ_DATA)
```
Use an absolute `--prefix` (Qt's deploy script rejects relative ones). Copy the folder to the
offline machine and run `bin\myhost.exe`. Release only: vcpkg's
debug DLLs are not deployed.

### Data (not part of the install)
Copy separately and keep the paths in the earth file valid (relative to its folder):
- `Data/maps/earthview.earth` (offline default) and `Data/imagery/` — `world_base.tif` is in
  git; `base.tif` and `hillshade.tif` come from `tools/offline_data/build_offline_data.py`.
- `Data/DEM/` (`*.dt2` + `dted_1arc.vrt`; the `.dt2` files are not in git) and its overviews.
- `Data/Borders/`, `Data/Cities/*.gpkg` (place labels, from the same tool).
- `Data/cache/` optional: a warm osgEarth cache makes the first start faster.

The demo (`earthview3d_demo`) is deployed next to its exe in the build tree the same way;
it reads data from the repo's `Data/` folder (or `--data-root`).

---

## B. Build on the air-gapped machine

### 1. Pin vcpkg (feeder)
```
git clone https://github.com/microsoft/vcpkg C:\vcpkg
git -C C:\vcpkg checkout 2cfff9c458d9dcf642e9fa09ba624f9931bb5358
C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
```
To upgrade dependencies later, move the checkout and `builtin-baseline` together.

### 2. Fill the caches (feeder, connected)
In an "x64 Native Tools Command Prompt for VS 2026":
```
set VCPKG_BINARY_SOURCES=clear;files,D:\vcpkg-bincache,readwrite
set X_VCPKG_ASSET_SOURCES=clear;x-azurl,file:///D:/vcpkg-assets,,readwrite
cd D:\Source\EarthView
cmake -S EarthView -B build-msvc -G "Visual Studio 18 2026" -A x64 ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_MANIFEST_DIR=D:/Source/EarthView
cmake --build build-msvc --config Release
```
This fills:
- `D:\vcpkg-assets` — every source archive and tool vcpkg downloaded (CMake, Ninja, 7-Zip,
  Perl, NASM, …); also the audit trail of third-party sources.
- `D:\vcpkg-bincache` — every package built for this compiler and triplet.

### 3. Transfer on approved media
- `C:\vcpkg` (without `buildtrees\`, `packages\`, `downloads\`)
- `D:\vcpkg-assets`, `D:\vcpkg-bincache`
- the source tree (`git bundle create earthview.bundle --all`)
- the data of section A
- installers if missing: VS 2026 offline layout
  (`vs_professional.exe --layout D:\vs-offline --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended`)
  and the Qt `msvc2022_64` kit (offline installer, or a copy of `C:\Qt\6.11.0\msvc2022_64`).

Use the same paths on the offline machine, or adjust the variables below.

### 4. Build offline
```
set VCPKG_BINARY_SOURCES=clear;files,D:\vcpkg-bincache,read
set X_VCPKG_ASSET_SOURCES=clear;x-azurl,file:///D:/vcpkg-assets,,read;x-block-origin
cd D:\Source\EarthView
cmake -S EarthView -B build-msvc -G "Visual Studio 18 2026" -A x64 ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_MANIFEST_DIR=D:/Source/EarthView
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release
```
`x-block-origin` makes vcpkg fail fast instead of trying the internet. Set the two variables
as system environment variables to avoid repeating them. VS Code with the CMake Tools
extension works the same way: put the cache settings in `cmake.configureSettings` /
`cmake.environment`.

### 5. Package for hosts (optional)
```
cmake --install build-msvc --config Release --prefix D:\sdk\earthview3d
```
installs the `earthview3d` package (headers, static libraries,
`lib/cmake/earthview3d/`). A host configures with
`-DCMAKE_PREFIX_PATH=D:/sdk/earthview3d;C:/Qt/6.11.0/msvc2022_64` and the **same vcpkg
toolchain and triplet** — earthview3d is static, so osgEarth/OSG/GDAL are found again when
the host links. The package records absolute vcpkg paths of the build machine: build hosts
on the machine (or the same paths) where the package was built.
`EarthView/tests/consumer/` is a minimal host and is built against the package in CI.

### Updating dependencies
Change `vcpkg.json` / the baseline on the feeder, rebuild there with the `readwrite`
settings, and carry over only the new files in `vcpkg-assets` and `vcpkg-bincache`.

### Team setup (later)
Host the two caches on an internal file share, or use a NuGet feed for binaries
(`VCPKG_BINARY_SOURCES=clear;nuget,<feed>,read`), so every developer and the build server
inside the secure network share them.
