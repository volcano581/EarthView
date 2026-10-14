#include "earthview3d/EarthFileCatalog.h"

#include <QtTest>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include <memory>

using earthview3d::EarthFileCatalog;
using earthview3d::MapCatalogEntry;
using earthview3d::TmsLayerSpec;

/**
 * @brief Unit tests for EarthFileCatalog: parsing EarthView layer-list files, discovery and
 *        generation of standard osgEarth earth files from a template.
 */
class TestEarthFileCatalog : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void parsesTmsImageLines();
    void parsesLevelsAndElevation();
    void skipsForeignDriversAndUrllessLayers();
    void namesLayersFromUrlAndKeepsThemUnique();
    void detectsStandardEarthFile();
    void rejectsFileWithoutLayers();
    void labelsFollowThe2DViewer_data();
    void labelsFollowThe2DViewer();
    void parsesDtedElevationFolder();
    void dtedMosaicUrlAppendsTheVrtOnlyToFolders_data();
    void dtedMosaicUrlAppendsTheVrtOnlyToFolders();
    void layersXmlIsWellFormedAndEscaped();
    void discoverSortsAndDeduplicates();
    void generateSplicesIntoTemplate();
    void generateIndexesADtedFolder();
    void generateLeavesStandardFilesAlone();
    void generateFailsWithoutPlaceholder();
    void buildReportsSkippedFiles();

private:
    QString write(const QString& relativePath, const QString& content) const;
    QString writeTemplate() const;

    std::unique_ptr<QTemporaryDir> m_tmp;   // fresh per test function
    QTemporaryDir& m_dir() const { return *m_tmp; }
};

QString TestEarthFileCatalog::write(const QString& relativePath, const QString& content) const
{
    const QString path = m_dir().filePath(relativePath);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    file.write(content.toUtf8());
    return path;
}

QString TestEarthFileCatalog::writeTemplate() const
{
    return write(QStringLiteral("maps/template.earth"),
                 QStringLiteral("<map name=\"EarthView\" version=\"3\">\n"
                                "    <GDALImage name=\"World base\"><url>../imagery/world_base.tif</url></GDALImage>\n"
                                "    <!--@EARTHVIEW3D_TMS_LAYERS@-->\n"
                                "    <GDALElevation name=\"DTED\"><url>../DEM/dted_1arc.vrt</url></GDALElevation>\n"
                                "</map>\n"));
}

void TestEarthFileCatalog::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmp->isValid());
}

void TestEarthFileCatalog::parsesTmsImageLines()
{
    // The exact shape of the offline machine's files: no root element, no header, one line
    // per layer, a space inside <url>.
    const QByteArray content =
        "<image name=\"Dummy_Layer_1\" driver=\"tms\"> <url> http://host:8080/imagery/l1/tilemapresource.xml</url></image>\n"
        "<image name=\"Dummy_Layer_2\" driver=\"tms\"> <url> http://host:8080/imagery/l2/tilemapresource.xml</url></image>\n";

    MapCatalogEntry entry;
    QString error;
    QVERIFY2(EarthFileCatalog::parse(content, &entry, &error), qPrintable(error));
    QVERIFY(!entry.standardEarthFile);
    QCOMPARE(entry.tmsLayers.size(), 2);
    QCOMPARE(entry.tmsLayers.at(0).name, QStringLiteral("Dummy_Layer_1"));
    QCOMPARE(entry.tmsLayers.at(0).url, QStringLiteral("http://host:8080/imagery/l1/tilemapresource.xml"));
    QCOMPARE(entry.tmsLayers.at(0).kind, TmsLayerSpec::Kind::Image);
    QCOMPARE(entry.tmsLayers.at(0).minLevel, -1);
    QCOMPARE(entry.tmsLayers.at(1).url, QStringLiteral("http://host:8080/imagery/l2/tilemapresource.xml"));
}

