#include <QSignalSpy>
#include <QtTest>

#include "GeoMeasure.h"
#include "MeasureModel.h"

/**
 * @brief Tests the measure tool: WGS-84 distances, units, formatting, Line / Path modes.
 */
class TestGeoMeasure : public QObject
{
    Q_OBJECT

private slots:
    void zeroForSamePoint();
    void oneDegreeOfLatitudeAtEquator();
    void knownCityPair();
    void antipodalFallsBackToSphere();
    void unitConversions();
    void formatting();
    void greatCircleEndsAndMidpoint();
    void lineModeRestartsOnThirdClick();
    void pathModeAccumulatesAndUndoes();
    void unitChangeReformats();
};

void TestGeoMeasure::zeroForSamePoint()
{
    QCOMPARE(GeoMeasure::distanceMeters(QPointF(33.6, 73.0), QPointF(33.6, 73.0)), 0.0);
}

void TestGeoMeasure::oneDegreeOfLatitudeAtEquator()
{
    // WGS-84 meridian arc from 0 to 1 degree north: 110 574.4 m.
    const double d = GeoMeasure::distanceMeters(QPointF(0.0, 0.0), QPointF(1.0, 0.0));
    QVERIFY2(std::abs(d - 110574.4) < 1.0, qPrintable(QString::number(d, 'f', 2)));
}

void TestGeoMeasure::knownCityPair()
{
    // Islamabad (33.6844, 73.0479) to Karachi (24.8607, 67.0011): about 1 144 km on WGS-84.
    const double d = GeoMeasure::distanceMeters(QPointF(33.6844, 73.0479), QPointF(24.8607, 67.0011));
    QVERIFY2(std::abs(d - 1144000.0) < 6000.0, qPrintable(QString::number(d, 'f', 0)));
}

void TestGeoMeasure::antipodalFallsBackToSphere()
{
    const double d = GeoMeasure::distanceMeters(QPointF(0.0, 0.0), QPointF(0.5, 179.7));
    QVERIFY(std::isfinite(d));
    QVERIFY(d > 19900000.0 && d < 20100000.0);
}

void TestGeoMeasure::unitConversions()
{
    QCOMPARE(GeoMeasure::fromMeters(1500.0, GeoMeasure::Unit::Kilometers), 1.5);
    QCOMPARE(GeoMeasure::fromMeters(1852.0, GeoMeasure::Unit::NauticalMiles), 1.0);
    QCOMPARE(GeoMeasure::fromMeters(1609.344, GeoMeasure::Unit::Miles), 1.0);
    QCOMPARE(GeoMeasure::fromMeters(0.3048, GeoMeasure::Unit::Feet), 1.0);
    QCOMPARE(GeoMeasure::fromMeters(42.0, GeoMeasure::Unit::Meters), 42.0);
}

void TestGeoMeasure::formatting()
{
    QCOMPARE(GeoMeasure::format(1234567.0, GeoMeasure::Unit::Kilometers), QString("1,235 km"));
    QCOMPARE(GeoMeasure::format(12345.0, GeoMeasure::Unit::Kilometers), QString("12.3 km"));
    QCOMPARE(GeoMeasure::format(1234.0, GeoMeasure::Unit::Kilometers), QString("1.23 km"));
    QCOMPARE(GeoMeasure::format(843.2, GeoMeasure::Unit::Meters), QString("843 m"));
    QCOMPARE(GeoMeasure::format(1852.0, GeoMeasure::Unit::NauticalMiles), QString("1.00 nmi"));
}

void TestGeoMeasure::greatCircleEndsAndMidpoint()
{
    const QVector<QPointF> points = GeoMeasure::greatCirclePoints(QPointF(0.0, 0.0), QPointF(0.0, 90.0), 2);
    QCOMPARE(points.size(), 3);
    QCOMPARE(points.first(), QPointF(0.0, 0.0));
    QCOMPARE(points.last(), QPointF(0.0, 90.0));
    QVERIFY(std::abs(points.at(1).x()) < 1e-9);
    QVERIFY(std::abs(points.at(1).y() - 45.0) < 1e-9);
}

void TestGeoMeasure::lineModeRestartsOnThirdClick()
{
    MeasureModel model;
    QCOMPARE(model.mode(), int(MeasureModel::LineMode));
    model.addPoint(QPointF(0.0, 0.0));
    QVERIFY(model.totalText().isEmpty());
    model.addPoint(QPointF(1.0, 0.0));
    QCOMPARE(model.pointCount(), 2);
    QCOMPARE(model.totalText(), QString("111 km"));

    model.addPoint(QPointF(10.0, 10.0));
    QCOMPARE(model.pointCount(), 1);
    QVERIFY(model.totalText().isEmpty());
}

void TestGeoMeasure::pathModeAccumulatesAndUndoes()
{
    MeasureModel model;
    model.setMode(MeasureModel::PathMode);
    model.addPoint(QPointF(0.0, 0.0));
    model.addPoint(QPointF(1.0, 0.0));
    model.addPoint(QPointF(2.0, 0.0));
    QCOMPARE(model.pointCount(), 3);
    QVERIFY(std::abs(model.totalMeters() - 2.0 * 110574.4) < 300.0);
    QVERIFY(!model.lastLegText().isEmpty());

    model.removeLastPoint();
    QCOMPARE(model.pointCount(), 2);

    // Back to Line with more than two points starts over.
    model.addPoint(QPointF(3.0, 0.0));
    model.setMode(MeasureModel::LineMode);
    QCOMPARE(model.pointCount(), 0);
}

void TestGeoMeasure::unitChangeReformats()
{
    MeasureModel model;
    model.addPoint(QPointF(0.0, 0.0));
    model.addPoint(QPointF(1.0, 0.0));
    QSignalSpy spy(&model, &MeasureModel::resultChanged);
    model.setUnit(static_cast<int>(GeoMeasure::Unit::NauticalMiles));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(model.totalText(), QString("59.7 nmi"));
    model.setUnit(99);
    QCOMPARE(model.unit(), static_cast<int>(GeoMeasure::Unit::NauticalMiles));
}

QTEST_GUILESS_MAIN(TestGeoMeasure)
#include "tst_geomeasure.moc"
