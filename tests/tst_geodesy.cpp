#include "Geodesy.h"

#include <QtTest>
#include <cmath>

using scene3d::Geodesy;
using scene3d::GeodeticPosition;

/**
 * @brief Unit tests for WGS84 geodesy (ECEF conversion, ENU frames, ray intersection).
 */
class TestGeodesy : public QObject
{
    Q_OBJECT

private slots:
    void knownEcefPoints();
    void roundTrip_data();
    void roundTrip();
    void enuOrthonormal_data();
    void enuOrthonormal();
    void enuUpMatchesEllipsoidNormal();
    void rayHitsFromSpace();
    void rayMisses();
    void rayFromInside();
};

void TestGeodesy::knownEcefPoints()
{
    const glm::dvec3 origin = Geodesy::geodeticToEcef(0.0, 0.0, 0.0);
    QVERIFY(std::abs(origin.x - Geodesy::kSemiMajorAxis) < 1e-6);
    QVERIFY(std::abs(origin.y) < 1e-6);
    QVERIFY(std::abs(origin.z) < 1e-6);

    const glm::dvec3 pole = Geodesy::geodeticToEcef(90.0, 0.0, 100.0);
    QVERIFY(std::abs(pole.z - (Geodesy::kSemiMinorAxis + 100.0)) < 1e-6);
    QVERIFY(std::abs(pole.x) < 1e-6);

    const glm::dvec3 east = Geodesy::geodeticToEcef(0.0, 90.0, 0.0);
    QVERIFY(std::abs(east.y - Geodesy::kSemiMajorAxis) < 1e-6);
}

void TestGeodesy::roundTrip_data()
{
    QTest::addColumn<double>("lat");
    QTest::addColumn<double>("lon");
    QTest::addColumn<double>("h");
    QTest::newRow("origin") << 0.0 << 0.0 << 0.0;
    QTest::newRow("islamabad") << 33.6844 << 73.0479 << 540.0;
    QTest::newRow("everest") << 27.9881 << 86.9250 << 8848.86;
    QTest::newRow("dead-sea") << 31.5590 << 35.4732 << -430.0;
    QTest::newRow("southwest") << -45.5 << -120.25 << 12000.0;
    QTest::newRow("near-north-pole") << 89.9999 << 45.0 << 1000.0;
    QTest::newRow("south-pole") << -90.0 << 0.0 << 2835.0;
    QTest::newRow("antimeridian") << 10.0 << 179.9999 << 0.0;
    QTest::newRow("leo") << 51.6 << -30.0 << 420000.0;
    QTest::newRow("geo") << 0.1 << 75.0 << 35786000.0;
}

void TestGeodesy::roundTrip()
{
    QFETCH(double, lat);
    QFETCH(double, lon);
    QFETCH(double, h);

    const glm::dvec3 ecef = Geodesy::geodeticToEcef(lat, lon, h);
    const GeodeticPosition geo = Geodesy::ecefToGeodetic(ecef);
    const glm::dvec3 back = Geodesy::geodeticToEcef(geo);

    // Roadmap acceptance: ECEF round-trip error below 1 mm.
    const double errorMeters = glm::length(back - ecef);
    QVERIFY2(errorMeters < 1e-3, qPrintable(QStringLiteral("error %1 m").arg(errorMeters)));
    QVERIFY(std::abs(geo.heightMeters - h) < 1e-3);
    if (std::abs(lat) < 90.0)
        QVERIFY(std::abs(geo.longitudeDeg - lon) < 1e-9);
}

void TestGeodesy::enuOrthonormal_data()
{
    QTest::addColumn<double>("lat");
    QTest::addColumn<double>("lon");
    QTest::newRow("origin") << 0.0 << 0.0;
    QTest::newRow("mid") << 33.7 << 73.0;
    QTest::newRow("south") << -60.0 << -150.0;
    QTest::newRow("pole") << 90.0 << 10.0;
}

void TestGeodesy::enuOrthonormal()
{
    QFETCH(double, lat);
    QFETCH(double, lon);
    const glm::dmat3 enu = Geodesy::enuFrame(lat, lon);
    for (int i = 0; i < 3; ++i) {
        QVERIFY(std::abs(glm::length(enu[i]) - 1.0) < 1e-12);
        for (int j = i + 1; j < 3; ++j)
            QVERIFY(std::abs(glm::dot(enu[i], enu[j])) < 1e-12);
    }
    // Right-handed: east x north = up.
    QVERIFY(glm::length(glm::cross(enu[0], enu[1]) - enu[2]) < 1e-12);
}

void TestGeodesy::enuUpMatchesEllipsoidNormal()
{
    // Geodetic up is the ellipsoid normal: moving along it changes height only.
    const double lat = 41.0;
    const double lon = -73.0;
    const glm::dvec3 base = Geodesy::geodeticToEcef(lat, lon, 0.0);
    const glm::dvec3 raised = base + Geodesy::surfaceNormal(lat, lon) * 5000.0;
    const GeodeticPosition geo = Geodesy::ecefToGeodetic(raised);
    QVERIFY(std::abs(geo.latitudeDeg - lat) < 1e-9);
    QVERIFY(std::abs(geo.longitudeDeg - lon) < 1e-9);
    QVERIFY(std::abs(geo.heightMeters - 5000.0) < 1e-3);
}

void TestGeodesy::rayHitsFromSpace()
{
    const glm::dvec3 target = Geodesy::geodeticToEcef(30.0, 70.0, 0.0);
    const glm::dvec3 origin = Geodesy::geodeticToEcef(30.0, 70.0, 1000000.0);
    double t = 0.0;
    QVERIFY(Geodesy::intersectRayEllipsoid(origin, target - origin, &t));
    const glm::dvec3 hit = origin + t * (target - origin);
    QVERIFY(glm::length(hit - target) < 1e-3);

    // Straight down the Z axis hits the north pole.
    const glm::dvec3 above(0.0, 0.0, 2.0e7);
    QVERIFY(Geodesy::intersectRayEllipsoid(above, glm::dvec3(0.0, 0.0, -1.0), &t));
    QVERIFY(std::abs((2.0e7 - t) - Geodesy::kSemiMinorAxis) < 1e-6);
}

void TestGeodesy::rayMisses()
{
    const glm::dvec3 origin(2.0 * Geodesy::kSemiMajorAxis, 0.0, 0.0);
    double t = 0.0;
    // Pointing away from the earth.
    QVERIFY(!Geodesy::intersectRayEllipsoid(origin, glm::dvec3(1.0, 0.0, 0.0), &t));
    // Passing beside it.
    QVERIFY(!Geodesy::intersectRayEllipsoid(origin, glm::dvec3(0.0, 1.0, 0.0), &t));
    // Grazing just outside the equator.
    const glm::dvec3 grazeOrigin(Geodesy::kSemiMajorAxis + 1.0, -1.0e7, 0.0);
    QVERIFY(!Geodesy::intersectRayEllipsoid(grazeOrigin, glm::dvec3(0.0, 1.0, 0.0), &t));
}

void TestGeodesy::rayFromInside()
{
    double t = 0.0;
    QVERIFY(Geodesy::intersectRayEllipsoid(glm::dvec3(0.0), glm::dvec3(1.0, 0.0, 0.0), &t));
    QVERIFY(std::abs(t - Geodesy::kSemiMajorAxis) < 1e-6);
}

QTEST_APPLESS_MAIN(TestGeodesy)
#include "tst_geodesy.moc"
