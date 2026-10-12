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
        {QStringLiteral("entities"), QStringLiteral("Number of synthetic entities."), QStringLiteral("n"), QStringLiteral("40")},
        {QStringLiteral("origin"), QStringLiteral("Scenario origin lat,lon in degrees."), QStringLiteral("lat,lon"),
         QStringLiteral("33.6844,73.0479")},
        {QStringLiteral("view"), QStringLiteral("3D start view: home, chase, stealth, fp or free (opens the 3D view)."),
         QStringLiteral("name")},
        {QStringLiteral("map"), QStringLiteral("Earth file (absolute or relative to --data-root)."), QStringLiteral("earth"),
         QStringLiteral("maps/earthview.earth")},
        {QStringLiteral("data-root"), QStringLiteral("Data root for relative paths."), QStringLiteral("dir"),
         QStringLiteral(EARTHVIEW3D_DEFAULT_DATA_ROOT)},
        {QStringLiteral("snapshot2d"), QStringLiteral("Save the 2D map (with entities) to this PNG."), QStringLiteral("png")},
        {QStringLiteral("snapshot3d"), QStringLiteral("Save the 3D view to this PNG (opens the 3D view)."), QStringLiteral("png")},
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

    QString view = parser.value(QStringLiteral("view")).toLower();
    static const QStringList kViews = {QStringLiteral("home"), QStringLiteral("chase"), QStringLiteral("stealth"),
                                       QStringLiteral("fp"), QStringLiteral("free")};
    if (!view.isEmpty() && !kViews.contains(view)) {
        fail(QStringLiteral("--view must be one of home, chase, stealth, fp, free"));
        return 2;
    }
    // First-person and free-fly cameras arrive with roadmap E4; use the closest existing view.
    if (view == QLatin1String("fp")) {
        fail(QStringLiteral("--view fp: first-person camera is roadmap E4; using stealth"));
        view = QStringLiteral("stealth");
    }
    else if (view == QLatin1String("free")) {
        fail(QStringLiteral("--view free: free-fly camera is roadmap E4; using home"));
        view = QStringLiteral("home");
    }

    earthview3d::MapConfig mapConfig;
    mapConfig.dataRoot = parser.value(QStringLiteral("data-root"));
    mapConfig.earthFile = parser.value(QStringLiteral("map"));
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
        QTimer::singleShot(500, &window, [&window, view] {
            earthview3d::GlobeView3D* view3D = window.open3DView();
            // Tether once the home fly-in has settled and entities exist.
            if (view == QLatin1String("chase") || view == QLatin1String("stealth")) {
                QTimer::singleShot(3000, view3D, [view3D, view] {
                    if (view == QLatin1String("chase"))
                        view3D->chaseSelected();
                    else
                        view3D->stealthSelected();
                });
            }
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