void TestEarthFileCatalog::parsesLevelsAndElevation()
{
    const QByteArray content =
        "<image name=\"A\" driver=\"tms\" min_level=\"2\" max_level=\"14\"><url>http://h/a/tilemapresource.xml</url></image>\n"
        "<elevation name=\"DTED TMS\" driver=\"tms\"><url>http://h/dem/tilemapresource.xml</url>\n"
        "  <vdatum>egm96</vdatum><min_level>1</min_level></elevation>\n";

    MapCatalogEntry entry;
    QVERIFY(EarthFileCatalog::parse(content, &entry, nullptr));
    QCOMPARE(entry.tmsLayers.size(), 2);
    QCOMPARE(entry.tmsLayers.at(0).minLevel, 2);
    QCOMPARE(entry.tmsLayers.at(0).maxLevel, 14);
    QCOMPARE(entry.tmsLayers.at(1).kind, TmsLayerSpec::Kind::Elevation);
    QCOMPARE(entry.tmsLayers.at(1).vdatum, QStringLiteral("egm96"));
    QCOMPARE(entry.tmsLayers.at(1).minLevel, 1);

    const QByteArray xml = EarthFileCatalog::layersXml(entry.tmsLayers);
    QVERIFY(xml.contains("<TMSImage name=\"A\">"));
    QVERIFY(xml.contains("<TMSElevation name=\"DTED TMS\">"));
    QVERIFY(xml.contains("<vdatum>egm96</vdatum>"));
    QVERIFY(xml.contains("<min_level>2</min_level>"));
    QVERIFY(xml.contains("<max_level>14</max_level>"));
}

void TestEarthFileCatalog::skipsForeignDriversAndUrllessLayers()
{
    const QByteArray content =
        "<image name=\"wms\" driver=\"wms\"><url>http://h/wms</url></image>\n"
        "<image name=\"nourl\" driver=\"tms\"></image>\n"
        "<image name=\"keep\" driver=\"tms\"><url>http://h/k/tilemapresource.xml</url></image>\n"
        "<image name=\"nodriver\"><url>http://h/n/tilemapresource.xml</url></image>\n";

    MapCatalogEntry entry;
    QVERIFY(EarthFileCatalog::parse(content, &entry, nullptr));
    QCOMPARE(entry.tmsLayers.size(), 2);
    QCOMPARE(entry.tmsLayers.at(0).name, QStringLiteral("keep"));
    QCOMPARE(entry.tmsLayers.at(1).name, QStringLiteral("nodriver"));
}

void TestEarthFileCatalog::namesLayersFromUrlAndKeepsThemUnique()
{
    const QByteArray content =
        "<image driver=\"tms\"><url>http://h/imagery/layer1/tilemapresource.xml</url></image>\n"
        "<image name=\"same\" driver=\"tms\"><url>http://h/a/tilemapresource.xml</url></image>\n"
        "<image name=\"same\" driver=\"tms\"><url>http://h/b/tilemapresource.xml</url></image>\n";

    MapCatalogEntry entry;
    QVERIFY(EarthFileCatalog::parse(content, &entry, nullptr));
    QCOMPARE(entry.tmsLayers.size(), 3);
    QCOMPARE(entry.tmsLayers.at(0).name, QStringLiteral("layer1"));
    QCOMPARE(entry.tmsLayers.at(1).name, QStringLiteral("same"));
    QCOMPARE(entry.tmsLayers.at(2).name, QStringLiteral("same (2)"));
}

void TestEarthFileCatalog::detectsStandardEarthFile()
{
    const QByteArray content = "<!-- comment -->\n<map name=\"EarthView\" version=\"3\">\n"
                               "  <GDALImage name=\"base\"><url>../imagery/world_base.tif</url></GDALImage>\n"
                               "</map>\n";
    MapCatalogEntry entry;
    QVERIFY(EarthFileCatalog::parse(content, &entry, nullptr));
    QVERIFY(entry.standardEarthFile);
    QVERIFY(entry.tmsLayers.isEmpty());
}

void TestEarthFileCatalog::rejectsFileWithoutLayers()
{
    MapCatalogEntry entry;
    QString error;
    QVERIFY(!EarthFileCatalog::parse("<!-- nothing here -->\n", &entry, &error));
    QVERIFY(!error.isEmpty());

    QVERIFY(!EarthFileCatalog::parse("<image name=\"broken\" driver=\"tms\"><url>http://h/t.xml</image>\n",
                                     &entry, &error));
    QVERIFY(!error.isEmpty());
}

void TestEarthFileCatalog::labelsFollowThe2DViewer_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<QString>("label");
    QTest::newRow("satellite") << "satellite_layers.earth" << "Satellite Imagery";
    QTest::newRow("imagery") << "offline_imagery.earth" << "Satellite Imagery";
    QTest::newRow("cardg") << "cardg_sheets.earth" << "CardG Maps";
    QTest::newRow("topo") << "topographic.earth" << "CardG Maps";
    QTest::newRow("other") << "local_mosaic_2026.earth" << "local mosaic 2026";
}

void TestEarthFileCatalog::labelsFollowThe2DViewer()
{
    QFETCH(QString, fileName);
    QFETCH(QString, label);
    QCOMPARE(EarthFileCatalog::labelForFile(QFileInfo(fileName)), label);
}

