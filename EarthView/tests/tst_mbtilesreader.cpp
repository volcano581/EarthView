#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QRandomGenerator>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QtTest>
#include <atomic>
#include <thread>

#include "MapSourceCatalog.h"
#include "MbTilesReader.h"
#include "RemoteMbTiles.h"

/**
 * @brief A minimal HTTP/1.1 file server with Range support (keep-alive), standing in for the
 * imagery server's web server. Paths under /norange/ ignore Range, as some servers do.
 */
class RangeHttpServer : public QObject
{
    Q_OBJECT

public:
    QHash<QString, QByteArray> files;   ///< path -> body
    std::atomic<qint64> bytesServed{ 0 };
    std::atomic<int> requests{ 0 };

    ~RangeHttpServer() override
    {
        // Clients may still hold keep-alive connections. The sockets are deleted with m_server,
        // after m_buffers is gone, and a socket closing then emits disconnected(): its handler
        // must not run on the half-destroyed server (it crashed on Linux).
        for (QTcpSocket* socket : m_server.findChildren<QTcpSocket*>())
            socket->disconnect(this);
    }

    Q_INVOKABLE quint16 start()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { serve(socket); });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
                    m_buffers.remove(socket);
                    socket->deleteLater();
                });
            }
        });
        return m_server.listen(QHostAddress::LocalHost, 0) ? m_server.serverPort() : 0;
    }

private:
    void serve(QTcpSocket* socket)
    {
        QByteArray& buffer = m_buffers[socket];
        buffer += socket->readAll();
        int end = 0;
        while ((end = buffer.indexOf("\r\n\r\n")) >= 0) {
            const QByteArray head = buffer.left(end);
            buffer.remove(0, end + 4);
            ++requests;
            const QList<QByteArray> lines = head.split('\n');
            const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
            QString path = QString::fromLatin1(requestLine.value(1));
            const bool ignoreRange = path.startsWith("/norange/");
            if (ignoreRange)
                path.remove(0, 8);
            QByteArray range;
            for (const QByteArray& line : lines) {
                if (line.toLower().startsWith("range:"))
                    range = line.mid(6).trimmed();
            }

            if (!files.contains(path)) {
                socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
                continue;
            }
            const QByteArray body = files.value(path);
            QByteArray status = "200 OK";
            QByteArray part = body;
            QByteArray extra;
            if (!range.isEmpty() && !ignoreRange && range.startsWith("bytes=")) {
                const QList<QByteArray> bounds = range.mid(6).split('-');
                const qint64 first = bounds.value(0).toLongLong();
                const qint64 last = qMin<qint64>(bounds.value(1).toLongLong(), body.size() - 1);
                part = body.mid(first, last - first + 1);
                status = "206 Partial Content";
                extra = "Content-Range: bytes " + QByteArray::number(first) + '-' + QByteArray::number(last)
                    + '/' + QByteArray::number(body.size()) + "\r\n";
            }
            bytesServed += part.size();
            socket->write("HTTP/1.1 " + status + "\r\nContent-Length: " + QByteArray::number(part.size())
                          + "\r\nContent-Type: application/octet-stream\r\n" + extra + "\r\n");
            socket->write(part);
        }
    }

    QTcpServer m_server{ this };  // a child, so it moves to the server thread with us
    QHash<QTcpSocket*, QByteArray> m_buffers;
};

/**
 * @brief Tests MBTiles reading as used for a large file on the imagery server's share:
 * per-thread persistent connections, immutable URIs, and `mbtiles` entries in earth files;
 * and the same files read by URL from its web server (range requests, three file layouts).
 */
class TestMbTilesReader : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void immutableUriForPathKinds_data();
    void immutableUriForPathKinds();
    void readsTileAndMissingTile();
    void readsFromReaderPoolThreads();
    void earthFileMbTilesEntry();
    void earthFileSkipsUnreadableMbTiles();
    void requestSetIsThreadSafe();
    void remoteReadsEveryLayout_data();
    void remoteReadsEveryLayout();
    void remoteReadsFetchOnlyWhatIsNeeded();
    void remoteReadsFromReaderPoolThreads();
    void remoteServerWithoutRangeSupport();
    void earthFileMbTilesUrlEntry();
    void tileServerUrlEntries();

