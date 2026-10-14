#include <earthview3d/DtedMosaic.h>

#include <QtTest>

#include <memory>

using namespace earthview3d;

/**
 * @brief Indexing a folder of DTED cells as a GDAL mosaic (I-042).
 *
 * The cells are synthesised here: only the 80-byte user header label matters, since that is
 * all the mosaic is built from.
 */
class TestDtedMosaic : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void readsAUserHeaderLabel();
    void rejectsNonDtedFiles();
    void scanWalksLongitudeFolders();
    void mosaicGridMatchesGdalsDtedConvention();
    void adjacentCellsShareOneRowOfPosts();
    void mixedPostingUsesTheFinestGrid();
    void sourcePathsStayRelativeInsideTheFolder();
    void ensureMosaicLeavesAFileAlone();
    void ensureMosaicIndexesAFolderAndReusesIt();
    void ensureMosaicReportsAnEmptyFolder();

private:
    /// A DTED cell with a valid UHL: south-west origin, posting in tenths of an arc-second.
    QString writeCell(const QString& relativePath, int lonDeg, int latDeg,
                      int lonIntervalTenths = 10, int latIntervalTenths = 10,
                      int columns = 3601, int rows = 3601) const;
    static QString angle(int degrees, bool isLongitude);

    std::unique_ptr<QTemporaryDir> m_tmp;
};

QString TestDtedMosaic::angle(int degrees, bool isLongitude)
{
    const QChar hemisphere = degrees < 0 ? (isLongitude ? QLatin1Char('W') : QLatin1Char('S'))
                                         : (isLongitude ? QLatin1Char('E') : QLatin1Char('N'));
    return QStringLiteral("%1%2%3%4")
        .arg(std::abs(degrees), 3, 10, QLatin1Char('0'))
        .arg(0, 2, 10, QLatin1Char('0'))
        .arg(0, 2, 10, QLatin1Char('0'))
        .arg(hemisphere);
}

QString TestDtedMosaic::writeCell(const QString& relativePath, int lonDeg, int latDeg,
                                  int lonIntervalTenths, int latIntervalTenths,
                                  int columns, int rows) const
{
    const QString path = m_tmp->filePath(relativePath);
    QDir().mkpath(QFileInfo(path).absolutePath());

    QString uhl;
    uhl += QStringLiteral("UHL1");
    uhl += angle(lonDeg, true);
    uhl += angle(latDeg, false);
    uhl += QStringLiteral("%1").arg(lonIntervalTenths, 4, 10, QLatin1Char('0'));
    uhl += QStringLiteral("%1").arg(latIntervalTenths, 4, 10, QLatin1Char('0'));
    uhl += QStringLiteral("0010");                       // absolute vertical accuracy
    uhl += QStringLiteral("   ");                        // security code
    uhl += QStringLiteral("            ");               // unique reference
    uhl += QStringLiteral("%1").arg(columns, 4, 10, QLatin1Char('0'));
    uhl += QStringLiteral("%1").arg(rows, 4, 10, QLatin1Char('0'));
    uhl += QStringLiteral("0");                          // multiple accuracy
    uhl += QString(24, QLatin1Char(' '));                // reserved
    Q_ASSERT(uhl.size() == 80);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(uhl.toLatin1());
    file.write(QByteArray(128, '\0'));   // stand-in for the DSI/ACC records and the data
    file.close();
    return path;
}

void TestDtedMosaic::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmp->isValid());
}

void TestDtedMosaic::readsAUserHeaderLabel()
{
    const QString path = writeCell(QStringLiteral("e066/n32.dt2"), 66, 32);
    DtedTile tile;
    QString error;
    QVERIFY2(DtedMosaic::readHeader(path, &tile, &error), qPrintable(error));
    QCOMPARE(tile.westLonDeg, 66.0);
    QCOMPARE(tile.southLatDeg, 32.0);
    QCOMPARE(tile.columns, 3601);
    QCOMPARE(tile.rows, 3601);
    QVERIFY(qFuzzyCompare(tile.lonIntervalDeg, 1.0 / 3600.0));
    QVERIFY(qFuzzyCompare(tile.latIntervalDeg, 1.0 / 3600.0));
    QVERIFY(tile.isValid());

    // Southern/western hemispheres carry the sign.
    DtedTile south;
    QVERIFY(DtedMosaic::readHeader(writeCell(QStringLiteral("w010/s05.dt1"), -10, -5), &south));
    QCOMPARE(south.westLonDeg, -10.0);
    QCOMPARE(south.southLatDeg, -5.0);
}

