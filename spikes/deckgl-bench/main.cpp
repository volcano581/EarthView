// deck.gl-in-QWebEngine spike: pushes N simulated entities from C++ to deck.gl (optionally
// over a MapLibre basemap served from local tiles) and reports fps / latency / drops / tile
// and picking cost. See README.md for options.

#include <QApplication>
#include <QBuffer>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRandomGenerator>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QUrlQuery>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineView>
#include <QtMath>

#include <cmath>
#include <cstring>

// Frame: [double t_ms][uint32 count][uint32 seq] + count * (float lon, lat, alt, heading)
class Sim : public QObject
{
    Q_OBJECT
public:
    Sim(int n, const QRectF &lonLatBounds, QObject *parent = nullptr)
        : QObject(parent), m_n(n), m_state(n * 4)
    {
        auto *rng = QRandomGenerator::global();
        for (int i = 0; i < n; ++i) {
            m_state[i * 4 + 0] = float(lonLatBounds.left() + rng->bounded(lonLatBounds.width()));
            m_state[i * 4 + 1] = float(lonLatBounds.top() + rng->bounded(lonLatBounds.height()));
            m_state[i * 4 + 2] = 0.f;
            m_state[i * 4 + 3] = float(rng->bounded(360.0));
        }
    }

    void step()
    {
        for (int i = 0; i < m_n; ++i) {
            const float h = float(qDegreesToRadians(double(m_state[i * 4 + 3])));
            m_state[i * 4 + 0] += 0.01f * std::sin(h);
            m_state[i * 4 + 1] += 0.01f * std::cos(h);
            m_state[i * 4 + 3] = std::fmod(m_state[i * 4 + 3] + 0.5f, 360.f);
        }
        QByteArray f(16 + m_n * 16, Qt::Uninitialized);
        const double t = double(QDateTime::currentMSecsSinceEpoch());
        const quint32 cnt = quint32(m_n);
        const quint32 seq = ++m_seq;
        std::memcpy(f.data(), &t, 8);
        std::memcpy(f.data() + 8, &cnt, 4);
        std::memcpy(f.data() + 12, &seq, 4);
        std::memcpy(f.data() + 16, m_state.constData(), size_t(m_n) * 16);
        m_latest = f;
    }

    QByteArray latest() const { return m_latest; }

private:
    int m_n;
    QVector<float> m_state;
    quint32 m_seq = 0;
    QByteArray m_latest;
};

// Serves tiles://local/meta and tiles://local/{z}/{x}/{y} from an .mbtiles file, an XYZ
// directory, or synthetic PNGs. Runs on the GUI thread, like the sim tick.
class TileScheme : public QWebEngineUrlSchemeHandler
{
public:
    TileScheme() { m_window.start(); }

    bool open(const QString &spec, QString *error)
    {
        m_meta = QJsonObject{ { "format", "none" } };
        if (spec == QLatin1String("none"))
            return true;
        if (spec == QLatin1String("synth")) {
            m_kind = Kind::Synth;
            m_meta = QJsonObject{ { "format", "png" }, { "minzoom", 0 }, { "maxzoom", 14 },
                                  { "source", "synth" } };
            return true;
        }
        const QFileInfo fi(spec);
        if (fi.isDir())
            return openDirectory(fi.absoluteFilePath(), error);
        if (fi.isFile())
            return openMbTiles(fi.absoluteFilePath(), error);
        *error = QStringLiteral("--tiles: '%1' is not none, synth, a directory or an .mbtiles file").arg(spec);
        return false;
    }

    QJsonObject meta() const { return m_meta; }

    QRectF bounds() const
    {
        const QStringList b = m_meta.value("bounds").toString().split(',');
        if (b.size() == 4) {
            const double w = b[0].toDouble(), s = b[1].toDouble();
            const double e = b[2].toDouble(), n = b[3].toDouble();
            if (e > w && n > s)
                return QRectF(w, s, e - w, n - s);
        }
        return QRectF(-180.0, -70.0, 360.0, 140.0);
    }

    QString takeStats()
    {
        const double secs = qMax(0.001, m_window.restart() / 1000.0);
        const QString s = QStringLiteral("tiles/s=%1 avg=%2ms max=%3ms miss=%4")
                              .arg(qRound(m_count / secs))
                              .arg(m_count ? m_totalMs / m_count : 0.0, 0, 'f', 1)
                              .arg(m_maxMs, 0, 'f', 1)
                              .arg(m_miss);
        m_count = m_miss = 0;
        m_totalMs = m_maxMs = 0.0;
        return s;
    }

