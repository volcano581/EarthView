#include "MapSourceCatalog.h"

#include <QtTest>

#include <memory>

/**
 * @brief The 2D map-source reader (layer-list .earth files).
 *
 * It lived unexercised inside MainWindow.cpp until the earthview3d demo needed the same
 * reader for its 2D pane (I-041); these are its first tests.
 */
class TestMapSourceCatalog : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void readsTmsImageLines();
    void keepsZoomAttributesAndElementForms();
    void skipsForeignDriversAndUrllessImages();
    void readsTheDtedFolder();
    void dtedEntryDoesNotBecomeAnImageLayer();
    void missingFileIsEmpty();
    void labelsFollowTheViewer_data();
    void labelsFollowTheViewer();

private:
    QString write(const QString& name, const QString& content) const;

    std::unique_ptr<QTemporaryDir> m_tmp;
};

QString TestMapSourceCatalog::write(const QString& name, const QString& content) const
{
    const QString path = m_tmp->filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    file.write(content.toUtf8());
    file.close();
    return path;
}

void TestMapSourceCatalog::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmp->isValid());
}

void TestMapSourceCatalog::readsTmsImageLines()
{
    const QString path = write(QStringLiteral("imagery.earth"),
        QStringLiteral("<image name=\"Layer_1\" driver=\"tms\"> <url> http://host:8123/i/l1/tilemapresource.xml</url></image>\n"
                       "<image name=\"Layer_2\" driver=\"tms\"> <url> http://host:8123/i/l2/tilemapresource.xml</url></image>\n"));
    const QList<TmsLoader::TileSourceLayer> layers = MapSourceCatalog::readEarthLayers(path);
    QCOMPARE(layers.size(), 2);
    QCOMPARE(layers.at(0).name, QStringLiteral("Layer_1"));
    QCOMPARE(layers.at(1).name, QStringLiteral("Layer_2"));
    // tilemapresource.xml becomes a {z}/{x}/{y} template; TMS tiles count y from the south.
    for (const TmsLoader::TileSourceLayer& layer : layers) {
        QVERIFY(layer.urlTemplate.contains(QStringLiteral("{z}")));
        QVERIFY(layer.urlTemplate.contains(QStringLiteral("{x}")));
        QVERIFY(layer.urlTemplate.contains(QStringLiteral("{y}")));
        QVERIFY(layer.tmsYOrigin);
    }
}

void TestMapSourceCatalog::keepsZoomAttributesAndElementForms()
{
    const QString path = write(QStringLiteral("topo.earth"),
        QStringLiteral("<image name=\"attr\" driver=\"tms\" min_level=\"2\" max_level=\"12\">"
                       " <url> http://host/a/tilemapresource.xml</url></image>\n"
                       "<image name=\"elem\" driver=\"tms\"><url>http://host/b/tilemapresource.xml</url>"
                       "<min_level>3</min_level><max_level>13</max_level></image>\n"));
    const QList<TmsLoader::TileSourceLayer> layers = MapSourceCatalog::readEarthLayers(path);
    QCOMPARE(layers.size(), 2);
    QCOMPARE(layers.at(0).minZoom, 2);
    QCOMPARE(layers.at(0).maxZoom, 12);
    QCOMPARE(layers.at(1).minZoom, 3);
    QCOMPARE(layers.at(1).maxZoom, 13);
}

void TestMapSourceCatalog::skipsForeignDriversAndUrllessImages()
{
    const QString path = write(QStringLiteral("mixed.earth"),
        QStringLiteral("<image name=\"wms\" driver=\"wms\"><url>http://host/wms</url></image>\n"
                       "<image name=\"nourl\" driver=\"tms\"></image>\n"
                       "<image name=\"keep\" driver=\"tms\"><url>http://host/k/tilemapresource.xml</url></image>\n"));
    const QList<TmsLoader::TileSourceLayer> layers = MapSourceCatalog::readEarthLayers(path);
    QCOMPARE(layers.size(), 1);
    QCOMPARE(layers.at(0).name, QStringLiteral("keep"));
}

void TestMapSourceCatalog::readsTheDtedFolder()
{
    const QString path = write(QStringLiteral("imagery.earth"),
        QStringLiteral("<image name=\"img\" driver=\"tms\"><url>http://host/i/tilemapresource.xml</url></image>\n"
                       "<elevation name=\"DTED\" driver=\"dted\"> <url> D:/EarthViewData/DTED2 </url></elevation>\n"));
    QString dted;
    const QList<TmsLoader::TileSourceLayer> layers = MapSourceCatalog::readEarthLayers(path, &dted);
    QCOMPARE(layers.size(), 1);
    QCOMPARE(dted, QStringLiteral("D:/EarthViewData/DTED2"));

    // Asking for the layers without the folder must still work.
    QCOMPARE(MapSourceCatalog::readEarthLayers(path).size(), 1);
}

void TestMapSourceCatalog::dtedEntryDoesNotBecomeAnImageLayer()
{
    // An elevation entry sits between two image entries: its <url> must not land in either.
    const QString path = write(QStringLiteral("imagery.earth"),
        QStringLiteral("<image name=\"a\" driver=\"tms\"><url>http://host/a/tilemapresource.xml</url></image>\n"
                       "<elevation name=\"DTED\" driver=\"dted\"><url>/srv/dted</url></elevation>\n"
                       "<image name=\"b\" driver=\"tms\"><url>http://host/b/tilemapresource.xml</url></image>\n"));
    QString dted;
    const QList<TmsLoader::TileSourceLayer> layers = MapSourceCatalog::readEarthLayers(path, &dted);
    QCOMPARE(layers.size(), 2);
    QCOMPARE(dted, QStringLiteral("/srv/dted"));
    for (const TmsLoader::TileSourceLayer& layer : layers)
        QVERIFY(!layer.urlTemplate.contains(QStringLiteral("dted")));
}

void TestMapSourceCatalog::missingFileIsEmpty()
{
    QString dted = QStringLiteral("untouched");
    QVERIFY(MapSourceCatalog::readEarthLayers(m_tmp->filePath(QStringLiteral("nope.earth")), &dted).isEmpty());
    QCOMPARE(dted, QStringLiteral("untouched"));
}

void TestMapSourceCatalog::labelsFollowTheViewer_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<QString>("label");
    QTest::newRow("satellite") << "satellite_imagery.earth" << "Satellite Imagery";
    QTest::newRow("imagery") << "my_imagery.earth" << "Satellite Imagery";
    QTest::newRow("cardg") << "cardg_topographic.earth" << "CardG Maps";
    QTest::newRow("topo") << "topo_50k.earth" << "CardG Maps";
    QTest::newRow("other") << "local_tiles.earth" << "local tiles";
}

void TestMapSourceCatalog::labelsFollowTheViewer()
{
    QFETCH(QString, fileName);
    QFETCH(QString, label);
    QCOMPARE(MapSourceCatalog::labelForFile(QFileInfo(fileName)), label);
}

QTEST_GUILESS_MAIN(TestMapSourceCatalog)   // the reader needs no GUI, and CI has no display
#include "tst_mapsourcecatalog.moc"
