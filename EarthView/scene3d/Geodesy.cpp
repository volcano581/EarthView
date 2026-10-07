#include "Geodesy.h"

#include <cmath>
#include <utility>

namespace scene3d {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

double primeVerticalRadius(double sinLat)
{
    return Geodesy::kSemiMajorAxis
        / std::sqrt(1.0 - Geodesy::kFirstEccentricitySquared * sinLat * sinLat);
}
} // namespace

glm::dvec3 Geodesy::geodeticToEcef(double latitudeDeg, double longitudeDeg, double heightMeters)
{
    const double lat = latitudeDeg * kDegToRad;
    const double lon = longitudeDeg * kDegToRad;
    const double sinLat = std::sin(lat);
    const double cosLat = std::cos(lat);
    const double n = primeVerticalRadius(sinLat);

    return glm::dvec3((n + heightMeters) * cosLat * std::cos(lon),
                      (n + heightMeters) * cosLat * std::sin(lon),
                      (n * (1.0 - kFirstEccentricitySquared) + heightMeters) * sinLat);
}

glm::dvec3 Geodesy::geodeticToEcef(const GeodeticPosition& position)
{
    return geodeticToEcef(position.latitudeDeg, position.longitudeDeg, position.heightMeters);
}

GeodeticPosition Geodesy::ecefToGeodetic(const glm::dvec3& ecef)
{
    const double a = kSemiMajorAxis;
    const double b = kSemiMinorAxis;
    const double e2 = kFirstEccentricitySquared;
    const double ep2 = (a * a - b * b) / (b * b); // second eccentricity squared
    const double p = std::sqrt(ecef.x * ecef.x + ecef.y * ecef.y);

    GeodeticPosition result;
    result.longitudeDeg = std::atan2(ecef.y, ecef.x) * kRadToDeg;

    if (p < 1e-9) {
        // On the polar axis.
        result.latitudeDeg = ecef.z >= 0.0 ? 90.0 : -90.0;
        result.heightMeters = std::abs(ecef.z) - b;
        return result;
    }

    // Bowring's initial estimate, refined by iteration on the parametric latitude.
    double beta = std::atan2(a * ecef.z, b * p);
    double lat = 0.0;
    for (int i = 0; i < 4; ++i) {
        const double sinBeta = std::sin(beta);
        const double cosBeta = std::cos(beta);
        lat = std::atan2(ecef.z + ep2 * b * sinBeta * sinBeta * sinBeta,
                         p - e2 * a * cosBeta * cosBeta * cosBeta);
        beta = std::atan2(b * std::sin(lat), a * std::cos(lat));
    }

    const double sinLat = std::sin(lat);
    const double cosLat = std::cos(lat);
    const double n = primeVerticalRadius(sinLat);
    // Pick the better-conditioned height formula away from / near the poles.
    if (std::abs(cosLat) > 0.5)
        result.heightMeters = p / cosLat - n;
    else
        result.heightMeters = ecef.z / sinLat - n * (1.0 - e2);

    result.latitudeDeg = lat * kRadToDeg;
    return result;
}

glm::dmat3 Geodesy::enuFrame(double latitudeDeg, double longitudeDeg)
{
    const double lat = latitudeDeg * kDegToRad;
    const double lon = longitudeDeg * kDegToRad;
    const double sinLat = std::sin(lat);
    const double cosLat = std::cos(lat);
    const double sinLon = std::sin(lon);
    const double cosLon = std::cos(lon);

    const glm::dvec3 east(-sinLon, cosLon, 0.0);
    const glm::dvec3 north(-sinLat * cosLon, -sinLat * sinLon, cosLat);
    const glm::dvec3 up(cosLat * cosLon, cosLat * sinLon, sinLat);
    return glm::dmat3(east, north, up);
}

glm::dvec3 Geodesy::surfaceNormal(double latitudeDeg, double longitudeDeg)
{
    return enuFrame(latitudeDeg, longitudeDeg)[2];
}

bool Geodesy::intersectRayEllipsoid(const glm::dvec3& origin,
                                    const glm::dvec3& direction,
                                    double* distance)
{
    // Scale space so the ellipsoid becomes the unit sphere; t is preserved.
    const glm::dvec3 inverseRadii(1.0 / kSemiMajorAxis, 1.0 / kSemiMajorAxis, 1.0 / kSemiMinorAxis);
    const glm::dvec3 o = origin * inverseRadii;
    const glm::dvec3 d = direction * inverseRadii;

    const double qa = glm::dot(d, d);
    if (qa <= 0.0)
        return false;
    const double qb = 2.0 * glm::dot(o, d);
    const double qc = glm::dot(o, o) - 1.0;
    const double discriminant = qb * qb - 4.0 * qa * qc;
    if (discriminant < 0.0)
        return false;

    // Numerically stable quadratic roots.
    const double sqrtDisc = std::sqrt(discriminant);
    const double q = -0.5 * (qb + (qb >= 0.0 ? sqrtDisc : -sqrtDisc));
    double t0 = q / qa;
    double t1 = q != 0.0 ? qc / q : t0;
    if (t0 > t1)
        std::swap(t0, t1);

    const double t = t0 >= 0.0 ? t0 : t1;
    if (t < 0.0)
        return false;
    if (distance)
        *distance = t;
    return true;
}

} // namespace scene3d
