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
 * defaults() if the host has not called it before creating the first view.
 *
 * Deployed layout (roadmap E7, earthview3d_deploy_runtime()): next to the executable
 *   osgPlugins-<version>/        OSG + osgEarth plugins (osgPluginDir = the exe folder)
 *   share/gdal, share/proj       GDAL_DATA, PROJ_DATA (build tree)
 *   ../share/gdal, ../share/proj (installed tree: exe in bin/)
 */
struct RuntimePaths
{
    QString osgPluginDir;   ///< Folder containing osgPlugins-<version>/ (OSG library path)
    QString gdalDataDir;    ///< GDAL_DATA
    QString projDataDir;    ///< PROJ_DATA / PROJ_LIB (contains proj.db)

    /// vcpkg folders of this build tree (from CMake); empty outside a vcpkg build.
    static RuntimePaths buildTreeDefaults();

    /// Deployed folders found next to an executable in @p appDir (layout above); a field
    /// stays empty when its folder is missing.
    static RuntimePaths fromAppDir(const QString& appDir);

    /// fromAppDir(application folder), each missing field filled from buildTreeDefaults().
    /// A deployed app therefore needs no host code; a build-tree run still finds vcpkg.
    static RuntimePaths defaults();

    /// True when the plugin folder and both data folders are set.
    bool isComplete() const;

    /// Fill each empty field from @p other.
    RuntimePaths& fillFrom(const RuntimePaths& other);

    /// Set GDAL_DATA, PROJ_DATA and PROJ_LIB to the existing folders above, unless the
    /// variable is already set (an explicit environment always wins). Returns the names set.
    QStringList applyDataEnvironment() const;
};

} // namespace earthview3d

#endif // EARTHVIEW3D_RUNTIMEPATHS_H
