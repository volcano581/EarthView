// CesiumJS tactical-map prototype host: serves the page, CesiumJS, offline tiles, DTED terrain
// and simulated ground units over a local HTTP server, and shows the page in QWebEngineView.
// See README.md for options.

#include "BaseMap.h"
#include "Dted.h"
#include "HttpServer.h"
#include "Sim.h"

#include <QApplication>
#include <QCache>
#include <QCommandLineParser>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QTcpSocket>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineView>

#include <cmath>
#include <memory>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif

#ifndef CESIUM_PROTO_WEB_DIR
#define CESIUM_PROTO_WEB_DIR "web"
#endif
#ifndef CESIUM_PROTO_DATA_DIR
#define CESIUM_PROTO_DATA_DIR "Data"
#endif

namespace {

/// @brief Forwards the page's console output to stdout, so JS errors show up in the log.
class ConsolePage : public QWebEnginePage
{
public:
    using QWebEnginePage::QWebEnginePage;

protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message,
                                  int line, const QString &source) override
    {
        static const char *const names[] = { "info", "warning", "error" };
        qInfo().noquote() << QStringLiteral("[js %1] %2 (%3:%4)")
                                 .arg(QLatin1String(names[qBound(0, int(level), 2)]), message,
                                      QUrl(source).fileName())
                                 .arg(line);
    }
};

/// @brief CPU time and working set of this process and of its children (Qt WebEngine's
///        renderer and helper processes; the GPU runs in-process), for the benchmark.
struct ProcSample
{
    double wallMs = 0.0;
    double appCpuMs = 0.0;
    double appRssMb = 0.0;
    QHash<quint32, double> childCpuMs; // by pid
    double childRssMb = 0.0;
};

#ifdef Q_OS_WIN
double fileTimeMs(const FILETIME &f)
{
    ULARGE_INTEGER v;
    v.LowPart = f.dwLowDateTime;
    v.HighPart = f.dwHighDateTime;
    return double(v.QuadPart) / 1e4;
}

bool processUsage(HANDLE h, double *cpuMs, double *rssMb)
{
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(h, &created, &exited, &kernel, &user))
        return false;
    *cpuMs = fileTimeMs(kernel) + fileTimeMs(user);
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    *rssMb = GetProcessMemoryInfo(h, &counters, sizeof(counters)) ? double(counters.WorkingSetSize) / (1024.0 * 1024.0) : 0.0;
    return true;
}

ProcSample sampleProcesses(const QElapsedTimer &wall)
{
    ProcSample s;
    s.wallMs = wall.nsecsElapsed() / 1e6;
    double rss = 0.0;
    processUsage(GetCurrentProcess(), &s.appCpuMs, &s.appRssMb);
    const DWORD self = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return s;
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry)) {
        if (entry.th32ParentProcessID != self)
            continue;
        HANDLE child = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID);
        if (!child)
            continue;
        double cpu = 0.0;
        if (processUsage(child, &cpu, &rss)) {
            s.childCpuMs.insert(entry.th32ProcessID, cpu);
            s.childRssMb += rss;
        }
        CloseHandle(child);
    }
    CloseHandle(snapshot);
    return s;
}
#else
ProcSample sampleProcesses(const QElapsedTimer &wall)
{
    ProcSample s;
    s.wallMs = wall.nsecsElapsed() / 1e6;
    return s;
}
#endif

/// @brief CPU of the children that exist in both samples, in ms.
double childCpuDeltaMs(const ProcSample &a, const ProcSample &b)
{
    double sum = 0.0;
    for (auto it = b.childCpuMs.cbegin(); it != b.childCpuMs.cend(); ++it)
        if (a.childCpuMs.contains(it.key()))
            sum += it.value() - a.childCpuMs.value(it.key());
    return sum;
}

/// @brief Per-endpoint request counts and generation times, reported once a second.
struct Stats
{
    struct Entry
    {
        int count = 0;
        double totalMs = 0.0, maxMs = 0.0;
    };
    QMap<QString, Entry> entries;

