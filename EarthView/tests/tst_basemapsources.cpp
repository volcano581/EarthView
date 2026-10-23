#include <QtTest>

#include "BaseMapSources.h"
#include "MapControlsModel.h"

/**
 * @brief Tests BaseMapSources: which 2D map source backs the OSM and Imagery tiles.
 */
class TestBaseMapSources : public QObject
{
    Q_OBJECT

private slots:
    void noSourcesMeansOnlineOsm();
    void satelliteFileIsImagery();
    void mbTilesPreferredForOsm();
    void topographicEarthFileIsOsmWithoutMbTiles();
    void imageryOnlyLeavesOsmUnavailable();
    void baseMapForSource();

private:
    static MapSourceCatalog::Source source(const QString& filePath, const QString& label);
};

MapSourceCatalog::Source TestBaseMapSources::source(const QString& filePath, const QString& label)
{
    MapSourceCatalog::Source s;
    s.filePath = filePath;
    s.label = label;
    return s;
}

void TestBaseMapSources::noSourcesMeansOnlineOsm()
{
    const BaseMapSources::Choice choice = BaseMapSources::choose({});
    QVERIFY(choice.noSources);
    QVERIFY(choice.osmAvailable());
    QVERIFY(!choice.imageryAvailable());
    QCOMPARE(BaseMapSources::sourceForBaseMap(choice, MapControlsModel::OsmBaseMap), -1);
}

void TestBaseMapSources::satelliteFileIsImagery()
{
    QVERIFY(BaseMapSources::isImagery(source("D:/maps/satellite_imagery.earth", "Satellite Imagery")));
    QVERIFY(BaseMapSources::isImagery(source("D:/maps/world.earth", "Imagery 2024")));
    QVERIFY(!BaseMapSources::isImagery(source("D:/maps/cardg_topographic.earth", "CardG Maps")));
    QVERIFY(!BaseMapSources::isImagery(source("D:/OSM/pakistan.mbtiles", "pakistan")));
}

void TestBaseMapSources::mbTilesPreferredForOsm()
{
    const QList<MapSourceCatalog::Source> sources{
        source("D:/maps/cardg_topographic.earth", "CardG Maps"),
        source("D:/maps/satellite_imagery.earth", "Satellite Imagery"),
        source("D:/OSM/pakistan.mbtiles", "pakistan"),
    };
    const BaseMapSources::Choice choice = BaseMapSources::choose(sources);
    QCOMPARE(choice.osmIndex, 2);
    QCOMPARE(choice.imageryIndex, 1);
    QCOMPARE(BaseMapSources::sourceForBaseMap(choice, MapControlsModel::ImageryBaseMap), 1);
}

void TestBaseMapSources::topographicEarthFileIsOsmWithoutMbTiles()
{
    const QList<MapSourceCatalog::Source> sources{
        source("D:/maps/satellite_imagery.earth", "Satellite Imagery"),
        source("D:/maps/cardg_topographic.earth", "CardG Maps"),
    };
    const BaseMapSources::Choice choice = BaseMapSources::choose(sources);
    QCOMPARE(choice.osmIndex, 1);
    QCOMPARE(choice.imageryIndex, 0);
}

void TestBaseMapSources::imageryOnlyLeavesOsmUnavailable()
{
    const BaseMapSources::Choice choice = BaseMapSources::choose(
        { source("D:/maps/satellite_imagery.earth", "Satellite Imagery") });
    QVERIFY(!choice.noSources);
    QVERIFY(!choice.osmAvailable());
    QVERIFY(choice.imageryAvailable());
}

void TestBaseMapSources::baseMapForSource()
{
    const QList<MapSourceCatalog::Source> sources{
        source("D:/OSM/pakistan.mbtiles", "pakistan"),
        source("D:/maps/satellite_imagery.earth", "Satellite Imagery"),
    };
    QCOMPARE(BaseMapSources::baseMapForSource(sources, 0), int(MapControlsModel::OsmBaseMap));
    QCOMPARE(BaseMapSources::baseMapForSource(sources, 1), int(MapControlsModel::ImageryBaseMap));
    QCOMPARE(BaseMapSources::baseMapForSource(sources, -1), int(MapControlsModel::OsmBaseMap));
}

QTEST_GUILESS_MAIN(TestBaseMapSources)
#include "tst_basemapsources.moc"
