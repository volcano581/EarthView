# Data/EarthFiles — EarthView layer-list files (2D) and the 3D catalog

The 2D viewer scans this folder for `*.earth` files and offers **one map source per file**
in its source dropdown. These files are *not* osgEarth earth files: they are a bare list of
layer elements, with no `<map>` root, no header and no comments — one line per TMS endpoint
of the local tile server:

```
<image name="Dummy_Layer_1" driver="tms"> <url> http://host:port/path/to/imagery/layer1/tilemapresource.xml</url></image>
<image name="Dummy_Layer_2" driver="tms"> <url> http://host:port/path/to/imagery/layer2/tilemapresource.xml</url></image>
```

The real files belong to the offline machine (they carry its tile-server address), so only
`*.earth.sample` copies are in git. Drop your own `*.earth` files here; they are git-ignored.

**3D (`earthview3d`):** osgEarth needs a standard earth file, so
`earthview3d::EarthFileCatalog` (`include/earthview3d/EarthFileCatalog.h`) reads these files
and writes one standard earth file per source — `<DataRoot>/maps/tms_<name>.earth` — by
splicing `<TMSImage>`/`<TMSElevation>` layers into the `<!--@EARTHVIEW3D_TMS_LAYERS@-->`
placeholder of a template earth file (`maps/earthview.earth` by default), which supplies the
local elevation, overlays and graticule. The demo lists the result in its **Map** dropdown:

```
earthview3d_demo --list-maps
earthview3d_demo --map-name "Satellite Imagery"
```

Run it from the folder the package was extracted to; `Data` beside the executable is found on
its own (the `run-earthview3d_demo.cmd` launcher stays as a convenience). `--list-maps` prints
the data root it resolved, then every folder searched (marking the ones that do not exist) and every map
found, with its TMS layer count — run it first on a machine where the 3D view comes up without
the tile server's imagery. With no `--earth-files`, the search covers the same folders as the
2D viewer: `<DataRoot>/EarthFiles`, `<DataRoot>/Data/EarthFiles`, `<DataRoot>`, and then the
working directory and the executable's folder, each with `Data/EarthFiles` (I-046). Pass
`--earth-files <dir>` to search one folder instead.

**The files must end in `.earth`.** A folder holding only the shipped `*.earth.sample`
templates offers no map, and the 3D view then loads the template map — the pale offline base
imagery and no tile server, which looks much like an unreachable server. The demo says so on
stderr, naming the folders it searched.

A file in this folder that already has a `<map>` root is passed to osgEarth unchanged.
2D behaviour is unchanged: it still reads these files itself.

## DTED from the offline server

A layer list may also name the folder of DTED tiles (`.dt1`/`.dt2`) that the offline machine
holds, so the elevation path is configured in the same one file as the imagery:

```
<elevation name="DTED" driver="dted"> <url> D:/EarthViewData/DTED </url></elevation>
```

Edit that path on each machine (the `*.earth.sample` files carry a placeholder).

* **2D** reads the path and reports it at start-up; its terrain still comes from the GeoTIFF
  DEM mirror until the DTED reader lands (I-039 in `3D_PROGRESS.md`).
* **3D** turns the entry into an osgEarth `<GDALElevation>` layer in the generated
  `tms_<name>.earth`. osgEarth opens a GDAL dataset, not a folder, so `earthview3d` indexes
  the folder itself while generating that file: it walks every sub-folder (`e066/n32.dt2`,
  `e067/…`), reads each cell's 80-byte DTED header and writes `dted_mosaic.vrt` into the
  folder, which the generated map then names. Nothing to run, and **no GDAL tools needed on
  the machine**. The mosaic is rebuilt only when a cell is newer than it; when the folder is
  read-only (a share from the elevation server) it is written next to the generated map
  instead, with absolute cell paths. A `<url>` that already names a file (`.vrt`, a single
  `.dt2`) is used as it is.

  The same mosaic can be built by hand, which is what a host loading `maps/earthview.earth`
  directly needs, since that file is read by osgEarth without passing through the catalog:

  ```
  python EarthView/tools/offline_data/build_offline_data.py dted-vrt D:/EarthViewData/DTED
  ```

Keep one DTED level per folder — a `DTED1` and a `DTED2` root, as the server holds them, with
one `<elevation>` line each and the finer level **last** (later elevation layers win). Cells of
different posting do mosaic together onto the finest grid, but they overlap by a pixel at the
seams, so separate roots give the better result.

Hosts that load `maps/earthview.earth` directly instead of a layer list have the same path
spelled out in that file, next to the SRTM mosaic.