private:
    QString remoteUrl(const QString& path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(m_port).arg(path);
    }
    bool makeLayout(const QString& path, const QString& layout);

    QTemporaryDir m_dir;
    QString m_mbTilesPath;
    QThread m_serverThread;
    RangeHttpServer* m_server = nullptr;
    quint16 m_port = 0;
    // Expected contents of the generated layouts: (z, x, tms row) -> tile_data.
    QHash<QString, QByteArray> m_expected;
};

namespace {
QString tileKey(int z, int x, int row)
{
    return QStringLiteral("%1/%2/%3").arg(z).arg(x).arg(row);
}

QByteArray tileBlob(int z, int x, int row)
{
    // Sizes from a few bytes to several pages, so cells spill into overflow chains. Unsigned:
    // for the z12 rows the sum overflows an int.
    const quint32 mix = quint32(z) * 7919u + quint32(x) * 104729u + quint32(row) * 1299709u;
    const int size = 1 + int(mix % 23000u);
    QByteArray blob(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i)
        blob[i] = char((i * 31 + z + x * 3 + row * 5) & 0xff);
    return blob;
}
}

bool TestMbTilesReader::makeLayout(const QString& path, const QString& layout)
{
    const QString connection = "layout_" + layout;
    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(path);
        if (!db.open())
            return false;
        QSqlQuery q(db);
        auto exec = [&q, &ok](const QString& sql) {
            if (!q.exec(sql)) {
                qWarning() << sql << q.lastError().text();
                ok = false;
            }
        };
        exec("create table metadata (name text, value text)");
        exec("insert into metadata values ('name','layout " + layout + "'),('format','pbf'),"
             "('minzoom','0'),('maxzoom','12'),('scheme','tms'),"
             "('json','{\"vector_layers\":[{\"id\":\"water\"},{\"id\":\"transportation\"},{\"id\":\"place\"}]}')");
        if (layout == "table") {
            exec("create table tiles (zoom_level integer, tile_column integer, tile_row integer, tile_data blob)");
            exec("create unique index tile_index on tiles (zoom_level, tile_column, tile_row)");
        }
        else if (layout == "pk") {
            // Composite primary key on a rowid table: an automatic index with no SQL.
            exec("create table tiles (zoom_level integer, tile_column integer, tile_row integer, tile_data blob, "
                 "primary key (zoom_level, tile_column, tile_row))");
        }
        else if (layout == "mapimages") {
            exec("create table map (zoom_level integer, tile_column integer, tile_row integer, tile_id text)");
            exec("create table images (tile_data blob, tile_id text)");
            exec("create unique index map_index on map (zoom_level, tile_column, tile_row)");
            exec("create unique index images_id on images (tile_id)");
            exec("CREATE VIEW tiles AS SELECT map.zoom_level AS zoom_level, map.tile_column AS tile_column, "
                 "map.tile_row AS tile_row, images.tile_data AS tile_data FROM map "
                 "JOIN images ON images.tile_id = map.tile_id");
        }
        else if (layout == "planetiler") {
            exec("create table tiles_shallow (zoom_level integer, tile_column integer, tile_row integer, "
                 "tile_data_id integer, primary key(zoom_level, tile_column, tile_row)) without rowid");
            exec("create table tiles_data (tile_data_id integer primary key, tile_data blob)");
            exec("create view tiles AS select tiles_shallow.zoom_level as zoom_level, "
                 "tiles_shallow.tile_column as tile_column, tiles_shallow.tile_row as tile_row, "
                 "tiles_data.tile_data as tile_data from tiles_shallow "
                 "join tiles_data on tiles_shallow.tile_data_id = tiles_data.tile_data_id");
        }

        db.transaction();
        int id = 0;
        // Enough rows for multi-level B-trees: z0..z5 complete, and a band of z12.
        auto insert = [&](int z, int x, int row) {
            const QByteArray blob = tileBlob(z, x, row);
            m_expected.insert(tileKey(z, x, row), blob);
            ++id;
            if (layout == "table" || layout == "pk") {
                q.prepare("insert into tiles values (?, ?, ?, ?)");
                q.addBindValue(z); q.addBindValue(x); q.addBindValue(row); q.addBindValue(blob);
                ok = q.exec() && ok;
            }
            else if (layout == "mapimages") {
                const QString tileId = QString::number(qHash(blob), 16) + "_" + QString::number(id);
                q.prepare("insert into map values (?, ?, ?, ?)");
                q.addBindValue(z); q.addBindValue(x); q.addBindValue(row); q.addBindValue(tileId);
                ok = q.exec() && ok;
                q.prepare("insert into images values (?, ?)");
                q.addBindValue(blob); q.addBindValue(tileId);
                ok = q.exec() && ok;
            }
            else {
                q.prepare("insert into tiles_data values (?, ?)");
                q.addBindValue(id * 3); q.addBindValue(blob);
                ok = q.exec() && ok;
                q.prepare("insert into tiles_shallow values (?, ?, ?, ?)");
                q.addBindValue(z); q.addBindValue(x); q.addBindValue(row); q.addBindValue(id * 3);
                ok = q.exec() && ok;
            }
        };
        for (int z = 0; z <= 5; ++z) {
            for (int x = 0; x < (1 << z); ++x) {
                for (int row = 0; row < (1 << z); ++row)
                    insert(z, x, row);
            }
        }
        for (int x = 2800; x < 2840; ++x) {
            for (int row = 2500; row < 2540; ++row)
                insert(12, x, row);
        }
        db.commit();
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return ok;
}