void TestEarthFileCatalog::parsesDtedElevationFolder()
{
    // The offline server's DTED folder, as the 2D layer list carries it.
    const QByteArray content =
        "<image name=\"img\" driver=\"tms\"><url>http://h/i/tilemapresource.xml</url></image>\n"
        "<elevation name=\"DTED\" driver=\"dted\"> <url> C:/EarthViewData/DTED </url></elevation>\n";

    MapCatalogEntry entry;
    QVERIFY(EarthFileCatalog::parse(content, &entry, nullptr));
    QCOMPARE(entry.tmsLayers.size(), 2);
    const TmsLayerSpec& dted = entry.tmsLayers.at(1);
    QCOMPARE(dted.kind, TmsLayerSpec::Kind::DtedElevation);
    QCOMPARE(dted.name, QStringLiteral("DTED"));
    QCOMPARE(dted.url, QStringLiteral("C:/EarthViewData/DTED"));

    // osgEarth needs the GDAL mosaic, and DTED is orthometric: vdatum is filled in.
    const QString xml = QString::fromUtf8(EarthFileCatalog::layersXml(entry.tmsLayers, 0));
    QVERIFY(xml.contains(QStringLiteral("<GDALElevation name=\"DTED\">")));
    QVERIFY(xml.contains(QStringLiteral("<url>C:/EarthViewData/DTED/dted_mosaic.vrt</url>")));
    QVERIFY(xml.contains(QStringLiteral("<vdatum>egm96</vdatum>")));
    QVERIFY(!xml.contains(QStringLiteral("TMSElevation")));
}

void TestEarthFileCatalog::dtedMosaicUrlAppendsTheVrtOnlyToFolders_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("expected");
    QTest::newRow("folder") << "C:/Data/DTED" << "C:/Data/DTED/dted_mosaic.vrt";
    QTest::newRow("trailing slash") << "C:/Data/DTED/" << "C:/Data/DTED/dted_mosaic.vrt";
    QTest::newRow("backslashes") << "D:\\Data\\DTED\\" << "D:\\Data\\DTED/dted_mosaic.vrt";
    QTest::newRow("spaces") << "  /srv/dted  " << "/srv/dted/dted_mosaic.vrt";
    QTest::newRow("vrt already") << "/srv/dted/mine.vrt" << "/srv/dted/mine.vrt";
    QTest::newRow("single tile") << "/srv/dted/n33_e073.dt2" << "/srv/dted/n33_e073.dt2";
    QTest::newRow("empty") << "" << "";
}

void TestEarthFileCatalog::dtedMosaicUrlAppendsTheVrtOnlyToFolders()
{
    QFETCH(QString, path);
    QFETCH(QString, expected);
    QCOMPARE(EarthFileCatalog::dtedMosaicUrl(path), expected);
}

void TestEarthFileCatalog::layersXmlIsWellFormedAndEscaped()
{
    TmsLayerSpec layer;
    layer.name = QStringLiteral("Imagery & \"topo\"");
    layer.url = QStringLiteral("http://host:8080/a?b=1&c=2");
    const QByteArray xml = EarthFileCatalog::layersXml({ layer }, 2);
    QVERIFY(xml.contains("name=\"Imagery &amp; &quot;topo&quot;\""));
    QVERIFY(xml.contains("<url>http://host:8080/a?b=1&amp;c=2</url>"));
    QVERIFY(xml.startsWith("  <TMSImage"));

    QXmlStreamReader reader(QByteArray("<map>") + xml + QByteArray("</map>"));
    while (!reader.atEnd())
        reader.readNext();
    QVERIFY2(!reader.hasError(), qPrintable(reader.errorString()));
}

void TestEarthFileCatalog::discoverSortsAndDeduplicates()
{
    const QString line = QStringLiteral("<image name=\"L\" driver=\"tms\"><url>http://h/l/tilemapresource.xml</url></image>\n");
    QVERIFY(!write(QStringLiteral("EarthFiles/cardg_topo.earth"), line).isEmpty());
    QVERIFY(!write(QStringLiteral("EarthFiles/satellite.earth"), line).isEmpty());
    QVERIFY(!write(QStringLiteral("EarthFiles/broken.earth"), QStringLiteral("<!-- no layers -->\n")).isEmpty());

    QStringList warnings;
    const QStringList dirs = { m_dir().filePath(QStringLiteral("EarthFiles")),
                               m_dir().filePath(QStringLiteral("EarthFiles")),   // duplicate: scanned once
                               m_dir().filePath(QStringLiteral("does/not/exist")) };
    const QList<MapCatalogEntry> entries = EarthFileCatalog::discover(dirs, &warnings);
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).label, QStringLiteral("CardG Maps"));
    QCOMPARE(entries.at(1).label, QStringLiteral("Satellite Imagery"));
    QCOMPARE(warnings.size(), 1);
    QVERIFY(warnings.at(0).contains(QStringLiteral("broken.earth")));
}

