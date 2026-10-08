#include "earthview3d/Runtime.h"

#include <QDir>
#include <QFileInfo>

#include <osgDB/Registry>

#ifdef EARTHVIEW3D_HAVE_GDAL
#include <cpl_conv.h>
#include <ogr_srs_api.h>
#endif

#include <algorithm>

namespace earthview3d {

namespace {
bool g_runtimeConfigured = false;
}

void configureRuntime(const RuntimePaths& paths)
{
    g_runtimeConfigured = true;

    if (!paths.osgPluginDir.isEmpty() && QFileInfo(paths.osgPluginDir).isDir()) {
        const std::string dir = QDir::toNativeSeparators(QDir::cleanPath(paths.osgPluginDir)).toStdString();
        osgDB::FilePathList& libraryPaths = osgDB::Registry::instance()->getLibraryFilePathList();
        if (std::find(libraryPaths.begin(), libraryPaths.end(), dir) == libraryPaths.end())
            libraryPaths.push_back(dir);
    }
    paths.applyDataEnvironment();

#ifdef EARTHVIEW3D_HAVE_GDAL
    // GDAL may already be loaded (osgEarth links it), so environment variables alone can be
    // too late; set its config option and PROJ search path directly as well. The effective
    // values come from the environment, so an explicit GDAL_DATA/PROJ_DATA still wins.
    const QByteArray gdalData = qgetenv("GDAL_DATA");
    if (!gdalData.isEmpty())
        CPLSetConfigOption("GDAL_DATA", gdalData.constData());
    const QByteArray projData = qgetenv("PROJ_DATA");
    if (!projData.isEmpty()) {
        const char* const searchPaths[] = {projData.constData(), nullptr};
        OSRSetPROJSearchPaths(searchPaths);
    }
#endif
}

bool isRuntimeConfigured()
{
    return g_runtimeConfigured;
}

} // namespace earthview3d
