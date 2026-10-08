# osgEarth 3D / Stealth View — Integration Design

Status: **Draft for review** · Date: 2026-10-08 · Decisions: D-007 (osgEarth), D-008 (MSVC)
Related: [ROADMAP_3D.md](ROADMAP_3D.md) (tasks O0–O11) · [3D_PROGRESS.md](3D_PROGRESS.md)
(status, decisions, issues) · [spikes/osgearth](../spikes/osgearth/README.md) (evaluation
code and results) · [OFFLINE_BUILD.md](../spikes/osgearth/OFFLINE_BUILD.md).

This document describes **how** the osgEarth-based 3D view is built and wired into Doctrine.
The roadmap says **what** is done in which order; this document is the reference that every
O-task must follow. Changes to the design go through a PR to this file plus a decision row
in `3D_PROGRESS.md`.

---

## 1. Goals and non-goals

**Goals**
- A VR-Vantage-style 3D "stealth" view of the Doctrine simulation: geospecific terrain
  (DTED), imagery, overlays, 3D entity models, tethered/first-person/free cameras.
- Runs **in-process** inside Doctrine as an additional window, fed from the existing
  simulation→render snapshot.
- Fully **offline**: no network at runtime or build time on the secure side.
- The existing 2D map views (EarthView in Doctrine, `DoctrineViewport`) stay unchanged.

**Non-goals (for now)**
- Replacing the 2D map with osgEarth.
- Network distribution of entity state (DIS/HLA). The design keeps this possible (§6.6).
- Weather, dynamic lighting of night-vision sensors, sensor-view post effects.

---

## 2. Context and constraints

| Item | Today | Consequence |
| ---- | ----- | ----------- |
| Doctrine + cgf-engine toolchain | Qt 6.11 **llvm-mingw**, MinGW Makefiles | Cannot link MSVC-built osgEarth → move to MSVC (D-008, tasks O1/O2) |
| osgEarth | 3.8.1 via vcpkg, MSVC, verified in the spike | Dependency comes from a pinned vcpkg manifest |
| Doctrine repository | `D:\Source\cgf-engine`, **not under git** | O0 must put it under git/GitHub before agents can work on it |
| Doctrine 2D GIS | copy of EarthView code in `doctrine/src/GIS/` (`DoctineGISMap`) | Unify in O10; not touched by the 3D work |
| Main window central widget | `DoctrineViewport` (own GL 4.5 renderer) | 3D view is a separate window/dock, not a replacement |
| Sim → render boundary | `SimRenderBridge` (mutex, copy-on-publish) at 100 Hz | 3D view becomes a second consumer |
| Entity coordinates | `TransformComp.position` = `glm::vec3` metres, x = east, z = north from a scenario reference point, y = altitude MSL; `orientation` = `glm::quat` | Needs a geodetic scenario origin + conversion (§5) |
| `RenderEntity` | position, colour, icon, frame, unit type, label — **no EntityID, no orientation, no kind** | Snapshot must be extended (§6.2) |
| Target hardware | Windows 10/11 x64, NVIDIA (spike: RTX 3050 Ti, GL 4.6) | GL 4.6 compatibility profile for the osgEarth widget |

---

## 3. Architecture