    void requestStarted(QWebEngineUrlRequestJob *job) override
    {
        const QString path = job->requestUrl().path();
        if (path == QLatin1String("/meta")) {
            reply(job, "application/json", QJsonDocument(m_meta).toJson(QJsonDocument::Compact));
            return;
        }
        const QStringList zxy = path.split('/', Qt::SkipEmptyParts);
        bool okZ = false, okX = false, okY = false;
        const int z = zxy.value(0).toInt(&okZ);
        const int x = zxy.value(1).toInt(&okX);
        const int y = zxy.value(2).section('.', 0, 0).toInt(&okY);
        if (zxy.size() != 3 || !okZ || !okX || !okY || z < 0 || z > 24) {
            job->fail(QWebEngineUrlRequestJob::UrlInvalid);
            return;
        }

        QElapsedTimer t;
        t.start();
        const QByteArray data = loadTile(z, x, y);
        const double ms = t.nsecsElapsed() / 1e6;
        ++m_count;
        m_totalMs += ms;
        m_maxMs = qMax(m_maxMs, ms);
        if (data.isEmpty()) {
            ++m_miss;
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        reply(job, contentType(), data);
    }

private:
    enum class Kind { None, Synth, Directory, MbTiles };

    static void reply(QWebEngineUrlRequestJob *job, const QByteArray &type, const QByteArray &data)
    {
        auto *buf = new QBuffer(job);
        buf->setData(data);
        buf->open(QIODevice::ReadOnly);
        job->reply(type, buf);
    }

    QByteArray contentType() const
    {
        const QString f = m_meta.value("format").toString();
        if (f == QLatin1String("pbf"))
            return "application/x-protobuf";
        if (f == QLatin1String("jpg") || f == QLatin1String("jpeg"))
            return "image/jpeg";
        if (f == QLatin1String("webp"))
            return "image/webp";
        return "image/png";
    }

    bool openMbTiles(const QString &file, QString *error)
    {
        if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
            *error = QStringLiteral("Qt SQLite driver (qsqlite) not found; run windeployqt");
            return false;
        }
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("deckgl_bench_tiles"));
        m_db.setDatabaseName(file);
        m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!m_db.open()) {
            *error = m_db.lastError().text();
            return false;
        }
        QJsonObject meta{ { "format", "png" }, { "minzoom", 0 }, { "maxzoom", 14 } };
        QSqlQuery q(m_db);
        if (q.exec(QStringLiteral("select name, value from metadata"))) {
            while (q.next()) {
                const QString name = q.value(0).toString();
                const QString value = q.value(1).toString();
                if (name == QLatin1String("minzoom") || name == QLatin1String("maxzoom"))
                    meta[name] = value.toInt();
                else if (name == QLatin1String("format") || name == QLatin1String("bounds")
                         || name == QLatin1String("center") || name == QLatin1String("name"))
                    meta[name] = value;
            }
        }
        meta["source"] = QStringLiteral("mbtiles");
        m_meta = meta;
        m_tileQuery = QSqlQuery(m_db);
        m_tileQuery.prepare(QStringLiteral("select tile_data from tiles where zoom_level = ? "
                                           "and tile_column = ? and tile_row = ?"));
        m_kind = Kind::MbTiles;
        return true;
    }

    bool openDirectory(const QString &dir, QString *error)
    {
        int minZoom = 99, maxZoom = -1;
        for (const QString &entry : QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            bool ok = false;
            const int z = entry.toInt(&ok);
            if (ok) {
                minZoom = qMin(minZoom, z);
                maxZoom = qMax(maxZoom, z);
            }
        }
        QString format;
        QDirIterator it(dir, { "*.png", "*.jpg", "*.jpeg", "*.webp", "*.pbf", "*.mvt" },
                        QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext())
            format = QFileInfo(it.next()).suffix().toLower();
        if (maxZoom < 0 || format.isEmpty()) {
            *error = QStringLiteral("'%1' has no {z}/{x}/{y}.<png|jpg|webp|pbf> tiles").arg(dir);
            return false;
        }
        m_dir = dir;
        m_ext = format;
        m_meta = QJsonObject{ { "format", format == QLatin1String("mvt") ? QStringLiteral("pbf") : format },
                              { "minzoom", minZoom }, { "maxzoom", maxZoom },
                              { "source", "directory" } };
        m_kind = Kind::Directory;
        return true;
    }

    QByteArray loadTile(int z, int x, int y)
    {
        switch (m_kind) {
        case Kind::MbTiles: {
            m_tileQuery.bindValue(0, z);
            m_tileQuery.bindValue(1, x);
            m_tileQuery.bindValue(2, (1 << z) - 1 - y); // MBTiles rows are TMS (flipped)
            if (m_tileQuery.exec() && m_tileQuery.next())
                return m_tileQuery.value(0).toByteArray();
            return {};
        }
        case Kind::Directory: {
            QFile f(QStringLiteral("%1/%2/%3/%4.%5").arg(m_dir).arg(z).arg(x).arg(y).arg(m_ext));
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }
        case Kind::Synth:
            return synthTile(z, x, y);
        case Kind::None:
            break;
        }
        return {};
    }

    static QByteArray synthTile(int z, int x, int y)
    {
        QImage img(256, 256, QImage::Format_RGB32);
        img.fill(QColor(16 + (x * 7 + y * 3) % 12, 22, 30 + (x + y) % 2 * 6));
        QPainter p(&img);
        p.setPen(QColor(40, 55, 70));
        for (int i = 0; i <= 256; i += 32) {
            p.drawLine(i, 0, i, 256);
            p.drawLine(0, i, 256, i);
        }
        p.setPen(QColor(90, 110, 130));
        p.drawRect(0, 0, 255, 255);
        p.drawText(QRect(0, 0, 256, 256), Qt::AlignCenter, QStringLiteral("%1/%2/%3").arg(z).arg(x).arg(y));
        p.end();
        QByteArray out;
        QBuffer buf(&out);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        return out;
    }

    Kind m_kind = Kind::None;
    QJsonObject m_meta;
    QSqlDatabase m_db;
    QSqlQuery m_tileQuery;
    QString m_dir, m_ext;
    QElapsedTimer m_window;
    int m_count = 0, m_miss = 0;
    double m_totalMs = 0.0, m_maxMs = 0.0;
};

