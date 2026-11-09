# deckgl-bench — QWebChannel / deck.gl throughput spike

Throwaway spike, **not** part of the EarthView build. It answers one question: can a C++
simulation push N moving entities into deck.gl inside `QWebEngineView` fast enough?

It compares three C++ → JS transports:

| mode     | transport                                                        |
|----------|------------------------------------------------------------------|
| `json`   | QWebChannel signal with a `QVariantList` of floats (naive)        |
| `b64`    | QWebChannel signal with base64 of the binary frame                |
| `scheme` | JS `XMLHttpRequest('sim://frame')` served raw by a `QWebEngineUrlSchemeHandler` |

Round 2 adds a MapLibre basemap from local tiles and realistic layers (heading icons, labels,
track trails), on-map controls, click-to-select, and a synthetic picking benchmark.

## Setup

1. Qt 6 MSVC kit with the **Qt WebEngine**, **Qt WebChannel** and **Qt SQL** components
   (normally `C:/Qt/6.11.0/msvc2022_64`). On the dev box only Qt 6.4.2 `msvc2019_64` has
   WebEngine: use `-G "Visual Studio 17 2022"` with it. Set `QT_ASSUME_STDERR_HAS_CONSOLE=1`
   to see the HUD on stdout. WebEngine is not available for MinGW.
2. Vendor the web libraries (offline, no CDN at runtime; all gitignored) into `web/`:
   - `deck.gl.min.js` ← `https://unpkg.com/deck.gl@9.1.14/dist.min.js`
   - `maplibre-gl.js` ← `https://unpkg.com/maplibre-gl@4.7.1/dist/maplibre-gl.js`
   - `maplibre-gl.css` ← `https://unpkg.com/maplibre-gl@4.7.1/dist/maplibre-gl.css`

## Build

```
cmake -S spikes/deckgl-bench -B spikes/deckgl-bench/build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build spikes/deckgl-bench/build --config Release
C:/Qt/6.11.0/msvc2022_64/bin/windeployqt.exe spikes/deckgl-bench/build/Release/deckgl_bench.exe
```

## Run

```
deckgl_bench <N> <Hz> <mode> [options]
deckgl_bench 5000 30 scheme
deckgl_bench 20000 30 scheme --tiles D:/Data/osm-2020-02-10-v3.11_asia_pakistan.mbtiles --autopick
```

| option | default | meaning |
|---|---|---|
| `--tiles <spec>` | `synth` | `none` (round-1 setup, no basemap), `synth` (generated PNG grid tiles), an XYZ directory `{z}/{x}/{y}.<png\|jpg\|webp\|pbf>`, or an `.mbtiles` file (raster, or OpenMapTiles vector) |
| `--labels <n>` | 1000 | first n entities get a `TextLayer` label (decluttered with `CollisionFilterExtension`) |
| `--trails <n>` | 1000 | first n entities get a `PathLayer` track trail |
| `--trail-len <k>` | 60 | points per trail, sampled 2×/s (60 → 30 s of track) |
| `--scatter` | off | draw entities as dots instead of heading-rotated `IconLayer` arrows |
| `--autopick` | off | each frame, `pickObject()` at a moving screen point; HUD shows pick p50/p95 |

With an `.mbtiles` file, entities spawn inside its `bounds` and the view opens at its
`center`. Vector tiles use a built-in dark OpenMapTiles style with fills and lines only (no
map text: symbol layers would need offline glyph files). Gzipped tiles are unpacked in JS.

On the map: zoom/rotate/pitch buttons and scale bar (MapLibre controls), a layer toggle panel,
hover tooltips, and click an entity to select it (info panel with live position and Follow).

The HUD's third line comes from C++: `tiles/s`, average and max tile load time on the GUI
thread, and `miss` (tiles not in the source).