```
 ┌──────────────────────── Doctrine process (MSVC, Qt msvc2022_64) ─────────────────────────┐
 │                                                                                            │
 │  SimulationThread (100 Hz)                     GUI thread (Qt event loop)                  │
 │  ┌──────────────────────┐   publish()   ┌──────────────────┐                              │
 │  │ Registry / Systems   │──────────────▶│ SimRenderBridge  │◀── consume() ── DoctrineViewport (2D, unchanged)
 │  │ PhysicsSystem::      │               │ (mutex, copy)    │◀── consume() ── DoctineGISMap   (2D, unchanged)
 │  │   buildSnapshot()    │               └──────────────────┘◀── consume() ─┐                │
 │  └──────────────────────┘                                                    │               │
 │                                                                              │               │
 │  ┌──────────────── Doctrine adapter (doctrine/src/view3d/) ───────────────┐  │               │
 │  │ StealthWindow (QMainWindow/dock)                                        │  │               │
 │  │  ├─ SceneSync: snapshot → EntityLayer3D updates (diff by EntityID)      │◀─┘               │
 │  │  ├─ ScenarioGeoReference: local metres ↔ geodetic (double)              │                  │
 │  │  └─ UI: camera mode, entity picker, HUD toggles                         │                  │
 │  └─────────────────────────────────────────────────────────────────────────┘                  │
 │                         │ uses (no Doctrine types cross this line)                           │
 │  ┌──────────────── earthview3d library (EarthView repo) ──────────────────┐                   │
 │  │ GlobeView3D : QOpenGLWidget   ── osgViewer::Viewer (embedded window)   │                   │
 │  │ EntityLayer3D                 ── GeoTransform/ModelNode/LabelNode      │                   │
 │  │ CameraControllers (orbit, tether, first-person, free-fly)              │                   │
 │  │ MapConfig (earth file + data root + cache)                             │                   │
 │  └──────────────────────────────────────────────────────────────────────────┘                  │
 │                         │                                                                    │
 │            osgEarth 3.8 / OSG 3.6.5 / GDAL / PROJ (vcpkg DLLs, osgPlugins-3.6.5)             │
 └────────────────────────────────────────────────────────────────────────────────────────────┘
         ▲ data (read-only, local disk): DTED VRT, raster MBTiles, shapefiles, models, cache
```

### 3.1 Components and ownership

| Component | Repo / location | Responsibility | Depends on |
| --------- | --------------- | -------------- | ---------- |
| `scene3d` (exists, M1) | EarthView `scene3d/` | WGS84 geodesy, ENU frames, camera maths (double) | GLM |
| `earthview3d` (new, O4) | EarthView `earthview3d/` | Qt widget hosting osgEarth, entity layer, camera controllers, map config | Qt, osgEarth, `scene3d` |
| `view3d` adapter (new, O5) | Doctrine `doctrine/src/view3d/` | Bridges Doctrine snapshot → `earthview3d`; scenario geo-reference; window/UI | Doctrine, `earthview3d` |
| Data package | outside git (`Data/…`) | DTED, imagery, overlays, models | — |

Rule: `earthview3d` **never** includes Doctrine headers. Doctrine talks to it only through
the public API in §7. This keeps EarthView reusable and testable on its own (example app).

### 3.2 Threading

- **Simulation thread** only writes the snapshot (`beginWrite()` / `publish()`), unchanged.
- **GUI thread** owns everything osgEarth: the viewer, scene graph, and all node changes.
  `SceneSync` runs on the GUI thread at the start of `GlobeView3D::paintGL()` (or a
  pre-frame callback), calls `SimRenderBridge::consume()`, and applies the diff.
- osgEarth worker threads (tile loading, GDAL) are internal; we never touch the scene graph
  from them.
- Viewer threading model: `SingleThreaded` (required for an embedded Qt widget).
- The 100 Hz snapshot is sampled at render rate (≈60 Hz); no interpolation in phase 1.
  Phase 2 (O11): keep the previous snapshot and interpolate by `simTime` for smooth motion.

### 3.3 OpenGL context

- `GlobeView3D` is its own `QOpenGLWidget` with its own context; **no sharing** with the 2D
  widgets (rule 5 in the roadmap). This isolates osgEarth's GL state from EarthView/Doctrine
  renderers.
- Surface format for this widget: OpenGL 4.6 **compatibility** profile, depth 24, stencil 8,
  4× MSAA (spike-verified). The 2D widgets keep their core-profile formats; set the format
  per widget (`QOpenGLWidget::setFormat`), not globally.
