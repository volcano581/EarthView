#pragma once
#ifndef SCENE3D_GEODESY_H
#define SCENE3D_GEODESY_H

#include <glm/glm.hpp>

namespace scene3d {

/**
 * @brief Geodetic position on the WGS84 ellipsoid.
 *
 * Latitude and longitude are in degrees, height in metres above the ellipsoid.
 */
struct GeodeticPosition
{
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    double heightMeters = 0.0;
};

/**
 * @brief WGS84 ellipsoid maths in double precision (ECEF metres).
 *
 * ECEF axes: +X through (0N, 0E), +Y through (0N, 90E), +Z through the north pole.
 */
class Geodesy
{
public:
    static constexpr double kSemiMajorAxis = 6378137.0;                 ///< a (m)
    static constexpr double kFlattening = 1.0 / 298.257223563;          ///< f
    static constexpr double kSemiMinorAxis = kSemiMajorAxis * (1.0 - kFlattening); ///< b (m)
    static constexpr double kFirstEccentricitySquared = kFlattening * (2.0 - kFlattening); ///< e^2

    /// Geodetic (degrees, metres) to ECEF metres.
    static glm::dvec3 geodeticToEcef(double latitudeDeg, double longitudeDeg, double heightMeters);
    static glm::dvec3 geodeticToEcef(const GeodeticPosition& position);

    /// ECEF metres to geodetic. Iterative Bowring; sub-millimetre from the core to GEO.
    static GeodeticPosition ecefToGeodetic(const glm::dvec3& ecef);

    /**
     * @brief Local East-North-Up frame at a geodetic location.
     * @return Matrix whose columns are the unit East, North and Up vectors in ECEF.
     */
    static glm::dmat3 enuFrame(double latitudeDeg, double longitudeDeg);

    /// Outward ellipsoid surface normal (geodetic up) at a geodetic location.
    static glm::dvec3 surfaceNormal(double latitudeDeg, double longitudeDeg);

    /**
     * @brief Nearest intersection of a ray with the WGS84 ellipsoid surface.
     * @param origin Ray origin in ECEF
     * @param direction Ray direction (need not be normalised)
     * @param distance Receives t >= 0 such that origin + t * direction is on the surface
     * @return false if the ray misses or the ellipsoid is entirely behind the origin.
     *         An origin inside the ellipsoid returns the exit point.
     */
    static bool intersectRayEllipsoid(const glm::dvec3& origin,
                                      const glm::dvec3& direction,
                                      double* distance);
};

} // namespace scene3d

#endif // SCENE3D_GEODESY_H
