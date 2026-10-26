#include "earthview3d/RuntimePaths.h"

#include <QCoreApplication>
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

RuntimePaths RuntimePaths::fromAppDir(const QString& appDir)
{
    RuntimePaths paths;
    if (appDir.isEmpty())
        return paths;
    const QDir dir(appDir);
    if (!dir.entryList({QStringLiteral("osgPlugins-*")}, QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
        paths.osgPluginDir = QDir::cleanPath(dir.absolutePath());

    auto firstDir = [&dir](const QString& name) {
        for (const QString& candidate : {QStringLiteral("share/") + name, QStringLiteral("../share/") + name}) {
            const QString path = QDir::cleanPath(dir.absoluteFilePath(candidate));
            if (QFileInfo(path).isDir())
                return path;
        }
        return QString();
    };
    paths.gdalDataDir = firstDir(QStringLiteral("gdal"));
    paths.projDataDir = firstDir(QStringLiteral("proj"));
    return paths;
}

RuntimePaths RuntimePaths::defaults()
{
    RuntimePaths paths;
    if (QCoreApplication::instance())
        paths = fromAppDir(QCoreApplication::applicationDirPath());
    return paths.fillFrom(buildTreeDefaults());
}

bool RuntimePaths::isComplete() const
{
    return !osgPluginDir.isEmpty() && !gdalDataDir.isEmpty() && !projDataDir.isEmpty();
}

RuntimePaths& RuntimePaths::fillFrom(const RuntimePaths& other)
{
    if (osgPluginDir.isEmpty())
        osgPluginDir = other.osgPluginDir;
    if (gdalDataDir.isEmpty())
        gdalDataDir = other.gdalDataDir;
    if (projDataDir.isEmpty())
        projDataDir = other.projDataDir;
    return *this;
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
