#include "OsgWidget.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMainWindow>
#include <QStatusBar>
#include <QSurfaceFormat>

#include <osgEarth/Common>
#include <osgEarth/Registry>

namespace {
void setEnvIfUnset(const char* name, const QString& path)
{
    if (qEnvironmentVariableIsEmpty(name) && QDir(path).exists())
        qputenv(name, QDir::toNativeSeparators(path).toLocal8Bit());
}

// Point OSG, GDAL and PROJ at the folders laid out by `cmake --install`
// (bin/osgPlugins-*, share/gdal, share/proj) so the app runs offline with
// no environment setup. Explicitly set variables always win.
void configureDeployedRuntime()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    setEnvIfUnset("OSG_LIBRARY_PATH", appDir.absolutePath());
    setEnvIfUnset("GDAL_DATA", appDir.absoluteFilePath("../share/gdal"));
    setEnvIfUnset("PROJ_DATA", appDir.absoluteFilePath("../share/proj"));
    setEnvIfUnset("PROJ_LIB", appDir.absoluteFilePath("../share/proj"));
}
}

int main(int argc, char** argv)
{
    // osgEarth needs a compatibility-profile context with the vcpkg OSG build.
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(4, 6);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument("earthfile", "Earth file to load (default: spike.earth next to the exe).");
    parser.addOption({ "lat", "Entity latitude in degrees.", "deg", "33.70" });
    parser.addOption({ "lon", "Entity longitude in degrees.", "deg", "73.05" });
    parser.process(app);

    const QString earthFile = parser.positionalArguments().value(
        0, QDir(QCoreApplication::applicationDirPath()).filePath("spike.earth"));

    configureDeployedRuntime();
    osgEarth::initialize();

    QMainWindow window;
    window.setWindowTitle("EarthView osgEarth spike");
    window.setCentralWidget(new OsgWidget(earthFile, parser.value("lat").toDouble(), parser.value("lon").toDouble()));
    window.statusBar()->showMessage(
        "Drag: rotate/pan  Wheel: zoom  T: chase entity  F: close stealth view  U: untether  H: home");
    window.resize(1400, 900);
    window.show();

    return app.exec();
}
