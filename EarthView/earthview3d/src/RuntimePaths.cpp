#include "earthview3d/RuntimePaths.h"

#include <QDir>
#include <QFileInfo>

namespace earthview3d {

RuntimePaths RuntimePaths::buildTreeDefaults()
{
    RuntimePaths paths;
#ifdef EARTHVIEW3D_DEV_OSG_PLUGIN_DIR
    paths.osgPluginDir = QStringLiteral(EARTHVIEW3D_DEV_OSG_PLUGIN_DIR);
#endif
#ifdef EARTHVIEW3D_DEV_SHARE_DIR
    paths.gdalDataDir = QStringLiteral(EARTHVIEW3D_DEV_SHARE_DIR "/gdal");
    paths.projDataDir = QStringLiteral(EARTHVIEW3D_DEV_SHARE_DIR "/proj");
#endif
    return paths;
}

QStringList RuntimePaths::applyDataEnvironment() const
{
    QStringList set;
    auto apply = [&set](const char* name, const QString& dir) {
        if (dir.isEmpty() || !qEnvironmentVariableIsEmpty(name) || !QFileInfo(dir).isDir())
            return;
        qputenv(name, QDir::toNativeSeparators(QDir::cleanPath(dir)).toLocal8Bit());
        set << QString::fromLatin1(name);
    };
    apply("GDAL_DATA", gdalDataDir);
    apply("PROJ_DATA", projDataDir);
    apply("PROJ_LIB", projDataDir);   // PROJ < 9.1 name
    return set;
}

} // namespace earthview3d
