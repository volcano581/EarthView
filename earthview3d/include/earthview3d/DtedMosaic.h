#pragma once
#ifndef EARTHVIEW3D_DTEDMOSAIC_H
#define EARTHVIEW3D_DTEDMOSAIC_H

#include <QList>
#include <QString>
#include <QStringList>

namespace earthview3d {

/**
 * @brief One DTED cell, as its User Header Label (UHL) describes it.
 *
 * DTED is one file per 1°×1° cell (`.dt0`/`.dt1`/`.dt2` for 30″/3″/1″ posting), and an
 * elevation server holds them in longitude folders: `DTED2/e066/n32.dt2`. The UHL is the
 * first 80 bytes of the file and gives everything a mosaic needs — the south-west origin
 * post, the posting interval and the grid size — so no GDAL is needed to index a folder.
 *
 * Posts sit *on* the cell boundary (pixel-is-point), while GDAL describes a raster by its
 * outer corner (pixel-is-area), so @ref originLonDeg / @ref originLatDeg report the corner
 * half a post outside the grid, exactly as GDAL's own DTED driver does. Neighbouring cells
 * therefore share one row or column of posts, which a mosaic simply overwrites.
 */
struct DtedTile
{
    QString path;                  ///< Absolute path of the .dt0/.dt1/.dt2 file
    double southLatDeg = 0.0;      ///< Latitude of the south-west origin post
    double westLonDeg = 0.0;       ///< Longitude of the south-west origin post
    double latIntervalDeg = 0.0;   ///< Latitude posting interval
    double lonIntervalDeg = 0.0;   ///< Longitude posting interval (wider near the poles)
    int columns = 0;               ///< Number of longitude lines
    int rows = 0;                  ///< Latitude posts per longitude line

    /// North-west raster corner, GDAL's convention: half a post outside the grid.
    double originLonDeg() const { return westLonDeg - 0.5 * lonIntervalDeg; }
    double originLatDeg() const { return southLatDeg + (rows - 0.5) * latIntervalDeg; }
    bool isValid() const { return columns > 0 && rows > 0 && latIntervalDeg > 0.0 && lonIntervalDeg > 0.0; }
};

/**
 * @brief Indexes a folder of DTED cells as one GDAL mosaic, so a map can name the folder.
 *
 * osgEarth's elevation layer hands its `<url>` to `GDALOpen`, which opens a dataset, not a
 * directory. Rather than making every machine run `gdalbuildvrt` by hand, this walks the
 * folder, reads each cell's UHL and writes the `.vrt` that indexes them (I-042). It is the
 * same file `gdalbuildvrt -resolution highest` would write, built from the headers alone:
 * no GDAL tools have to be installed on the offline machine.
 *
 * Cells of different posting (a DTED1 and a DTED2 folder merged, or the wider longitude
 * spacing DTED uses above 50° latitude) mosaic onto the finest grid present; GDAL resamples
 * the coarser ones. Keeping one level per folder still gives the better result.
 *
 * Qt Core only — no GDAL, no osgEarth, no GL — so it is unit tested like any other logic.
 */
class DtedMosaic
{
public:
    /// Name of the mosaic written into a DTED folder (matches build_offline_data.py).
    static const char* defaultFileName();

    /// Elevation value DTED uses for a void post, and the mosaic's nodata value.
    static int voidValue();

    /// Read one cell's UHL. Fails on a file that is not DTED or whose header is unreadable.
    static bool readHeader(const QString& file, DtedTile* tile, QString* error = nullptr);

    /// Every readable cell under @p folder, recursively, sorted south-west to north-east.
    /// Files that are not DTED are reported in @p warnings and left out.
    static QList<DtedTile> scan(const QString& folder, QStringList* warnings = nullptr);

    /// The VRT for @p tiles as it would be written at @p vrtPath (which fixes the relative
    /// source paths). Empty on failure.
    static QByteArray buildVrt(const QList<DtedTile>& tiles, const QString& vrtPath, QString* error = nullptr);

    /// buildVrt() written to @p vrtPath.
    static bool write(const QList<DtedTile>& tiles, const QString& vrtPath, QString* error = nullptr);

    /**
     * @brief Resolve what a map's DTED `<url>` points at into something GDAL can open.
     *
     * A path naming a file (`.vrt`, a single `.dt2`) is returned unchanged — nothing is
     * written. A folder is scanned and indexed as `<folder>/dted_mosaic.vrt`, which is
     * rebuilt only when a cell is newer than it, so repeated starts cost one stat per file.
     * When the folder cannot be written to (a read-only share from the elevation server),
     * the mosaic goes to @p fallbackDir instead, with absolute source paths; passing an
     * empty @p fallbackDir makes that case an error rather than a silent write elsewhere.
     *
     * @return the path to give GDAL, or an empty string with @p error set.
     */
    static QString ensureMosaic(const QString& path, const QString& fallbackDir = QString(),
                                QString* error = nullptr, QStringList* warnings = nullptr);
};

} // namespace earthview3d

#endif // EARTHVIEW3D_DTEDMOSAIC_H
