#pragma once
#ifndef EARTHVIEW3D_MAPCONFIG_H
#define EARTHVIEW3D_MAPCONFIG_H

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace earthview3d {

/**
 * @brief What the 3D view loads: earth file, data root, cache and sky settings.
 *
 * Relative paths: earthFile and cacheDir resolve against dataRoot (or the current directory
 * when dataRoot is empty). Layers inside the earth file resolve relative to the earth file
 * itself (osgEarth rule), so keep the earth file inside the data tree.
 *
 * JSON form (all keys optional except earthFile; unknown keys are an error):
 * @code
 * { "earthFile": "maps/earthview.earth", "dataRoot": "../Data", "cacheDir": "cache",
 *   "modelTable": "models/models.json", "sky": true, "skyTime": "2026-06-01T07:00:00Z" }
 * @endcode
 * In JSON, a relative dataRoot resolves against the JSON file's folder, and a missing
 * dataRoot defaults to that folder.
 */
struct MapConfig
{
    QString earthFile;   ///< Absolute, or relative to dataRoot
    QString dataRoot;    ///< Root for relative paths; empty = current directory
    QString cacheDir;    ///< osgEarth filesystem cache; empty = no cache. Relative to dataRoot
    QString modelTable;  ///< models.json (ModelTable); empty = placeholder shapes. Relative to dataRoot
    bool sky = true;     ///< Sky, sun and atmosphere
    QDateTime skyTime;   ///< Sun position time (UTC); invalid = now

    /// Absolute, cleaned earth file path (empty if earthFile is empty).
    QString resolvedEarthFile() const;
    /// Absolute, cleaned cache folder (empty if no cache).
    QString resolvedCacheDir() const;
    /// Absolute, cleaned model table path (empty if none).
    QString resolvedModelTable() const;
    /// skyTime in UTC, or the current UTC time when skyTime is invalid.
    QDateTime effectiveSkyTime() const;

    /// Check the configuration can be loaded: earth file set and present, data root present.
    bool validate(QString* error = nullptr) const;

    /// The data root to use when the host was given none: the first existing `Data` folder
    /// among `<appDir>/Data`, `<appDir>/../Data` and `<workingDir>/Data`, else
    /// @p compiledDefault (which may itself be missing — the caller then reports it, and the
    /// path in the message is the one a developer recognises).
    ///
    /// A build-time path is not a default a shipped binary can use: it names the source tree
    /// of the machine that compiled it, so running the executable anywhere else failed with
    /// "Data root ... does not exist" and needed a launcher script to supply `--data-root`
    /// (I-049). Hosts that deploy their data beside the executable need no such wrapper.
    static QString discoverDataRoot(const QString& appDir, const QString& workingDir,
                                    const QString& compiledDefault);

    /// Parse the JSON form. Relative dataRoot resolves against @p baseDir.
    static bool fromJson(const QByteArray& json, const QString& baseDir, MapConfig* config,
                         QString* error = nullptr);
    /// Read and parse a JSON file (relative dataRoot resolves against the file's folder).
    static bool fromJsonFile(const QString& path, MapConfig* config, QString* error = nullptr);
};

} // namespace earthview3d

#endif // EARTHVIEW3D_MAPCONFIG_H
