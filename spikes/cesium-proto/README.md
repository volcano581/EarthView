# cesium-proto — CesiumJS tactical map in Qt WebEngine

Throwaway prototype, **not** part of the EarthView build. It answers: can an open-source web
globe (CesiumJS) inside a Qt desktop app give a Google-Maps-like flat tactical map, an
occasional 3D view on real DTED terrain, and smooth display of simulated ground units, fully
offline?

## What it shows

- **Flat 2D map** (Web Mercator) with an offline base map drawn from local data: land/sea and
  country borders from the borders shapefile, **relief shading computed from DTED**, and city
  and town names. With `--mbtiles`, an OpenMapTiles vector (or raster) file is drawn on top.
- **3D view** on SRTM DTED2 terrain (`CustomHeightmapTerrainProvider` fed by the host).
- **300 simulated ground units** (friendly/hostile, five types, simplified MIL-STD-2525 frames)
  moving on the terrain; vehicles slow uphill. The page dead-reckons them between frames
  (position + velocity x elapsed time, as DIS does), so motion is smooth at any frame rate.
- **Google-Maps-style controls**: compass (click = north, arrows rotate 45°), 3D/2D tilt
  button, locate, zoom, a draggable figure for a ground-level view (drag to look, WASD to walk),
  map-style tile with an overlay panel, scale bar (km / nm), cursor coordinates with DTED
  elevation, unit info card with Follow / Ground view, night theme. All colours are CSS
  variables in `web/app.css`.

## How it is built

```
QWebEngineView ──http://127.0.0.1:<port>/<token>/──► HttpServer (GUI thread)
   page: web/index.html, app.js, CesiumJS            ├─ static: web/ (page + cesium/)
                                                     ├─ meta.json, places.json, elevation
                                                     ├─ terrain/  base/  hillshade/  mbtiles/
                                                     │    (generated on a small low-priority pool)
   long poll ──http://127.0.0.1:<port2>/<token>/──►  frame server: sim/frame?after=N (binary)
```

| File | Role |
|---|---|
| `main.cpp` | options, routes, simulation tick, window, test hooks |
| `HttpServer.*` | minimal GET-only HTTP/1.1 server, 127.0.0.1 only, per-run URL token |
| `Dted.*` | memory-mapped DTED cells, bilinear heights, Cesium heightmap and relief tiles |
| `BaseMap.*` | borders base-map tiles (QPainter) and MBTiles reader |
| `Sim.*` | stand-in for the CGF: units, binary frames (double lon/lat, float h/heading/speed) |
| `web/app.js` | Cesium setup, layers, units, controls, ground view |
| `web/mvt-worker.js` | OpenMapTiles vector-tile decoder + renderer in web workers |
| `web/polyfills.js` | ES2023 built-ins CesiumJS needs on older Qt WebEngine (see findings) |
| `web/bench.html`, `web/bench.js`, `bench/run-bench.ps1` | deck.gl vs CesiumJS benchmark (needs `web/vendor/`: deck.gl and MapLibre, git-ignored) |
| `package.ps1`, `package/` | builds the offline folder (`dist/`), with its run script and readme |

## Build and run

Needs a Qt kit **with Qt WebEngine** (MSVC). On this machine that is Qt 6.4.2 today; the
6.11.1 WebEngine extension is installed but the matching 6.11.1 base kit is not yet.

```
cmake -S spikes/cesium-proto -B spikes/cesium-proto/build-642 -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.4.2/msvc2019_64
cmake --build spikes/cesium-proto/build-642 --config Release
```

For Qt 6.11.1 (once its base kit, Qt WebChannel and Qt Positioning are installed), with the
CMake bundled with VS 2026:

```
cmake -S spikes/cesium-proto -B spikes/cesium-proto/build-611 -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64
cmake --build spikes/cesium-proto/build-611 --config Release
```

Vendor CesiumJS once (git-ignored): `npm pack cesium@1.146.0`, then copy the tarball's
`package/Build/Cesium` folder to `web/cesium`. Then run `build-642/Release/cesium_proto.exe`
(run `windeployqt` on it first) or build the offline package:

```
powershell -File spikes/cesium-proto/package.ps1
```

→ `dist/CesiumTacticalMapDemo` (~1.15 GB, mostly DTED): exe, Qt runtime incl. WebEngine,
app-local MSVC runtime, `web/`, `data/`, `Run-Demo.cmd`, `README.txt`. Copy the folder to any
Windows 10/11 x64 machine and double-click `Run-Demo.cmd`. Options: `cesium_proto --help`.

Test hooks: `--grab shot_%1.png --grab-every 4` saves window screenshots;
`--run-js "<code>"` runs a script in the page on the same timer and prints the result.
`--no-window` serves only (open the printed URL in a browser).

## Results (2026-10-20, RTX 3050 Ti laptop, Qt 6.4.2, CesiumJS 1.146.0, 300 units at 20 Hz)

Measured in the Qt window with a scripted tour (2D → 3D tilt → rotate → zoom → 2D → 3D →
select → ground view → walk → turn → exit):

| View | fps | unit updates/s | sim→page latency p50 / p95 |
|---|---|---|---|
| 2D map | 54–60 | 18–20 | 2–4 / 3–6 ms |
| 3D tilted | 54–56 | 18 | 9–12 / 11–15 ms |
| Ground view, tiles streaming | 35–44 | 12–14 | ~90–105 / ~120 ms |
| Ground view, tiles loaded | 55 | 18 | 8 / 14 ms |

