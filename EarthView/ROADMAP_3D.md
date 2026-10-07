# EarthView 3D Renderer — Roadmap for Agent Handoff

This document is written to be handed to cloud AI coding agents (Claude Code on the web /
`claude --remote`), one milestone task at a time. Each task is self-contained: an agent
should be able to read **this file + the files listed in the task** and finish it in a single
session, producing one branch / one PR.

---

## 0. Ground rules for every agent (paste into each task prompt)

1. **Do not modify the 2D path behaviour.** Mercator and Orthographic modes (`Camera`,
   `TileRenderer`, `VectorTileRenderer`, `BorderRenderer`, `GridRenderer`, `CityRenderer`,
   `TextRenderer`) are integrated in Doctrine and must keep working identically. New 3D work
   lives in new files under `scene3d/`. Touching existing files is allowed only for small,
   explicit hook points named in the task.
2. **Do not extend the existing pseudo-3D path** (`Camera::terrainMercatorToScreen`,
   `stealthViewMercatorToScreen`, `shaders/terrain_height.vert`, `colored_terrain.vert`,
   `terrain_line.vert`). It is being replaced, not fixed. It is deleted in milestone M9.
3. **Coordinates:** world space is WGS84 → ECEF in `double`. Anything uploaded to the GPU is
   relative-to-eye (RTE) `float` (subtract camera ECEF position in double on CPU first, or
   per-tile origin + camera offset). Never put absolute ECEF into a float.
4. **Depth:** reversed-Z (`glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)`, clear depth 0,
   `GL_GREATER`), infinite far plane. OpenGL 4.5 core path; for the 3.3 fallback use
   logarithmic depth in the vertex shader instead. Detect via `OpenGLRuntime`.
5. **Math library:** add GLM (header-only, vendored under `third_party/glm` or via
   FetchContent) and use `glm::dvec3/dmat4` for CPU world math. Qt `QMatrix4x4` is float-only
   — do not use it for world transforms.
6. **No build output in commits.** `build/` and `x64/` are currently tracked in git; never
   commit changes in them (see task M0.1).
7. **Unit tests:** every math/LOD module gets tests in `tests/` (Qt Test). Rendering code
   that cannot be tested headlessly must at least compile and must include a debug toggle.
8. **Each PR includes:** summary, files changed, how to verify manually on Windows, and any
   known limitations. Keep PRs under ~1500 changed lines; split if larger.
9. Code style: match the existing code (C++17, Qt naming, `m_` members, `#pragma once` +
   include guards, Doxygen `@brief` on public classes).

### Environment caveat (important)
Cloud agents run in **Linux containers without a GPU**. The project currently hard-codes
Windows Qt paths (`C:/Qt/6.11.0/llvm-mingw_64`). Consequences:

- Agents **can** write code, compile it on Linux against Qt 6 (`apt install qt6-base-dev
  libqt6opengl6-dev libgl-dev` or `aqtinstall`), and run unit tests (math, LOD selection,
  tile math, parsers) — task M0.2 makes this possible.
- Agents **cannot** visually verify rendering. Optionally they can run under
  Mesa llvmpipe (`QT_QPA_PLATFORM=offscreen`, `EARTHVIEW_FORCE_SOFTWARE_OPENGL=1`) and
  grab a framebuffer PNG for smoke tests (task M0.3).
- **You (human) must do the visual check on Windows** after each rendering milestone before
  merging. Budget for that review — it is the main bottleneck, not the agent.

---

## 1. Milestone overview

