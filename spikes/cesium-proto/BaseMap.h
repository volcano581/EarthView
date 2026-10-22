#pragma once
#ifndef CESIUM_PROTO_BASEMAP_H
#define CESIUM_PROTO_BASEMAP_H

#include <QByteArray>
#include <QJsonObject>
#include <QPolygonF>
#include <QRectF>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>

#include <vector>

/// @brief Offline land/sea/borders base map drawn from the country borders shapefile
///        (polygons in Web Mercator metres) as 256 px Web Mercator PNG tiles.
class BordersMap
{
public:
    bool open(const QString &shapefile, QString *error);
    bool isOpen() const { return !m_shapes.empty(); }

    /// @brief Thread-safe: shapes are plain point lists, painter paths are built per call.
    QByteArray tile(int z, int x, int y) const;

private:
    struct Shape
    {
        QRectF bounds;
        QVector<QPolygonF> rings;
    };
    std::vector<Shape> m_shapes;
};

/// @brief Tiles from an .mbtiles file (raster or OpenMapTiles vector), XYZ addressing.
class MbTiles
{
public:
    ~MbTiles();
    bool open(const QString &file, QString *error);
    bool isOpen() const { return m_db.isOpen(); }
    QJsonObject meta() const { return m_meta; }
    QByteArray tile(int z, int x, int y);

private:
    QSqlDatabase m_db;
    QSqlQuery m_query;
    QJsonObject m_meta;
};

#endif // CESIUM_PROTO_BASEMAP_H
