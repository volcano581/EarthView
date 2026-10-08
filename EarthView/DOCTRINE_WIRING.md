# Wiring earthview3d into Doctrine (local, off GitHub)

Doctrine (inside the Conquer superbuild) is **not on GitHub**. Cloud agents finish and
polish everything in this repo; the final wiring into Doctrine is done **on the local
machine** by following this document. Agents keep this document and the
`earthview3d` API in sync — if a task changes the API, it updates the steps below.

Everything Doctrine-specific described here is a **contract**, not code from Doctrine.

---

## 1. What lives where

| Piece | Repo | Who |
| ----- | ---- | --- |
| `earthview3d` library (osgEarth widget, entities, cameras, map config) | EarthView (GitHub) | cloud agents |
| `earthview3d_demo` app with a synthetic entity feed (stands in for Doctrine) | EarthView (GitHub) | cloud agents |
| CMake package / `add_subdirectory` support + runtime deploy helper | EarthView (GitHub) | cloud agents |
| Doctrine adapter (~150 lines): snapshot → `EntityState3D`, window + menu action | Conquer (local) | human / local Claude session |
| Build switch + runtime deploy in Doctrine's CMake | Conquer (local) | human / local Claude session |

A working adapter already exists locally on Conquer branch `earthview-3d-test`
(`doctrine/src/view3d/Doctrine3DWindow.*`, commit `1fd3d89`). After E-tasks change the
API, update that adapter, not a new one.

---

## 2. Host contract (what the adapter must provide)

The adapter runs on the GUI thread inside the widget's pre-frame callback.

| `EntityState3D` field | Meaning | Doctrine source (Conquer, as of 2026-10-08) |
| --------------------- | ------- | ------------------------------------------- |
| `id` | stable per entity, 64-bit | `RenderEntity::id` (flecs entity id) |
| `latDeg`, `lonDeg` | WGS-84, double | `RenderEntity::lat_rad`, `lon_rad` × 180/π |
| `altM` | height above the **WGS-84 ellipsoid** | `RenderEntity::alt_m` |
| `headingDeg` | clockwise from north | `RenderEntity::heading_rad` × 180/π |
| `pitchDeg`, `rollDeg` (E3) | attitude | not in the snapshot yet — 0 until added |
| `shape` / `kindKey` (E3) | marker shape or model-table key | `RenderEntity::unitType` (0 inf, 1 air, 2 armour, 3 arty); SISO kind later |
| `rgba` | 0xRRGGBBAA | `RenderEntity::symbolColor` |
| `label` | short text | `RenderEntity::label` (char[8]) |
| `clampToTerrain` | put on 3D terrain | `true` for non-air units (I-015: sim ground is a flat plane at the origin altitude, ~20 m under DTED near Islamabad) |
| `visible` | skip when false | `RenderEntity::visible` |

Rules for the adapter:
- Read the snapshot the same way the 2D map does (copy on new tick only:
  compare the latest tick count before copying).
- No coordinate maths: Doctrine already produces geodetic positions (CoordBridge).
- Home viewpoint = the scenario `GeodeticOrigin`.
- Never include osgEarth headers in Doctrine files; include only `earthview3d` public
  headers (D-011).

---

## 3. Local wiring steps (Conquer)

Prerequisites: Conquer's `msvc` preset builds; osgEarth available (vcpkg tree or the
`earthview3d` package from E7); EarthView checked out locally.

1. **Bring in earthview3d.** In Conquer's root `CMakeLists.txt`, after EarthView 2D:
   ```cmake
   option(CONQUER_ENABLE_3D "Build the osgEarth 3D view" OFF)
   if(CONQUER_ENABLE_3D)
       add_subdirectory(${EARTHVIEW_REPO}/EarthView/earthview3d earthview3d)   # or find_package(earthview3d) after E7
   endif()
   ```
2. **Adapter.** Copy `doctrine/src/view3d/Doctrine3DWindow.*` from branch
   `earthview-3d-test`; replace its `GlobeView3D` usage with the `earthview3d` public API
   (names per E1/E3). Keep the mapping table of §2.
3. **Link + define.** In `doctrine/CMakeLists.txt`:
   `target_link_libraries(doctrine PRIVATE earthview3d::earthview3d)` and
   `DOCTRINE_HAVE_OSGEARTH=1`; call `earthview3d_deploy_runtime(doctrine)` (E7) instead of
   the test's hand-written copy commands.
4. **UI.** Keep the "3D View" toolbar action and View-menu entry from the test branch
   (`DoctrineMainWindow::open3DView`). Connect `entityPicked(id)` to Doctrine's selection
   (E3) and Doctrine's selection to `setSelected(id)`.
5. **Data.** Point the map config at the data package from E6 (relative paths).
6. **Verify** with the test hooks (`DOCTRINE_AUTOSTART_DEMO=1`, `DOCTRINE_OPEN_3D=1`,
   `DOCTRINE_2D_SNAPSHOT`, `DOCTRINE_3D_SNAPSHOT`, `DOCTRINE_3D_VIEW=chase|stealth`) and the
   checklist below.

## 4. Acceptance checklist (local)

- [ ] Conquer builds with `CONQUER_ENABLE_3D=OFF` exactly as before (no new deps).
- [ ] With it ON: "3D View" opens; entities appear after the scenario starts.
- [ ] 2D and 3D positions agree (compare snapshots taken at the same moment).
- [ ] Ground units sit on the terrain; air units at sim altitude.
- [ ] Chase / stealth / first-person / free-fly work on a moving entity.
- [ ] Selecting in 2D selects in 3D and vice versa.
- [ ] Offline: network unplugged, map and entities still render.
- [ ] Closing and reopening the 3D window works; app exit is clean.
- [ ] Record the result in `3D_PROGRESS.md` (Verification log) — that file is on GitHub.