| #  | Milestone                                   | Depends on | Size | Visual check | Status |
| -- | ------------------------------------------- | ---------- | ---- | ------------ | ------ |
| M0 | Repo hygiene, cross-platform build, CI, tests | —        | S    | no  | **Done** (M0.1 `2cda9e8`, M0.2+M0.3 merged `5682f87`) |
| M1 | Geodesy + Camera3D + RTE math               | M0         | M    | no  | |
| M2 | Scene3D skeleton wired into MapWidget       | M1         | M    | yes | |
| M3 | Globe quadtree tiling + frustum/SSE LOD     | M1         | L    | yes | |
| M4 | DEM-displaced terrain (skirts, normals)     | M3         | L    | yes | |
| M5 | Imagery draping (TMS/MBTiles on terrain)    | M4         | M    | yes | |
| M6 | Vectors, borders, grid, cities, labels in 3D| M5         | L    | yes | |
| M7 | Lighting, sky/atmosphere, fog               | M4         | M    | yes | |
| M8 | Entities: glTF models, picking              | M2, M4     | L    | yes | |
| M9 | Stealth-view controller + cleanup of old 3D | M6, M8     | M    | yes | |
| M10| Performance pass + Doctrine integration API | all        | M    | yes | |

S ≈ 1 agent session, M ≈ 2–3, L ≈ 4–6 sessions. M7 and M8 can run in parallel with M5/M6.

---

## 2. Tasks

Agents: trust the Status column / DONE markers; do not redo finished tasks. Update the
marker for your task in the same PR that completes it.

### M0 — Foundation

**M0.1 Repo hygiene** — DONE (`2cda9e8`). Build output is untracked; ignore rules live in the repo-root `.gitignore` (`[Bb]uild/`, `x64/`, `[Oo]ut/`, `*.user`, `.vs/`).
- Add `build/`, `x64/`, `*.user` to `.gitignore`; `git rm -r --cached build x64`.
- Acceptance: `git status` clean after a fresh build.

**M0.2 Cross-platform CMake + tests** — DONE (merged in `5682f87`)
- Make the Qt prefix logic in `CMakeLists.txt` optional (only apply Windows paths if they exist — already partly true; ensure Linux `find_package(Qt6)` works).
- Guard `windeployqt` with `if(WIN32)`.
- Add `tests/` with Qt Test + `enable_testing()`; first test: `MercatorProjection` round-trips.
- Add `.github/workflows/ci.yml`: Ubuntu, install Qt6, build, `ctest`.
- Acceptance: `cmake -B out && cmake --build out && ctest --test-dir out` passes on Linux.

**M0.3 Offscreen smoke-render harness** — DONE (`EarthView/tools/earthview_snapshot.cpp`, merged in `5682f87`)
- Small executable `earthview_snapshot` that creates a `QOffscreenSurface` + FBO, renders one frame of a given mode/camera, writes PNG. Used by later milestones for CI artifacts.

### M1 — Geodesy and camera math
Files: new `scene3d/Geodesy.h/.cpp`, `scene3d/Camera3D.h/.cpp`, `third_party/glm`.
- `Geodesy`: WGS84 constants; `geodeticToEcef(lat,lon,h)`, `ecefToGeodetic` (Bowring or
  iterative), `enuFrame(lat,lon)` → `dmat3`, ellipsoid ray intersection.
- `Camera3D`: `dvec3 position` (ECEF), `dquat orientation`, fovY, aspect, near; methods
  `viewMatrixRTE()` (float, rotation only), `projectionReversedZ()`, `frustumPlanes()`
  (double), `screenRay(px,py)`, `setFromGeodetic(lat,lon,h,heading,pitch,roll)`,
  `lookAt(target)`.
- Tests: ECEF round-trip < 1 mm, ENU orthonormal, ray-ellipsoid hits, frustum contains/excludes known points.
- Acceptance: tests pass; no rendering code yet.

### M2 — Scene3D skeleton
Files: `scene3d/Scene3D.h/.cpp`, `scene3d/RenderContext.h`; hook in `MapWidget.cpp`, `Camera.h` (add `ProjectionMode::Globe3D` only).
- `Scene3D` owns `Camera3D`, a list of `ILayer3D { update(ctx); render(ctx); }`.
- `MapWidget::paintGL`: if mode == Globe3D → `m_scene3d->render()` and return; else existing path untouched.
- First layer: ellipsoid drawn as a tessellated sphere with a lat/lon checker shader (RTE). Reversed-Z setup + 3.3 log-depth fallback.
- Input: orbit camera (drag rotates around globe, wheel zooms toward cursor ray hit).
- MainWindow: menu/toolbar action to select Globe3D.
- Acceptance: switching modes works both ways; 2D unchanged; visual: globe visible, no z-fighting zooming from 20,000 km to 100 m.

