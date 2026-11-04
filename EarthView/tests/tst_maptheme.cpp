#include <QtTest>

#include "MapTheme.h"

/**
 * @brief The vector map's look: road classes with casings and widths by zoom, areas, dashed
 * boundaries, labels; the three themes and the over-imagery (overlay) look.
 */
class TestMapTheme : public QObject
{
    Q_OBJECT

private slots:
    void themes();
    void roadsHaveCasingsAndWidenWithZoom();
    void roadClassesShowFromTheirZoom();
    void tunnelsAndBridges();
    void areasAndWater();
    void overlayDrawsNoAreas();
    void boundariesAreDashedAndMaritimeHidden();
    void labelsFollowPlaceHierarchy();
    void roadNamesFollowTheRoad();
    void poisHaveMarkers();

private:
    static QHash<QString, QVariant> tags(std::initializer_list<std::pair<const char*, QVariant>> values)
    {
        QHash<QString, QVariant> hash;
        for (const auto& value : values)
            hash.insert(QString::fromLatin1(value.first), value.second);
        return hash;
    }
};

void TestMapTheme::themes()
{
    QCOMPARE(MapTheme::ids(), QStringList({ "standard", "positron", "dark" }));
    QCOMPARE(MapTheme::displayName("dark"), QString("Dark"));
    QCOMPARE(MapTheme::theme("nonsense").id(), QString("standard"));
    // OSM Carto's land and water.
    QCOMPARE(MapTheme::theme("standard").background(), QColor("#f2efe9"));
    QCOMPARE(MapTheme::theme("standard").water(), QColor("#aad3df"));
    QVERIFY(MapTheme::theme("dark").background().lightness() < 60);
    QVERIFY(MapTheme::theme("positron").background().lightness() > 230);
}

void TestMapTheme::roadsHaveCasingsAndWidenWithZoom()
{
    const MapTheme& theme = MapTheme::theme("standard");
    const auto motorway = tags({ { "class", "motorway" } });
    const MapTheme::FeatureStyle z12 = theme.featureStyle("transportation", motorway, 2, 12, false);
    const MapTheme::FeatureStyle z16 = theme.featureStyle("transportation", motorway, 2, 16, false);
    QCOMPARE(z16.line.color, QColor("#e892a2"));
    QCOMPARE(z16.casing.color, QColor("#dc2a67"));
    QVERIFY(z16.line.width > z12.line.width * 2.0f);
    QVERIFY(z16.casing.width > z16.line.width);
    QVERIFY(z16.line.growth > 1.0f);
    // Every casing is drawn below every road fill.
    const MapTheme::FeatureStyle minor = theme.featureStyle("transportation", tags({ { "class", "minor" } }), 2, 16, false);
    QVERIFY(minor.line.order < z16.line.order);
    QVERIFY(z16.casing.order < minor.line.order);
    // Wider classes are wider.
    QVERIFY(MapTheme::roadWidth("motorway", 14) > MapTheme::roadWidth("secondary", 14));
    QVERIFY(MapTheme::roadWidth("secondary", 14) > MapTheme::roadWidth("service", 14));
    // Dark Matter has no casings.
    QVERIFY(!MapTheme::theme("dark").featureStyle("transportation", motorway, 2, 16, false).casing.isVisible());
}

void TestMapTheme::roadClassesShowFromTheirZoom()
{
    const MapTheme& theme = MapTheme::theme("standard");
    QVERIFY(theme.featureStyle("transportation", tags({ { "class", "motorway" } }), 2, 5, false).line.isVisible());
    QVERIFY(!theme.featureStyle("transportation", tags({ { "class", "minor" } }), 2, 9, false).line.isVisible());
    QVERIFY(theme.featureStyle("transportation", tags({ { "class", "minor" } }), 2, 13, false).line.isVisible());
    // Paths are dashed.
    const MapTheme::FeatureStyle path = theme.featureStyle("transportation", tags({ { "class", "path" } }), 2, 16, false);
    QVERIFY(path.line.dash > 0.0f);
    // Name layers are labels only.
    QVERIFY(!theme.featureStyle("transportation_name", tags({ { "class", "primary" } }), 2, 14, false).line.isVisible());
}

void TestMapTheme::tunnelsAndBridges()
{
    const MapTheme& theme = MapTheme::theme("standard");
    const MapTheme::FeatureStyle plain = theme.featureStyle("transportation", tags({ { "class", "primary" } }), 2, 15, false);
    const MapTheme::FeatureStyle bridge = theme.featureStyle("transportation", tags({ { "class", "primary" }, { "brunnel", "bridge" } }), 2, 15, false);
    const MapTheme::FeatureStyle tunnel = theme.featureStyle("transportation", tags({ { "class", "primary" }, { "brunnel", "tunnel" } }), 2, 15, false);
    QVERIFY(bridge.line.order > plain.line.order);
    QVERIFY(bridge.casing.order > plain.line.order);  // a bridge is above the roads under it
    QVERIFY(tunnel.line.color.alpha() < plain.line.color.alpha());
}

