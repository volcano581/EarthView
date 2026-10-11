#include "earthview3d/RuntimePaths.h"

#include <QtTest>
#include <QTemporaryDir>

using earthview3d::RuntimePaths;

/**
 * @brief Unit tests for RuntimePaths (GDAL/PROJ data environment for osgEarth).
 */
class TestRuntimePaths : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void setsUnsetVariablesToExistingDirs();
    void explicitEnvironmentWins();
    void skipsMissingOrEmptyDirs();

private:
    QTemporaryDir m_dir;
};

void TestRuntimePaths::init()
{
    qunsetenv("GDAL_DATA");
    qunsetenv("PROJ_DATA");
    qunsetenv("PROJ_LIB");
}

void TestRuntimePaths::setsUnsetVariablesToExistingDirs()
{
    QVERIFY(m_dir.isValid());
    const QString gdal = QDir(m_dir.path()).filePath(QStringLiteral("gdal"));
    const QString proj = QDir(m_dir.path()).filePath(QStringLiteral("proj"));
    QVERIFY(QDir().mkpath(gdal));
    QVERIFY(QDir().mkpath(proj));

    RuntimePaths paths;
    paths.gdalDataDir = gdal;
    paths.projDataDir = proj;
    const QStringList set = paths.applyDataEnvironment();
    QCOMPARE(set, (QStringList{QStringLiteral("GDAL_DATA"), QStringLiteral("PROJ_DATA"), QStringLiteral("PROJ_LIB")}));
    QCOMPARE(QDir::fromNativeSeparators(qEnvironmentVariable("GDAL_DATA")), QDir::cleanPath(gdal));
    QCOMPARE(QDir::fromNativeSeparators(qEnvironmentVariable("PROJ_DATA")), QDir::cleanPath(proj));
    QCOMPARE(QDir::fromNativeSeparators(qEnvironmentVariable("PROJ_LIB")), QDir::cleanPath(proj));
}

void TestRuntimePaths::explicitEnvironmentWins()
{
    qputenv("GDAL_DATA", "explicit");
    RuntimePaths paths;
    paths.gdalDataDir = m_dir.path();
    const QStringList set = paths.applyDataEnvironment();
    QVERIFY(!set.contains(QStringLiteral("GDAL_DATA")));
    QCOMPARE(qEnvironmentVariable("GDAL_DATA"), QStringLiteral("explicit"));
}

void TestRuntimePaths::skipsMissingOrEmptyDirs()
{
    RuntimePaths paths;
    paths.gdalDataDir = QDir(m_dir.path()).filePath(QStringLiteral("does-not-exist"));
    QVERIFY(paths.applyDataEnvironment().isEmpty());
    QVERIFY(qEnvironmentVariableIsEmpty("GDAL_DATA"));
    QVERIFY(qEnvironmentVariableIsEmpty("PROJ_DATA"));
}

QTEST_GUILESS_MAIN(TestRuntimePaths)
#include "tst_runtimepaths.moc"
