# deckgl-bench — QWebChannel / deck.gl throughput spike

Throwaway spike, **not** part of the EarthView build. It answers one question: can a C++
simulation push N moving entities into deck.gl inside `QWebEngineView` fast enough?

It compares three C++ → JS transports:

| mode     | transport                                                        |
|----------|------------------------------------------------------------------|
| `json`   | QWebChannel signal with a `QVariantList` of floats (naive)        |
| `b64`    | QWebChannel signal with base64 of the binary frame                |
| `scheme` | JS `fetch('sim://frame')` served raw by a `QWebEngineUrlSchemeHandler` |

## Setup

1. Qt 6 MSVC kit with the **Qt WebEngine** and **Qt WebChannel** components
   (`C:/Qt/6.11.0/msvc2022_64`). WebEngine is not available for MinGW.
2. Vendor deck.gl (offline, no CDN at runtime). Download the UMD bundle of a pinned version,
   e.g. from `https://unpkg.com/deck.gl@9.1.14/dist.min.js`, and save it as
   `web/deck.gl.min.js`. It is gitignored.

## Build

```
cmake -S spikes/deckgl-bench -B spikes/deckgl-bench/build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build spikes/deckgl-bench/build --config Release
C:/Qt/6.11.0/msvc2022_64/bin/windeployqt.exe spikes/deckgl-bench/build/Release/deckgl_bench.exe
```

## Run

```
deckgl_bench <N> <Hz> <mode>
deckgl_bench 5000 30 scheme
```

The HUD (top-left, also printed to stdout once per second) shows:
`fps`, `recv/s` (frames ingested), latency `p50/p95/p99` (C++ frame creation → deck.gl
render, same-machine wall clock), and `dropped` (sequence gaps; `scheme` and `b64` only —
`scheme` intentionally skips stale frames, so drops there mean "the renderer is slower
than the sim").

Matrix: N ∈ {5k, 20k, 50k, 100k} × Hz ∈ {30, 60} × mode ∈ {json, b64, scheme}.
Also record CPU % and working set of `deckgl_bench.exe` and `QtWebEngineProcess.exe`, and
hover entities during the run to check picking stays smooth.

## Pass criteria

At the real target entity count in the best mode: fps ≥ 55, p95 latency ≤ 2 sim ticks,
near-zero drops, smooth hover/highlight.

## Limitations

- Positions are float32 lon/lat — a throughput test, not a precision test.
- No basemap, so tile streaming load is not measured (add MapLibre + local tiles next).
- Only one layer type; icons, labels and paths cost more.