void TestDtedMosaic::rejectsNonDtedFiles()
{
    const QString path = m_tmp->filePath(QStringLiteral("notes.dt2"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("this is not a DTED cell, but it is long enough to be read as one, honestly it is");
    file.close();

    DtedTile tile;
    QString error;
    QVERIFY(!DtedMosaic::readHeader(path, &tile, &error));
    QVERIFY(error.contains(QStringLiteral("user header label")));
    QVERIFY(!DtedMosaic::readHeader(m_tmp->filePath(QStringLiteral("missing.dt2")), &tile));
}

void TestDtedMosaic::scanWalksLongitudeFolders()
{
    writeCell(QStringLiteral("e066/n32.dt2"), 66, 32);
    writeCell(QStringLiteral("e066/n33.dt2"), 66, 33);
    writeCell(QStringLiteral("e067/N32.DT2"), 67, 32);   // upper case, as some servers hold it
    writeCell(QStringLiteral("readme.txt"), 0, 0);       // not DTED: wrong extension

    QStringList warnings;
    const QList<DtedTile> tiles = DtedMosaic::scan(m_tmp->path(), &warnings);
    QCOMPARE(tiles.size(), 3);
    QVERIFY(warnings.isEmpty());
    // Sorted south to north, then west to east.
    QCOMPARE(tiles.at(0).southLatDeg, 32.0);
    QCOMPARE(tiles.at(0).westLonDeg, 66.0);
    QCOMPARE(tiles.at(1).westLonDeg, 67.0);
    QCOMPARE(tiles.at(2).southLatDeg, 33.0);
}

void TestDtedMosaic::mosaicGridMatchesGdalsDtedConvention()
{
    writeCell(QStringLiteral("e066/n32.dt2"), 66, 32);
    const QList<DtedTile> tiles = DtedMosaic::scan(m_tmp->path());
    QCOMPARE(tiles.size(), 1);

    QString error;
    const QString xml = QString::fromUtf8(
        DtedMosaic::buildVrt(tiles, m_tmp->filePath(QStringLiteral("dted_mosaic.vrt")), &error));
    QVERIFY2(!xml.isEmpty(), qPrintable(error));

    // DTED posts sit on the cell edges; GDAL describes the raster by its outer corner, half a
    // post further out. 3601 posts at 1" therefore start at 66 - 0.5/3600 and 33 + 0.5/3600.
    const double half = 0.5 / 3600.0;
    QVERIFY(xml.contains(QStringLiteral("rasterXSize=\"3601\"")));
    QVERIFY(xml.contains(QStringLiteral("rasterYSize=\"3601\"")));
    QVERIFY(xml.contains(QString::number(66.0 - half, 'g', 17)));
    QVERIFY(xml.contains(QString::number(33.0 + half, 'g', 17)));
    QVERIFY(xml.contains(QStringLiteral("<NoDataValue>-32767</NoDataValue>")));
    QVERIFY(xml.contains(QStringLiteral("dataType=\"Int16\"")));
    QVERIFY(xml.contains(QStringLiteral("EPSG\",\"4326")));
    QVERIFY(xml.contains(QStringLiteral("<DstRect xOff=\"0\" yOff=\"0\" xSize=\"3601\" ySize=\"3601\"/>")));
}

void TestDtedMosaic::adjacentCellsShareOneRowOfPosts()
{
    writeCell(QStringLiteral("e066/n32.dt2"), 66, 32);
    writeCell(QStringLiteral("e067/n32.dt2"), 67, 32);
    const QList<DtedTile> tiles = DtedMosaic::scan(m_tmp->path());
    QCOMPARE(tiles.size(), 2);

    const QString xml = QString::fromUtf8(
        DtedMosaic::buildVrt(tiles, m_tmp->filePath(QStringLiteral("dted_mosaic.vrt"))));
    // Two 1-degree cells side by side share their boundary posts: 3601 + 3600, not 7202.
    QVERIFY(xml.contains(QStringLiteral("rasterXSize=\"7201\"")));
    QVERIFY(xml.contains(QStringLiteral("rasterYSize=\"3601\"")));
    QVERIFY(xml.contains(QStringLiteral("<DstRect xOff=\"0\" yOff=\"0\"")));
    QVERIFY(xml.contains(QStringLiteral("<DstRect xOff=\"3600\" yOff=\"0\"")));
}

void TestDtedMosaic::mixedPostingUsesTheFinestGrid()
{
    // A DTED1 cell (3", 1201 posts) next to a DTED2 cell (1", 3601 posts).
    writeCell(QStringLiteral("e066/n32.dt2"), 66, 32, 10, 10, 3601, 3601);
    writeCell(QStringLiteral("e067/n32.dt1"), 67, 32, 30, 30, 1201, 1201);
    const QList<DtedTile> tiles = DtedMosaic::scan(m_tmp->path());
    QCOMPARE(tiles.size(), 2);

    const QString xml = QString::fromUtf8(
        DtedMosaic::buildVrt(tiles, m_tmp->filePath(QStringLiteral("dted_mosaic.vrt"))));
    // The mosaic takes the 1" grid. The two cells do not quite abut on it: each raster reaches
    // half of *its own* post beyond the degree line, so the 3" cell starts one 1" pixel west of
    // where the 1" cell ends, and the union is 7202 pixels with a two-pixel overlap at the seam
    // (which the later source wins). gdalbuildvrt -resolution highest does the same. It is the
    // reason to keep one DTED level per folder.
    QVERIFY(xml.contains(QStringLiteral("rasterXSize=\"7202\"")));
    // The coarse cell reads 1201 source pixels into 3603 mosaic pixels; GDAL resamples.
    QVERIFY(xml.contains(QStringLiteral("<SrcRect xOff=\"0\" yOff=\"0\" xSize=\"1201\" ySize=\"1201\"/>")));
    QVERIFY(xml.contains(QStringLiteral("<DstRect xOff=\"3599\" yOff=\"0\" xSize=\"3603\" ySize=\"3603\"/>")));
}

void TestDtedMosaic::sourcePathsStayRelativeInsideTheFolder()
{
    writeCell(QStringLiteral("e066/n32.dt2"), 66, 32);
    const QList<DtedTile> tiles = DtedMosaic::scan(m_tmp->path());

    const QString inside = QString::fromUtf8(
        DtedMosaic::buildVrt(tiles, m_tmp->filePath(QStringLiteral("dted_mosaic.vrt"))));
    QVERIFY(inside.contains(QStringLiteral("relativeToVRT=\"1\">e066/n32.dt2<")));

    // A mosaic kept elsewhere (a read-only DTED share) must name the cells absolutely.
    QTemporaryDir elsewhere;
    QVERIFY(elsewhere.isValid());
    const QString outside = QString::fromUtf8(
        DtedMosaic::buildVrt(tiles, elsewhere.filePath(QStringLiteral("dted_x.vrt"))));
    QVERIFY(outside.contains(QStringLiteral("relativeToVRT=\"0\">")));
    QVERIFY(outside.contains(tiles.first().path));
}

void TestDtedMosaic::ensureMosaicLeavesAFileAlone()
{
    const QString cell = writeCell(QStringLiteral("e066/n32.dt2"), 66, 32);
    QString error;
    // A single cell, or a .vrt someone built by hand, is already a dataset GDAL can open.
    QCOMPARE(DtedMosaic::ensureMosaic(cell, QString(), &error), QFileInfo(cell).absoluteFilePath());
    QVERIFY(!QFile::exists(m_tmp->filePath(QStringLiteral("e066/dted_mosaic.vrt"))));
}

void TestDtedMosaic::ensureMosaicIndexesAFolderAndReusesIt()
{
    writeCell(QStringLiteral("e066/n32.dt2"), 66, 32);
    writeCell(QStringLiteral("e067/n32.dt2"), 67, 32);

    QString error;
    const QString mosaic = DtedMosaic::ensureMosaic(m_tmp->path(), QString(), &error);
    QVERIFY2(!mosaic.isEmpty(), qPrintable(error));
    QCOMPARE(QFileInfo(mosaic).fileName(), QString::fromLatin1(DtedMosaic::defaultFileName()));
    QVERIFY(QFile::exists(mosaic));

    // A trailing separator is the same folder.
    QCOMPARE(DtedMosaic::ensureMosaic(m_tmp->path() + QLatin1Char('/')), mosaic);

    // Built once: an unchanged folder must not be rewritten.
    const QDateTime written = QFileInfo(mosaic).lastModified();
    QFile::setPermissions(mosaic, QFileDevice::ReadOwner);   // a rewrite would now fail
    QCOMPARE(DtedMosaic::ensureMosaic(m_tmp->path(), QString(), &error), mosaic);
    QCOMPARE(QFileInfo(mosaic).lastModified(), written);
    QFile::setPermissions(mosaic, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

void TestDtedMosaic::ensureMosaicReportsAnEmptyFolder()
{
    QString error;
    QVERIFY(DtedMosaic::ensureMosaic(m_tmp->path(), QString(), &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral(".dt0/.dt1/.dt2")));

    error.clear();
    QVERIFY(DtedMosaic::ensureMosaic(m_tmp->filePath(QStringLiteral("nope")), QString(), &error).isEmpty());
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(DtedMosaic::ensureMosaic(QString(), QString(), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

QTEST_MAIN(TestDtedMosaic)
#include "tst_dtedmosaic.moc"