void TestMbTilesReader::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_mbTilesPath = m_dir.filePath("planet small.mbtiles");  // space: exercises URI escaping

    QImage image(256, 256, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(image.save(&buffer, "PNG"));

    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "fixture");
        db.setDatabaseName(m_mbTilesPath);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec("create table metadata (name text, value text)"));
        QVERIFY(q.exec("create table tiles (zoom_level integer, tile_column integer, tile_row integer, tile_data blob)"));
        QVERIFY(q.exec("create unique index tile_index on tiles (zoom_level, tile_column, tile_row)"));
        QVERIFY(q.exec("insert into metadata values ('name','planet'),('format','png'),('minzoom','0'),"
                       "('maxzoom','2'),('scheme','tms')"));
        QVERIFY(q.prepare("insert into tiles values (?, ?, ?, ?)"));
        // TMS row 3 at z2 = XYZ y 0.
        q.addBindValue(2);
        q.addBindValue(1);
        q.addBindValue(3);
        q.addBindValue(png);
        QVERIFY(q.exec());
        db.close();
    }
    QSqlDatabase::removeDatabase("fixture");

    // The web server: every layout, plus the small fixture and a few tile-service documents.
    m_server = new RangeHttpServer;
    for (const QString& layout : { QString("table"), QString("pk"), QString("mapimages"), QString("planetiler") }) {
        const QString path = m_dir.filePath(layout + ".mbtiles");
        QVERIFY2(makeLayout(path, layout), qPrintable(layout));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        m_server->files.insert("/maps/" + layout + ".mbtiles", file.readAll());
    }
    QFile small(m_mbTilesPath);
    QVERIFY(small.open(QIODevice::ReadOnly));
    m_server->files.insert("/maps/small.mbtile", small.readAll());
    m_server->files.insert("/osm/tiles.json",
        "{\"tilejson\":\"2.2.0\",\"name\":\"osm\",\"format\":\"pbf\",\"minzoom\":0,\"maxzoom\":14,"
        "\"tiles\":[\"v/{z}/{x}/{y}.pbf\"],\"vector_layers\":[{\"id\":\"poi\"},{\"id\":\"building\"}]}");
    m_server->files.insert("/osm/tilemapresource.xml",
        "<?xml version=\"1.0\"?><TileMap version=\"1.0.0\"><Title>osm</Title>"
        "<TileFormat width=\"256\" height=\"256\" mime-type=\"application/x-protobuf\" extension=\"pbf\"/>"
        "<TileSets profile=\"mercator\"><TileSet href=\"0\" order=\"0\"/><TileSet href=\"13\" order=\"13\"/>"
        "</TileSets></TileMap>");
    m_server->moveToThread(&m_serverThread);
    m_serverThread.start();
    QMetaObject::invokeMethod(m_server, "start", Qt::BlockingQueuedConnection, Q_RETURN_ARG(quint16, m_port));
    QVERIFY(m_port != 0);
}

void TestMbTilesReader::cleanupTestCase()
{
    QMetaObject::invokeMethod(m_server, "deleteLater");
    m_serverThread.quit();
    m_serverThread.wait();
}

