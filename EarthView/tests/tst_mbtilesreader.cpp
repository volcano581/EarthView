#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtTest>
#include <atomic>
#include <thread>

#include "MapSourceCatalog.h"
#include "MbTilesReader.h"

/**
 * @brief Tests MBTiles reading as used for a large file on the imagery server's share:
 * per-thread persistent connections, immutable URIs, and `mbtiles` entries in earth files.
 */
class TestMbTilesReader : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void immutableUriForPathKinds_data();
    void immutableUriForPathKinds();
    void readsTileAndMissingTile();
    void readsFromReaderPoolThreads();
    void earthFileMbTilesEntry();
    void earthFileSkipsUnreadableMbTiles();
    void requestSetIsThreadSafe();

private:
    QTemporaryDir m_dir;
    QString m_mbTilesPath;
};

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
