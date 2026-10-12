// earthview3d_demo — host simulator for earthview3d (roadmap E2).
//
// Mirrors how a host (Doctrine) uses the library: a 2D EarthView map, an "Open 3D View"
// action, and entities produced on another thread at 100 Hz, consumed per frame by both views.
//
//   earthview3d_demo --entities 40 --origin 33.6844,73.0479 --view chase
//                    --snapshot2d 2d.png --snapshot3d 3d.png --after 30

#include "DemoWindow.h"
#include "SyntheticFeed.h"

#include "OpenGLRuntime.h"

#include <earthview3d/CameraControl.h>
#include <earthview3d/GlobeView3D.h>
#include <earthview3d/MapConfig.h>
#include <earthview3d/Runtime.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QSurfaceFormat>
#include <QTimeZone>
#include <QTimer>

#include <cstdio>

namespace {
void fail(const QString& message)
{
    // stderr, not qWarning: Qt routes messages to the debugger when stderr is not a console.
    std::fprintf(stderr, "%s\n", qPrintable(message));
}

bool parseOrigin(const QString& text, double* lat, double* lon)
{
    const QStringList parts = text.split(QLatin1Char(','));
    if (parts.size() != 2)
        return false;
    bool okLat = false;
    bool okLon = false;
    *lat = parts[0].trimmed().toDouble(&okLat);
    *lon = parts[1].trimmed().toDouble(&okLon);
    return okLat && okLon && *lat >= -90.0 && *lat <= 90.0 && *lon >= -180.0 && *lon <= 180.0;
}
} // namespace

// E4 acceptance script: attach to a moving ground entity and an air entity (chase, then
// first person), detach to orbit, then free-fly (hovering; WASD is manual). Prints the state
// (including AGL) after each step; run with --after 45 so it completes before snapshots.
static void runCameraTour(earthview3d::GlobeView3D* view)
{
    using earthview3d::CameraMode;
    struct Step { int atMs; quint64 select; CameraMode mode; };
    // Entity 1 is infantry (ground), entity 4 is a fixed-wing aircraft (SyntheticFeed kinds).
    static const Step kSteps[] = {
        {3000, 1, CameraMode::Chase},  {9000, 1, CameraMode::FirstPerson},
        {15000, 4, CameraMode::Chase}, {21000, 4, CameraMode::FirstPerson},
        {27000, 0, CameraMode::Orbit}, {31000, 0, CameraMode::FreeFly},
    };
    for (const Step& step : kSteps) {
        QTimer::singleShot(step.atMs, view, [view, step] {
            if (step.select != 0)
                view->setSelected(step.select);
            view->setCameraMode(step.mode);
        });
        QTimer::singleShot(step.atMs + 5500, view, [view] {
            std::fprintf(stderr, "Tour: %s\n", qPrintable(view->viewpointDescription()));
        });
    }
}

