// earthview3d::earthview3d from an installed package, deployed with
// earthview3d_deploy_runtime(): the runtime must be found next to the exe, with no
// environment variables and no vcpkg on PATH. No GL is needed (CI has no GPU).
#include <earthview3d/Runtime.h>

#include <QCoreApplication>
#include <QDir>

#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString appDir = QCoreApplication::applicationDirPath();
    const earthview3d::RuntimePaths deployed = earthview3d::RuntimePaths::fromAppDir(appDir);
    if (!deployed.isComplete()) {
        std::printf("consumer_3d: deployed runtime incomplete in %s (plugins '%s', gdal '%s', proj '%s')\n",
                    qPrintable(appDir), qPrintable(deployed.osgPluginDir),
                    qPrintable(deployed.gdalDataDir), qPrintable(deployed.projDataDir));
        return 1;
    }

    const QDir pluginRoot(deployed.osgPluginDir);
    const QStringList pluginDirs = pluginRoot.entryList({QStringLiteral("osgPlugins-*")}, QDir::Dirs);
    const QDir plugins(pluginRoot.filePath(pluginDirs.value(0)));
    for (const char* plugin : {"osgdb_earth", "osgdb_osgearth_vdatum_egm96", "osgdb_freetype"}) {
        if (plugins.entryList({QString::fromLatin1(plugin) + QStringLiteral(".*")}, QDir::Files).isEmpty()) {
            std::printf("consumer_3d: missing plugin %s in %s\n", plugin, qPrintable(plugins.path()));
            return 1;
        }
    }
    if (!QDir(deployed.projDataDir).exists(QStringLiteral("proj.db"))) {
        std::puts("consumer_3d: proj.db missing");
        return 1;
    }

    earthview3d::configureRuntime(earthview3d::RuntimePaths::defaults());
    if (!earthview3d::isRuntimeConfigured())
        return 1;
    std::printf("consumer_3d: OK (plugins %s)\n", qPrintable(plugins.path()));
    return 0;
}
