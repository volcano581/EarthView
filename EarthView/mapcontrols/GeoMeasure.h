#pragma once
#ifndef GEOMEASURE_H
#define GEOMEASURE_H

#include <QPointF>
#include <QString>
#include <QVector>

/**
 * @brief Distance maths for the measure tool. GL-free; points are (lat, lon) in degrees,
 * as MercatorProjection::mercatorToLatLon returns them.
 */
namespace GeoMeasure {

enum class Unit {
    Kilometers = 0,
    Miles = 1,
    NauticalMiles = 2,
    Meters = 3,
    Feet = 4
};

/// Ellipsoidal (WGS-84) distance in metres, Vincenty's inverse formula; falls back to the
/// spherical great-circle distance for near-antipodal points where Vincenty does not converge.
double distanceMeters(const QPointF& fromLatLon, const QPointF& toLatLon);

/// Sum of the leg distances of a polyline, in metres.
double pathLengthMeters(const QVector<QPointF>& latLonPoints);

double fromMeters(double meters, Unit unit);
QString unitSymbol(Unit unit);
/// "12.4 km", "843 m", "1,250 ft": precision adapts to the size of the value.
QString format(double meters, Unit unit);

/// Points along the great circle from @p a to @p b (both included), @p segments legs, so a
/// long measure line is drawn as the shortest path rather than a straight screen line.
QVector<QPointF> greatCirclePoints(const QPointF& a, const QPointF& b, int segments);

} // namespace GeoMeasure

#endif // GEOMEASURE_H
