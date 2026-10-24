#include <QSignalSpy>
#include <QtTest>

#include "Camera.h"
#include "MapControlsModel.h"
#include "MapFeatures.h"

/**
 * @brief Tests MapControlsModel: projection sync with Camera, bearing, panel state.
 */
class TestMapControlsModel : public QObject
{
    Q_OBJECT

private slots:
    void defaultsMatchFirstRelease();
    void normalizesBearing_data();
    void normalizesBearing();
    void resetNorthClosesPanelAndZeroesBearing();
    void bindingTakesCameraProjection();
    void settingProjectionDrivesCamera();
    void cameraChangeUpdatesModel();
    void unboundModelKeepsItsOwnState();
    void panelsAreExclusive();
    void baseMapIndexFollowsList();
    void measurePanelRules();
    void layerTogglesNotifyOnce();
    void featureLayers();
    void themesAndThumbnails();
    void selectedFeature();
};

void TestMapControlsModel::featureLayers()
{
    MapControlsModel model;
    QVERIFY(!model.areFeatureLayersAvailable());
    QVERIFY(model.hiddenFeatureLayers().isEmpty());

    // Every layer of the source, in panel order, with its display name.
    QSignalSpy layersSpy(&model, &MapControlsModel::featureLayersChanged);
    model.setFeatureLayerIds({ "poi", "water", "transportation", "place", "ski_area" });
    model.setFeatureLayerIds({ "poi", "water", "transportation", "place", "ski_area" });
    QCOMPARE(layersSpy.count(), 1);
    QVERIFY(model.areFeatureLayersAvailable());
    const QVariantList layers = model.featureLayers();
    QCOMPARE(layers.size(), 5);
    QCOMPARE(layers.at(0).toMap().value("id").toString(), QString("place"));
    QCOMPARE(layers.at(0).toMap().value("label").toString(), QString("Places"));
    QCOMPARE(layers.at(3).toMap().value("label").toString(), QString("POIs"));
    QCOMPARE(layers.at(4).toMap().value("label").toString(), QString("Ski area"));

    QSignalSpy hiddenSpy(&model, &MapControlsModel::hiddenFeatureLayersChanged);
    model.toggleFeatureLayer("transportation");
    QVERIFY(!model.isFeatureLayerVisible("transportation"));
    QVERIFY(model.isFeatureLayerVisible("water"));
    model.toggleFeatureLayer("water");
    QCOMPARE(model.hiddenFeatureLayers(), QStringList({ "transportation", "water" }));
    model.toggleFeatureLayer("transportation");
    QCOMPARE(model.hiddenFeatureLayers(), QStringList({ "water" }));
    model.setHiddenFeatureLayers({ "water", "water" });
    QCOMPARE(hiddenSpy.count(), 3);

    model.setFeatureLayerIds({});
    QVERIFY(!model.areFeatureLayersAvailable());
}

void TestMapControlsModel::themesAndThumbnails()
{
    MapControlsModel model;
    QCOMPARE(model.currentTheme(), QString("standard"));
    QCOMPARE(MapControlsModel::themes().size(), 3);
    QCOMPARE(MapControlsModel::themes().at(1).toMap().value("label").toString(), QString("Positron"));
    QSignalSpy themeSpy(&model, &MapControlsModel::currentThemeChanged);
    model.setCurrentTheme("dark");
    model.setCurrentTheme("no such theme");
    QCOMPARE(model.currentTheme(), QString("dark"));
    QCOMPARE(themeSpy.count(), 1);

    QSignalSpy revisionSpy(&model, &MapControlsModel::thumbnailRevisionChanged);
    const int before = model.thumbnailRevision();
    model.bumpThumbnailRevision();
    QCOMPARE(model.thumbnailRevision(), before + 1);
    QCOMPARE(revisionSpy.count(), 1);

    model.setPanelMaxHeight(20);
    QCOMPARE(model.panelMaxHeight(), 120);  // never smaller than a section
}

void TestMapControlsModel::selectedFeature()
{
    MapControlsModel model;
    QVERIFY(!model.isFeatureSelected());
    QSignalSpy spy(&model, &MapControlsModel::selectedFeatureChanged);
    const QVariantMap feature{ { "title", "Rawal Lake" }, { "subtitle", "Lake" } };
    model.setSelectedFeature(feature);
    model.setSelectedFeature(feature);
    QVERIFY(model.isFeatureSelected());
    QCOMPARE(model.selectedFeature().value("title").toString(), QString("Rawal Lake"));
    model.clearSelectedFeature();
    QVERIFY(!model.isFeatureSelected());
    QCOMPARE(spy.count(), 2);
}

void TestMapControlsModel::defaultsMatchFirstRelease()
{
    MapControlsModel model;
    QVERIFY(!model.isOrthographic());
    QCOMPARE(model.bearingDegrees(), 0.0);
    QVERIFY(!model.isGlobe3DAvailable());
    QVERIFY(!model.isMeasureAvailable());
    QVERIFY(!model.isSettingsOpen());
}

void TestMapControlsModel::normalizesBearing_data()
{
    QTest::addColumn<double>("input");
    QTest::addColumn<double>("expected");
    QTest::newRow("zero") << 0.0 << 0.0;
    QTest::newRow("in range") << 123.5 << 123.5;
    QTest::newRow("full turn") << 360.0 << 0.0;
    QTest::newRow("over") << 725.0 << 5.0;
    QTest::newRow("negative") << -90.0 << 270.0;
    QTest::newRow("nan") << qQNaN() << 0.0;
}

