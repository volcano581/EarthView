#pragma once
#ifndef EARTHVIEW3D_EARTHFILECATALOG_H
#define EARTHVIEW3D_EARTHFILECATALOG_H

#include <QByteArray>
#include <QFileInfo>
#include <QList>
#include <QString>
#include <QStringList>

namespace earthview3d {

/**
 * @brief One TMS layer taken from an EarthView (2D) earth file.
 *
 * The 2D viewer reads layer-list files that are not osgEarth earth files: a bare list of
 * elements with no `<map>` root, one per TMS endpoint of the local tile server, e.g.
 * @code
 * <image name="Layer_1" driver="tms"><url>http://host:port/imagery/l1/tilemapresource.xml</url></image>
 * @endcode
 * Only `driver="tms"` (or no driver) entries are kept; anything else is ignored, as in 2D.
 */
struct TmsLayerSpec
{
    enum class Kind
    {
        Image,     ///< `<image>` → osgEarth TMSImage
        Elevation  ///< `<elevation>` / `<heightfield>` → osgEarth TMSElevation
    };

    Kind kind = Kind::Image;
    QString name;        ///< Layer name; also the osgEarth layer name (unique per file)
    QString url;         ///< TMS endpoint, normally .../tilemapresource.xml
    int minLevel = -1;   ///< -1 = not set
    int maxLevel = -1;   ///< -1 = not set
    QString vdatum;      ///< Elevation only: vertical datum, e.g. "egm96" (empty = source datum)
};

/**
 * @brief One selectable map: one file found in the earth-files folder = one dropdown entry.
 *
 * `earthFile` is what to put into MapConfig::earthFile. For a file that already is a
 * standard osgEarth map it is the file itself; for an EarthView layer-list file it is the
 * standard earth file generated from it (see EarthFileCatalog::generate()).
 */
struct MapCatalogEntry
{
    QString label;           ///< Dropdown text, derived from the file name
    QString sourcePath;      ///< The file found in the earth-files folder (absolute)
    QString earthFile;       ///< Earth file osgEarth can load (absolute); empty until generated
    bool standardEarthFile = false;  ///< true: sourcePath has a `<map>` root and is used as is
    QList<TmsLayerSpec> tmsLayers;   ///< Parsed TMS layers (empty for a standard earth file)
};

/**
 * @brief Turns EarthView's (2D) TMS layer-list earth files into standard osgEarth earth files.
 *
 * The 2D viewer scans `Data/EarthFiles` and offers one map source per file; the 3D view needs
 * a standard osgEarth map. This class does the translation without touching 2D code:
 * scan the same folder, parse each file, and write one generated earth file per source by
 * splicing TMS layers into a template earth file (`Data/maps/earthview.earth` by default) at
 * its layer placeholder (@ref layerPlaceholder).
 *
 * The template supplies everything that is not served by TMS (local elevation, overlays,
 * graticule, sky) and resolves its own relative paths against itself (osgEarth rule), so a
 * generated file is written next to its template unless `outputDir` says otherwise — then
 * every relative path in the template must still resolve from there.
 *
 * GL-free and host-independent (Qt Core only); no osgEarth headers.
 */
class EarthFileCatalog
{
public:
    /// Marker comment in the template; generated TMS layers replace it (all occurrences).
    static const char* layerPlaceholder();

    /// Folders scanned for earth files, in order, mirroring the 2D viewer's search
    /// (`<dataRoot>/EarthFiles`, `<dataRoot>/Data/EarthFiles`, `<dataRoot>`).
    static QStringList defaultSearchDirectories(const QString& dataRoot);

    /// Dropdown label for a file: "Satellite Imagery"/"CardG Maps" for the names the 2D
    /// viewer special-cases, otherwise the base name with underscores as spaces.
    static QString labelForFile(const QFileInfo& fileInfo);

    /// Parse one file's content. Fills label-independent fields of @p entry (tmsLayers,
    /// standardEarthFile). Returns false on malformed XML, or on a layer list with no usable
    /// TMS layer.
    static bool parse(const QByteArray& content, MapCatalogEntry* entry, QString* error = nullptr);

    /// Read and parse one file (sets sourcePath, label; earthFile for a standard map).
    static bool parseFile(const QString& path, MapCatalogEntry* entry, QString* error = nullptr);

    /// The osgEarth layer elements for @p layers, indented by @p indent spaces.
    static QByteArray layersXml(const QList<TmsLayerSpec>& layers, int indent = 4);

    /// Scan @p directories (first file wins per absolute path) for `*.earth`, parsed and
    /// sorted by label. Unreadable or empty files are skipped and reported in @p warnings.
    static QList<MapCatalogEntry> discover(const QStringList& directories, QStringList* warnings = nullptr);

    /// Write the standard earth file for @p entry: @p templatePath with its placeholder
    /// replaced by the entry's layers. @p outputDir empty = the template's folder. Sets
    /// `entry->earthFile` on success; a standard earth file is left untouched (no write).
    static bool generate(MapCatalogEntry* entry, const QString& templatePath, const QString& outputDir,
                         QString* error = nullptr);

    /// File name of the generated earth file for @p entry: `tms_<source base name>.earth`.
    static QString generatedFileName(const MapCatalogEntry& entry);

    /// discover() + generate() for every entry; entries that cannot be generated are dropped
    /// and reported in @p warnings.
    static QList<MapCatalogEntry> build(const QStringList& directories, const QString& templatePath,
                                        const QString& outputDir, QStringList* warnings = nullptr);
};

} // namespace earthview3d

#endif // EARTHVIEW3D_EARTHFILECATALOG_H