The HUD (top-left, also printed to stdout once per second) shows:
`fps`, `recv/s` (frames ingested), latency `p50/p95/p99` (C++ frame creation → deck.gl
render, same-machine wall clock), and `dropped` (sequence gaps; `scheme` and `b64` only —
`scheme` intentionally skips stale frames, so drops there mean "the renderer is slower
than the sim"). Since round 2, `scheme` ignores a re-read of an unchanged frame, so `recv/s`
no longer exceeds the sim rate and latency is no longer measured from stale frames; round-1
`scheme` latency at 30 Hz is inflated by this.

Round-2 matrix: with `--tiles <real mbtiles>` and default layers, N ∈ {5k, 20k, 50k} ×
Hz ∈ {30, 60} × mode `scheme` (and `b64` up to 20k), with and without `--autopick`, while
panning and zooming by hand. Compare against `--tiles none --labels 0 --trails 0 --scatter`
(the round-1 setup).

Matrix: N ∈ {5k, 20k, 50k, 100k} × Hz ∈ {30, 60} × mode ∈ {json, b64, scheme}.
Also record CPU % and working set of `deckgl_bench.exe` and `QtWebEngineProcess.exe`, and
hover entities during the run to check picking stays smooth.

## Pass criteria

At the real target entity count in the best mode: fps ≥ 55, p95 latency ≤ 2 sim ticks,
near-zero drops, smooth hover/highlight.

## Results

Run 2026-10-18 on the dev box (Qt 6.4.2 msvc2019_64 / WebEngine, deck.gl 9.1.14, one
`ScatterplotLayer`, no basemap). Each run: 14 s, first 4 HUD samples discarded, columns are the
median of the remaining 1 s samples (`dropped` is the final cumulative count). CPU is % of one
core averaged over 5 s (`app` = `deckgl_bench.exe`, `web` = `QtWebEngineProcess.exe`); RSS is the
sum of both. `stalled` = the HUD never produced a sample (page frozen).

| N | Hz | mode | fps | recv/s | p50 ms | p95 ms | dropped | app CPU % | web CPU % | RSS MB |
|---|---|---|---|---|---|---|---|---|---|---|
| 5000 | 30 | json | 60 | 30 | 25 | 30 | 0 | 88 | 35 | 370 |
| 5000 | 30 | b64 | 60 | 30 | 11 | 19 | 0 | 45 | 38 | 385 |
| 5000 | 30 | scheme | 60 | 60 | 30 | 45 | 4 | 61 | 54 | 362 |
| 5000 | 60 | json | 24 | 58 | 22 | 24 | 0 | 112 | 32 | 363 |
| 5000 | 60 | b64 | 60 | 62 | 11 | 17 | 0 | 97 | 30 | 404 |
| 5000 | 60 | scheme | 60 | 60 | 23 | 30 | 52 | 59 | 49 | 378 |
| 20000 | 30 | json | 6 | 14 | 88 | 92 | 0 | 103 | 31 | 378 |
| 20000 | 30 | b64 | 60 | 30 | 16 | 22 | 0 | 53 | 51 | 386 |
| 20000 | 30 | scheme | 60 | 60 | 30 | 45 | 6 | 66 | 55 | 401 |
| 20000 | 60 | json | 5 | 14 | 91 | 100 | 0 | 100 | 32 | 400 |
| 20000 | 60 | b64 | 60 | 63 | 14 | 21 | 0 | 59 | 66 | 373 |
| 20000 | 60 | scheme | 60 | 60 | 21 | 30 | 54 | 64 | 51 | 402 |
| 50000 | 30 | json | 1 | 7 | 874 | 874 | 0 | 100 | 39 | 383 |
| 50000 | 30 | b64 | 60 | 30 | 27 | 32 | 0 | 60 | 77 | 382 |
| 50000 | 30 | scheme | 60 | 59 | 29 | 42 | 4 | 84 | 57 | 379 |
| 50000 | 60 | json | stalled | stalled | stalled | stalled | 0 | 100 | 3 | 471 |
| 50000 | 60 | b64 | 52 | 63 | 39 | 54 | 0 | 97 | 121 | 400 |
| 50000 | 60 | scheme | 60 | 59 | 22 | 29 | 61 | 76 | 58 | 411 |
| 100000 | 30 | json | stalled | stalled | stalled | stalled | 0 | 100 | 22 | 531 |
| 100000 | 30 | b64 | 59 | 30 | 44 | 46 | 0 | 79 | 113 | 404 |
| 100000 | 30 | scheme | 60 | 59 | 29 | 45 | 4 | 80 | 68 | 365 |
| 100000 | 60 | json | stalled | stalled | stalled | stalled | 0 | 101 | 3 | 481 |
| 100000 | 60 | b64 | 39 | 157 | 2679 | 3216 | 0 | 104 | 114 | 750 |
| 100000 | 60 | scheme | 60 | 58 | 20 | 27 | 76 | 72 | 67 | 409 |

Findings:

- **`json` (QVariantList) is unusable** beyond ~5k: 20k already drops to 5-6 fps with ~90 ms
  latency, 50k+ freezes the page. The app process pegs a core serialising.
- **`b64` is the best mode up to 20k at either rate** (60 fps, p95 <= 22 ms, zero drops). It holds
  60 fps / zero drops at 100k @ 30 Hz (p95 46 ms) but degrades at 50k @ 60 Hz (52 fps) and
  collapses at 100k @ 60 Hz (39 fps, multi-second latency from queued frames, 750 MB RSS).
- **`scheme` is the only mode that stays at 60 fps everywhere**, including 100k @ 60 Hz (p95
  27 ms). Its drops are intentional (stale frames skipped; ~76 at 100k @ 60 Hz, i.e. the
  renderer is slower than the sim for part of the run). At 30 Hz the poll-per-animation-frame
  design adds latency (p95 ~45 ms, vs ~20 ms for `b64` at low N), because JS picks up a frame
  up to one rAF after it is produced.
- Web-process CPU grows with N for `b64` (decode on the render side, 113-121% at 50k-100k),
  whereas `scheme` stays at 50-70%.

Against the pass criteria (fps >= 55, p95 <= 2 sim ticks, near-zero drops): at 30 Hz (2 ticks =
67 ms) both `b64` and `scheme` pass up to 100k. At 60 Hz (2 ticks = 33 ms) `scheme` passes up to
100k in latency and fps, but with frame skipping (not "near-zero drops"); `b64` passes up to 20k
only. Hover/picking smoothness was **not** checked (needs a human at the keyboard).

Caveats: single run per cell, no warm repeat; the latency clock is JS `Date.now()` vs C++ wall
clock on the same machine; the first `scheme` HUD rows include startup drops (4-6 at 30 Hz).

## Limitations

- Positions are float32 lon/lat — a throughput test, not a precision test.
- No basemap, so tile streaming load is not measured (add MapLibre + local tiles next).
- Only one layer type; icons, labels and paths cost more.
