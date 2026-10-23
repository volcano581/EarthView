#include <QtTest>

#include "BaseMapSources.h"

/**
 * @brief Tests BaseMapSources: one base map tile per discovered map source, in dropdown order.
 */
class TestBaseMapSources : public QObject
{
    Q_OBJECT

private slots:
    void noSourcesGiveOnlineOsmTile();
    void oneTilePerSourceInOrder();
    void iconsFollowTheKindOfFile();
    void severalMbTilesAreNamed();

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

void TestBaseMapSources::noSourcesGiveOnlineOsmTile()
{
    const QVariantList entries = BaseMapSources::entries({});
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().toMap().value("label").toString(), QString("OSM (online)"));
    QCOMPARE(entries.first().toMap().value("icon").toString(), QString("thumb_osm"));
}

void TestBaseMapSources::oneTilePerSourceInOrder()
{
    // discoverMapSources() order: MBTiles first, then earth files.
    const QList<MapSourceCatalog::Source> sources{
        source("D:/Data/OSM/pakistan.mbtiles", "pakistan"),
        source("D:/Data/EarthFiles/satellite_imagery.earth", "Satellite Imagery"),
        source("D:/Data/EarthFiles/cardg_topographic.earth", "CardG Maps"),
        source("D:/Data/EarthFiles/roads_2024.earth", "roads 2024"),
    };
    const QVariantList entries = BaseMapSources::entries(sources);
    QCOMPARE(entries.size(), 4);
    QCOMPARE(entries.at(0).toMap().value("label").toString(), QString("OSM Map"));
    QCOMPARE(entries.at(1).toMap().value("label").toString(), QString("Satellite Imagery"));
    QCOMPARE(entries.at(2).toMap().value("label").toString(), QString("CardG Maps"));
    QCOMPARE(entries.at(3).toMap().value("label").toString(), QString("roads 2024"));
}

void TestBaseMapSources::iconsFollowTheKindOfFile()
{
    QCOMPARE(BaseMapSources::iconFor(source("D:/OSM/a.mbtiles", "a")), QString("thumb_osm"));
    QCOMPARE(BaseMapSources::iconFor(source("D:/E/satellite_imagery.earth", "Satellite Imagery")), QString("thumb_imagery"));
    QCOMPARE(BaseMapSources::iconFor(source("D:/E/world.earth", "Imagery 2024")), QString("thumb_imagery"));
    QCOMPARE(BaseMapSources::iconFor(source("D:/E/cardg_topographic.earth", "CardG Maps")), QString("thumb_topo"));
    QCOMPARE(BaseMapSources::iconFor(source("D:/E/roads.earth", "roads")), QString("thumb_map"));
}

void TestBaseMapSources::severalMbTilesAreNamed()
{
    const QList<MapSourceCatalog::Source> sources{
        source("D:/OSM/pakistan.mbtiles", "pakistan"),
        source("D:/OSM/world.mbtiles", "world"),
    };
    const QVariantList entries = BaseMapSources::entries(sources);
    QCOMPARE(entries.at(0).toMap().value("label").toString(), QString("OSM Map pakistan"));
    QCOMPARE(entries.at(1).toMap().value("label").toString(), QString("OSM Map world"));
}

QTEST_GUILESS_MAIN(TestBaseMapSources)
#include "tst_basemapsources.moc"