int main(int argc, char** argv)
{
    // Same GL setup as the EarthView app for the 2D map; the 3D widget sets its own format.
    if (OpenGLRuntime::softwareOpenGLRequested())
        QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QSurfaceFormat::setDefaultFormat(OpenGLRuntime::defaultSurfaceFormat());

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("earthview3d_demo"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("earthview3d demo host: 2D map + 3D view with a synthetic entity feed."));
    parser.addHelpOption();
    parser.addOptions({
        {QStringLiteral("entities"), QStringLiteral("Number of synthetic entities."), QStringLiteral("n"), QStringLiteral("200")},
        {QStringLiteral("origin"), QStringLiteral("Scenario origin lat,lon in degrees."), QStringLiteral("lat,lon"),
         QStringLiteral("33.6844,73.0479")},
        {QStringLiteral("view"), QStringLiteral("3D camera: home, chase, stealth, fp, free, or tour (scripted E4 check; use --after 45)."),
         QStringLiteral("name")},
        {QStringLiteral("map"), QStringLiteral("Earth file (absolute or relative to --data-root)."), QStringLiteral("earth"),
         QStringLiteral("maps/earthview.earth")},
        {QStringLiteral("data-root"), QStringLiteral("Data root for relative paths."), QStringLiteral("dir"),
         QStringLiteral(EARTHVIEW3D_DEFAULT_DATA_ROOT)},
        {QStringLiteral("snapshot2d"), QStringLiteral("Save the 2D map (with entities) to this PNG."), QStringLiteral("png")},
        {QStringLiteral("snapshot3d"), QStringLiteral("Save the 3D view to this PNG (opens the 3D view)."), QStringLiteral("png")},
        {QStringLiteral("eye-height"), QStringLiteral("Stealth camera eye height above terrain (m, default 10)."),
         QStringLiteral("m")},
        {QStringLiteral("select"), QStringLiteral("Select this entity id in the 3D view (highlight + label)."),
         QStringLiteral("id")},
        {QStringLiteral("models"), QStringLiteral("Model table (absolute or relative to --data-root)."),
         QStringLiteral("json"), QStringLiteral("models/models.json")},
        {QStringLiteral("after"), QStringLiteral("Seconds before taking snapshots, then exit."), QStringLiteral("sec"),
         QStringLiteral("30")},
        {QStringLiteral("check-agreement"),
         QStringLiteral("After --after seconds: pause the feed, compare 2D and 3D entity positions, exit 0 if they agree.")},
    });
    parser.process(app);

    earthview3d::demo::SyntheticFeed::Settings feedSettings;
    bool ok = false;
    feedSettings.entityCount = parser.value(QStringLiteral("entities")).toInt(&ok);
    if (!ok || feedSettings.entityCount < 0) {
        fail(QStringLiteral("--entities must be a non-negative integer"));
        return 2;
    }
    if (!parseOrigin(parser.value(QStringLiteral("origin")), &feedSettings.originLatDeg, &feedSettings.originLonDeg)) {
        fail(QStringLiteral("--origin must be lat,lon in degrees, e.g. 33.6844,73.0479"));
        return 2;
    }

    const QString view = parser.value(QStringLiteral("view")).toLower();
    earthview3d::CameraMode startMode = earthview3d::CameraMode::Orbit;
    if (!view.isEmpty() && view != QLatin1String("tour") && !earthview3d::parseCameraMode(view, &startMode)) {
        fail(QStringLiteral("--view must be one of home, chase, stealth, fp, free, tour"));
        return 2;
    }
    double eyeHeightM = 10.0;
    if (parser.isSet(QStringLiteral("eye-height"))) {
        eyeHeightM = parser.value(QStringLiteral("eye-height")).toDouble(&ok);
        if (!ok || eyeHeightM < 2.0) {
            fail(QStringLiteral("--eye-height must be a number of metres >= 2"));
            return 2;
        }
    }

    earthview3d::MapConfig mapConfig;
    mapConfig.dataRoot = parser.value(QStringLiteral("data-root"));
    mapConfig.earthFile = parser.value(QStringLiteral("map"));
    mapConfig.modelTable = parser.value(QStringLiteral("models"));
    mapConfig.skyTime = QDateTime(QDate(2026, 6, 1), QTime(7, 0), QTimeZone::utc());   // daylight over the theatre
    QString error;
    if (!mapConfig.validate(&error)) {
        fail(error);
        return 2;
    }

    // Before any GlobeView3D exists (E1 review): OSG reads its plugin path too early for env vars.
    earthview3d::configureRuntime(earthview3d::RuntimePaths::buildTreeDefaults());

    // Host simulation: entities published from a worker thread at 100 Hz.
    const earthview3d::demo::SyntheticFeed feed(feedSettings);
    earthview3d::demo::SnapshotBuffer buffer;
    earthview3d::demo::FeedPublisher publisher(feed, &buffer, 100.0);
    publisher.start();

    earthview3d::demo::DemoWindow window(mapConfig, feedSettings, &buffer);
    window.resize(1280, 800);
    window.show();

    const QString snapshot2D = parser.value(QStringLiteral("snapshot2d"));
    const QString snapshot3D = parser.value(QStringLiteral("snapshot3d"));
    const bool checkAgreement = parser.isSet(QStringLiteral("check-agreement"));
    if (!view.isEmpty() || !snapshot3D.isEmpty() || checkAgreement) {
        const quint64 selectId = parser.value(QStringLiteral("select")).toULongLong();
        QTimer::singleShot(500, &window, [&window, view, startMode, selectId, eyeHeightM] {
            earthview3d::GlobeView3D* view3D = window.open3DView();
            view3D->setStealthEyeHeight(eyeHeightM);
            if (selectId != 0)
                QTimer::singleShot(1000, view3D, [view3D, selectId] { view3D->setSelected(selectId); });
            // Applied once the home viewpoint is set and entities exist (pending otherwise).
            if (view == QLatin1String("tour"))
                runCameraTour(view3D);
            else if (startMode != earthview3d::CameraMode::Orbit)
                QTimer::singleShot(3000, view3D, [view3D, startMode] { view3D->setCameraMode(startMode); });
        });
    }

    if (!snapshot2D.isEmpty() || !snapshot3D.isEmpty() || checkAgreement) {
        const int afterMs = int(parser.value(QStringLiteral("after")).toDouble() * 1000.0);
        auto finish = [&window, snapshot2D, snapshot3D, checkAgreement] {
            bool allSaved = true;
            if (checkAgreement) {
                // Both views now hold the same (last) tick, so positions must match exactly.
                const earthview3d::demo::AgreementReport r = window.checkAgreement();
                const bool agree = r.compared > 0 && r.missing == 0 && r.maxErrorM < 0.5;
                std::fprintf(stderr, "2D/3D agreement %s: %d entities compared, %d missing in 3D, max horizontal difference %.3f m (entity %llu)\n",
                             agree ? "OK" : "FAILED", r.compared, r.missing, r.maxErrorM,
                             static_cast<unsigned long long>(r.worstId));
                allSaved = allSaved && agree;
            }
            if (!snapshot2D.isEmpty()) {
                const bool saved = window.grab2D().save(snapshot2D);
                std::fprintf(stderr, "2D snapshot %s: %s\n", saved ? "saved" : "FAILED", qPrintable(snapshot2D));
                allSaved = allSaved && saved;
            }
            if (!snapshot3D.isEmpty()) {
                earthview3d::GlobeView3D* view3D = window.view3D();
                const bool saved = view3D && view3D->saveSnapshot(snapshot3D);
                std::fprintf(stderr, "3D snapshot %s: %s\nViewpoint: %s\n", saved ? "saved" : "FAILED",
                             qPrintable(snapshot3D), view3D ? qPrintable(view3D->viewpointDescription()) : "none");
                allSaved = allSaved && saved;
            }
            QCoreApplication::exit(allSaved ? 0 : 1);
        };
        QTimer::singleShot(afterMs, &window, [&publisher, &window, finish, checkAgreement] {
            if (!checkAgreement) {
                finish();
                return;
            }
            // Freeze the feed and give both views time to consume the final tick.
            publisher.stop();
            QTimer::singleShot(500, &window, finish);
        });
    }

    const int result = app.exec();
    publisher.stop();
    return result;
}
