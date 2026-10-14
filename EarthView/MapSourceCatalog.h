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
 * @endcode
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

} // namespace MapSourceCatalog

#endif // MAPSOURCECATALOG_H