    void add(const QString &name, double ms)
    {
        Entry &e = entries[name];
        ++e.count;
        e.totalMs += ms;
        e.maxMs = qMax(e.maxMs, ms);
    }

    QString take()
    {
        QStringList parts;
        for (auto it = entries.cbegin(); it != entries.cend(); ++it)
            parts << QStringLiteral("%1 %2/s avg %3 max %4 ms")
                         .arg(it.key())
                         .arg(it->count)
                         .arg(it->totalMs / it->count, 0, 'f', 1)
                         .arg(it->maxMs, 0, 'f', 1);
        entries.clear();
        return parts.isEmpty() ? QStringLiteral("no tile requests") : parts.join(QStringLiteral(" | "));
    }
};

/// @brief The Latin-script part of a name such as "Dirgi ڈرگی"; empty when it has none.
QString latinPart(const QString &name)
{
    QString out;
    bool latin = false;
    for (const QChar c : name) {
        if (c.script() == QChar::Script_Latin)
            latin = true;
        if (c.script() == QChar::Script_Latin || c.script() == QChar::Script_Common)
            out += c;
    }
    return latin ? out.simplified() : QString();
}

/// @brief City and town names inside @p area (lon/lat degrees) from
///        Data/Cities/<cc>/place_city.ndjson and place-town.ndjson, with their DTED height.
QByteArray loadPlaces(const QString &citiesDir, const QRectF &area, const DtedStore &dted)
{
    QJsonArray places;
    QDirIterator it(citiesDir, { "*city*.ndjson", "*town*.ndjson" }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const int kind = path.contains(QStringLiteral("city")) ? 0 : 1;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        while (!f.atEnd()) {
            const QJsonObject o = QJsonDocument::fromJson(f.readLine()).object();
            const QJsonArray location = o.value(QStringLiteral("location")).toArray();
            QString name = o.value(QStringLiteral("other_names")).toObject().value(QStringLiteral("name:en")).toString();
            if (name.isEmpty())
                name = latinPart(o.value(QStringLiteral("name")).toString());
            if (location.size() != 2 || name.isEmpty() || name.startsWith(QLatin1Char('(')))
                continue;
            const double lon = location[0].toDouble(), lat = location[1].toDouble();
            if (!area.contains(lon, lat))
                continue;
            double h = 0.0;
            dted.height(lon, lat, &h);
            places.append(QJsonArray{ name, lon, lat, qRound(h), kind });
        }
    }
    return QJsonDocument(places).toJson(QJsonDocument::Compact);
}