void TestMbTilesReader::remoteReadsEveryLayout_data()
{
    QTest::addColumn<QString>("layout");
    QTest::newRow("tiles table + index") << QString("table");
    QTest::newRow("tiles with primary key") << QString("pk");
    QTest::newRow("map + images view") << QString("mapimages");
    QTest::newRow("planetiler") << QString("planetiler");
}

void TestMbTilesReader::remoteReadsEveryLayout()
{
    QFETCH(QString, layout);
    const QString url = remoteUrl("/maps/" + layout + ".mbtiles");

    QString error;
    const MbTilesMetadata metadata = MbTilesReader::readMetadata(url, &error);
    QVERIFY2(metadata.valid, qPrintable(error));
    QCOMPARE(metadata.name, "layout " + layout);
    QCOMPARE(metadata.format, QString("pbf"));
    QCOMPARE(metadata.maxZoom, 12);
    // The layers the file declares: the Layers panel lists them.
    QCOMPARE(metadata.vectorLayers, QStringList({ "water", "transportation", "place" }));

    // Every tile, byte for byte as SQLite stored it.
    for (auto it = m_expected.cbegin(); it != m_expected.cend(); ++it) {
        const QStringList zxy = it.key().split('/');
        const QByteArray data = RemoteMbTiles::readTile(url, zxy[0].toInt(), zxy[1].toInt(), zxy[2].toInt(), &error);
        QVERIFY2(data == it.value(), qPrintable(QString("%1 %2 %3").arg(layout, it.key(), error)));
    }
    // A tile the file does not have is empty, without an error.
    error.clear();
    QVERIFY(RemoteMbTiles::readTile(url, 12, 0, 0, &error).isEmpty());
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(RemoteMbTiles::readTile(url, 7, 3, 3, &error).isEmpty());
}

void TestMbTilesReader::remoteReadsFetchOnlyWhatIsNeeded()
{
    RemoteMbTiles::clearCaches();
    const QString path = "/maps/mapimages.mbtiles";
    const qint64 fileSize = m_server->files.value(path).size();
    const qint64 before = m_server->bytesServed;
    QString error;
    const QByteArray data = RemoteMbTiles::readTile(remoteUrl(path), 12, 2810, 2510, &error);
    QCOMPARE(data, m_expected.value(tileKey(12, 2810, 2510)));
    const qint64 served = m_server->bytesServed - before;
    QVERIFY2(served < fileSize / 2, qPrintable(QString("%1 of %2 bytes").arg(served).arg(fileSize)));
}

void TestMbTilesReader::remoteReadsFromReaderPoolThreads()
{
    RemoteMbTiles::clearCaches();
    const QString url = remoteUrl("/maps/planetiler.mbtiles");
    std::atomic<int> matched{ 0 };
    const QList<QString> keys = m_expected.keys().mid(0, 200);
    for (const QString& key : keys) {
        MbTilesReader::threadPool()->start([this, url, key, &matched]() {
            const QStringList zxy = key.split('/');
            if (RemoteMbTiles::readTile(url, zxy[0].toInt(), zxy[1].toInt(), zxy[2].toInt()) == m_expected.value(key))
                ++matched;
        });
    }
    MbTilesReader::threadPool()->waitForDone();
    QCOMPARE(matched.load(), int(keys.size()));
}

void TestMbTilesReader::remoteServerWithoutRangeSupport()
{
    RemoteMbTiles::clearCaches();
    // Small file: read once, whole.
    QString error;
    const QImage tile = MbTilesReader::readTileImage(remoteUrl("/norange/maps/small.mbtile"), 2, 1, 0, true, "png", &error);
    QVERIFY2(!tile.isNull(), qPrintable(error));
    QCOMPARE(tile.pixelColor(10, 10), QColor(Qt::red));
}