- Embedded-window requirements (all found the hard way in the spike):
  1. `GraphicsWindowEmbedded::setDefaultFboId(defaultFramebufferObject())` every frame —
     Qt recreates the FBO on resize.
  2. `State::setUseVertexAttributeAliasing(true)` and
     `setUseModelViewAndProjectionUniforms(true)` before the first frame — normally done by
     osgEarth's `GL3RealizeOperation`, which never runs for embedded windows.
  3. Never query `MapNode::getTerrain()` before the first frame (null).
  4. Register the home viewpoint with `EarthManipulator::setHomeViewpoint()` and apply the
     initial viewpoint after the first frame; otherwise the camera frames the sky dome.
  5. Viewport/projection in **device pixels** (`width() * devicePixelRatioF()`).
- Multiple 3D windows (O11): one `osgViewer::Viewer` per widget; models shared through the
  OSG object cache; GL objects are per context.

---

## 4. Build, toolchain and dependencies

### 4.1 Toolchain (D-008)
- Visual Studio **2026** (MSVC v145), x64, C++17. Same VS update on developer, CI and the
  offline build machine (binary-cache key; see I-009).
- Qt **6.11 `msvc2022_64`** kit (binary-compatible with VS 2026).
- CMake ≥ 4.x, generator `Visual Studio 18 2026` or Ninja from the VS developer prompt.
- llvm-mingw remains supported for EarthView 2D until O10, so existing users are not broken
  mid-migration. `earthview3d` is only built when osgEarth is found.

### 4.2 vcpkg manifest (Doctrine root, O3)
```json
{
  "name": "doctrine",
  "version-string": "0.1",
  "builtin-baseline": "2cfff9c458d9dcf642e9fa09ba624f9931bb5358",
  "dependencies": [ "osgearth", "glm", "zlib" ]
}
```
- Baseline is shared with `spikes/osgearth/vcpkg.json`; bump both together.
- osgEarth's default features pulled in GDAL, PROJ, GEOS, protobuf, blend2d, curl, sqlite3
  (see `vcpkg depend-info osgearth`). curl stays linked but is unused offline.
- No `FetchContent` for new dependencies (offline rule). Doctrine's current
  `FetchContent(glm)` is replaced by the vcpkg port in O2.

### 4.3 CMake integration
```cmake
find_package(OpenSceneGraph REQUIRED COMPONENTS osgDB osgGA osgUtil osgViewer)
find_package(osgEarth CONFIG REQUIRED)

add_subdirectory(${EARTHVIEW_DIR}/EarthView/earthview3d earthview3d)   # library target earthview3d
target_link_libraries(doctrine PRIVATE earthview3d)
```
- How Doctrine obtains EarthView sources (submodule vs. vcpkg overlay port) is decided in
  O10; until then O5 uses a git submodule pinned to an EarthView commit.
- `earthview3d` exports a CMake target with include dirs; it links osgEarth `PUBLIC` only if
  public headers expose osgEarth types (they should not — §7 uses Qt/STL/GLM types).

### 4.4 CI
- Linux job (existing): builds non-osgEarth parts and unit tests.
- Windows job (new, O1/O2): MSVC + Qt via `install-qt-action`, vcpkg with
  `VCPKG_BINARY_SOURCES=clear;x-gha,readwrite` (GitHub Actions cache) so osgEarth builds
  once, then restores in minutes.
- Smoke render on CI is not possible for osgEarth (no GPU); visual checks are human.

---

## 5. Coordinate systems and conversion

### 5.1 Frames

| Frame | Where | Units / type |
| ----- | ----- | ------------ |
| **Sim local** | Doctrine `TransformComp` | metres, `float`; x = east, z = north, y = altitude MSL; origin = scenario reference |
| **Geodetic** | osgEarth `GeoPoint` (WGS84 geographic SRS) | lat/lon degrees, height metres, `double` |
| **ECEF** | osgEarth world space, `scene3d::Geodesy` | metres, `double` |
| **Entity body** | model space | x = right, y = forward, z = up (OSG convention after model offset) |