The packaged folder, run with `PATH` reduced to the Windows system folders, behaves the same
(60 fps, 20 updates/s, 2–3 ms) and fetches only from its own two `127.0.0.1` ports.

## Findings

1. **CesiumJS needs a recent Chromium.** CesiumJS 1.146 calls `Array.prototype.toSorted`
   (Chromium 110+); Qt 6.4's WebEngine is Chromium 102 and fails to start Cesium without
   `web/polyfills.js`. Qt 6.11's WebEngine is far newer — another reason to ship on 6.11.
2. **Keep heavy work off the Qt GUI thread.** In Qt WebEngine it is also Chromium's browser
   thread: generating tiles there dropped the page to 9–12 fps at start-up.
3. **Bound the worker pool.** Rendering tiles on all 20 cores starved Chromium's own threads
   (2D fell to 24–41 fps); a 3-thread low-priority pool keeps 55–60 fps.
4. **Give the live data stream its own connection pool.** Chromium allows 6 connections per
   server; Cesium's tile requests queued the unit long poll behind them (1–6 updates/s in
   ground view) until frames moved to a second port. A production design would use a
   WebSocket or QWebChannel for the stream.
5. Long-polling binary frames gives 2–4 ms latency in 2D (round 1's per-animation-frame pull:
   ~40 ms).

## Engine benchmark: deck.gl vs CesiumJS

`web/bench.html` + `web/bench.js` + `bench/run-bench.ps1` run the same workload through both
engines in the same Qt WebEngine window, to compare them like for like.

**Identical for both engines**

- Host process, 20 Hz binary unit frames (double lon/lat, float heading) and the same
  long-poll connection; `--flat-sim` so the host does no terrain lookups.
- 1600x900 window; both engines draw 1539x900 canvases (CSS pixels; display scaling is forced
  off for deck.gl `useDevicePixels:false`, MapLibre `pixelRatio:1`, Cesium default).
- The same raster base map (`base/{z}/{x}/{y}.png` from the host), and the same scripted
  camera: pan on a 4 km circle, zoom between 0.57x and 1.74x of 12 m/px, rotate +-30 degrees,
  one 10 s cycle. In the tilted view the camera has the same pitch (55 degrees from vertical),
  the same field of view and distance. `?verify=1` checks this: five fixed ground points land on
  the same screen pixel in both engines (2D: 0.0 px difference; tilted: at most 1.2 px).
- Layers: heading-rotated icons (16 px), optional 1000 text labels and 1000 trails of 60 points
  sampled at 2 Hz. No dead reckoning in either: positions change when a frame arrives.
- Metrics from the page: `requestAnimationFrame` rate and spread, JS time to apply an update
  and to set the camera, sim-to-screen latency, picking cost (one pick per frame for 6 s);
  from the host: CPU of the app (includes the in-process GPU thread) and of the Chromium
  renderer, memory, sim step time.

**Idiomatic, not identical, code**

- deck.gl: `IconLayer`/`PathLayer` with binary attributes (`Float64Array` positions, no
  copy), `TextLayer` with an accessor, MapLibre base map, `MapboxOverlay` not interleaved.
- CesiumJS: `BillboardCollection` / `LabelCollection` / `PolylineCollection` (the fast
  primitive APIs, not the `Entity` API), `CesiumWidget`, per-object property setters. Sky,
  sun, fog and FXAA are off in both.

**Gotchas found while building it**

- deck.gl `IconLayer` ignores a constant `getIcon: 'arrow'` and draws nothing; it must be a
  function. Cesium's `Billboard.rotation` is counter-clockwise, so a heading needs `-heading`.
- `?verify=1` with `--grab` screenshots 8 units with headings 0..315 to check orientations.

**Run it** (laptops: plug in AC power first)

```
powershell -File spikes/cesium-proto/bench/run-bench.ps1
```

Full matrix is 26 runs (about 25 minutes): N = 300, 5k, 20k, 50k x {2D icons, 2D icons + labels
+ trails, tilted icons}, plus Cesium-only tilted runs on real DTED terrain at 300 and 5k. It
refuses to run on battery, uses `--force-high-performance-gpu`, retries a run that changes
GPU, draws no frames, or loses AC power, and writes `bench/results/<time>/summary.md`.
`-Quick` gives a 2-minute smoke run; `-Ns`, `-Scenarios`, `-OutDir` redo single cells.

**Status of results:** the first full run (2026-10-22) is **invalid**: the laptop was on
battery (10%), Windows moved Chromium from the NVIDIA to the Intel GPU part-way through, and one
cell drew no frames. No numbers are recorded here until it is rerun on AC power.

## Not verified / limitations

- **Qt 6.11 build**: blocked until the 6.11.1 base kit is installed (only its WebEngine is).
- **Vector OSM map** (`--mbtiles`, `web/mvt-worker.js`): no `.mbtiles` on this machine; only the
  decoder was tested (against a hand-encoded tile). The renderer and style are untested.
- Controls were exercised by script (calling the same functions the buttons call) and by
  screenshots, not by real mouse clicks and drags, except in the browser pane for 2D.
- Terrain heights are DTED (EGM96 MSL) used as ellipsoid heights, consistently for terrain,
  units and camera; real positions would need the geoid (~47 m near Islamabad, D-021).
- Place names are not decluttered; unit symbols are simplified frames, not full MIL-STD-2525
  (e.g. via the MIT `milsymbol` library); there is no measure tool yet.
- Rotation and relief/tile generation were not tested with more than 300 units or on a
  machine without a discrete GPU.