void TestMbTilesReader::earthFileMbTilesUrlEntry()
{
    RemoteMbTiles::clearCaches();
    const QString earthPath = m_dir.filePath("osm_url.earth");
    QFile earth(earthPath);
    QVERIFY(earth.open(QIODevice::WriteOnly | QIODevice::Text));
    earth.write(QString("<image name=\"OSM\" driver=\"mbtiles\"> <url> %1 </url></image>\n")
                    .arg(remoteUrl("/maps/table.mbtiles")).toUtf8());
    // ".mbtile" and any other name: recognised by the file's SQLite header.
    earth.write(QString("<image name=\"Small\" driver=\"mbtiles\"> <url> %1 </url></image>\n")
                    .arg(remoteUrl("/maps/small.mbtile")).toUtf8());
    earth.close();

    const QList<TmsLoader::TileSourceLayer> layers = MapSourceCatalog::readEarthLayers(earthPath);
    QCOMPARE(layers.size(), 2);
    QCOMPARE(layers.at(0).sourceType, TmsLoader::TileSourceLayer::SourceType::VectorMbTiles);
    QCOMPARE(layers.at(0).mbTilesPath, remoteUrl("/maps/table.mbtiles"));
    QCOMPARE(layers.at(0).vectorLayers, QStringList({ "water", "transportation", "place" }));
    QCOMPARE(layers.at(0).maxTileZoom, 12);
    // No min_level / max_level: shown at every zoom (it used to be -1..-1, never drawn).
    QCOMPARE(layers.at(0).minZoom, 0);
    QCOMPARE(layers.at(0).maxZoom, int(GIS::MAX_TILE_ZOOM));
    QCOMPARE(layers.at(1).sourceType, TmsLoader::TileSourceLayer::SourceType::MbTiles);

    MapSourceCatalog::Source source;
    source.filePath = earthPath;
    source.layers = layers;
    QVERIFY(MapSourceCatalog::isMbTilesSource(source));

    // The vector path reads the same bytes through the reader pool's API.
    const QByteArray expected = m_expected.value(tileKey(3, 2, 5));
    QString error;
    QCOMPARE(RemoteMbTiles::readTile(layers.at(0).mbTilesPath, 3, 2, 5, &error), expected);
}

void TestMbTilesReader::tileServerUrlEntries()
{
    TmsLoader::TileSourceLayer layer;
    QString error;
    QVERIFY2(MapSourceCatalog::urlMbTilesLayer(remoteUrl("/osm/tiles.json"), &layer, &error), qPrintable(error));
    QCOMPARE(layer.sourceType, TmsLoader::TileSourceLayer::SourceType::VectorMbTiles);
    QVERIFY(layer.mbTilesPath.isEmpty());
    QCOMPARE(layer.urlTemplate, remoteUrl("/osm/v/{z}/{x}/{y}.pbf"));
    QCOMPARE(layer.maxTileZoom, 14);
    QVERIFY(!layer.tmsYOrigin);
    QCOMPARE(layer.vectorLayers, QStringList({ "poi", "building" }));

    layer = TmsLoader::TileSourceLayer();
    QVERIFY2(MapSourceCatalog::urlMbTilesLayer(remoteUrl("/osm/tilemapresource.xml"), &layer, &error), qPrintable(error));
    QCOMPARE(layer.sourceType, TmsLoader::TileSourceLayer::SourceType::VectorMbTiles);
    QCOMPARE(layer.urlTemplate, remoteUrl("/osm/{z}/{x}/{y}.pbf"));
    QCOMPARE(layer.maxTileZoom, 13);
    QVERIFY(layer.tmsYOrigin);

    layer = TmsLoader::TileSourceLayer();
    QVERIFY(MapSourceCatalog::urlMbTilesLayer("http://127.0.0.1:1/osm/{z}/{x}/{-y}.png", &layer, &error));
    QCOMPARE(layer.sourceType, TmsLoader::TileSourceLayer::SourceType::UrlTemplate);
    QCOMPARE(layer.urlTemplate, QString("http://127.0.0.1:1/osm/{z}/{x}/{y}.png"));
    QVERIFY(layer.tmsYOrigin);

    QVERIFY(!MapSourceCatalog::urlMbTilesLayer(remoteUrl("/nothing/here.mbtiles"), &layer, &error));
    QVERIFY(!error.isEmpty());
}

void TestMbTilesReader::immutableUriForPathKinds_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("uri");
    QTest::newRow("unc") << QString("\\\\imagery-server\\maps\\planet.mbtiles")
                         << QString("file:////imagery-server/maps/planet.mbtiles?mode=ro&immutable=1");
    QTest::newRow("drive") << QString("M:/maps/planet.mbtiles")
                           << QString("file:///M:/maps/planet.mbtiles?mode=ro&immutable=1");
    QTest::newRow("unix") << QString("/mnt/maps/planet.mbtiles")
                          << QString("file:///mnt/maps/planet.mbtiles?mode=ro&immutable=1");
    QTest::newRow("escaped") << QString("M:/my maps/a#b?.mbtiles")
                             << QString("file:///M:/my%20maps/a%23b%3f.mbtiles?mode=ro&immutable=1");
}