void TestMapTheme::areasAndWater()
{
    const MapTheme& theme = MapTheme::theme("standard");
    QCOMPARE(theme.featureStyle("landcover", tags({ { "class", "wood" } }), 3, 12, false).fill, QColor("#add19e"));
    QCOMPARE(theme.featureStyle("landuse", tags({ { "class", "residential" } }), 3, 12, false).fill, QColor("#e0dfdf"));
    const MapTheme::FeatureStyle water = theme.featureStyle("water", tags({ { "class", "lake" } }), 3, 12, false);
    QCOMPARE(water.fill, QColor("#aad3df"));
    QVERIFY(water.fillOrder > theme.featureStyle("landuse", tags({ { "class", "residential" } }), 3, 12, false).fillOrder);
    const MapTheme::FeatureStyle river = theme.featureStyle("waterway", tags({ { "class", "river" } }), 2, 14, false);
    QVERIFY(river.line.isVisible());
    QCOMPARE(river.line.color, QColor("#aad3df"));
    // Buildings from z13, outlined from z14.
    QVERIFY(theme.featureStyle("building", {}, 3, 12, false).fill.alpha() == 0);
    QVERIFY(theme.featureStyle("building", {}, 3, 14, false).outline.alpha() > 0);
}

void TestMapTheme::overlayDrawsNoAreas()
{
    const MapTheme& theme = MapTheme::theme("standard");
    QCOMPARE(theme.featureStyle("landcover", tags({ { "class", "wood" } }), 3, 12, true).fill.alpha(), 0);
    const MapTheme::FeatureStyle water = theme.featureStyle("water", tags({ { "class", "lake" } }), 3, 12, true);
    QCOMPARE(water.fill.alpha(), 0);
    QVERIFY(water.outline.alpha() > 0);  // shores still show over imagery
    const MapTheme::FeatureStyle road = theme.featureStyle("transportation", tags({ { "class", "primary" } }), 2, 14, true);
    QVERIFY(road.line.isVisible());
    QVERIFY(!road.casing.isVisible());
    // Names are white with a dark halo over imagery.
    const MapTheme::LabelStyle label = theme.labelStyle("place", tags({ { "class", "city" }, { "rank", 2 } }), 10, true);
    QCOMPARE(label.color, QColor(Qt::white));
    QVERIFY(label.halo.lightness() < 40);
}

void TestMapTheme::boundariesAreDashedAndMaritimeHidden()
{
    const MapTheme& theme = MapTheme::theme("standard");
    const MapTheme::FeatureStyle country = theme.featureStyle("boundary", tags({ { "admin_level", 2 } }), 2, 6, false);
    QVERIFY(country.line.isVisible());
    QVERIFY(country.line.dash > 0.0f);
    QVERIFY(!theme.featureStyle("boundary", tags({ { "admin_level", 2 }, { "maritime", 1 } }), 2, 6, false).line.isVisible());
    const MapTheme::FeatureStyle state = theme.featureStyle("boundary", tags({ { "admin_level", 4 } }), 2, 8, false);
    QVERIFY(state.line.width < country.line.width * 1.5f);
}

void TestMapTheme::labelsFollowPlaceHierarchy()
{
    const MapTheme& theme = MapTheme::theme("standard");
    const MapTheme::LabelStyle city = theme.labelStyle("place", tags({ { "class", "city" }, { "rank", 2 } }), 10, false);
    const MapTheme::LabelStyle town = theme.labelStyle("place", tags({ { "class", "town" }, { "rank", 5 } }), 10, false);
    QVERIFY(city.visible);
    QVERIFY(town.visible);
    QVERIFY(city.size > town.size);
    QVERIFY(city.priority < town.priority);
    QVERIFY(city.halo.alpha() > 0);  // text with a halo, no box
    QVERIFY(!theme.labelStyle("place", tags({ { "class", "village" } }), 9, false).visible);
    QVERIFY(theme.labelStyle("place", tags({ { "class", "village" } }), 12, false).visible);
    // Small cities wait for closer zooms.
    QVERIFY(!theme.labelStyle("place", tags({ { "class", "city" }, { "rank", 9 } }), 5, false).visible);
    const MapTheme::LabelStyle sea = theme.labelStyle("water_name", tags({ { "class", "sea" } }), 6, false);
    QVERIFY(sea.visible && sea.italic);
}

void TestMapTheme::roadNamesFollowTheRoad()
{
    const MapTheme& theme = MapTheme::theme("standard");
    const MapTheme::LabelStyle primary = theme.labelStyle("transportation_name", tags({ { "class", "primary" } }), 13, false);
    QVERIFY(primary.visible);
    QVERIFY(primary.alongLine);
    QVERIFY(!theme.labelStyle("transportation_name", tags({ { "class", "minor" } }), 13, false).visible);
    QVERIFY(theme.labelStyle("transportation_name", tags({ { "class", "minor" } }), 15, false).visible);
}

void TestMapTheme::poisHaveMarkers()
{
    const MapTheme& theme = MapTheme::theme("standard");
    QVERIFY(!theme.labelStyle("poi", tags({ { "class", "cafe" }, { "rank", 1 } }), 14, false).visible);
    const MapTheme::LabelStyle cafe = theme.labelStyle("poi", tags({ { "class", "cafe" }, { "rank", 1 } }), 16, false);
    QVERIFY(cafe.visible);
    QCOMPARE(cafe.marker, MapTheme::Marker::Circle);
    const MapTheme::LabelStyle shop = theme.labelStyle("poi", tags({ { "class", "shop" }, { "rank", 1 } }), 16, false);
    QVERIFY(shop.markerColor != cafe.markerColor);  // coloured by kind, as on the OSM website
    QVERIFY(MapTheme::isLabelLayer("poi"));
    QVERIFY(!MapTheme::isLabelLayer("landcover"));
}

QTEST_GUILESS_MAIN(TestMapTheme)
#include "tst_maptheme.moc"