void TestMapControlsModel::normalizesBearing()
{
    QFETCH(double, input);
    QFETCH(double, expected);
    QCOMPARE(MapControlsModel::normalizeBearing(input), expected);
}

void TestMapControlsModel::resetNorthClosesPanelAndZeroesBearing()
{
    MapControlsModel model;
    model.setBearingDegrees(45.0);
    model.setSettingsOpen(true);
    QSignalSpy resetSpy(&model, &MapControlsModel::northResetRequested);

    model.resetNorth();

    QCOMPARE(model.bearingDegrees(), 0.0);
    QVERIFY(!model.isSettingsOpen());
    QCOMPARE(resetSpy.count(), 1);
}

void TestMapControlsModel::bindingTakesCameraProjection()
{
    Camera camera;
    camera.setProjectionMode(Camera::ProjectionMode::Orthographic);
    MapControlsModel model;
    QSignalSpy spy(&model, &MapControlsModel::orthographicChanged);

    model.bindCamera(&camera);

    QVERIFY(model.isOrthographic());
    QCOMPARE(spy.count(), 1);
}

void TestMapControlsModel::settingProjectionDrivesCamera()
{
    Camera camera;
    MapControlsModel model;
    model.bindCamera(&camera);

    model.setOrthographic(true);
    QVERIFY(camera.isOrthographic());
    QVERIFY(model.isOrthographic());

    model.setOrthographic(false);
    QVERIFY(!camera.isOrthographic());
    QVERIFY(!model.isOrthographic());
}

void TestMapControlsModel::cameraChangeUpdatesModel()
{
    Camera camera;
    MapControlsModel model;
    model.bindCamera(&camera);
    QSignalSpy spy(&model, &MapControlsModel::orthographicChanged);

    // e.g. the View menu's Globe View action.
    camera.setProjectionMode(Camera::ProjectionMode::Orthographic);

    QVERIFY(model.isOrthographic());
    QCOMPARE(spy.count(), 1);
}

void TestMapControlsModel::unboundModelKeepsItsOwnState()
{
    MapControlsModel model;
    {
        Camera camera;
        model.bindCamera(&camera);
    }
    QVERIFY(model.camera() == nullptr);
    model.setOrthographic(true);
    QVERIFY(model.isOrthographic());
}

void TestMapControlsModel::panelsAreExclusive()
{
    MapControlsModel model;
    model.setSettingsOpen(true);
    model.setLayersOpen(true);
    QVERIFY(model.isLayersOpen());
    QVERIFY(!model.isSettingsOpen());

    model.toggleSettings();
    QVERIFY(model.isSettingsOpen());
    QVERIFY(!model.isLayersOpen());

    model.closePanels();
    QVERIFY(!model.isSettingsOpen());
    QVERIFY(!model.isLayersOpen());
}

void TestMapControlsModel::baseMapIndexFollowsList()
{
    MapControlsModel model;
    QCOMPARE(model.currentBaseMap(), -1);
    QSignalSpy spy(&model, &MapControlsModel::currentBaseMapChanged);

    model.setCurrentBaseMap(0);  // empty list: ignored
    QCOMPARE(model.currentBaseMap(), -1);

    model.setBaseMaps({ QVariantMap{ { "label", "OSM Map" }, { "icon", "thumb_osm" } },
                        QVariantMap{ { "label", "Satellite Imagery" }, { "icon", "thumb_imagery" } } });
    model.setCurrentBaseMap(1);
    QCOMPARE(model.currentBaseMap(), 1);
    model.setCurrentBaseMap(5);
    QCOMPARE(model.currentBaseMap(), 1);
    QCOMPARE(spy.count(), 1);

    // A shorter list resets an index that no longer exists.
    model.setBaseMaps({ QVariantMap{ { "label", "OSM Map" }, { "icon", "thumb_osm" } } });
    QCOMPARE(model.currentBaseMap(), 0);
}

void TestMapControlsModel::measurePanelRules()
{
    MapControlsModel model;
    model.setMeasureOpen(true);  // ruler not available yet
    QVERIFY(!model.isMeasureOpen());

    model.setMeasureAvailable(true);
    model.setSettingsOpen(true);
    model.toggleMeasure();
    QVERIFY(model.isMeasureOpen());
    QVERIFY(!model.isSettingsOpen());

    // A click on the map (closePanels) keeps measuring.
    model.closePanels();
    QVERIFY(model.isMeasureOpen());

    model.setLayersOpen(true);
    QVERIFY(!model.isMeasureOpen());

    model.toggleMeasure();
    model.resetNorth();
    QVERIFY(!model.isMeasureOpen());
}

void TestMapControlsModel::layerTogglesNotifyOnce()
{
    MapControlsModel model;
    QVERIFY(model.isHillshadeVisible());
    QVERIFY(model.areCitiesVisible());
    QVERIFY(model.areBordersVisible());
    QVERIFY(model.isGridVisible());
    QSignalSpy citiesSpy(&model, &MapControlsModel::citiesVisibleChanged);

    model.setCitiesVisible(false);
    model.setCitiesVisible(false);
    QCOMPARE(citiesSpy.count(), 1);
    QVERIFY(!model.areCitiesVisible());
}

QTEST_GUILESS_MAIN(TestMapControlsModel)
#include "tst_mapcontrolsmodel.moc"