### 5.2 Scenario geo-reference (new)
Doctrine has no geodetic origin today. Add `ScenarioGeoReference` (Doctrine, O5):
- `originLatDeg`, `originLonDeg` (double), loaded from the scenario file / settings, also
  passed to `DoctineGISMap::setScenarioCenter()` so 2D and 3D agree.
- Projection of the sim plane: **local tangent-plane approximation is not acceptable** for a
  500 km theatre (Earth curvature drops ~19.6 km at 500 km), and y is already altitude MSL.
  Treat (x, z) as **projected easting/northing** of an **azimuthal equidistant** projection
  centred on the origin (PROJ string `+proj=aeqd +lat_0=… +lon_0=… +datum=WGS84`), and y as
  height above MSL:
  ```
  (lat, lon) = aeqd⁻¹(x, z)      // PROJ via osgEarth SpatialReference, double precision
  height     = y                 // MSL; see 5.4 for MSL vs ellipsoid
  ```
  Distances and bearings from the origin are then exact, and the error elsewhere in a
  500 km theatre stays small. **This must match what cgf-engine's physics and terrain
  assume** — confirm in O5 (open question Q1). If cgf-engine uses a different projection,
  use that one instead; the converter is the single place to change.
- Float precision of sim positions: at 500 km a `float` resolves ~3 cm — acceptable. All
  conversion maths after reading the float is in `double`.

### 5.3 Orientation
- Sim `glm::quat` is in the sim local frame (x east, y up, z north). Convert to heading /
  pitch / roll relative to the **local** ENU frame at the entity's geodetic position, then
  build the model matrix with `scene3d::Geodesy::enuFrame(lat, lon)` (same conventions as
  `Camera3D::setFromGeodetic`, D-005: heading clockwise from north, pitch +up, roll +right).
- Grid convergence (difference between projected north and true north) is applied from the
  aeqd projection so headings stay correct away from the origin.

### 5.4 Heights and terrain agreement
- osgEarth's map uses ellipsoid heights unless a vertical datum is set; DTED is MSL (EGM96).
  Set the map's vertical datum to `egm96` in the earth file so `ALTMODE_ABSOLUTE` heights are
  MSL, matching sim y. **Verify in O8:** osgEarth's `egm96` datum may need a geoid grid
  file shipped with the data/osgEarth folder; check the log for vdatum errors and compare a
  known spot height (e.g. a surveyed point near Islamabad) against the rendered terrain.
- Ground entities: the sim's terrain (`cgf/infra/terrain.hpp`) and osgEarth's terrain must
  come from **the same DTED** at the same resolution, otherwise vehicles float or sink.
  Phase 1 safety net: per-kind `clampToTerrain` flag → `ALTMODE_RELATIVE` with 0 offset for
  ground units (visual only; the sim stays authoritative). Log the difference for QA.

---

## 6. Data flow and the snapshot contract

### 6.1 Per frame (GUI thread)
```
GlobeView3D::paintGL()
  └─ preFrame callback → SceneSync::update()
       1. bridge.consume(m_frame)                    // copy, mutex held briefly
       2. for each RenderEntity e in m_frame:
            id = e.entityId
            geo = geoRef.toGeodetic(e.position)      // double
            hpr = geoRef.toLocalHpr(e.orientation, geo)
            if id not in m_nodes: entityLayer.add(id, kindOf(e), geo, hpr, label, force)
            else                : entityLayer.update(id, geo, hpr, label, visible)
       3. for each id in m_nodes not seen this frame: entityLayer.remove(id)
       4. camera controller update (tether target may have moved)
  └─ viewer->frame()
```
Cost: O(n) per frame with a hash map; 1,000 entities is cheap compared to rendering.

