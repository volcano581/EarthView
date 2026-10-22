#pragma once
#ifndef CESIUM_PROTO_DTED_H
#define CESIUM_PROTO_DTED_H

#include <QByteArray>
#include <QHash>
#include <QRectF>
#include <QString>

#include <memory>
#include <vector>

class QFile;

/// @brief Read-only DTED cells (.dt0/.dt1/.dt2) from a folder, memory-mapped and sampled
///        bilinearly. Heights are as stored: metres above mean sea level (EGM96).
class DtedStore
{
public:
    DtedStore();
    ~DtedStore();

    /// @brief Indexes every DTED file under @p dir (recursively). Returns the cell count.
    int open(const QString &dir);

    bool isEmpty() const { return m_cells.empty(); }
    int cellCount() const { return int(m_cells.size()); }

    /// @brief Union of the cells: x = west longitude, y = south latitude, in degrees.
    QRectF bounds() const { return m_bounds; }

    /// @brief Bilinear height at @p lon / @p lat (degrees). False outside the cells.
    bool height(double lon, double lat, double *metres) const;

    /// @brief Cesium CustomHeightmapTerrainProvider tile (GeographicTilingScheme): @p size x
    ///        @p size float32 heights, row-major from the north-west corner.
    QByteArray heightmapTile(int level, int x, int y, int size) const;

    /// @brief 256 px Web Mercator shaded-relief PNG (dark and light alpha over the base map).
    QByteArray hillshadeTile(int z, int x, int y) const;

private:
    struct Cell;

    const Cell *cellAt(int lonDegree, int latDegree) const;
    bool addFile(const QString &path);

    std::vector<std::unique_ptr<Cell>> m_cells;
    QHash<qint32, const Cell *> m_index;
    QRectF m_bounds;
};

#endif // CESIUM_PROTO_DTED_H