// Modes "json" and "b64": frames pushed through QWebChannel signals.
class Bridge : public QObject
{
    Q_OBJECT
public:
    Bridge(Sim *sim, TileScheme *tiles, QObject *parent = nullptr)
        : QObject(parent), m_sim(sim), m_tiles(tiles) {}

    Q_INVOKABLE void report(const QString &stats) { qInfo().noquote() << stats; }
    Q_INVOKABLE QString tileStats() { return m_tiles->takeStats(); }

    void push(const QString &mode)
    {
        const QByteArray f = m_sim->latest();
        if (mode == QLatin1String("json")) {
            double t;
            std::memcpy(&t, f.constData(), 8);
            const auto *v = reinterpret_cast<const float *>(f.constData() + 16);
            const int n = int((f.size() - 16) / 4);
            QVariantList list;
            list.reserve(n);
            for (int i = 0; i < n; ++i)
                list.append(v[i]);
            emit frameJson(t, list);
        } else {
            emit frameB64(QString::fromLatin1(f.toBase64()));
        }
    }

signals:
    void frameJson(double t, const QVariantList &data);
    void frameB64(const QString &b64);

private:
    Sim *m_sim;
    TileScheme *m_tiles;
};

// Mode "scheme": JS pulls raw bytes from sim://frame, no JSON/base64.
class SimScheme : public QWebEngineUrlSchemeHandler
{
public:
    explicit SimScheme(Sim *sim) : m_sim(sim) {}

    void requestStarted(QWebEngineUrlRequestJob *job) override
    {
        auto *buf = new QBuffer(job);
        buf->setData(m_sim->latest());
        buf->open(QIODevice::ReadOnly);
        job->reply("application/octet-stream", buf);
    }

private:
    Sim *m_sim;
};

