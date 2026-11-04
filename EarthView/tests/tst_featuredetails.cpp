#include <QtTest>

#include "FeatureDetails.h"
#include "MapFeatures.h"

/**
 * @brief The vector-layer catalog (names and order in the Layers panel) and the details card
 * text for a clicked feature.
 */
class TestFeatureDetails : public QObject
{
    Q_OBJECT

private slots:
    void layerNames_data();
    void layerNames();
    void layersSortInPanelOrder();
    void areaLayers();
    void humanize();
    void namedRoad();
    void englishNameIsShownWhenDifferent();
    void unnamedBuilding();
    void locationIsFormatted();

private:
    static QString row(const QVariantMap& details, const QString& name)
    {
        for (const QVariant& r : details.value("rows").toList()) {
            const QVariantMap map = r.toMap();
            if (map.value("name").toString() == name)
                return map.value("value").toString();
        }
        return QString();
    }
    static QStringList rowNames(const QVariantMap& details)
    {
        QStringList names;
        for (const QVariant& r : details.value("rows").toList())
            names.append(r.toMap().value("name").toString());
        return names;
    }
};

void TestFeatureDetails::layerNames_data()
{
    QTest::addColumn<QString>("layer");
    QTest::addColumn<QString>("label");
    // OpenMapTiles
    QTest::newRow("place") << "place" << "Places";
    QTest::newRow("transportation") << "transportation" << "Roads";
    QTest::newRow("transportation_name") << "transportation_name" << "Road names";
    QTest::newRow("waterway") << "waterway" << "Rivers";
    QTest::newRow("water") << "water" << "Water";
    QTest::newRow("landcover") << "landcover" << "Land cover";
    QTest::newRow("mountain_peak") << "mountain_peak" << "Peaks";
    QTest::newRow("aerodrome_label") << "aerodrome_label" << "Airport names";
    // Mapbox Streets
    QTest::newRow("road") << "road" << "Roads";
    QTest::newRow("admin") << "admin" << "Boundaries";
    // Any other layer: its own name.
    QTest::newRow("unknown") << "ski_area" << "Ski area";
}

void TestFeatureDetails::layerNames()
{
    QFETCH(QString, layer);
    QFETCH(QString, label);
    QCOMPARE(MapFeatures::layerLabel(layer), label);
}

void TestFeatureDetails::layersSortInPanelOrder()
{
    const QStringList sorted = MapFeatures::sortedLayers(
        { "poi", "water", "zzz_custom", "transportation", "place", "water", "aaa_custom", "building" });
    QCOMPARE(sorted, QStringList({ "place", "transportation", "water", "building", "poi", "aaa_custom", "zzz_custom" }));
}

void TestFeatureDetails::areaLayers()
{
    QVERIFY(MapFeatures::isAreaLayer("landcover"));
    QVERIFY(MapFeatures::isAreaLayer("water"));
    QVERIFY(!MapFeatures::isAreaLayer("transportation"));
    QVERIFY(!MapFeatures::isAreaLayer("place"));
    QVERIFY(!MapFeatures::isAreaLayer("ski_area"));
}

void TestFeatureDetails::humanize()
{
    QCOMPARE(FeatureDetails::humanize("admin_level"), QString("Admin level"));
    QCOMPARE(FeatureDetails::humanize("name:en"), QString("Name (en)"));
    QCOMPARE(FeatureDetails::humanize("place_of_worship"), QString("Place of worship"));
    QCOMPARE(FeatureDetails::humanize(""), QString(""));
}

void TestFeatureDetails::namedRoad()
{
    const QHash<QString, QVariant> tags{ { "name", "Srinagar Highway" }, { "name:de", "Srinagar-Schnellstrasse" },
                                         { "class", "motorway" }, { "ref", "M-2" }, { "oneway", true } };
    const QVariantMap details = FeatureDetails::describe("transportation_name", 2, tags, QPointF(33.69, 73.06));
    QCOMPARE(details.value("title").toString(), QString("Srinagar Highway"));
    QVERIFY(details.value("subtitle").toString().startsWith("Motorway"));
    QVERIFY(details.value("subtitle").toString().endsWith("Road names"));
    QCOMPARE(details.value("layer").toString(), QString("transportation_name"));
    QCOMPARE(row(details, "Class"), QString("Motorway"));
    QCOMPARE(row(details, "Ref"), QString("M-2"));
    QCOMPARE(row(details, "Oneway"), QString("Yes"));
    QCOMPARE(row(details, "Geometry"), QString("Line"));
    QCOMPARE(row(details, "Map layer"), QString("transportation_name"));
    // Classification first; other-language names are left out.
    QCOMPARE(rowNames(details).first(), QString("Class"));
    QVERIFY(!rowNames(details).contains("Name (de)"));
}

void TestFeatureDetails::englishNameIsShownWhenDifferent()
{
    const QHash<QString, QVariant> tags{ { "name", QString::fromUtf8("اسلام آباد") }, { "name:en", "Islamabad" },
                                         { "class", "city" } };
    const QVariantMap details = FeatureDetails::describe("place", 1, tags, QPointF(33.68, 73.05));
    QCOMPARE(details.value("title").toString(), QString::fromUtf8("اسلام آباد"));
    QCOMPARE(row(details, "English name"), QString("Islamabad"));
    QCOMPARE(row(details, "Geometry"), QString("Point"));

    const QHash<QString, QVariant> same{ { "name", "Islamabad" }, { "name:en", "Islamabad" } };
    QVERIFY(!rowNames(FeatureDetails::describe("place", 1, same, QPointF())).contains("English name"));
}

void TestFeatureDetails::unnamedBuilding()
{
    const QVariantMap details = FeatureDetails::describe("building", 3, {}, QPointF(33.7, 73.0));
    QCOMPARE(details.value("title").toString(), QString("Building"));
    QCOMPARE(details.value("subtitle").toString(), QString("Buildings"));
    QCOMPARE(row(details, "Geometry"), QString("Area"));
}

void TestFeatureDetails::locationIsFormatted()
{
    const QVariantMap details = FeatureDetails::describe("poi", 1, {}, QPointF(-12.5, -70.25));
    QCOMPARE(row(details, "Location"), QString("12.50000%1 S, 70.25000%1 W").arg(QChar(0x00B0)));
}

QTEST_GUILESS_MAIN(TestFeatureDetails)
#include "tst_featuredetails.moc"
