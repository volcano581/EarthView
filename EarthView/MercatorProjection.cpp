#include "MercatorProjection.h"
#include "Constants.h"
#include <QtMath>
#include <cmath>

QPointF MercatorProjection::latLonToMercator(double lat, double lon)
{
    double x = lon * M_PI / 180.0;
    double y = log(tan(M_PI / 4.0 + lat * M_PI / 360.0));
    y = qBound(-M_PI + 0.01, y, M_PI - 0.01);
    return QPointF(x, y);
}

QPointF MercatorProjection::mercatorToLatLon(double x, double y)
{
    double lon = wrapMercatorX(x) * 180.0 / M_PI;
    double lat = atan(sinh(y)) * 180.0 / M_PI;
    return QPointF(lat, lon);
}

QPointF MercatorProjection::screenToMercator(
    const QPointF& screenPos,
    const QPointF& centerMercator,
    int viewportWidth,
    int viewportHeight,
    double resolution)
{
    double xOffset = (screenPos.x() - viewportWidth / 2.0) * resolution;
    double yOffset = (viewportHeight / 2.0 - screenPos.y()) * resolution;

    double mercatorX = centerMercator.x() + (xOffset / GIS::EARTH_RADIUS);
    double mercatorY = centerMercator.y() + (yOffset / GIS::EARTH_RADIUS);

    return QPointF(mercatorX, mercatorY);
}

QPointF MercatorProjection::mercatorToScreen(
    const QPointF& mercatorPos,
    const QPointF& centerMercator,
    int viewportWidth,
    int viewportHeight,
    double resolution)
{
    double dx = (mercatorPos.x() - centerMercator.x()) * GIS::EARTH_RADIUS;
    double dy = (mercatorPos.y() - centerMercator.y()) * GIS::EARTH_RADIUS;

    double screenX = viewportWidth / 2.0 + dx / resolution;
    double screenY = viewportHeight / 2.0 - dy / resolution;

    return QPointF(screenX, screenY);
}

bool MercatorProjection::orthographicMercatorToScreen(
    const QPointF& mercatorPos,
    const QPointF& centerMercator,
    int viewportWidth,
    int viewportHeight,
    double radiusPixels,
    QPointF* screenPos)
{
    if (!screenPos || radiusPixels <= 0.0)
        return false;

    const QPointF centerLatLon = mercatorToLatLon(centerMercator.x(), centerMercator.y());
    const QPointF pointLatLon = mercatorToLatLon(mercatorPos.x(), mercatorPos.y());

    const double centerLat = centerLatLon.x() * M_PI / 180.0;
    const double centerLon = centerLatLon.y() * M_PI / 180.0;
    const double lat = pointLatLon.x() * M_PI / 180.0;
    const double lon = pointLatLon.y() * M_PI / 180.0;
    const double dLon = wrapMercatorX(lon - centerLon);

    const double sinCenterLat = std::sin(centerLat);
    const double cosCenterLat = std::cos(centerLat);
    const double sinLat = std::sin(lat);
    const double cosLat = std::cos(lat);
    const double cosDLon = std::cos(dLon);

    const double visibility = sinCenterLat * sinLat + cosCenterLat * cosLat * cosDLon;
    const double x = radiusPixels * cosLat * std::sin(dLon);
    const double y = radiusPixels * (cosCenterLat * sinLat - sinCenterLat * cosLat * cosDLon);

    *screenPos = QPointF(
        viewportWidth / 2.0 + x,
        viewportHeight / 2.0 - y);

    return visibility >= 0.0;
}

bool MercatorProjection::orthographicScreenToMercator(
    const QPointF& screenPos,
    const QPointF& centerMercator,
    int viewportWidth,
    int viewportHeight,
    double radiusPixels,
    QPointF* mercatorPos)
{
    if (!mercatorPos || radiusPixels <= 0.0)
        return false;

    const double x = (screenPos.x() - viewportWidth / 2.0) / radiusPixels;
    const double y = (viewportHeight / 2.0 - screenPos.y()) / radiusPixels;
    const double rhoSquared = x * x + y * y;
    if (rhoSquared > 1.0)
        return false;

    const QPointF centerLatLon = mercatorToLatLon(centerMercator.x(), centerMercator.y());
    const double centerLat = centerLatLon.x() * M_PI / 180.0;
    const double centerLon = centerLatLon.y() * M_PI / 180.0;
    const double rho = std::sqrt(rhoSquared);

    double lat = centerLat;
    double lon = centerLon;

    if (rho > 0.0) {
        const double c = std::asin(qMin(1.0, rho));
        const double sinC = std::sin(c);
        const double cosC = std::cos(c);
        const double sinCenterLat = std::sin(centerLat);
        const double cosCenterLat = std::cos(centerLat);

        lat = std::asin(cosC * sinCenterLat + (y * sinC * cosCenterLat) / rho);
        lon = centerLon + std::atan2(
            x * sinC,
            rho * cosCenterLat * cosC - y * sinCenterLat * sinC);
    }

    *mercatorPos = latLonToMercator(lat * 180.0 / M_PI, wrapMercatorX(lon) * 180.0 / M_PI);
    return true;
}

double MercatorProjection::wrapMercatorX(double x)
{
    const double worldWidth = 2.0 * M_PI;
    double wrapped = std::fmod(x + M_PI, worldWidth);
    if (wrapped < 0.0) {
        wrapped += worldWidth;
    }
    return wrapped - M_PI;
}

int MercatorProjection::wrapTileX(int x, int zoomLevel)
{
    int tileCount = 1 << zoomLevel;
    int wrapped = x % tileCount;
    if (wrapped < 0) {
        wrapped += tileCount;
    }
    return wrapped;
}

QRectF MercatorProjection::tileToMercatorBounds(int z, int x, int y)
{
    double tileSize = 2.0 * M_PI / (1 << z);
    double left = -M_PI + x * tileSize;
    double right = left + tileSize;
    double top = M_PI - y * tileSize;
    double bottom = top - tileSize;

    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

QRect MercatorProjection::getTileRange(const QRectF& extent, int zoomLevel, bool wrapX)
{
    int minTileX = static_cast<int>(floor((extent.left() + M_PI) / (2.0 * M_PI) * (1 << zoomLevel)));
    int maxTileX = static_cast<int>(ceil((extent.right() + M_PI) / (2.0 * M_PI) * (1 << zoomLevel))) - 1;
    int minTileY = static_cast<int>(floor((M_PI - extent.top()) / (2.0 * M_PI) * (1 << zoomLevel)));
    int maxTileY = static_cast<int>(ceil((M_PI - extent.bottom()) / (2.0 * M_PI) * (1 << zoomLevel))) - 1;

    int maxTile = (1 << zoomLevel) - 1;
    if (!wrapX) {
        minTileX = qBound(0, minTileX, maxTile);
        maxTileX = qBound(0, maxTileX, maxTile);
    }
    minTileY = qBound(0, minTileY, maxTile);
    maxTileY = qBound(0, maxTileY, maxTile);

    return QRect(minTileX, minTileY, maxTileX - minTileX + 1, maxTileY - minTileY + 1);
}