### M3 — Quadtree tiling and LOD
Files: `scene3d/TileQuadtree.h/.cpp`, `scene3d/TileKey.h`.
- Tiling scheme: **Web Mercator tile keys** (so imagery/MBTiles keys map 1:1) with polar caps filled by a separate cap mesh. (Alternative geographic scheme needs reprojection — avoid.)
- Per tile: bounding sphere/OBB in ECEF (sample corners + midpoints), geometric error = tile size / grid res.
- Selection: frustum cull + horizon cull (ellipsoid occlusion) + screen-space error < `maxSSE` (default 2 px). Output a sorted list of tile keys, nearest first; budget max tiles/frame.
- Request queue interface to loaders: prioritised by SSE; cancel when no longer needed.
- Tests: selection from a fixed camera yields expected levels; no tiles behind horizon.
- Acceptance: debug layer draws tile bounds coloured by level.

### M4 — Terrain
Files: `scene3d/TerrainLayer3D.h/.cpp`, `scene3d/shaders/terrain3d.vert/.frag`; read-only use of `DemLoader`.
- Mesh per tile: shared 65×65 grid index buffer; vertex = UV; vertex shader samples height texture, computes ECEF via tile-local origin (RTE: `tileOriginRTE + localOffset`) — precompute per-vertex local ENU offsets on CPU in double, upload float.
- Skirts on all 4 edges to hide cracks. Normals from height texture (central differences) in shader.
- Missing DEM → ellipsoid height 0; parent tile heights used while child loads (no holes).
- Vertical exaggeration uniform (reuse existing UI slider value).
- Expose `heightAt(lat,lon)` (CPU, from loaded tiles) for camera collision/clamping.
- Acceptance: mountains correct scale vs known elevation (e.g. Everest ≈ 8848 m); no cracks; no popping holes.

### M5 — Imagery draping
Files: `scene3d/ImageryLayer3D` or integrate in terrain fragment shader; read-only `TMSLoader`, `TextureManager`.
- Because tiles are Mercator-keyed, imagery tile z/x/y matches terrain tile → direct UV. When imagery is coarser, use ancestor texture with UV scale/offset.
- Support multiple imagery layers with opacity.
- Acceptance: imagery aligned with terrain at coastlines; no seams; MBTiles + URL sources both work.

### M6 — Vector overlays and labels
Files: `scene3d/VectorLayer3D`, `scene3d/LabelLayer3D`; refactor existing renderers only to expose geometry (lat/lon / Mercator vertex arrays), not to change 2D drawing.
- Lines (borders, grid, vector tiles): convert to ECEF per tile chunk with RTE origin; clamp-to-ground in vertex shader by sampling terrain height texture, or render with depth bias. Screen-space width via geometry expansion.
- Polygons (fills): drape as decal (render into the imagery texture per tile) — simplest robust approach.
- Cities + labels: billboards placed at ECEF + terrain height, projected in shader; reuse `TextRenderer` atlas; declutter in screen space; fade by distance.
- Acceptance: borders follow terrain without z-fighting; labels stable while orbiting.

