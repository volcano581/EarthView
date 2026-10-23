#include <QSignalSpy>
#include <QtTest>

#include "Camera.h"
#include "MapControlsModel.h"

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
    void unavailableBaseMapIsIgnored();
    void layerTogglesNotifyOnce();
};

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

void TestMapControlsModel::unavailableBaseMapIsIgnored()
{
    MapControlsModel model;
    QCOMPARE(model.baseMap(), int(MapControlsModel::OsmBaseMap));
    QSignalSpy spy(&model, &MapControlsModel::baseMapChanged);

    model.setBaseMap(MapControlsModel::ImageryBaseMap);  // no imagery source yet
    QCOMPARE(model.baseMap(), int(MapControlsModel::OsmBaseMap));
    QCOMPARE(spy.count(), 0);

    model.setImageryAvailable(true);
    model.setBaseMap(MapControlsModel::ImageryBaseMap);
    QCOMPARE(model.baseMap(), int(MapControlsModel::ImageryBaseMap));
    QCOMPARE(spy.count(), 1);

    model.setBaseMap(42);
    QCOMPARE(model.baseMap(), int(MapControlsModel::ImageryBaseMap));
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
