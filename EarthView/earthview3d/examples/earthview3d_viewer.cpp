// earthview3d_viewer — the osgEarth spike app (spikes/osgearth) ported onto the earthview3d
// library: loads a map, shows one moving test entity and supports home/chase/stealth views
// plus an automated snapshot. Superseded by earthview3d_demo (roadmap E2).

#include <earthview3d/GlobeView3D.h>
#include <earthview3d/MapConfig.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QStatusBar>
#include <QTimer>

#include <cmath>
#include <cstdio>

namespace {
void setEnvIfUnset(const char* name, const QString& path)
{
    if (qEnvironmentVariableIsEmpty(name) && !path.isEmpty() && QDir(path).exists())
        qputenv(name, QDir::toNativeSeparators(path).toLocal8Bit());
}

// Development builds: point OSG at the vcpkg plugin folder so .earth files load without
// environment setup. Deployed builds get this from earthview3d_deploy_runtime() (E7).
void configureDevRuntime()
{
#ifdef EARTHVIEW3D_DEV_OSG_PLUGIN_DIR
    setEnvIfUnset("OSG_LIBRARY_PATH", QStringLiteral(EARTHVIEW3D_DEV_OSG_PLUGIN_DIR));
#endif
#ifdef EARTHVIEW3D_DEV_SHARE_DIR
    setEnvIfUnset("GDAL_DATA", QStringLiteral(EARTHVIEW3D_DEV_SHARE_DIR "/gdal"));
    setEnvIfUnset("PROJ_DATA", QStringLiteral(EARTHVIEW3D_DEV_SHARE_DIR "/proj"));
#endif
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("earthview3d_viewer"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("earthview3d test viewer (ported osgEarth spike)."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("earthfile"),
        QStringLiteral("Earth file to load (default: maps/earthview.earth under the data root)."));
    parser.addOption({QStringLiteral("config"), QStringLiteral("Map config JSON (overrides earthfile)."), QStringLiteral("json")});
    parser.addOption({QStringLiteral("data-root"), QStringLiteral("Data root for relative paths."), QStringLiteral("dir"),
                      QStringLiteral(EARTHVIEW3D_DEFAULT_DATA_ROOT)});
    parser.addOption({QStringLiteral("lat"), QStringLiteral("Entity/home latitude in degrees."), QStringLiteral("deg"), QStringLiteral("33.70")});
    parser.addOption({QStringLiteral("lon"), QStringLiteral("Entity/home longitude in degrees."), QStringLiteral("deg"), QStringLiteral("73.05")});
    parser.addOption({QStringLiteral("view"), QStringLiteral("Start view: home, chase or stealth."), QStringLiteral("name"), QStringLiteral("home")});
    parser.addOption({QStringLiteral("sky-time"), QStringLiteral("Sun time, ISO 8601 UTC (default 2026-06-01T07:00:00Z)."),
                      QStringLiteral("time"), QStringLiteral("2026-06-01T07:00:00Z")});
    parser.addOption({QStringLiteral("no-sky"), QStringLiteral("Disable sky and sun.")});
    parser.addOption({QStringLiteral("snapshot"), QStringLiteral("Save a rendered frame to this PNG and exit."), QStringLiteral("png")});
    parser.addOption({QStringLiteral("after"), QStringLiteral("Seconds to wait before --snapshot."), QStringLiteral("sec"), QStringLiteral("20")});
    parser.process(app);

    earthview3d::MapConfig config;
    QString error;
    if (parser.isSet(QStringLiteral("config"))) {
        if (!earthview3d::MapConfig::fromJsonFile(parser.value(QStringLiteral("config")), &config, &error)) {
            std::fprintf(stderr, "%s\n", qPrintable(error));
            return 2;
        }
    }
    else {
        config.dataRoot = parser.value(QStringLiteral("data-root"));
        config.earthFile = parser.positionalArguments().value(0, QStringLiteral("maps/earthview.earth"));
        config.sky = !parser.isSet(QStringLiteral("no-sky"));
        config.skyTime = QDateTime::fromString(parser.value(QStringLiteral("sky-time")), Qt::ISODate);
    }
    if (!config.validate(&error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 2;
    }

    configureDevRuntime();

    const double lat = parser.value(QStringLiteral("lat")).toDouble();
    const double lon = parser.value(QStringLiteral("lon")).toDouble();

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("earthview3d viewer"));
    auto* view = new earthview3d::GlobeView3D(config, lat, lon);
    window.setCentralWidget(view);
    window.statusBar()->showMessage(QStringLiteral(
        "Drag: rotate/pan  Wheel: zoom  N: next entity  T: chase  F: stealth  U: untether  H: home"));
    window.resize(1400, 900);
    window.show();

    QObject::connect(view, &earthview3d::GlobeView3D::loadFailed, &app, [](const QString& reason) {
        std::fprintf(stderr, "Map load failed: %s\n", qPrintable(reason));
    });

    // One test entity circling 300 m above the ground, like the spike.
    QElapsedTimer clock;
    clock.start();
    view->setPreFrameCallback([view, lat, lon, &clock] {
        const double t = clock.elapsed() / 1000.0;
        const double angle = t * 0.05;  // rad/s
        earthview3d::EntityState3D entity;
        entity.id = 1;
        entity.latDeg = lat + 0.01 * std::cos(angle);
        entity.lonDeg = lon + 0.012 * std::sin(angle);
        entity.altM = 900.0;
        entity.headingDeg = std::fmod(90.0 + angle * 180.0 / 3.14159265358979323846, 360.0);
        entity.shape = 1;
        entity.rgba = 0xE03030FFu;
        entity.label = QStringLiteral("TEST-1");
        view->setEntities({entity});
    });

    const QString startView = parser.value(QStringLiteral("view"));
    if (startView == QLatin1String("chase") || startView == QLatin1String("stealth")) {
        QTimer::singleShot(3000, view, [view, startView] {
            if (startView == QLatin1String("chase"))
                view->chaseSelected();
            else
                view->stealthSelected();
        });
    }

    if (parser.isSet(QStringLiteral("snapshot"))) {
        const QString path = parser.value(QStringLiteral("snapshot"));
        QTimer::singleShot(int(parser.value(QStringLiteral("after")).toDouble() * 1000), view, [view, path] {
            const bool saved = view->saveSnapshot(path);
            // stderr, not qInfo: Qt routes qInfo to the debugger when stderr is not a console.
            std::fprintf(stderr, "Snapshot %s: %s\nViewpoint: %s\n", saved ? "saved" : "FAILED",
                qPrintable(path), qPrintable(view->viewpointDescription()));
            QCoreApplication::exit(saved ? 0 : 1);
        });
    }

    return app.exec();
}
