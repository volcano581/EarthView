# Offline build and deployment (osgEarth spike)

Two separate needs:

- **A. Running** on offline machines → ship the folder produced by `cmake --install`.
- **B. Building** on an air-gapped machine → pinned vcpkg + binary cache + asset (source)
  cache carried over from a connected "feeder" machine.

Toolchain on both machines: **Visual Studio 2026** (C++ desktop workload), Qt 6.11
**`msvc2022_64`** kit (binary-compatible with VS 2026), CMake ≥ 4.x.

> **Keep VS 2026 on the same update (18.x.y) on the feeder and offline machines.** The vcpkg
> binary-cache key includes the exact compiler version. On a mismatch vcpkg rebuilds from
> the source cache — still offline, but ~30–60 min.

---

## A. Deploy a self-contained app folder

On any machine that has built the project:
```
cmake --build build --config Release
cmake --install build --config Release --prefix D:\deploy\osgearth_spike
```
Result:
```
D:\deploy\osgearth_spike\
  bin\osgearth_spike.exe, spike.earth
  bin\Qt6*.dll, platforms\, imageformats\, ...   (Qt deploy script)
  bin\osg*.dll, osgEarth*.dll, gdal*.dll, proj*.dll, ... (vcpkg runtime)
  bin\osgPlugins-<version>\                     (OSG + osgEarth plugins)
  share\gdal\                                   (GDAL_DATA)
  share\proj\proj.db                            (PROJ_DATA)
```
`main.cpp` sets `OSG_LIBRARY_PATH`, `GDAL_DATA` and `PROJ_DATA`/`PROJ_LIB` to these folders
when they are not already set, so no launcher script is needed. Copy the folder to the
offline machine and run `bin\osgearth_spike.exe`.

Data is **not** part of the install — copy it separately and keep the paths in
`spike.earth` and `osm_style.json` valid:
- `EarthView\Data\DEM\` (`*.dt2` + `dted_1arc.vrt`; the `.dt2` files are not in git)
- `EarthView\Data\Borders\`
- the OpenMapTiles vector file `osm-2020-02-10-v3.11_asia_pakistan.mbtiles` (path in
  `osm_style.json` → `sources.openmaptiles.url`). Imagery is rendered from it, so no
  network is needed.

---

## B. Build on the air-gapped machine

### 1. Pin vcpkg
`vcpkg.json` has `builtin-baseline` = the vcpkg commit used for the spike. Check out that
same commit on the feeder:
```
git clone https://github.com/microsoft/vcpkg C:\vcpkg
git -C C:\vcpkg checkout 2cfff9c458d9dcf642e9fa09ba624f9931bb5358
C:\vcpkg\bootstrap-vcpkg.bat
```
To upgrade dependencies later, move the checkout and the `builtin-baseline` together.

### 2. Feeder machine (connected)
In an "x64 Native Tools Command Prompt for VS 2026":
```
set VCPKG_BINARY_SOURCES=clear;files,D:\vcpkg-bincache,readwrite
set X_VCPKG_ASSET_SOURCES=clear;x-azurl,file:///D:/vcpkg-assets,,readwrite
cd D:\Source\EarthView1\spikes\osgearth
cmake -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build build --config Release
```
This fills:
- `D:\vcpkg-assets` — every source archive and build tool vcpkg downloaded (CMake, Ninja,
  7-Zip, Perl, NASM, …). Also the audit trail of third-party sources.
- `D:\vcpkg-bincache` — every package built for this compiler and triplet.

### 3. Transfer on approved media
- `C:\vcpkg` (exclude `buildtrees\`, `packages\`, `downloads\`)
- `D:\vcpkg-assets`
- `D:\vcpkg-bincache`
- the source tree (`git bundle create earthview.bundle --all` is convenient)
- installers if missing: VS 2026 offline layout
  (`vs_professional.exe --layout D:\vs-offline --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended`)
  and the Qt `msvc2022_64` kit (offline installer, or copy `C:\Qt\6.11.0\msvc2022_64`).

Place them at the same paths on the offline machine (or adjust the variables below).

### 4. Offline machine
```
set VCPKG_BINARY_SOURCES=clear;files,D:\vcpkg-bincache,read
set X_VCPKG_ASSET_SOURCES=clear;x-azurl,file:///D:/vcpkg-assets,,read;x-block-origin
cd D:\Source\EarthView1\spikes\osgearth
cmake -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build build --config Release
```
`x-block-origin` makes vcpkg fail fast instead of trying the internet. Set the two
variables as system environment variables to avoid repeating them.

### Updating dependencies
Change `vcpkg.json` / the baseline on the feeder, rebuild there with the `readwrite`
settings, and carry over only the new files in `vcpkg-assets` and `vcpkg-bincache`.

### Team setup (later)
Host the two caches on an internal file share — or use a NuGet feed for binaries
(`VCPKG_BINARY_SOURCES=clear;nuget,<feed>,read`) — so every developer and the build server
inside the secure network share them.
