Tactical map demo (CesiumJS in Qt WebEngine) - offline package
================================================================

A prototype of a flat tactical map with a 3D terrain view: 300 simulated ground units move
around Islamabad on real SRTM DTED2 terrain. Everything runs from this folder; no network,
no installation, no admin rights.

Requirements
  Windows 10 or 11, 64-bit, with a GPU driver that supports OpenGL (any recent Intel, AMD
  or NVIDIA driver). About 1 GB of disk space.

Start
  Double-click Run-Demo.cmd (or run cesium_proto.exe).

Map controls (bottom right, as in Google Maps)
  Compass ............ click: face north; the arrows beside it rotate 45 degrees
  3D / 2D ............ tilt the flat map into the 3D terrain view, and back
  Target button ...... fly to the units (or to the selected unit)
  + / - .............. zoom
  Yellow figure ...... drag it onto the map for a ground-level view
                       (drag to look, W A S D to walk, Shift to run, Esc or Exit to leave)
  Mouse .............. drag to pan, wheel to zoom; in 3D, Ctrl+drag or middle-drag to tilt
  Bottom left tile ... switch between "Terrain" (relief shading) and plain "Map";
                       hover it for overlays (units, labels, place names, 3D terrain,
                       night controls, performance HUD)
  Click a unit ....... info card with position, elevation, speed, Follow and Ground view
  Scale bar .......... click to switch km / nautical miles

Options (append to Run-Demo.cmd or cesium_proto.exe)
  --units N          number of simulated units (default 300)
  --hz N             simulation rate (default 20)
  --time-scale X     simulation speed-up (default 4)
  --center LAT,LON   area centre (default 33.70,73.05)
  --radius KM        area radius (default 25)
  --mbtiles FILE     OpenMapTiles vector .mbtiles (or raster) for a detailed street map
  --no-window        serve only; open the printed http://127.0.0.1 address in a browser
  --help             all options

Included data (folder data\)
  DEM\       SRTM 1-arc-second DTED2 cells, 30-35 N / 66-77 E (terrain and relief)
  Borders\   country borders (base map)
  Cities\    city and town names

Notes
  - Built with Qt 6.4.2; CesiumJS 1.146.0 (in web\cesium).
  - The program listens on 127.0.0.1 only (two random ports), behind a random per-run URL.
  - The top-right panel shows frame rate, unit update rate and latency.