### 6.2 Snapshot extension (Doctrine, O5)
Add to `RenderEntity` (keep existing fields so 2D consumers are unaffected):

| Field | Type | Source | Why |
| ----- | ---- | ------ | --- |
| `entityId` | `uint32_t` | `EntityID` | stable identity for add/update/remove diffing |
| `orientation` | `glm::quat` | `TransformComp.orientation` | model attitude |
| `kind` | SISO entity type (packed 64-bit or struct) | `IdentityComp`/`SisoCatalog` | model selection |
| `force` | `ForceID` | identity | colours, labels, filtering |
| `velocity` | `glm::vec3` | `VelocityComp` (optional) | interpolation, HUD speed |

`PhysicsSystem::buildSnapshot()` fills them. Cost: a few bytes per entity per tick.

### 6.3 Entity lifecycle in the 3D view
- **Add:** create `GeoTransform` → `MatrixTransform` (attitude) → model (shared, from model
  cache) + optional `LabelNode`. Register in osgEarth `ObjectIndex` for picking.
- **Update:** `GeoTransform::setPosition`, attitude matrix, label text if changed.
- **Remove:** detach node; model stays in cache.
- **Visibility:** `RenderEntity.visible` → node mask; force/kind filters from UI.

### 6.4 Model selection
- `models.json` (data package): SISO kind pattern → model file (`.osgb` preferred for load
  speed; glTF via `osgdb_gltf` if available), scale, heading/pitch/roll offset, clamp flag,
  fallback icon. Most-specific pattern wins; default = coloured cone/box per unit type.
- LOD: model when ≥ N pixels, billboard icon (Doctrine symbol atlas) below — osgEarth
  `PlaceNode` or a custom `osg::LOD` with screen-size range mode.

### 6.5 Selection and picking
- `GlobeView3D` emits `entityPicked(id)` from osgEarth `RTTPicker`.
- Doctrine routes selection both ways: 3D pick → select in Doctrine UI/2D map; 2D selection
  → `GlobeView3D::setSelected(id)` (highlight) and optionally tether the camera.

### 6.6 Future: networked feed
`SceneSync` consumes "entity state" records, not ECS internals. A later DIS/HLA receiver can
produce the same records, allowing a remote stealth viewer without changing `earthview3d`.

---

## 7. Public API — `earthview3d`

Header sketch (final names settled in O4; no osgEarth or Doctrine types in public headers):

```cpp
namespace earthview3d {

struct MapConfig {
    QString earthFile;          // absolute or relative to dataRoot
    QString dataRoot;           // root for relative paths in the earth file
    QString cacheDir;           // osgEarth filesystem cache; empty = no cache
    bool    sky = true;
    QDateTime skyTime;          // UTC; invalid = now
};

struct GeoPose {               // double precision, WGS84
    double latDeg = 0, lonDeg = 0, heightM = 0;   // heightM: MSL (egm96 vdatum)
    double headingDeg = 0, pitchDeg = 0, rollDeg = 0;
};

enum class CameraMode { Orbit, Tether, FirstPerson, FreeFly };

class GlobeView3D : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit GlobeView3D(const MapConfig& config, QWidget* parent = nullptr);

    EntityLayer3D& entities();

    void setCameraMode(CameraMode mode, quint32 targetEntityId = 0);
    void flyTo(const GeoPose& viewpoint, double rangeM, double seconds = 2.0);
    GeoPose cameraPose() const;
    bool saveSnapshot(const QString& pngPath);   // tests / bug reports

signals:
    void entityPicked(quint32 entityId);
    void cameraChanged();
    void mapLoadFailed(QString reason);
};

class EntityLayer3D {
public:
    struct Visual { QString kindKey; QString label; QColor color; bool clampToTerrain = false; };
    void add(quint32 id, const Visual& visual, const GeoPose& pose);
    void update(quint32 id, const GeoPose& pose);
    void setLabel(quint32 id, const QString& label);
    void setVisible(quint32 id, bool visible);
    void remove(quint32 id);
    void setSelected(quint32 id);
    void clear();
};

}
```
All methods are GUI-thread only (assert in debug builds).