void TestEarthFileCatalog::generateSplicesIntoTemplate()
{
    const QString templatePath = writeTemplate();
    QVERIFY(!templatePath.isEmpty());
    const QString source = write(QStringLiteral("EarthFiles/satellite layers.earth"),
                                 QStringLiteral("<image name=\"L1\" driver=\"tms\"><url>http://h/l1/tilemapresource.xml</url></image>\n"
                                                "<image name=\"L2\" driver=\"tms\"><url>http://h/l2/tilemapresource.xml</url></image>\n"));
    QVERIFY(!source.isEmpty());

    MapCatalogEntry entry;
    QString error;
    QVERIFY2(EarthFileCatalog::parseFile(source, &entry, &error), qPrintable(error));
    QVERIFY2(EarthFileCatalog::generate(&entry, templatePath, QString(), &error), qPrintable(error));

    // Written next to the template, so the template's relative paths still resolve.
    QCOMPARE(QFileInfo(entry.earthFile).absolutePath(), QFileInfo(templatePath).absolutePath());
    QCOMPARE(QFileInfo(entry.earthFile).fileName(), QStringLiteral("tms_satellite_layers.earth"));

    QFile generated(entry.earthFile);
    QVERIFY(generated.open(QIODevice::ReadOnly));
    const QByteArray content = generated.readAll();
    QVERIFY(!content.contains(EarthFileCatalog::layerPlaceholder()));
    QVERIFY(content.contains("<TMSImage name=\"L1\">"));
    QVERIFY(content.contains("<TMSImage name=\"L2\">"));
    QVERIFY(content.contains("<url>http://h/l1/tilemapresource.xml</url>"));
    QVERIFY(content.contains("satellite layers.earth"));          // provenance comment
    QVERIFY(content.contains("../imagery/world_base.tif"));       // template layers kept
    QVERIFY(content.contains("../DEM/dted_1arc.vrt"));

    // The result is a well-formed osgEarth map with the TMS layers between the template's.
    QXmlStreamReader reader(content);
    QStringList layerNames;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.attributes().hasAttribute(QStringLiteral("name")))
            layerNames << reader.attributes().value(QStringLiteral("name")).toString();
    }
    QVERIFY2(!reader.hasError(), qPrintable(reader.errorString()));
    QCOMPARE(layerNames, QStringList({ QStringLiteral("EarthView"), QStringLiteral("World base"),
                                       QStringLiteral("L1"), QStringLiteral("L2"), QStringLiteral("DTED") }));
}

void TestEarthFileCatalog::generateIndexesADtedFolder()
{
    // A layer list naming the elevation server's DTED folder, with one cell in it. osgEarth
    // opens datasets, not folders, so generate() has to leave a mosaic path behind (I-042).
    const QString dtedDir = m_dir().filePath(QStringLiteral("DTED2/e066"));
    QVERIFY(QDir().mkpath(dtedDir));
    QString uhl = QStringLiteral("UHL1") + QStringLiteral("066000 0E").remove(QLatin1Char(' '))
        + QStringLiteral("0320000N") + QStringLiteral("0010") + QStringLiteral("0010")
        + QStringLiteral("0010") + QStringLiteral("   ") + QStringLiteral("            ")
        + QStringLiteral("3601") + QStringLiteral("3601") + QStringLiteral("0") + QString(24, QLatin1Char(' '));
    QCOMPARE(uhl.size(), 80);
    QFile cell(dtedDir + QStringLiteral("/n32.dt2"));
    QVERIFY(cell.open(QIODevice::WriteOnly));
    cell.write(uhl.toLatin1());
    cell.close();

    const QString source = write(QStringLiteral("imagery.earth"),
        QStringLiteral("<image name=\"img\" driver=\"tms\"><url>http://h/i/tilemapresource.xml</url></image>\n"
                       "<elevation name=\"DTED\" driver=\"dted\"><url>%1</url></elevation>\n")
            .arg(m_dir().filePath(QStringLiteral("DTED2"))));
    const QString templatePath = writeTemplate();

    MapCatalogEntry entry;
    QVERIFY(EarthFileCatalog::parseFile(source, &entry, nullptr));
    QStringList warnings;
    QString error;
    QVERIFY2(EarthFileCatalog::generate(&entry, templatePath, QString(), &error, &warnings), qPrintable(error));
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QStringLiteral("; "))));

    // The mosaic was written into the DTED folder and the generated map names it.
    const QString mosaic = m_dir().filePath(QStringLiteral("DTED2/dted_mosaic.vrt"));
    QVERIFY(QFile::exists(mosaic));
    QFile generated(entry.earthFile);
    QVERIFY(generated.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(generated.readAll());
    QVERIFY(content.contains(QStringLiteral("<GDALElevation name=\"DTED\">")));
    QVERIFY(content.contains(QStringLiteral("dted_mosaic.vrt")));
    QVERIFY(content.contains(QStringLiteral("<vdatum>egm96</vdatum>")));
}

