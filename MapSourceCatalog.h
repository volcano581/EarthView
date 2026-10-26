#pragma once
#ifndef MAPSOURCECATALOG_H
#define MAPSOURCECATALOG_H

#include "TMSLoader.h"

#include <QFileInfo>
#include <QList>
#include <QString>
#include <QStringList>

/**
 * @brief Discovery of the 2D map sources: MBTiles files and EarthView layer-list earth files.
 *
 * This is the reader the 2D viewer has always used, moved out of `MainWindow.cpp` so the
 * earthview3d demo's 2D pane can use the same one. The demo builds without `MainWindow.cpp`,
 * so its map never got a source and fell back to the online OpenStreetMap default, which
 * shows nothing on an offline machine (I-041). Behaviour is unchanged for `MainWindow`.
 *
 * Layer-list earth files are not osgEarth earth files: a bare list of elements with no `<map>`
 * root, one per TMS endpoint of the local tile server, optionally naming the DTED folder:
 * @code
 * <image name="Layer_1" driver="tms"><url>http://host:port/imagery/l1/tilemapresource.xml</url></image>
 * <elevation name="DTED" driver="dted"><url>D:/EarthViewData/DTED</url></elevation>
 * <image name="OSM" driver="mbtiles"><url>\\imagery-server\maps\planet.mbtiles</url></image>
 * @endcode
 * An `mbtiles` entry names an MBTiles file by path: a UNC path or mapped drive on the imagery
 * server's share, or a path relative to the earth file. Only the tiles in view are read from
 * it (see MbTilesReader), so the file can be very large. It can also name it by URL, like the
 * TMS entries: the file on the imagery server's web server, or a tile service in front of it
 * (see urlMbTilesLayer()).
 */
namespace MapSourceCatalog {

/// One selectable 2D map source: one MBTiles file or one layer-list earth file.
struct Source
{
    QString label;                                 ///< Dropdown text
    QString filePath;                              ///< The file it came from (absolute)
    QList<TmsLoader::TileSourceLayer> layers;      ///< Imagery layers, bottom to top
    /// Folder of DTED tiles (.dt1/.dt2) from an `<elevation driver="dted">` entry, empty when
    /// the file has none. Handed to MapWidget; 2D terrain still reads the GeoTIFF DEM mirror
    /// (I-039).
    QString dtedDirectory;
};

/// Dropdown label for a file: "Satellite Imagery" / "CardG Maps" for the names the viewer
/// special-cases, otherwise the base name with underscores as spaces.
QString labelForFile(const QFileInfo& fileInfo);

/// Layer for one MBTiles file (raster or vector, from its metadata). False, with
/// @p errorMessage set, when the file cannot be opened or has no metadata. @p filePath may be
/// an http(s) URL of the file (read with range requests, see RemoteMbTiles).
bool mbTilesLayer(const QString& filePath, TmsLoader::TileSourceLayer* layer, QString* errorMessage = nullptr);

/// Layer for an `mbtiles` entry given by http(s) URL, as the TMS entries are:
/// - the `.mbtiles` file on the web server (any URL whose content is an SQLite file);
/// - a tile server in front of it: a `{z}/{x}/{y}` template (`{-y}` for TMS rows), a TMS
///   `tilemapresource.xml`, or a TileJSON document (`.json`).
/// Vector tiles (pbf/mvt) become a VectorMbTiles layer read by URL; raster ones an ordinary
/// URL template layer.
bool urlMbTilesLayer(const QString& url, TmsLoader::TileSourceLayer* layer, QString* errorMessage = nullptr);

/// True when @p source draws from an MBTiles file (a Data/OSM file or an earth file entry).
bool isMbTilesSource(const Source& source);

/// Imagery layers of one layer-list earth file. @p dtedDirectory, when given, receives the
/// folder of an `<elevation driver="dted">` entry. Empty list when the file cannot be read or
/// holds no usable `<image>` entry.
QList<TmsLoader::TileSourceLayer> readEarthLayers(const QString& filePath,
                                                  QString* dtedDirectory = nullptr);

/// Folders scanned for `*.earth` (`Data/EarthFiles`, the working directory, next to the
/// executable, …) and for `*.mbtiles` (`Data/OSM`), in search order.
QStringList earthSearchDirectories();
QStringList mbTilesSearchDirectories();

/// One entry per file found, first file per absolute path winning.
QList<Source> discoverEarthSources();
QList<Source> discoverMbTilesSources();

/// Every source the 2D viewer offers: MBTiles first, then the layer-list earth files.
QList<Source> discoverMapSources();

/// The vector (OSM) MBTiles layers of the first source that has any: the details (roads,
/// places, ...) MapWidget::setFeatureLayers() draws over the other base maps. Empty when no
/// source is vector.
QList<TmsLoader::TileSourceLayer> featureLayers(const QList<Source>& sources);

} // namespace MapSourceCatalog

#endif // MAPSOURCECATALOG_H
