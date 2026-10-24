#include "Camera.h"
#include "Constants.h"
#include "MercatorProjection.h"

#include <QtTest>

/**
 * @brief The 2D camera's projections, which E9 must not have changed.
 *
 * Camera lost its pseudo-3D terrain projection and its first-person "stealth" view in E9. What
 * is left — Mercator and orthographic — is what the 2D viewer has always drawn with, and rule 1
 * of CLAUDE.md says it keeps working identically. These are the round trips that say so.
 */
class TestCamera2D : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void mercatorRoundTripHoldsAcrossZoom();
    void centreMapsToTheMiddleOfTheViewport();
    void panMovesTheCentreByThePixelsAsked();
    void orthographicRoundTripHoldsOnTheVisibleFace();
    void zoomChangesResolutionButNotTheCentre();
    void projectionModeSwitchIsAnnouncedOnce();

private:
    std::unique_ptr<Camera> m_camera;
};

void TestCamera2D::init()
{
    m_camera = std::make_unique<Camera>();
    m_camera->setViewportSize(1280, 800);
    m_camera->setZoomLevel(6.0);
    m_camera->setCenter(MercatorProjection::latLonToMercator(33.6844, 73.0479));
}

void TestCamera2D::mercatorRoundTripHoldsAcrossZoom()
{
    for (const double zoom : { 2.0, 6.0, 11.0, 16.0 }) {
        m_camera->setZoomLevel(zoom);
        for (const QPointF screen : { QPointF(0, 0), QPointF(640, 400), QPointF(1279, 799), QPointF(100, 700) }) {
            const QPointF mercator = m_camera->screenToMercator(screen);
            const QPointF back = m_camera->mercatorToScreen(mercator);
            QVERIFY2(std::abs(back.x() - screen.x()) < 1e-6 && std::abs(back.y() - screen.y()) < 1e-6,
                     qPrintable(QStringLiteral("zoom %1, screen %2,%3 came back as %4,%5")
                                    .arg(zoom).arg(screen.x()).arg(screen.y()).arg(back.x()).arg(back.y())));
        }
    }
}

void TestCamera2D::centreMapsToTheMiddleOfTheViewport()
{
    const QPointF screen = m_camera->mercatorToScreen(m_camera->getCenterMercator());
    QVERIFY(std::abs(screen.x() - 640.0) < 1e-9);
    QVERIFY(std::abs(screen.y() - 400.0) < 1e-9);
}

void TestCamera2D::panMovesTheCentreByThePixelsAsked()
{
    const QPointF before = m_camera->getCenterMercator();
    const double metresPerPixel = m_camera->getResolution();
    m_camera->pan(QPointF(100.0, 0.0));   // drag right: the map follows, the centre goes west
    const QPointF after = m_camera->getCenterMercator();

    const double movedMercator = before.x() - after.x();
    const double expected = 100.0 * metresPerPixel / GIS::EARTH_RADIUS;
    QVERIFY2(std::abs(movedMercator - expected) < 1e-9,
             qPrintable(QStringLiteral("moved %1, expected %2").arg(movedMercator).arg(expected)));
    QVERIFY(std::abs(after.y() - before.y()) < 1e-12);   // a horizontal drag stays horizontal
}

void TestCamera2D::orthographicRoundTripHoldsOnTheVisibleFace()
{
    m_camera->setProjectionMode(Camera::ProjectionMode::Orthographic);
    m_camera->setZoomLevel(1.0);
    QVERIFY(m_camera->isOrthographic());

    // Near the centre of the globe's disc, where the projection is well conditioned.
    const QPointF screen(640.0, 400.0);
    const QPointF mercator = m_camera->screenToMercator(screen);
    const QPointF back = m_camera->mercatorToScreen(mercator);
    QVERIFY(std::abs(back.x() - screen.x()) < 1e-6);
    QVERIFY(std::abs(back.y() - screen.y()) < 1e-6);

    // A point on the far side of the globe does not project onto the visible face.
    QPointF offScreen;
    const QPointF antipode(m_camera->getCenterMercator().x() + M_PI, m_camera->getCenterMercator().y());
    QVERIFY(!m_camera->projectMercatorToScreen(antipode, &offScreen));
}

void TestCamera2D::zoomChangesResolutionButNotTheCentre()
{
    const QPointF centre = m_camera->getCenterMercator();
    const double before = m_camera->getResolution();
    m_camera->setZoomLevel(m_camera->getZoomLevel() + 1.0);
    QVERIFY(std::abs(m_camera->getResolution() - before / 2.0) < 1e-9);   // one zoom level = half
    QCOMPARE(m_camera->getCenterMercator(), centre);
}

void TestCamera2D::projectionModeSwitchIsAnnouncedOnce()
{
    QSignalSpy spy(m_camera.get(), &Camera::projectionModeChanged);
    m_camera->setProjectionMode(Camera::ProjectionMode::Orthographic);
    QCOMPARE(spy.count(), 1);
    m_camera->setProjectionMode(Camera::ProjectionMode::Orthographic);   // already there
    QCOMPARE(spy.count(), 1);
    m_camera->setProjectionMode(Camera::ProjectionMode::Mercator);
    QCOMPARE(spy.count(), 2);
}

QTEST_GUILESS_MAIN(TestCamera2D)
#include "tst_camera2d.moc"
