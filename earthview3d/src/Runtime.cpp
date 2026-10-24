#include "earthview3d/Runtime.h"

#include <QDir>
#include <QFileInfo>

#include <osgDB/Registry>
#include <osgText/Font>

#include <osgEarth/Registry>

#ifdef EARTHVIEW3D_HAVE_GDAL
#include <cpl_conv.h>
#include <ogr_srs_api.h>
#endif

#include <algorithm>

namespace earthview3d {

namespace {
bool g_runtimeConfigured = false;

/**
 * @brief Re-read osgEarth's default font once the OSG plugin folder is known (issue #11).
 *
 * osgEarth::Registry loads the default font in its constructor, which runs before
 * configureRuntime() can register the plugin folder below. Without osgdb_freetype no .ttf
 * can be read, so osgText silently falls back to its built-in font — that font has ASCII
 * glyphs only, and the degree sign of the graticule labels ("N33°45'") comes out blank.
 * Reading the font again repairs the already-built Registry; it is a no-op once a real
 * font is loaded, and leaves the built-in fallback in place if there is still none.
 */
void reloadDefaultFont()
{
    osgEarth::Registry* registry = osgEarth::Registry::instance();
    if (!registry)
        return;
    const osgText::Font* current = registry->getDefaultFont();
    if (current && current != osgText::Font::getDefaultFont().get())
        return;   // a real font file is already loaded

    osg::ref_ptr<osgText::Font> font = osgText::readRefFontFile("arial.ttf");
    if (font.valid() && font.get() != osgText::Font::getDefaultFont().get())
        registry->setDefaultFont(font.get());
}
}

void configureRuntime(const RuntimePaths& paths)
{
    g_runtimeConfigured = true;

    if (!paths.osgPluginDir.isEmpty() && QFileInfo(paths.osgPluginDir).isDir()) {
        const std::string dir = QDir::toNativeSeparators(QDir::cleanPath(paths.osgPluginDir)).toStdString();
        osgDB::FilePathList& libraryPaths = osgDB::Registry::instance()->getLibraryFilePathList();
        if (std::find(libraryPaths.begin(), libraryPaths.end(), dir) == libraryPaths.end())
            libraryPaths.push_back(dir);
        reloadDefaultFont();
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
