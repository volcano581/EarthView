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
earthview3d_demo --earth-files EarthFiles --list-maps
earthview3d_demo --earth-files EarthFiles --map-name "Satellite Imagery"
```

A file in this folder that already has a `<map>` root is passed to osgEarth unchanged.
2D behaviour is unchanged: it still reads these files itself.
