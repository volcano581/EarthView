#pragma once
#ifndef MERCATORPROJECTION_H
#define MERCATORPROJECTION_H

#include <QPointF>
#include <QRectF>
#include <QRect>

/**
 * @brief MercatorProjection handles Web Mercator coordinate transformations
 *
 * Converts between:
 * - Latitude/Longitude (geographic)
 * - Mercator (projected coordinates)
 * - Tile coordinates (TMS/XYZ)
 */
class MercatorProjection
{
public:
    /**
     * @brief Convert latitude/longitude to Mercator coordinates
     * @param lat Latitude in degrees
     * @param lon Longitude in degrees
     * @return Mercator coordinates as QPointF
     */
    static QPointF latLonToMercator(double lat, double lon);

    /**
     * @brief Convert Mercator coordinates to latitude/longitude
     * @param x Mercator X coordinate
     * @param y Mercator Y coordinate
     * @return Latitude/longitude as QPointF (lat, lon)
     */
    static QPointF mercatorToLatLon(double x, double y);

    /**
     * @brief Get Mercator bounds for a tile
     * @param z Zoom level
     * @param x Tile X coordinate
     * @param y Tile Y coordinate
     * @return QRectF with tile boundaries in Mercator space
     */
    static QRectF tileToMercatorBounds(int z, int x, int y);

    /**
     * @brief Calculate tile range visible in a Mercator extent
     * @param extent Visible Mercator rectangle
     * @param zoomLevel Tile zoom level
     * @return QRect containing tile coordinates (x, y, width, height)
     */
    static QRect getTileRange(const QRectF& extent, int zoomLevel);
};

#endif // MERCATORPROJECTION_H