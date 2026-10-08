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
 *   "sky": true, "skyTime": "2026-06-01T07:00:00Z" }
 * @endcode
 * In JSON, a relative dataRoot resolves against the JSON file's folder, and a missing
 * dataRoot defaults to that folder.
 */
struct MapConfig
{
    QString earthFile;   ///< Absolute, or relative to dataRoot
    QString dataRoot;    ///< Root for relative paths; empty = current directory
    QString cacheDir;    ///< osgEarth filesystem cache; empty = no cache. Relative to dataRoot
    bool sky = true;     ///< Sky, sun and atmosphere
    QDateTime skyTime;   ///< Sun position time (UTC); invalid = now

    /// Absolute, cleaned earth file path (empty if earthFile is empty).
    QString resolvedEarthFile() const;
    /// Absolute, cleaned cache folder (empty if no cache).
    QString resolvedCacheDir() const;
    /// skyTime in UTC, or the current UTC time when skyTime is invalid.
    QDateTime effectiveSkyTime() const;

    /// Check the configuration can be loaded: earth file set and present, data root present.
    bool validate(QString* error = nullptr) const;

    /// Parse the JSON form. Relative dataRoot resolves against @p baseDir.
    static bool fromJson(const QByteArray& json, const QString& baseDir, MapConfig* config,
                         QString* error = nullptr);
    /// Read and parse a JSON file (relative dataRoot resolves against the file's folder).
    static bool fromJsonFile(const QString& path, MapConfig* config, QString* error = nullptr);
};

} // namespace earthview3d

#endif // EARTHVIEW3D_MAPCONFIG_H
