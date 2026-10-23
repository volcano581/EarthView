#include <QtTest>

#include "Camera.h"
#include "Constants.h"
#include "SmoothNavigation.h"
#include "SmoothNavigator.h"

/**
 * @brief Tests smooth zoom and inertial pan: easing maths and SmoothNavigator on a 2D Camera.
 */
class TestSmoothNavigation : public QObject
{
    Q_OBJECT

private slots:
    void easeIsFrameRateIndependent();
    void easeNeverOvershoots();
    void decayHalvesAfterTauLn2();
    void releaseVelocityFromRecentSamples();
    void pausedDragDoesNotGlide();
    void wheelZoomReachesTargetAnchored();
    void zoomTargetIsClamped();
    void fastDragGlidesAndStops();
    void minimumZoomOffByDefault();
    void minimumZoomFillsHeight();
    void navigatorStopsAtFillHeight();
};

void TestSmoothNavigation::easeIsFrameRateIndependent()
{
    // Two 8 ms steps land where one 16 ms step does.
    double a = 0.0;
    a += SmoothNavigation::easeStep(a, 1.0, 0.008, SmoothNavigation::kZoomTau);
    a += SmoothNavigation::easeStep(a, 1.0, 0.008, SmoothNavigation::kZoomTau);
    const double b = SmoothNavigation::easeStep(0.0, 1.0, 0.016, SmoothNavigation::kZoomTau);
    QVERIFY(std::abs(a - b) < 1e-12);
}

void TestSmoothNavigation::easeNeverOvershoots()
{
    QCOMPARE(SmoothNavigation::easeStep(2.0, 3.0, 10.0, 0.07), 1.0);
    QVERIFY(SmoothNavigation::easeStep(2.0, 3.0, 0.016, 0.07) < 1.0);
    QCOMPARE(SmoothNavigation::easeStep(2.0, 3.0, 0.0, 0.07), 1.0);
}

void TestSmoothNavigation::decayHalvesAfterTauLn2()
{
    const QPointF v = SmoothNavigation::decayVelocity(QPointF(400, -200), 0.3 * std::log(2.0), 0.3);
    QVERIFY(std::abs(v.x() - 200.0) < 1e-9);
    QVERIFY(std::abs(v.y() + 100.0) < 1e-9);
}

void TestSmoothNavigation::releaseVelocityFromRecentSamples()
{
    const QVector<SmoothNavigation::DragSample> samples{
        { 0, QPointF(0, 0) },        // outside the window
        { 950, QPointF(100, 0) },
        { 1000, QPointF(150, 25) },
    };
    const QPointF v = SmoothNavigation::releaseVelocity(samples, 1000);
    QCOMPARE(v, QPointF(1000.0, 500.0));
}

void TestSmoothNavigation::pausedDragDoesNotGlide()
{
    const QVector<SmoothNavigation::DragSample> samples{
        { 0, QPointF(0, 0) },
        { 20, QPointF(50, 0) },
    };
    QCOMPARE(SmoothNavigation::releaseVelocity(samples, 500), QPointF());
}

void TestSmoothNavigation::wheelZoomReachesTargetAnchored()
{
    Camera camera;
    camera.setViewportSize(800, 600);
    camera.setZoomLevel(4.0);
    const QPointF anchor(200, 150);
    const QPointF before = camera.screenToMercator(anchor);

    SmoothNavigator navigator(&camera);
    navigator.wheel(240, anchor);  // two notches
    QCOMPARE(navigator.zoomTarget(), 4.0 + 2 * SmoothNavigation::kZoomPerNotch);
    QTRY_VERIFY_WITH_TIMEOUT(!navigator.isAnimating(), 3000);
    QVERIFY(std::abs(camera.getZoomLevel() - 5.0) < 1e-6);

    // The point under the cursor stays put (within a pixel).
    const QPointF after = camera.mercatorToScreen(before);
    QVERIFY2((after - anchor).manhattanLength() < 1.0,
             qPrintable(QString("%1,%2").arg(after.x()).arg(after.y())));
}

void TestSmoothNavigation::zoomTargetIsClamped()
{
    Camera camera;
    camera.setViewportSize(800, 600);
    camera.setZoomLevel(GIS::MAX_ZOOM - 0.2);
    SmoothNavigator navigator(&camera);
    navigator.zoomBy(5.0);
    QCOMPARE(navigator.zoomTarget(), GIS::MAX_ZOOM);
    navigator.stop();
}

void TestSmoothNavigation::fastDragGlidesAndStops()
{
    Camera camera;
    camera.setViewportSize(800, 600);
    camera.setZoomLevel(5.0);
    const QPointF start = camera.getCenterMercator();

    SmoothNavigator navigator(&camera);
    navigator.beginDrag(1000, QPointF(400, 300));
    navigator.dragTo(1040, QPointF(440, 300));
    navigator.dragTo(1060, QPointF(460, 300));  // 1000 px/s to the right
    navigator.endDrag(1060);
    QVERIFY(navigator.isAnimating());
    QTRY_VERIFY_WITH_TIMEOUT(!navigator.isAnimating(), 5000);
    // Dragging right moves the view west: the centre's x goes down.
    QVERIFY(camera.getCenterMercator().x() < start.x());
}

void TestSmoothNavigation::minimumZoomOffByDefault()
{
    Camera camera;
    camera.setViewportSize(800, 1024);
    QCOMPARE(camera.minimumZoomLevel(), GIS::MIN_ZOOM);
    camera.setZoomLevel(0.0);
    QCOMPARE(camera.getZoomLevel(), 0.0);
}

void TestSmoothNavigation::minimumZoomFillsHeight()
{
    Camera camera;
    camera.setViewportSize(800, 1024);  // 1024 px = 256 * 2^2
    camera.setFitHeightMinimumZoom(true);
    QVERIFY(std::abs(camera.minimumZoomLevel() - 2.0) < 1e-12);

    camera.setZoomLevel(0.5);
    QVERIFY(std::abs(camera.getZoomLevel() - 2.0) < 1e-12);
    camera.zoom(-1.0f);
    QVERIFY(std::abs(camera.getZoomLevel() - 2.0) < 1e-12);

    // A taller window raises the floor and pulls the zoom up with it.
    camera.setViewportSize(800, 2048);
    QVERIFY(std::abs(camera.getZoomLevel() - 3.0) < 1e-12);

    // A tiny window never goes below the absolute minimum.
    camera.setViewportSize(800, 100);
    QCOMPARE(camera.minimumZoomLevel(), GIS::MIN_ZOOM);
}

void TestSmoothNavigation::navigatorStopsAtFillHeight()
{
    Camera camera;
    camera.setViewportSize(800, 1024);
    camera.setFitHeightMinimumZoom(true);
    camera.setZoomLevel(3.0);
    SmoothNavigator navigator(&camera);
    navigator.zoomBy(-5.0);
    QCOMPARE(navigator.zoomTarget(), 2.0);
    QTRY_VERIFY_WITH_TIMEOUT(!navigator.isAnimating(), 3000);
    QVERIFY(std::abs(camera.getZoomLevel() - 2.0) < 1e-6);
}

QTEST_GUILESS_MAIN(TestSmoothNavigation)
#include "tst_smoothnavigation.moc"
