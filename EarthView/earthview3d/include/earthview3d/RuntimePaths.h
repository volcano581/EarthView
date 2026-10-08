#pragma once
#ifndef EARTHVIEW3D_RUNTIMEPATHS_H
#define EARTHVIEW3D_RUNTIMEPATHS_H

#include <QString>
#include <QStringList>

namespace earthview3d {

/**
 * @brief Where osgEarth's runtime finds OSG plugins and GDAL/PROJ data.
 *
 * osgDB reads OSG_LIBRARY_PATH once, when the OSG DLLs load (before main()), so setting
 * that variable from the application is too late. earthview3d::configureRuntime()
 * (Runtime.h) therefore adds osgPluginDir to osgDB's library path list directly and sets
 * GDAL_DATA / PROJ_DATA before GDAL/PROJ are first used. GlobeView3D calls it with
 * buildTreeDefaults() if the host has not called it before creating the first view.
 * Deployed hosts pass their install folders (roadmap E7).
 */
struct RuntimePaths
{
    QString osgPluginDir;   ///< Folder containing osgPlugins-<version>/ (OSG library path)
    QString gdalDataDir;    ///< GDAL_DATA
    QString projDataDir;    ///< PROJ_DATA / PROJ_LIB (contains proj.db)

    /// vcpkg folders of this build tree (from CMake); empty outside a vcpkg build.
    static RuntimePaths buildTreeDefaults();

    /// Set GDAL_DATA, PROJ_DATA and PROJ_LIB to the existing folders above, unless the
    /// variable is already set (an explicit environment always wins). Returns the names set.
    QStringList applyDataEnvironment() const;
};

} // namespace earthview3d

#endif // EARTHVIEW3D_RUNTIMEPATHS_H
