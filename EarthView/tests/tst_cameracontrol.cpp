#include "earthview3d/CameraControl.h"
#include "Geodesy.h"

#include <QtTest>

#include <cmath>

using namespace earthview3d;

class TestCameraControl : public QObject
{
    Q_OBJECT

private slots:
    void modeNamesRoundTrip();
    void smoothStepEndsAndMiddle();
    void blendEndpoints();
    void blendHeadingShortWay();
    void blendAcrossAntimeridian();
    void clampKeepsClearance();
    void clearanceKeepsLastGoodGround();
    void freeFlySpeedScalesWithHeight();
    void offsetMatchesGeodesy();
    void lookAtNorthAndUp();
    void hudShowsAllFields();
};

void TestCameraControl::modeNamesRoundTrip()
{
    for (CameraMode m : {CameraMode::Orbit, CameraMode::Chase, CameraMode::Stealth, CameraMode::FirstPerson,
                         CameraMode::FreeFly}) {
        CameraMode parsed = CameraMode::Orbit;
        QVERIFY(parseCameraMode(cameraModeName(m), &parsed));
        QCOMPARE(parsed, m);
    }
    CameraMode m = CameraMode::Chase;
    QVERIFY(parseCameraMode(QStringLiteral(" HOME "), &m));
    QCOMPARE(m, CameraMode::Orbit);
    QVERIFY(!parseCameraMode(QStringLiteral("cockpit"), &m));
}

void TestCameraControl::smoothStepEndsAndMiddle()
{
    QCOMPARE(smoothStep(-1.0), 0.0);
    QCOMPARE(smoothStep(0.0), 0.0);
    QCOMPARE(smoothStep(0.5), 0.5);
    QCOMPARE(smoothStep(1.0), 1.0);
    QCOMPARE(smoothStep(2.0), 1.0);
    QVERIFY(smoothStep(0.1) < 0.1);   // eases in
}

void TestCameraControl::blendEndpoints()
{
    CameraPose a{33.0, 73.0, 1000.0, 10.0, -20.0, 0.0};
    CameraPose b{34.0, 74.0, 500.0, 50.0, 0.0, 10.0};
    const CameraPose p0 = blendPose(a, b, 0.0);
    const CameraPose p1 = blendPose(a, b, 1.0);
    QCOMPARE(p0.latDeg, a.latDeg);
    QCOMPARE(p0.altM, a.altM);
    QCOMPARE(p1.lonDeg, b.lonDeg);
    QCOMPARE(p1.headingDeg, b.headingDeg);
    QCOMPARE(p1.rollDeg, b.rollDeg);
    const CameraPose mid = blendPose(a, b, 0.5);
    QCOMPARE(mid.altM, 750.0);
    QCOMPARE(mid.headingDeg, 30.0);
}

void TestCameraControl::blendHeadingShortWay()
{
    CameraPose a;
    CameraPose b;
    a.headingDeg = 350.0;
    b.headingDeg = 10.0;
    QCOMPARE(blendPose(a, b, 0.5).headingDeg, 0.0);
    a.headingDeg = 10.0;
    b.headingDeg = 350.0;
    QCOMPARE(blendPose(a, b, 0.5).headingDeg, 0.0);
}

void TestCameraControl::blendAcrossAntimeridian()
{
    CameraPose a;
    CameraPose b;
    a.lonDeg = 179.0;
    b.lonDeg = -179.0;
    const double lon = blendPose(a, b, 0.5).lonDeg;
    QVERIFY(std::abs(std::abs(lon) - 180.0) < 1e-9);
}

void TestCameraControl::clampKeepsClearance()
{
    QCOMPARE(clampEyeAltitude(100.0, 500.0), 502.0);
    QCOMPARE(clampEyeAltitude(600.0, 500.0), 600.0);
    QCOMPARE(clampEyeAltitude(501.0, 500.0, 10.0), 510.0);
}

void TestCameraControl::clearanceKeepsLastGoodGround()
{
    TerrainClearance c;
    QVERIFY(!c.hasGround());
    QCOMPARE(c.clamp(-50.0), -50.0);   // nothing known yet: unchanged
    c.update(true, 540.0);
    c.update(false, 0.0);              // tile not loaded: keep 540
    QVERIFY(c.hasGround());
    QCOMPARE(c.groundAltM(), 540.0);
    QCOMPARE(c.clamp(530.0), 542.0);
    c.update(true, 600.0);
    QCOMPARE(c.clamp(530.0), 602.0);
}