bool parseTile(const QStringList &path, int *z, int *x, int *y, int maxX, int maxY)
{
    if (path.size() != 4)
        return false;
    bool okZ = false, okX = false, okY = false;
    *z = path[1].toInt(&okZ);
    *x = path[2].toInt(&okX);
    *y = path[3].section(QLatin1Char('.'), 0, 0).toInt(&okY);
    if (!okZ || !okX || !okY || *z < 0 || *z > 22 || *x < 0 || *y < 0)
        return false;
    return *x < (maxX << *z) && *y < (maxY << *z);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);

    // A packaged copy carries web/ and data/ next to the exe; a build-tree run uses the sources.
    const QString exeDir = QCoreApplication::applicationDirPath();
    auto nextToExe = [&exeDir](const QString &name, const char *fallback) {
        return QDir(exeDir + QLatin1Char('/') + name).exists() ? exeDir + QLatin1Char('/') + name
                                                              : QString::fromUtf8(fallback);
    };

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("CesiumJS tactical map prototype"));
    cli.addHelpOption();
    const QCommandLineOption dataOpt("data", "EarthView data folder (DEM/, Borders/, Cities/).", "dir",
                                     nextToExe(QStringLiteral("data"), CESIUM_PROTO_DATA_DIR));
    const QCommandLineOption dtedOpt("dted", "DTED folder (default <data>/DEM).", "dir");
    const QCommandLineOption mbtilesOpt("mbtiles", "OpenMapTiles vector or raster .mbtiles for the map.", "file");
    const QCommandLineOption unitsOpt("units", "Simulated ground units.", "n", "300");
    const QCommandLineOption hzOpt("hz", "Simulation rate.", "hz", "20");
    const QCommandLineOption timeScaleOpt("time-scale", "Simulation speed-up.", "x", "4");
    const QCommandLineOption centerOpt("center", "Area centre lat,lon.", "lat,lon", "33.70,73.05");
    const QCommandLineOption radiusOpt("radius", "Area radius in km.", "km", "25");
    const QCommandLineOption portOpt("port", "HTTP port (0 = any free port).", "port", "0");
    const QCommandLineOption webOpt("web", "Web folder (page + cesium/).", "dir",
                                    nextToExe(QStringLiteral("web"), CESIUM_PROTO_WEB_DIR));
    const QCommandLineOption noWindowOpt("no-window", "Serve only; open the printed URL in a browser.");
    const QCommandLineOption grabOpt("grab", "Save a screenshot of the window to this PNG (repeats every --grab-every s).", "file");
    const QCommandLineOption grabEveryOpt("grab-every", "Seconds between --grab screenshots.", "s", "5");
    const QCommandLineOption scriptOpt("run-js", "JavaScript to run in the page after --grab-every seconds (testing).", "code");
    const QCommandLineOption pageOpt("page", "Page to open, relative to the web folder.", "file", "index.html");
    const QCommandLineOption queryOpt("query", "Query string appended to the page URL (key=value&...).", "q");
    const QCommandLineOption sizeOpt("size", "Window size WxH.", "WxH", "1600x960");
    const QCommandLineOption flatSimOpt("flat-sim", "Simulate on flat ground (no DTED lookups); cheaper at large --units.");
    const QCommandLineOption benchOutOpt("bench-out", "Enable the /bench routes: the page posts its result, which is "
                                         "written here with host CPU/memory, then the program exits.", "file");
    cli.addOptions({ dataOpt, dtedOpt, mbtilesOpt, unitsOpt, hzOpt, timeScaleOpt, centerOpt, radiusOpt, portOpt, webOpt,
                     noWindowOpt, grabOpt, grabEveryOpt, scriptOpt, pageOpt, queryOpt, sizeOpt, flatSimOpt, benchOutOpt });
    cli.process(app);
    QElapsedTimer wallClock;
    wallClock.start();

    const QString dataDir = cli.value(dataOpt);
    const QStringList centre = cli.value(centerOpt).split(QLatin1Char(','));
    const double centreLat = centre.value(0).toDouble();
    const double centreLon = centre.value(1).toDouble();
    const int hz = qBound(1, cli.value(hzOpt).toInt(), 120);

    DtedStore dted;
    const QString dtedDir = cli.isSet(dtedOpt) ? cli.value(dtedOpt) : dataDir + QStringLiteral("/DEM");
    qInfo().noquote() << QStringLiteral("DTED: %1 cells from %2").arg(dted.open(dtedDir)).arg(dtedDir);

    BordersMap borders;
    QString error;
    const QString shapefile = dataDir + QStringLiteral("/Borders/World_Countries_Generalized_Shapefile/World_Countries_Generalized.shp");
    if (!borders.open(shapefile, &error))
        qWarning().noquote() << "Borders base map unavailable:" << error;

    MbTiles mbtiles;
    if (cli.isSet(mbtilesOpt) && !mbtiles.open(cli.value(mbtilesOpt), &error)) {
        qCritical().noquote() << "--mbtiles:" << error;
        return 1;
    }

    const QRectF placeArea = dted.isEmpty() ? QRectF(centreLon - 5.0, centreLat - 5.0, 10.0, 10.0)
                                            : dted.bounds().adjusted(-3.0, -3.0, 3.0, 3.0);
    const QByteArray places = loadPlaces(dataDir + QStringLiteral("/Cities"), placeArea, dted);
    Sim sim(cli.isSet(flatSimOpt) ? nullptr : &dted, centreLon, centreLat, cli.value(radiusOpt).toDouble(),
            qMax(1, cli.value(unitsOpt).toInt()), qMax(0.1, cli.value(timeScaleOpt).toDouble()));

    QJsonObject meta{
        { "center", QJsonArray{ centreLon, centreLat } },
        { "bordersBase", borders.isOpen() },
        { "mbtiles", mbtiles.isOpen() ? QJsonValue(mbtiles.meta()) : QJsonValue() },
        { "simHz", hz },
    };
    if (!dted.isEmpty()) {
        const QRectF b = dted.bounds();
        meta["dted"] = QJsonObject{ { "bounds", QJsonArray{ b.left(), b.top(), b.right(), b.bottom() } },
                                    { "cells", dted.cellCount() } };
    }
    QByteArray metaJson; // completed once both servers listen

    QCache<QString, QByteArray> tileCache(256 * 1024 * 1024);
    Stats stats;
    struct Waiter
    {
        QPointer<QTcpSocket> socket;
        quint32 after;
    };
    QVector<Waiter> waiters;

    HttpServer server;

    // Generated tiles are built on a small low-priority pool and answered back on the GUI
    // thread. The GUI thread is also Qt WebEngine's browser thread (blocking it stalls the page),
    // and an unbounded pool starves Chromium's renderer and GPU threads of CPU.
    // An empty cache key means "do not cache".
    QThreadPool tilePool;
    tilePool.setMaxThreadCount(qBound(1, QThread::idealThreadCount() / 4, 3));
    tilePool.setThreadPriority(QThread::LowPriority);
    auto replyGenerated = [&](QTcpSocket *s, const QString &key, const QString &statName,
                              const QByteArray &contentType, std::function<QByteArray()> make) {
        if (!key.isEmpty()) {
            if (const QByteArray *hit = tileCache.object(key)) {
                HttpServer::reply(s, 200, contentType, *hit);
                return;
            }
        }
        const QPointer<QTcpSocket> socket(s);
        tilePool.start([&, socket, key, statName, contentType, make] {
            QElapsedTimer t;
            t.start();
            const QByteArray data = make();
            const double ms = t.nsecsElapsed() / 1e6;
            QMetaObject::invokeMethod(&server, [&, socket, key, statName, contentType, data, ms] {
                stats.add(statName, ms);
                if (!key.isEmpty())
                    tileCache.insert(key, new QByteArray(data), int(qMin<qsizetype>(data.size(), 1 << 30)));
                if (socket)
                    HttpServer::reply(socket, 200, contentType, data);
            }, Qt::QueuedConnection);
        });
    };

    // Benchmark support (--bench-out): the page marks phases and posts one result.
    const QString benchOut = cli.value(benchOutOpt);
    QMap<QString, ProcSample> marks;
    struct SimTimes
    {
        double sumMs = 0.0, maxMs = 0.0;
        int steps = 0;
    } simTimes, simAtStart;
    int tileRequests = 0, tileRequestsAtStart = 0;

    server.setStaticRoot(cli.value(webOpt));
    server.setHandler([&](QTcpSocket *s, const QStringList &path, const QUrlQuery &query) -> bool {
        const QString head = path.value(0);
        int z = 0, x = 0, y = 0;
        if (!benchOut.isEmpty() && head == QLatin1String("bench") && path.size() == 2) {
            if (path[1] == QLatin1String("mark")) {
                const QString phase = query.queryItemValue(QStringLiteral("phase"));
                marks[phase] = sampleProcesses(wallClock);
                if (phase == QLatin1String("measure_start")) {
                    simAtStart = simTimes;
                    tileRequestsAtStart = tileRequests;
                    simTimes.maxMs = 0.0;
                }
                HttpServer::reply(s, 200, "text/plain", "ok");
            } else if (path[1] == QLatin1String("result")) {
                QJsonObject result = QJsonDocument::fromJson(
                    query.queryItemValue(QStringLiteral("json"), QUrl::FullyDecoded).toUtf8()).object();
                const ProcSample a = marks.value(QStringLiteral("measure_start"));
                const ProcSample b = marks.value(QStringLiteral("measure_end"));
                const double wall = b.wallMs - a.wallMs;
                if (wall > 0.0) {
                    const int steps = simTimes.steps - simAtStart.steps;
                    result["host"] = QJsonObject{
                        { "windowMs", wall },
                        { "appCpuPct", (b.appCpuMs - a.appCpuMs) / wall * 100.0 },
                        { "webCpuPct", childCpuDeltaMs(a, b) / wall * 100.0 },
                        { "appRssMb", b.appRssMb },
                        { "webRssMb", b.childRssMb },
                        { "childProcesses", b.childCpuMs.size() },
                        { "simStepMsAvg", steps ? (simTimes.sumMs - simAtStart.sumMs) / steps : 0.0 },
                        { "simStepMsMax", simTimes.maxMs },
                        { "tileRequests", tileRequests - tileRequestsAtStart },
                    };
                }
                QFile out(benchOut);
                if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    out.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
                HttpServer::reply(s, 200, "text/plain", "ok");
                QTimer::singleShot(300, qApp, &QCoreApplication::quit);
            } else {
                return false;
            }
        } else if (path.size() == 1 && head == QLatin1String("meta.json")) {
            HttpServer::reply(s, 200, "application/json", metaJson);
        } else if (path.size() == 1 && head == QLatin1String("places.json")) {
            HttpServer::reply(s, 200, "application/json", places);
        } else if (head == QLatin1String("sim") && path.value(1) == QLatin1String("meta")) {
            HttpServer::reply(s, 200, "application/json", sim.metaJson());
        } else if (head == QLatin1String("terrain") && parseTile(path, &z, &x, &y, 2, 1)) {
            replyGenerated(s, QString(), QStringLiteral("terrain"), "application/octet-stream",
                           [&dted, z, x, y] { return dted.heightmapTile(z, x, y, 65); });
        } else if (head == QLatin1String("hillshade") && parseTile(path, &z, &x, &y, 1, 1)) {
            replyGenerated(s, QStringLiteral("h/%1/%2/%3").arg(z).arg(x).arg(y), QStringLiteral("relief"), "image/png",
                           [&dted, z, x, y] { return dted.hillshadeTile(z, x, y); });
        } else if (head == QLatin1String("base") && borders.isOpen() && parseTile(path, &z, &x, &y, 1, 1)) {
            ++tileRequests;
            replyGenerated(s, QStringLiteral("b/%1/%2/%3").arg(z).arg(x).arg(y), QStringLiteral("base"), "image/png",
                           [&borders, z, x, y] { return borders.tile(z, x, y); });
        } else if (head == QLatin1String("mbtiles") && mbtiles.isOpen() && parseTile(path, &z, &x, &y, 1, 1)) {
            QElapsedTimer t;
            t.start();
            const QByteArray data = mbtiles.tile(z, x, y);
            stats.add(QStringLiteral("mbtiles"), t.nsecsElapsed() / 1e6);
            if (data.isEmpty())
                HttpServer::reply(s, 404, "text/plain", "no tile");
            else
                HttpServer::reply(s, 200, "application/octet-stream", data);
        } else if (path.size() == 1 && head == QLatin1String("elevation")) {
            double h = 0.0;
            const bool ok = dted.height(query.queryItemValue(QStringLiteral("lon")).toDouble(),
                                        query.queryItemValue(QStringLiteral("lat")).toDouble(), &h);
            HttpServer::reply(s, 200, "application/json",
                              QJsonDocument(QJsonObject{ { "ok", ok }, { "h", h } }).toJson(QJsonDocument::Compact));
        } else if (path.size() == 1 && head == QLatin1String("report")) {
            qInfo().noquote() << query.queryItemValue(QStringLiteral("s"), QUrl::FullyDecoded);
            HttpServer::reply(s, 200, "text/plain", stats.take().toUtf8());
        } else {
            return false;
        }
        return true;
    });
    if (!server.listen(quint16(cli.value(portOpt).toUInt()))) {
        qCritical("Cannot listen on 127.0.0.1");
        return 1;
    }

    // Unit frames get their own port: Chromium allows 6 connections per server, and Cesium's
    // tile requests would otherwise queue the long poll behind them (seen in ground view).
    HttpServer frameServer;
    const QByteArray cors = "Access-Control-Allow-Origin: http://127.0.0.1:"
        + QByteArray::number(server.baseUrl().port()) + "\r\n";
    frameServer.setHandler([&](QTcpSocket *s, const QStringList &path, const QUrlQuery &query) -> bool {
        if (path != QStringList{ QStringLiteral("sim"), QStringLiteral("frame") })
            return false;
        const quint32 after = query.queryItemValue(QStringLiteral("after")).toUInt();
        if (sim.seq() > after)
            HttpServer::reply(s, 200, "application/octet-stream", sim.frame(), false, cors);
        else
            waiters.append({ s, after }); // answered on the next sim step (long poll)
        return true;
    });
    if (!frameServer.listen(0)) {
        qCritical("Cannot listen on 127.0.0.1");
        return 1;
    }
    meta["frameUrl"] = frameServer.baseUrl().toString() + QStringLiteral("sim/frame");
    metaJson = QJsonDocument(meta).toJson(QJsonDocument::Compact);

    QUrl url = server.baseUrl().resolved(QUrl(cli.value(pageOpt)));
    if (cli.isSet(queryOpt))
        url.setQuery(cli.value(queryOpt));
    qInfo().noquote() << "Serving" << url.toString();
    if (!QFile::exists(cli.value(webOpt) + QStringLiteral("/cesium/Cesium.js")))
        qWarning().noquote() << "web/cesium/Cesium.js is missing: vendor CesiumJS first (see README.md).";

    QElapsedTimer clock;
    clock.start();
    QTimer tick;
    tick.setTimerType(Qt::PreciseTimer);
    QObject::connect(&tick, &QTimer::timeout, [&] {
        QElapsedTimer stepTimer;
        stepTimer.start();
        sim.step(clock.restart() / 1000.0);
        for (const Waiter &w : std::as_const(waiters))
            if (w.socket)
                HttpServer::reply(w.socket, 200, "application/octet-stream", sim.frame(), false, cors);
        waiters.clear();
        const double ms = stepTimer.nsecsElapsed() / 1e6;
        simTimes.sumMs += ms;
        simTimes.maxMs = qMax(simTimes.maxMs, ms);
        ++simTimes.steps;
    });
    tick.start(1000 / hz);

    std::unique_ptr<QWebEngineView> view;
    if (!cli.isSet(noWindowOpt)) {
        view = std::make_unique<QWebEngineView>();
        view->setPage(new ConsolePage(view.get()));
        QObject::connect(view.get(), &QWebEngineView::loadFinished, [](bool ok) {
            qInfo() << "Page load finished, ok =" << ok;
        });
        view->setUrl(url);
        const QStringList size = cli.value(sizeOpt).split(QLatin1Char('x'));
        view->resize(qBound(320, size.value(0).toInt(), 4000), qBound(240, size.value(1).toInt(), 3000));
        view->setWindowTitle(QStringLiteral("Cesium tactical map prototype"));
        view->show();
    }

    // Test hooks: periodic window screenshots and an optional script run in the page.
    QTimer grabTimer;
    if (view && (cli.isSet(grabOpt) || cli.isSet(scriptOpt))) {
        const QString grabFile = cli.value(grabOpt);
        const QString script = cli.value(scriptOpt);
        QObject::connect(&grabTimer, &QTimer::timeout, [&view, grabFile, script] {
            static int shot = 0;
            ++shot;
            if (!grabFile.isEmpty()) // "%1" in the name numbers the screenshots
                view->grab().save(grabFile.contains(QLatin1String("%1")) ? grabFile.arg(shot) : grabFile);
            if (!script.isEmpty())
                view->page()->runJavaScript(script, [](const QVariant &result) {
                    qInfo().noquote() << "[run-js]" << result.toString();
                });
        });
        grabTimer.start(qMax(1, cli.value(grabEveryOpt).toInt()) * 1000);
    }
    const int rc = app.exec();
    tilePool.waitForDone();
    return rc;
}
