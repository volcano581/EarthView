#include "OsgWidget.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMainWindow>
#include <QStatusBar>
#include <QSurfaceFormat>

#include <osgEarth/Common>
#include <osgEarth/Registry>

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
