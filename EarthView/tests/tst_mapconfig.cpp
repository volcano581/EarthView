#include "earthview3d/MapConfig.h"

#include <QtTest>
#include <QTemporaryDir>

using earthview3d::MapConfig;

/**
 * @brief Unit tests for MapConfig parsing, path resolution and validation.
 */
class TestMapConfig : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void resolvesRelativeToDataRoot();
    void absolutePathsUnchanged();
    void emptyCacheMeansNoCache();
    void parseFullConfig();
    void parseRelativeDataRootAgainstFile();
    void parseDefaultsDataRootToConfigFolder();
    void parseErrors_data();
    void parseErrors();
    void validateChecksFiles();
    void skyTimeDefaultsToNow();

private:
    QTemporaryDir m_dir;
    QString m_data;   // <tmp>/data with maps/test.earth
};

void TestMapConfig::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_data = QDir(m_dir.path()).filePath(QStringLiteral("data"));
    QVERIFY(QDir().mkpath(QDir(m_data).filePath(QStringLiteral("maps"))));
    QFile earth(QDir(m_data).filePath(QStringLiteral("maps/test.earth")));
    QVERIFY(earth.open(QIODevice::WriteOnly));
    earth.write("<map version=\"3\"/>");
}

void TestMapConfig::resolvesRelativeToDataRoot()
{
    MapConfig config;
    config.dataRoot = m_data;
    config.earthFile = QStringLiteral("maps/../maps/test.earth");
    config.cacheDir = QStringLiteral("cache");
    QCOMPARE(config.resolvedEarthFile(), QDir(m_data).filePath(QStringLiteral("maps/test.earth")));
    QCOMPARE(config.resolvedCacheDir(), QDir(m_data).filePath(QStringLiteral("cache")));
}

void TestMapConfig::absolutePathsUnchanged()
{
    const QString earth = QDir(m_data).filePath(QStringLiteral("maps/test.earth"));
    MapConfig config;
    config.dataRoot = QStringLiteral("/somewhere/else");
    config.earthFile = earth;
    QCOMPARE(config.resolvedEarthFile(), earth);
}

void TestMapConfig::emptyCacheMeansNoCache()
{
    MapConfig config;
    config.dataRoot = m_data;
    QVERIFY(config.resolvedCacheDir().isEmpty());
    QVERIFY(config.resolvedEarthFile().isEmpty());
}

void TestMapConfig::parseFullConfig()
{
    const QByteArray json = R"({"earthFile": "maps/test.earth", "dataRoot": ")" + m_data.toUtf8()
        + R"(", "cacheDir": "cache", "sky": false, "skyTime": "2026-06-01T07:30:00Z"})";
    MapConfig config;
    QString error;
    QVERIFY2(MapConfig::fromJson(json, QString(), &config, &error), qPrintable(error));
    QCOMPARE(config.earthFile, QStringLiteral("maps/test.earth"));
    QCOMPARE(config.dataRoot, QDir::cleanPath(m_data));
    QCOMPARE(config.cacheDir, QStringLiteral("cache"));
    QCOMPARE(config.sky, false);
    QCOMPARE(config.skyTime, QDateTime(QDate(2026, 6, 1), QTime(7, 30), QTimeZone::utc()));
    QVERIFY(config.validate(&error));
}

void TestMapConfig::parseRelativeDataRootAgainstFile()
{
    // <tmp>/config/map.json with "dataRoot": "../data"
    const QString configDir = QDir(m_dir.path()).filePath(QStringLiteral("config"));
    QVERIFY(QDir().mkpath(configDir));
    QFile file(QDir(configDir).filePath(QStringLiteral("map.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"earthFile": "maps/test.earth", "dataRoot": "../data"})");
    file.close();

    MapConfig config;
    QString error;
    QVERIFY2(MapConfig::fromJsonFile(file.fileName(), &config, &error), qPrintable(error));
    QCOMPARE(config.dataRoot, QDir::cleanPath(m_data));
    QCOMPARE(config.sky, true);
    QVERIFY(!config.skyTime.isValid());
    QVERIFY2(config.validate(&error), qPrintable(error));
}

void TestMapConfig::parseDefaultsDataRootToConfigFolder()
{
    MapConfig config;
    QString error;
    QVERIFY(MapConfig::fromJson(R"({"earthFile": "maps/test.earth"})", m_data, &config, &error));
    QCOMPARE(config.dataRoot, QDir::cleanPath(m_data));
    QVERIFY(config.validate());
}

void TestMapConfig::parseErrors_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("expected");
    QTest::newRow("not json") << QByteArray("{") << QStringLiteral("Invalid JSON");
    QTest::newRow("array") << QByteArray("[]") << QStringLiteral("JSON object");
    QTest::newRow("missing earth") << QByteArray("{}") << QStringLiteral("required");
    QTest::newRow("unknown key") << QByteArray(R"({"earthFile":"a","skyy":true})") << QStringLiteral("skyy");
    QTest::newRow("bad type") << QByteArray(R"({"earthFile":1})") << QStringLiteral("must be a string");
    QTest::newRow("bad sky") << QByteArray(R"({"earthFile":"a","sky":"yes"})") << QStringLiteral("true or false");
    QTest::newRow("bad time") << QByteArray(R"({"earthFile":"a","skyTime":"noon"})") << QStringLiteral("ISO 8601");
}

void TestMapConfig::parseErrors()
{
    QFETCH(QByteArray, json);
    QFETCH(QString, expected);
    MapConfig config;
    config.earthFile = QStringLiteral("untouched");
    QString error;
    QVERIFY(!MapConfig::fromJson(json, m_data, &config, &error));
    QVERIFY2(error.contains(expected), qPrintable(error));
    QCOMPARE(config.earthFile, QStringLiteral("untouched"));
}

void TestMapConfig::validateChecksFiles()
{
    QString error;
    MapConfig config;
    QVERIFY(!config.validate(&error));
    QVERIFY(error.contains(QStringLiteral("No earth file")));

    config.dataRoot = QDir(m_dir.path()).filePath(QStringLiteral("missing"));
    config.earthFile = QStringLiteral("maps/test.earth");
    QVERIFY(!config.validate(&error));
    QVERIFY(error.contains(QStringLiteral("Data root")));

    config.dataRoot = m_data;
    config.earthFile = QStringLiteral("maps/none.earth");
    QVERIFY(!config.validate(&error));
    QVERIFY(error.contains(QStringLiteral("none.earth")));

    config.earthFile = QStringLiteral("maps/test.earth");
    QVERIFY(config.validate(&error));
}

void TestMapConfig::skyTimeDefaultsToNow()
{
    MapConfig config;
    const QDateTime before = QDateTime::currentDateTimeUtc();
    const QDateTime t = config.effectiveSkyTime();
    QVERIFY(t >= before.addSecs(-1));
    QCOMPARE(t.timeSpec(), Qt::UTC);

    config.skyTime = QDateTime(QDate(2026, 1, 2), QTime(3, 4), QTimeZone::utc());
    QCOMPARE(config.effectiveSkyTime(), config.skyTime);
}

QTEST_GUILESS_MAIN(TestMapConfig)
#include "tst_mapconfig.moc"