### M7 — Lighting and atmosphere
- Directional sun (configurable time/date → sun direction), Lambert + ambient on terrain.
- Sky: atmosphere scattering approximation (Sean O'Neil / Bruneton-lite) for sky dome + ground haze; distance fog.
- Acceptance: horizon looks correct from 2 m to orbit; toggleable.

### M8 — Entities
Files: `scene3d/EntityLayer3D`, `scene3d/GltfModel` (use **tinygltf** or **cgltf**, header-only).
- Entity = id, geodetic position, heading/pitch/roll, model handle, scale/min-pixel-size.
- Model matrix from ENU frame in double → RTE float.
- Instancing for repeated models; LOD by screen size (icon/billboard when tiny).
- Picking: render IDs to an R32UI target, or ray-cast against bounding spheres + terrain.
- Public API: `addEntity/updateEntity/removeEntity` (thread-safe queue) — this is what Doctrine will feed.
- Acceptance: 1,000 moving entities at 60 fps on a mid GPU; click selects entity.

### M9 — Stealth view
- Camera controllers: `OrbitController`, `FreeFlyController` (WASD + mouse look), `TetherController` (attached to entity with offset, smooth follow), `FirstPersonController` (eye at entity, uses its attitude).
- Terrain collision (`heightAt` + min clearance). Smooth transitions between controllers.
- HUD overlay: heading/pitch/alt, entity name.
- **Remove** old pseudo-3D: terrain3D/stealth code paths in `Camera`, the `isStealthViewEnabled()` branches in the 8 renderers, old terrain shaders. Map existing UI actions to new controllers.
- Acceptance: VR-Forces-like stealth: attach to entity, look around, fly free; 2D modes still pass tests.

### M10 — Performance and integration
- `FrameProfiler` integration for each layer; GPU timer queries.
- Background mesh building (worker thread → upload on GL thread with PBO/persistent mapping via `StreamingBuffer`).
- Memory budgets / LRU for tiles.
- Document the Doctrine-facing API in `DEVELOPERS_GUIDE.md` and `RENDERING_PIPELINE.md`.
- Acceptance: steady 60 fps at 1440p over mountainous terrain with imagery + 1k entities.

---

## 3. How to run this with Claude cloud agents

1. **Push the repo to GitHub** (cloud sessions clone from GitHub). Do M0.1 first, locally,
   so the repo is small.
2. Add a `CLAUDE.md` at repo root containing section 0 of this file (ground rules) plus build
   commands, so every session picks it up automatically.
3. Optionally add `.claude/settings.json` / environment setup script that installs Qt6 + GLM
   in the cloud container (`apt-get install -y qt6-base-dev libqt6opengl6-dev libgl1-mesa-dev cmake ninja-build`).
4. Launch **one task per session**, e.g. from claude.ai/code or `claude --remote`:
   > Implement task **M1** from `EarthView/ROADMAP_3D.md`. Follow section 0 ground rules.
   > Work on branch `3d/m1-geodesy`. Add unit tests, make sure `ctest` passes, open a PR.
5. Parallelism: only run tasks in parallel when their "Depends on" are merged (e.g. M7 ∥ M5,
   M8 ∥ M6). Never run two sessions touching `MapWidget.cpp` at once.
6. **Review loop per PR:** read the diff, run `/code-review` on it, pull branch on Windows,
   build, visually verify the acceptance criterion, then merge. Feed concrete failures back
   into the same session rather than starting fresh (keeps context, saves credits).

### Credit budgeting tips
- The large tasks (M3, M4, M6, M8) are the expensive ones; split them into sub-PRs
  (e.g. M4a mesh+heights, M4b skirts+normals, M4c parent fallback) for cheaper, more reliable sessions.
- Write precise acceptance criteria in the prompt — vague prompts burn credits on rework.
- Use a cheaper model for M0, tests, docs; the strongest model for M3/M4/M6 (LOD + precision are where bugs hide).
- Expect ~25–40 sessions total including fix-up rounds.

---

## 4. Risks

| Risk | Mitigation |
| ---- | ---------- |
| Agents can't see rendering output | M0.3 snapshot harness + human Windows check per milestone |
| Float precision jitter | Enforce RTE rule (section 0.3); add test that camera at 1 m altitude produces stable vertex positions |
| Mercator tiling at poles | Polar cap meshes in M3; acceptable for a defence sim at ±85° |
| Doctrine regression | 2D path untouched until M9; keep Mercator tests in CI |
| Scope creep vs. adopting osgEarth/Cesium | Re-evaluate after M4: if terrain quality/perf is not acceptable, switch to a library rather than continue |