void TestMbTilesReader::immutableUriForPathKinds()
{
    QFETCH(QString, path);
    QFETCH(QString, uri);
#ifndef Q_OS_WIN
    if (path.contains('\\'))
        QSKIP("Backslash separators are Windows paths");
#endif
    QCOMPARE(MbTilesReader::immutableUri(path), uri);
}

void TestMbTilesReader::readsTileAndMissingTile()
{
    QString error;
    const QImage tile = MbTilesReader::readTileImage(m_mbTilesPath, 2, 1, 0, true, "png", &error);
    QVERIFY2(!tile.isNull(), qPrintable(error));
    QCOMPARE(tile.pixelColor(10, 10), QColor(Qt::red));

    // Same thread, same file: the second read reuses the open connection.
    QVERIFY(MbTilesReader::readTileImage(m_mbTilesPath, 2, 0, 0, true, "png", &error).isNull());
    QVERIFY(!MbTilesReader::readTileImage(m_mbTilesPath, 2, 1, 0, true, "png", &error).isNull());
}

void TestMbTilesReader::readsFromReaderPoolThreads()
{
    QThreadPool* pool = MbTilesReader::threadPool();
    QCOMPARE(pool->expiryTimeout(), -1);
    std::atomic<int> found{ 0 };
    for (int i = 0; i < 32; ++i) {
        pool->start([this, &found]() {
            if (!MbTilesReader::readTileImage(m_mbTilesPath, 2, 1, 0, true, "png").isNull())
                ++found;
        });
    }
    pool->waitForDone();
    QCOMPARE(found.load(), 32);
}

void TestMbTilesReader::earthFileMbTilesEntry()
{
    // Relative to the earth file, as a share-mounted folder layout would be.
    const QString earthPath = m_dir.filePath("osm_mbtiles.earth");
    QFile earth(earthPath);
    QVERIFY(earth.open(QIODevice::WriteOnly | QIODevice::Text));
    earth.write("<image name=\"OSM\" driver=\"mbtiles\"> <url> planet small.mbtiles </url></image>\n");
    earth.close();

    const QList<TmsLoader::TileSourceLayer> layers = MapSourceCatalog::readEarthLayers(earthPath);
    QCOMPARE(layers.size(), 1);
    const TmsLoader::TileSourceLayer& layer = layers.first();
    QCOMPARE(layer.sourceType, TmsLoader::TileSourceLayer::SourceType::MbTiles);
    QCOMPARE(layer.name, QString("OSM"));
    QCOMPARE(QFileInfo(layer.mbTilesPath).canonicalFilePath(), QFileInfo(m_mbTilesPath).canonicalFilePath());
    QCOMPARE(layer.maxTileZoom, 2);
    QVERIFY(layer.tmsYOrigin);

    MapSourceCatalog::Source source;
    source.filePath = earthPath;
    source.layers = layers;
    QVERIFY(MapSourceCatalog::isMbTilesSource(source));
}

void TestMbTilesReader::earthFileSkipsUnreadableMbTiles()
{
    const QString earthPath = m_dir.filePath("missing.earth");
    QFile earth(earthPath);
    QVERIFY(earth.open(QIODevice::WriteOnly | QIODevice::Text));
    earth.write("<image name=\"OSM\" driver=\"mbtiles\"><url>no_such_file.mbtiles</url></image>\n");
    earth.close();
    QVERIFY(MapSourceCatalog::readEarthLayers(earthPath).isEmpty());
}

void TestMbTilesReader::requestSetIsThreadSafe()
{
    MbTilesRequestSet set;
    set.insert("a");
    QVERIFY(set.contains("a"));
    std::thread writer([&set]() {
        for (int i = 0; i < 10000; ++i) {
            set.insert(QString::number(i));
            set.remove(QString::number(i));
        }
    });
    for (int i = 0; i < 10000; ++i)
        set.contains(QString::number(i));
    writer.join();
    set.clear();
    QVERIFY(!set.contains("a"));
}

QTEST_MAIN(TestMbTilesReader)
#include "tst_mbtilesreader.moc"