void TestEarthFileCatalog::generateLeavesStandardFilesAlone()
{
    const QString templatePath = writeTemplate();
    const QString source = write(QStringLiteral("EarthFiles/standard.earth"),
                                 QStringLiteral("<map name=\"Std\" version=\"3\"></map>\n"));
    MapCatalogEntry entry;
    QVERIFY(EarthFileCatalog::parseFile(source, &entry, nullptr));
    QVERIFY(entry.standardEarthFile);
    QVERIFY(EarthFileCatalog::generate(&entry, templatePath, QString(), nullptr));
    QCOMPARE(entry.earthFile, QFileInfo(source).absoluteFilePath());
    QVERIFY(!QFile::exists(m_dir().filePath(QStringLiteral("maps/tms_standard.earth"))));
}

void TestEarthFileCatalog::generateFailsWithoutPlaceholder()
{
    const QString templatePath = write(QStringLiteral("maps/plain.earth"),
                                       QStringLiteral("<map name=\"Plain\" version=\"3\"></map>\n"));
    MapCatalogEntry entry;
    entry.sourcePath = m_dir().filePath(QStringLiteral("EarthFiles/x.earth"));
    TmsLayerSpec layer;
    layer.name = QStringLiteral("L");
    layer.url = QStringLiteral("http://h/l/tilemapresource.xml");
    entry.tmsLayers << layer;

    QString error;
    QVERIFY(!EarthFileCatalog::generate(&entry, templatePath, QString(), &error));
    QVERIFY(error.contains(QStringLiteral("placeholder")));
    QVERIFY(!EarthFileCatalog::generate(&entry, m_dir().filePath(QStringLiteral("maps/missing.earth")), QString(), &error));
    QVERIFY(error.contains(QStringLiteral("Cannot read template")));
}

void TestEarthFileCatalog::buildReportsSkippedFiles()
{
    const QString templatePath = writeTemplate();
    QVERIFY(!write(QStringLiteral("EarthFiles/good.earth"),
                   QStringLiteral("<image name=\"L\" driver=\"tms\"><url>http://h/l/tilemapresource.xml</url></image>\n"))
                 .isEmpty());
    QVERIFY(!write(QStringLiteral("EarthFiles/empty.earth"), QStringLiteral("\n")).isEmpty());

    QStringList warnings;
    const QString outDir = m_dir().filePath(QStringLiteral("maps/generated"));
    const QList<MapCatalogEntry> entries =
        EarthFileCatalog::build({ m_dir().filePath(QStringLiteral("EarthFiles")) }, templatePath, outDir, &warnings);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.at(0).label, QStringLiteral("good"));
    QCOMPARE(QFileInfo(entries.at(0).earthFile).absolutePath(), QDir(outDir).absolutePath());
    QVERIFY(QFile::exists(entries.at(0).earthFile));
    QCOMPARE(warnings.size(), 1);
    QVERIFY(warnings.at(0).contains(QStringLiteral("empty.earth")));

    // Default search folders follow the 2D viewer's layout.
    const QStringList dirs = EarthFileCatalog::defaultSearchDirectories(m_dir().path());
    QCOMPARE(dirs.size(), 3);
    QCOMPARE(dirs.at(0), QDir(m_dir().path()).absoluteFilePath(QStringLiteral("EarthFiles")));
    QCOMPARE(dirs.at(1), QDir(m_dir().path()).absoluteFilePath(QStringLiteral("Data/EarthFiles")));
}

QTEST_MAIN(TestEarthFileCatalog)
#include "tst_earthfilecatalog.moc"
