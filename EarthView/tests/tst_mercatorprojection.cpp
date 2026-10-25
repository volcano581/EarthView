#include "MercatorProjection.h"

#include <QtTest>
#include <cmath>

/**
 * @brief Unit tests for MercatorProjection coordinate conversions.
 */
class TestMercatorProjection : public QObject
{
    Q_OBJECT

private slots:
    void latLonRoundTrip_data();
    void latLonRoundTrip();
    void screenRoundTrip();
    void orthographicRoundTrip();
    void wrapMercatorX();
    void wrapTileX();
    void tileBoundsCoverWorld();
};

void TestMercatorProjection::latLonRoundTrip_data()
{
    QTest::addColumn<double>("lat");
    QTest::addColumn<double>("lon");
    QTest::newRow("origin") << 0.0 << 0.0;
    QTest::newRow("islamabad") << 33.6844 << 73.0479;
    QTest::newRow("southwest") << -45.5 << -120.25;
    QTest::newRow("high-lat") << 80.0 << 179.0;
    QTest::newRow("antimeridian-west") << 10.0 << -179.999;
}

void TestMercatorProjection::latLonRoundTrip()
{
    QFETCH(double, lat);
    QFETCH(double, lon);
    const QPointF m = MercatorProjection::latLonToMercator(lat, lon);
    const QPointF ll = MercatorProjection::mercatorToLatLon(m.x(), m.y());
    QVERIFY2(std::abs(ll.x() - lat) < 1e-9, qPrintable(QString::number(ll.x(), 'g', 17)));
    QVERIFY2(std::abs(ll.y() - lon) < 1e-9, qPrintable(QString::number(ll.y(), 'g', 17)));
}

void TestMercatorProjection::screenRoundTrip()
{
    const QPointF center = MercatorProjection::latLonToMercator(33.0, 73.0);
    const QPointF screen(123.5, 456.25);
    const QPointF m = MercatorProjection::screenToMercator(screen, center, 1920, 1080, 250.0);
    const QPointF back = MercatorProjection::mercatorToScreen(m, center, 1920, 1080, 250.0);
    QVERIFY(std::abs(back.x() - screen.x()) < 1e-6);
    QVERIFY(std::abs(back.y() - screen.y()) < 1e-6);

    const QPointF c = MercatorProjection::mercatorToScreen(center, center, 1920, 1080, 250.0);
    QVERIFY(std::abs(c.x() - 960.0) < 1e-9);
    QVERIFY(std::abs(c.y() - 540.0) < 1e-9);
}

void TestMercatorProjection::orthographicRoundTrip()
{
    const QPointF center = MercatorProjection::latLonToMercator(20.0, 40.0);
    const QPointF target = MercatorProjection::latLonToMercator(35.0, 55.0);
    QPointF screen;
    QVERIFY(MercatorProjection::orthographicMercatorToScreen(target, center, 800, 600, 250.0, &screen));
    QPointF back;
    QVERIFY(MercatorProjection::orthographicScreenToMercator(screen, center, 800, 600, 250.0, &back));
    QVERIFY(std::abs(back.x() - target.x()) < 1e-6);
    QVERIFY(std::abs(back.y() - target.y()) < 1e-6);
}

void TestMercatorProjection::wrapMercatorX()
{
    const double x = MercatorProjection::wrapMercatorX(M_PI + 0.5);
    QVERIFY(x >= -M_PI && x < M_PI);
    QVERIFY(std::abs(x - (-M_PI + 0.5)) < 1e-12);
    QVERIFY(std::abs(MercatorProjection::wrapMercatorX(0.25) - 0.25) < 1e-12);
}

void TestMercatorProjection::wrapTileX()
{
    QCOMPARE(MercatorProjection::wrapTileX(-1, 3), 7);
    QCOMPARE(MercatorProjection::wrapTileX(8, 3), 0);
    QCOMPARE(MercatorProjection::wrapTileX(5, 3), 5);
}

void TestMercatorProjection::tileBoundsCoverWorld()
{
    const QRectF world = MercatorProjection::tileToMercatorBounds(0, 0, 0);
    QVERIFY(std::abs(world.width() - 2.0 * M_PI) < 1e-9);
    QVERIFY(std::abs(std::abs(world.height()) - 2.0 * M_PI) < 1e-9);

    const QRectF a = MercatorProjection::tileToMercatorBounds(2, 1, 1);
    QVERIFY(std::abs(a.width() - M_PI / 2.0) < 1e-9);
}

QTEST_APPLESS_MAIN(TestMercatorProjection)
#include "tst_mercatorprojection.moc"
