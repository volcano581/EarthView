#include "OsgWidget.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QKeyEvent>
#include <QTimer>
#include <QMainWindow>
#include <QStatusBar>
#include <QSurfaceFormat>

#include <cstdio>

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
    parser.addOption({ "view", "Start view: home, chase or stealth.", "name", "home" });
    parser.addOption({ "snapshot", "Save a rendered frame to this PNG and exit.", "png" });
    parser.addOption({ "after", "Seconds to wait before --snapshot.", "sec", "20" });
    parser.process(app);

    const QString earthFile = parser.positionalArguments().value(
        0, QDir(QCoreApplication::applicationDirPath()).filePath("spike.earth"));

    configureDeployedRuntime();
    osgEarth::initialize();

    QMainWindow window;
    window.setWindowTitle("EarthView osgEarth spike");
    auto* osgWidget = new OsgWidget(earthFile, parser.value("lat").toDouble(), parser.value("lon").toDouble());
    window.setCentralWidget(osgWidget);
    window.statusBar()->showMessage(
        "Drag: rotate/pan  Wheel: zoom  T: chase entity  F: close stealth view  U: untether  H: home");
    window.resize(1400, 900);
    window.show();

    // Same as pressing T / F once the home fly-to has finished.
    const QString view = parser.value("view");
    const int viewKey = view == "chase" ? Qt::Key_T : view == "stealth" ? Qt::Key_F : 0;
    if (viewKey) {
        QTimer::singleShot(3000, osgWidget, [osgWidget, viewKey] {
            QKeyEvent press(QEvent::KeyPress, viewKey, Qt::NoModifier);
            QCoreApplication::sendEvent(osgWidget, &press);
        });
    }

    if (parser.isSet("snapshot")) {
        const QString path = parser.value("snapshot");
        QTimer::singleShot(int(parser.value("after").toDouble() * 1000), osgWidget, [osgWidget, path] {
            const bool saved = osgWidget->grabFramebuffer().save(path);
            // stderr, not qInfo: Qt routes qInfo to the debugger when stderr is not a console.
            std::fprintf(stderr, "Snapshot %s: %s\nViewpoint: %s\n", saved ? "saved" : "FAILED",
                qPrintable(path), qPrintable(osgWidget->viewpointDescription()));
            QCoreApplication::exit(saved ? 0 : 1);
        });
    }

    return app.exec();
}