void TestCameraControl::freeFlySpeedScalesWithHeight()
{
    QCOMPARE(freeFlySpeed(-10.0), 5.0);
    QCOMPARE(freeFlySpeed(0.0), 5.0);
    QVERIFY(freeFlySpeed(100.0) < freeFlySpeed(1000.0));
    QCOMPARE(freeFlySpeed(1.0e6), 2500.0);
}

void TestCameraControl::offsetMatchesGeodesy()
{
    // 1 km in four directions near Islamabad; compare against exact ECEF distance.
    const double lat = 33.6844;
    const double lon = 73.0479;
    for (double bearing : {0.0, 90.0, 180.0, 270.0, 45.0}) {
        double lat2 = 0.0;
        double lon2 = 0.0;
        offsetLatLon(lat, lon, bearing, 1000.0, &lat2, &lon2);
        const glm::dvec3 a = scene3d::Geodesy::geodeticToEcef(lat, lon, 0.0);
        const glm::dvec3 b = scene3d::Geodesy::geodeticToEcef(lat2, lon2, 0.0);
        QVERIFY2(std::abs(glm::length(b - a) - 1000.0) < 1.0, qPrintable(QString::number(bearing)));
    }
}

void TestCameraControl::lookAtNorthAndUp()
{
    CameraPose eye{33.0, 73.0, 100.0, 0.0, 0.0, 0.0};
    double lat2 = 0.0;
    double lon2 = 0.0;
    offsetLatLon(eye.latDeg, eye.lonDeg, 90.0, 1000.0, &lat2, &lon2);
    double heading = 0.0;
    double pitch = 0.0;
    lookAtAngles(eye, lat2, lon2, 1100.0, &heading, &pitch);
    QVERIFY(std::abs(heading - 90.0) < 0.1);
    QVERIFY(std::abs(pitch - 45.0) < 0.1);
    lookAtAngles(eye, eye.latDeg - 0.01, eye.lonDeg, 100.0, &heading, &pitch);
    QVERIFY(std::abs(heading - 180.0) < 0.1);
    QVERIFY(std::abs(pitch) < 0.1);
}

void TestCameraControl::hudShowsAllFields()
{
    HudData hud;
    hud.mode = CameraMode::Chase;
    hud.chaseFreeLook = true;
    hud.headingDeg = 365.0;
    hud.pitchDeg = -12.34;
    hud.altM = 1234.4;
    hud.haveAgl = true;
    hud.aglM = 87.6;
    hud.speedMps = 12.25;
    hud.target = QStringLiteral("ARM-04");
    const QString text = formatHud(hud);
    QVERIFY(text.contains(QStringLiteral("CHASE (free look)")));
    QVERIFY(text.contains(QStringLiteral("005.0")));
    QVERIFY(text.contains(QStringLiteral("-12.3")));
    QVERIFY(text.contains(QStringLiteral("1234 m")));
    QVERIFY(text.contains(QStringLiteral("AGL   88 m")));
    QVERIFY(text.contains(QStringLiteral("ARM-04")));
    hud.haveAgl = false;
    hud.target.clear();
    QVERIFY(formatHud(hud).contains(QStringLiteral("AGL   --")));
    QVERIFY(formatHud(hud).contains(QStringLiteral("TGT   --")));
    QVERIFY(!formatHud(hud).contains(QStringLiteral("MSL")));
    hud.haveMsl = true;
    hud.mslM = 1282.0;
    QVERIFY(formatHud(hud).contains(QStringLiteral("MSL   1282 m")));
    QVERIFY(!formatHud(hud).contains(QStringLiteral("GND")));
    hud.showGroundOffset = true;
    hud.groundOffsets.add(-20.0);
    hud.groundOffsets.add(-30.0);
    hud.haveTargetOffset = true;
    hud.targetOffsetM = -22.5;
    const QString withOffsets = formatHud(hud);
    QVERIFY(withOffsets.contains(QStringLiteral("-22.5 m (target)")));
    QVERIFY(withOffsets.contains(QStringLiteral("n=2 mean -25.0 min -30.0 max -20.0")));
}

QTEST_GUILESS_MAIN(TestCameraControl)
#include "tst_cameracontrol.moc"
