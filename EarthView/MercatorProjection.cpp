#include "mercatorprojection.h"
#include "constants.h"
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
    double lon = x * 180.0 / M_PI;
    double lat = atan(sinh(y)) * 180.0 / M_PI;
    return QPointF(lat, lon);
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

QRect MercatorProjection::getTileRange(const QRectF& extent, int zoomLevel)
{
    int minTileX = static_cast<int>(floor((extent.left() + M_PI) / (2.0 * M_PI) * (1 << zoomLevel)));
    int maxTileX = static_cast<int>(ceil((extent.right() + M_PI) / (2.0 * M_PI) * (1 << zoomLevel))) - 1;
    int minTileY = static_cast<int>(floor((M_PI - extent.bottom()) / (2.0 * M_PI) * (1 << zoomLevel)));
    int maxTileY = static_cast<int>(ceil((M_PI - extent.top()) / (2.0 * M_PI) * (1 << zoomLevel))) - 1;

    int maxTile = (1 << zoomLevel) - 1;
    minTileX = qBound(0, minTileX, maxTile);
    maxTileX = qBound(0, maxTileX, maxTile);
    minTileY = qBound(0, minTileY, maxTile);
    maxTileY = qBound(0, maxTileY, maxTile);

    return QRect(minTileX, minTileY, maxTileX - minTileX + 1, maxTileY - minTileY + 1);
}