static void registerScheme(const QByteArray &name)
{
    QWebEngineUrlScheme scheme(name);
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setFlags(QWebEngineUrlScheme::CorsEnabled | QWebEngineUrlScheme::LocalAccessAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
}

int main(int argc, char **argv)
{
    registerScheme("sim"); // schemes must be registered before QApplication
    registerScheme("tiles");

    QApplication app(argc, argv);
    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("deck.gl / MapLibre in QWebEngine benchmark"));
    cli.addHelpOption();
    cli.addPositionalArgument(QStringLiteral("N"), QStringLiteral("Entity count (default 5000)."));
    cli.addPositionalArgument(QStringLiteral("Hz"), QStringLiteral("Sim rate (default 30)."));
    cli.addPositionalArgument(QStringLiteral("mode"), QStringLiteral("scheme | b64 | json (default scheme)."));
    const QCommandLineOption tilesOpt(QStringLiteral("tiles"),
        QStringLiteral("Basemap: none, synth, an XYZ tile directory or an .mbtiles file."),
        QStringLiteral("spec"), QStringLiteral("synth"));
    const QCommandLineOption labelsOpt(QStringLiteral("labels"),
        QStringLiteral("Entities with a text label."), QStringLiteral("n"), QStringLiteral("1000"));
    const QCommandLineOption trailsOpt(QStringLiteral("trails"),
        QStringLiteral("Entities with a track trail."), QStringLiteral("n"), QStringLiteral("1000"));
    const QCommandLineOption trailLenOpt(QStringLiteral("trail-len"),
        QStringLiteral("Points per trail (2 samples/s)."), QStringLiteral("k"), QStringLiteral("60"));
    const QCommandLineOption scatterOpt(QStringLiteral("scatter"),
        QStringLiteral("Draw entities as dots instead of heading icons."));
    const QCommandLineOption autopickOpt(QStringLiteral("autopick"),
        QStringLiteral("Run a synthetic pick every frame and report its cost."));
    cli.addOptions({ tilesOpt, labelsOpt, trailsOpt, trailLenOpt, scatterOpt, autopickOpt });
    cli.process(app);

    const QStringList pos = cli.positionalArguments();
    const int n = qMax(1, pos.value(0, QStringLiteral("5000")).toInt());
    const int hz = qBound(1, pos.value(1, QStringLiteral("30")).toInt(), 240);
    const QString mode = pos.value(2, QStringLiteral("scheme"));
    if (mode != QLatin1String("scheme") && mode != QLatin1String("b64")
        && mode != QLatin1String("json")) {
        qCritical("mode must be scheme, b64 or json");
        return 1;
    }

    TileScheme tiles;
    QString error;
    if (!tiles.open(cli.value(tilesOpt), &error)) {
        qCritical().noquote() << error;
        return 1;
    }

    Sim sim(n, tiles.bounds());
    sim.step();
    Bridge bridge(&sim, &tiles);
    SimScheme simHandler(&sim);
    QWebEngineProfile::defaultProfile()->installUrlSchemeHandler("sim", &simHandler);
    QWebEngineProfile::defaultProfile()->installUrlSchemeHandler("tiles", &tiles);

    QTimer tick;
    tick.setTimerType(Qt::PreciseTimer);
    QObject::connect(&tick, &QTimer::timeout, [&] {
        sim.step();
        if (mode != QLatin1String("scheme"))
            bridge.push(mode);
    });
    tick.start(1000 / hz);

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("mode"), mode);
    query.addQueryItem(QStringLiteral("n"), QString::number(n));
    query.addQueryItem(QStringLiteral("hz"), QString::number(hz));
    query.addQueryItem(QStringLiteral("basemap"),
                       tiles.meta().value("format").toString() == QLatin1String("none")
                           ? QStringLiteral("0") : QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("labels"), QString::number(qMax(0, cli.value(labelsOpt).toInt())));
    query.addQueryItem(QStringLiteral("trails"), QString::number(qMax(0, cli.value(trailsOpt).toInt())));
    query.addQueryItem(QStringLiteral("trailLen"), QString::number(qMax(2, cli.value(trailLenOpt).toInt())));
    query.addQueryItem(QStringLiteral("icons"), cli.isSet(scatterOpt) ? QStringLiteral("0") : QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("autopick"), cli.isSet(autopickOpt) ? QStringLiteral("1") : QStringLiteral("0"));
    QUrl url(QStringLiteral("qrc:/web/index.html"));
    url.setQuery(query);

    QWebEngineView view;
    QWebChannel channel;
    channel.registerObject(QStringLiteral("bridge"), &bridge);
    view.page()->setWebChannel(&channel);
    view.setUrl(url);
    view.resize(1600, 900);
    view.setWindowTitle(QStringLiteral("deckgl_bench %1 N=%2 %3Hz tiles=%4")
                            .arg(mode).arg(n).arg(hz).arg(cli.value(tilesOpt)));
    view.show();
    return app.exec();
}

#include "main.moc"
