#include "GeoMeasure.h"

#include <QLocale>
#include <QtMath>
#include <cmath>

namespace {
constexpr double kWgs84A = 6378137.0;
constexpr double kWgs84F = 1.0 / 298.257223563;
constexpr double kWgs84B = kWgs84A * (1.0 - kWgs84F);
constexpr double kMeanRadius = 6371008.8;

double sphericalDistance(double lat1, double lon1, double lat2, double lon2)
{
    const double dLat = lat2 - lat1;
    const double dLon = lon2 - lon1;
    const double h = std::sin(dLat / 2) * std::sin(dLat / 2)
        + std::cos(lat1) * std::cos(lat2) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2.0 * kMeanRadius * std::asin(std::min(1.0, std::sqrt(h)));
}
}

double GeoMeasure::distanceMeters(const QPointF& fromLatLon, const QPointF& toLatLon)
{
    const double lat1 = qDegreesToRadians(fromLatLon.x());
    const double lon1 = qDegreesToRadians(fromLatLon.y());
    const double lat2 = qDegreesToRadians(toLatLon.x());
    const double lon2 = qDegreesToRadians(toLatLon.y());
    if (lat1 == lat2 && lon1 == lon2)
        return 0.0;

    const double L = lon2 - lon1;
    const double U1 = std::atan((1.0 - kWgs84F) * std::tan(lat1));
    const double U2 = std::atan((1.0 - kWgs84F) * std::tan(lat2));
    const double sinU1 = std::sin(U1), cosU1 = std::cos(U1);
    const double sinU2 = std::sin(U2), cosU2 = std::cos(U2);

    double lambda = L;
    double sinSigma = 0.0, cosSigma = 0.0, sigma = 0.0, cosSqAlpha = 0.0, cos2SigmaM = 0.0;
    for (int iteration = 0; iteration < 200; ++iteration) {
        const double sinLambda = std::sin(lambda), cosLambda = std::cos(lambda);
        sinSigma = std::sqrt((cosU2 * sinLambda) * (cosU2 * sinLambda)
            + (cosU1 * sinU2 - sinU1 * cosU2 * cosLambda) * (cosU1 * sinU2 - sinU1 * cosU2 * cosLambda));
        if (sinSigma == 0.0)
            return 0.0;
        cosSigma = sinU1 * sinU2 + cosU1 * cosU2 * cosLambda;
        sigma = std::atan2(sinSigma, cosSigma);
        const double sinAlpha = cosU1 * cosU2 * sinLambda / sinSigma;
        cosSqAlpha = 1.0 - sinAlpha * sinAlpha;
        cos2SigmaM = cosSqAlpha != 0.0 ? cosSigma - 2.0 * sinU1 * sinU2 / cosSqAlpha : 0.0;
        const double C = kWgs84F / 16.0 * cosSqAlpha * (4.0 + kWgs84F * (4.0 - 3.0 * cosSqAlpha));
        const double previous = lambda;
        lambda = L + (1.0 - C) * kWgs84F * sinAlpha
            * (sigma + C * sinSigma * (cos2SigmaM + C * cosSigma * (-1.0 + 2.0 * cos2SigmaM * cos2SigmaM)));
        if (std::abs(lambda - previous) < 1e-12) {
            const double uSq = cosSqAlpha * (kWgs84A * kWgs84A - kWgs84B * kWgs84B) / (kWgs84B * kWgs84B);
            const double A = 1.0 + uSq / 16384.0 * (4096.0 + uSq * (-768.0 + uSq * (320.0 - 175.0 * uSq)));
            const double B = uSq / 1024.0 * (256.0 + uSq * (-128.0 + uSq * (74.0 - 47.0 * uSq)));
            const double deltaSigma = B * sinSigma * (cos2SigmaM + B / 4.0
                * (cosSigma * (-1.0 + 2.0 * cos2SigmaM * cos2SigmaM)
                   - B / 6.0 * cos2SigmaM * (-3.0 + 4.0 * sinSigma * sinSigma)
                     * (-3.0 + 4.0 * cos2SigmaM * cos2SigmaM)));
            return kWgs84B * A * (sigma - deltaSigma);
        }
    }
    return sphericalDistance(lat1, lon1, lat2, lon2);
}

double GeoMeasure::pathLengthMeters(const QVector<QPointF>& latLonPoints)
{
    double total = 0.0;
    for (int i = 1; i < latLonPoints.size(); ++i)
        total += distanceMeters(latLonPoints.at(i - 1), latLonPoints.at(i));
    return total;
}

double GeoMeasure::fromMeters(double meters, Unit unit)
{
    switch (unit) {
    case Unit::Kilometers: return meters / 1000.0;
    case Unit::Miles: return meters / 1609.344;
    case Unit::NauticalMiles: return meters / 1852.0;
    case Unit::Meters: return meters;
    case Unit::Feet: return meters / 0.3048;
    }
    return meters;
}

QString GeoMeasure::unitSymbol(Unit unit)
{
    switch (unit) {
    case Unit::Kilometers: return QStringLiteral("km");
    case Unit::Miles: return QStringLiteral("mi");
    case Unit::NauticalMiles: return QStringLiteral("nmi");
    case Unit::Meters: return QStringLiteral("m");
    case Unit::Feet: return QStringLiteral("ft");
    }
    return QString();
}

QString GeoMeasure::format(double meters, Unit unit)
{
    const double value = fromMeters(meters, unit);
    const double magnitude = std::abs(value);
    const int decimals = magnitude >= 100.0 ? 0 : (magnitude >= 10.0 ? 1 : 2);
    return QLocale(QLocale::English).toString(value, 'f', decimals) + QLatin1Char(' ') + unitSymbol(unit);
}

QVector<QPointF> GeoMeasure::greatCirclePoints(const QPointF& a, const QPointF& b, int segments)
{
    segments = qMax(1, segments);
    const double lat1 = qDegreesToRadians(a.x()), lon1 = qDegreesToRadians(a.y());
    const double lat2 = qDegreesToRadians(b.x()), lon2 = qDegreesToRadians(b.y());
    const double x1 = std::cos(lat1) * std::cos(lon1), y1 = std::cos(lat1) * std::sin(lon1), z1 = std::sin(lat1);
    const double x2 = std::cos(lat2) * std::cos(lon2), y2 = std::cos(lat2) * std::sin(lon2), z2 = std::sin(lat2);
    const double omega = std::acos(qBound(-1.0, x1 * x2 + y1 * y2 + z1 * z2, 1.0));

    QVector<QPointF> points;
    points.reserve(segments + 1);
    points.append(a);
    if (omega < 1e-12) {
        points.append(b);
        return points;
    }
    const double sinOmega = std::sin(omega);
    for (int i = 1; i < segments; ++i) {
        const double t = double(i) / segments;
        const double k1 = std::sin((1.0 - t) * omega) / sinOmega;
        const double k2 = std::sin(t * omega) / sinOmega;
        const double x = k1 * x1 + k2 * x2, y = k1 * y1 + k2 * y2, z = k1 * z1 + k2 * z2;
        points.append(QPointF(qRadiansToDegrees(std::atan2(z, std::hypot(x, y))),
                              qRadiansToDegrees(std::atan2(y, x))));
    }
    points.append(b);
    return points;
}
