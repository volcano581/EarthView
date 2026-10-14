#pragma once
#ifndef EARTHVIEW3D_SCREENSCALE_H
#define EARTHVIEW3D_SCREENSCALE_H

namespace earthview3d {

/**
 * @brief Screen-size arithmetic for entity markers (GL-free, so it can be unit tested).
 *
 * Entity markers are modelled in metres at roughly real size. Far away a real-size vehicle is
 * well under a pixel, so the marker is scaled up until it covers a fixed number of pixels.
 * OSG's osg::AutoTransform does this itself, but it *caches* the scale it computed during the
 * last cull traversal: any traversal with a different camera (or one where the marker sat on
 * or behind the near plane, as happens in the first-person and stealth cameras) leaves a
 * scale behind that the next frame still draws with. A marker scaled by such a stale value is
 * kilometres long, and a long thin shape lying in the local tangent plane grazes the terrain
 * for hundreds of kilometres — the dotted "great circle" trails seen from orbit (I-037).
 *
 * GlobeView3D therefore computes the scale itself, once per frame, from its own camera, and
 * these helpers hold that arithmetic.
 */
namespace screenscale {

/// Metres covered by one pixel at @p distanceM from a perspective camera with vertical field
/// of view @p verticalFovDeg rendering into a viewport @p viewportHeightPx pixels high.
/// Returns 0 for a degenerate camera (non-positive distance, fov or height).
double metresPerPixel(double distanceM, double verticalFovDeg, int viewportHeightPx);

/// Uniform scale for a marker modelled at real size and @p markerSizeM metres across, so that
/// it covers at least @p minPixels pixels at @p metresPerPixel.
///
/// Never below 1 (a marker is never drawn smaller than real size) and never so large that the
/// marker exceeds @p maxMarkerSizeM metres: that cap is what keeps a marker on the map instead
/// of smeared across the globe, however odd the camera gets. Returns 1 for a marker with no
/// size or a degenerate scale request.
double markerScale(double markerSizeM, double metresPerPixel, double minPixels, double maxMarkerSizeM);

/// Size in pixels of something @p sizeM metres across at @p metresPerPixel. Used to choose
/// between the detailed model and the marker icon (the job osg::LOD's PIXEL_SIZE_ON_SCREEN
/// mode would do, which has the same per-camera-traversal problem as osg::AutoTransform).
double pixelSize(double sizeM, double metresPerPixel);

} // namespace screenscale
} // namespace earthview3d

#endif // EARTHVIEW3D_SCREENSCALE_H