---

## 8. Cameras (stealth view)

| Mode | Behaviour | Implementation |
| ---- | --------- | -------------- |
| Orbit | Free orbit around a focal point | `EarthManipulator` |
| Tether (chase) | Follow selected entity at range/pitch, heading-locked or free-look | `EarthManipulator` tether (`TETHER_CENTER_AND_HEADING`) + terrain avoidance; fixes I-008 |
| First-person | Eye at entity + offset, entity attitude, mouse free-look within limits | custom `osgGA::CameraManipulator` using `GeoPose` + `scene3d` maths |
| Free-fly | WASD/QE + mouse look, speed scales with height AGL | custom manipulator; terrain clearance via `Terrain::getHeight` |

- Transitions: 1–2 s eased fly between modes.
- Terrain clearance: never below terrain + 2 m (query each frame; keep last value when tiles
  are still loading — same pattern as the spike's `m_groundMsl`).
- HUD: `QPainter` overlay after osgEarth renders (heading, pitch, MSL/AGL altitude, speed,
  target name, mode, sim time) — no osgText needed.

---

## 9. Map content and data package

```
<DataRoot>/
  maps/doctrine.earth          ← relative paths only
  dem/dted_1arc.vrt + *.dt2 (+ .ovr overviews)
  imagery/<region>.mbtiles     ← raster (PNG/JPEG) tiles
  overlays/borders/*.shp, cities/*.ndjson→converted
  models/models.json + *.osgb
  osgearth/                    ← osgEarth data (moon texture, etc.)
  cache/                       ← osgEarth cache (writable)
```
Earth file outline:
```xml
<map name="Doctrine" version="3">
  <options><cache type="filesystem"><path>../cache</path></cache></options>
  <MBTilesImage name="Imagery"><filename>../imagery/region.mbtiles</filename></MBTilesImage>
  <GDALElevation name="DTED 1 arc-sec"><url>../dem/dted_1arc.vrt</url><vdatum>egm96</vdatum></GDALElevation>
  <FeatureModel name="Borders"> … terrain-drape … </FeatureModel>
  <GraticuleLayer name="Grid" open="false"/>
</map>
```
- **Imagery (open decision, I-007):** osgEarth's MapBoxGL layer renders nothing in our build
  and GDAL hangs on the vector MBTiles. Options: (a) pre-render the OpenMapTiles vector file
  into a raster MBTiles once with a style renderer, (b) procure satellite/raster imagery for
  the theatre, (c) debug MapBoxGL in osgEarth. Decide in O8.
- DEM: build overviews once (`gdaladdo -ro dted_1arc.vrt 2 4 8 16 32 64`) for fast zoomed-out
  loading.
- Cities: convert NDJSON to GeoJSON/GPKG for `OGRFeatures` + `PlaceNode` labels (O8).

---

## 10. Deployment (offline)

Install layout (extends the spike's `cmake --install`):
```
Doctrine/
  bin/doctrine.exe, Qt DLLs (qt_generate_deploy_app_script), qt.conf
  bin/osg*.dll, osgEarth*.dll, gdal*.dll, proj*.dll, …       (vcpkg runtime)
  bin/osgPlugins-3.6.5/*.dll
  bin/vcruntime140*.dll, msvcp140*.dll                         (InstallRequiredSystemLibraries)
  plugins/ …                                                   (Qt plugins)
  share/gdal, share/proj (proj.db), share/osgearth (data)
  licenses/                                                    (LGPL/OSGPL/MIT/… texts)
```
Runtime environment set by `main()` when unset: `OSG_LIBRARY_PATH`, `GDAL_DATA`,
`PROJ_DATA`/`PROJ_LIB`, `OSG_FILE_PATH` (osgEarth data). Verify GDAL/PROJ really pick them up
(I-011). Air-gapped build: `spikes/osgearth/OFFLINE_BUILD.md`, generalised for Doctrine in O9.

**Licences:** osgEarth (LGPL 3 with exceptions) and OSG (OSGPL) are linked dynamically
(DLLs), unmodified; ship their licence texts and an offer of source for any modified copy.
GDAL/PROJ (MIT), GEOS (LGPL), Qt (per your Qt licence). Have legal confirm before release.

---

## 11. Testing and verification

| Level | What | Where |
| ----- | ---- | ----- |
| Unit | `ScenarioGeoReference` round-trips (sim ↔ geodetic ↔ sim < 1 cm across ±300 km), HPR conversion, grid convergence; snapshot diffing (add/update/remove); model table matching; camera maths | Qt Test, Linux + Windows CI |
| Integration (no GPU) | `earthview3d` loads an earth file headlessly? — not possible reliably; instead parse/validate `MapConfig` and earth file paths | CI |
| Visual (human, Windows) | Snapshot set per PR: home, tether, first-person over the Margalla Hills; entity models at 100 m / 2 km / 20 km; 2D views unchanged | `GlobeView3D::saveSnapshot` + checklist in PR |
| Performance | 1,000 moving entities ≥ 60 fps at 1440p on target GPU; tile streaming while flying | O11, numbers in `3D_PROGRESS.md` |
| Offline | Clean Windows VM without network: install folder runs 2D + 3D | O9 |

---

## 12. Risks

| Risk | Impact | Mitigation |
| ---- | ------ | ---------- |
| MSVC migration changes 2D behaviour | Doctrine regression | O1/O2 isolated PRs, screenshot comparison, llvm-mingw kept until O10 |
| Sim projection ≠ aeqd assumption | entities misplaced | Q1 resolved in O5 before any visuals; converter is one class |
| Sim terrain ≠ osgEarth terrain | floating/sinking vehicles | same DTED for both; clamp flag for ground units |
| osgEarth GL state leaks | 2D corruption | separate widget/context, no sharing |
| Imagery source unresolved | grey terrain offline | O8 decision; online OSM only for development |
| CI time for osgEarth | slow PRs | vcpkg binary cache |
| Doctrine not in git | no agent work, no history | O0 first |

---

## 13. Open questions

| # | Question | Needed by |
| - | -------- | --------- |
| Q1 | Which projection does cgf-engine assume for (x, z)? Where is the scenario origin defined? | O5 |
| Q2 | Does cgf-engine's terrain read the same DTED? At what resolution? | O5 |
| Q3 | Which imagery source for the theatre (pre-rendered OSM vs satellite)? | O8 |
| Q4 | Model sources/formats for the SISO kinds in use (licensing)? | O6 |
| Q5 | 3D view as floating window, dock, or second monitor full-screen? | O5 |
| Q6 | Should the 3D view also show sensor/contact data (`RenderContact`)? | O6 |
| Q7 | How does Doctrine consume EarthView sources long-term (submodule vs vcpkg port)? | O10 |

---

## 14. Lessons from the spike (S1)

1. Embedded osgEarth needs vertex-attribute aliasing + matrix uniforms set manually.
2. `MapNode::getTerrain()` is null until the first frame.
3. `EarthManipulator` ignores viewpoints set before it is attached; set a home viewpoint.
4. `qInfo` output is lost when stderr is redirected on Windows GUI apps — use `stderr` or a
   file log for diagnostics.
5. `OSGEARTH_NOTIFY_LEVEL=INFO|DEBUG` and the `--snapshot` option make headless-ish checks
   possible on a developer machine.
6. MapBoxGL vector rendering did not work with osgEarth 3.8.1 from vcpkg; plan for raster
   imagery.
