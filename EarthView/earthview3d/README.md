# earthview3d / reference

`GlobeView3D.hpp/.cpp` here is the osgEarth widget that ran successfully inside the host
simulation (Doctrine, in the Conquer superbuild) during the local integration test on
2026-10-08: DTED terrain, OSM imagery, live entities from the host snapshot, and home, chase
and ground-level stealth cameras, built with MSVC.

It is **not built** by CMake. Roadmap task **E1** turns it into the `earthview3d` library:
move it to `earthview3d/src`, put it in a namespace, rename the `DOCTRINE_3D_*` diagnostic
environment variables to `EARTHVIEW3D_*`, and keep all osgEarth includes out of public
headers (see `OSGEARTH_INTEGRATION.md` §7 and D-011).

Lessons already baked into this code (do not undo them):
1. Per-widget compatibility-profile `QSurfaceFormat` (host apps default to core).
2. `setUseVertexAttributeAliasing(true)` + `setUseModelViewAndProjectionUniforms(true)`.
3. `setDefaultFboId(defaultFramebufferObject())` every frame.
4. Home viewpoint set via `EarthManipulator::setHomeViewpoint` and applied after frame 1.
5. `MapNode::getTerrain()` is null before the first frame.
6. **No `LogarithmicDepthBuffer`** (drops near-camera terrain); `setNearFarRatio(2e-5)` (D-010).
7. Ground entities clamped to terrain with a cached last-good height (I-015).
8. Entity markers: real size up close, minimum pixel size far away (`AutoTransform`
   with pre-scale + minimum scale).
