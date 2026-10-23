#include "SmoothNavigator.h"
#include "Camera.h"
#include "Constants.h"

#include <QtMath>

SmoothNavigator::SmoothNavigator(Camera* camera, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
    , m_lastTickMs(0)
    , m_zooming(false)
    , m_zoomTarget(camera ? camera->getZoomLevel() : 0.0)
    , m_gliding(false)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(8);
    connect(&m_timer, &QTimer::timeout, this, &SmoothNavigator::tick);
    m_clock.start();
}

void SmoothNavigator::zoomBy(double levels, const QPointF& anchor)
{
    if (!m_camera)
        return;
    // A new zoom starts from where the camera is (it may have been moved by other code).
    const double base = m_zooming ? m_zoomTarget : m_camera->getZoomLevel();
    m_zoomTarget = qBound(GIS::MIN_ZOOM, base + levels, GIS::MAX_ZOOM);
    m_zoomAnchor = anchor;
    m_zooming = true;
    m_gliding = false;
    ensureRunning();
}

void SmoothNavigator::wheel(int angleDeltaY, const QPointF& anchor)
{
    if (angleDeltaY == 0)
        return;
    zoomBy(angleDeltaY / 120.0 * SmoothNavigation::kZoomPerNotch, anchor);
}

void SmoothNavigator::beginDrag(qint64 timeMs, const QPointF& position)
{
    stop();
    m_dragSamples.clear();
    m_dragSamples.append({ timeMs, position });
}

void SmoothNavigator::dragTo(qint64 timeMs, const QPointF& position)
{
    m_dragSamples.append({ timeMs, position });
    // Keep only what the velocity window can use.
    while (m_dragSamples.size() > 2
           && timeMs - m_dragSamples.first().timeMs > 4 * SmoothNavigation::kVelocityWindowMs)
        m_dragSamples.removeFirst();
}

void SmoothNavigator::endDrag(qint64 timeMs)
{
    const QPointF velocity = SmoothNavigation::releaseVelocity(m_dragSamples, timeMs);
    m_dragSamples.clear();
    if (std::hypot(velocity.x(), velocity.y()) < SmoothNavigation::kMinGlideSpeed)
        return;
    m_glideVelocity = velocity;
    m_gliding = true;
    ensureRunning();
}

void SmoothNavigator::stop()
{
    m_zooming = false;
    m_gliding = false;
    m_timer.stop();
}

void SmoothNavigator::ensureRunning()
{
    if (m_timer.isActive())
        return;
    m_lastTickMs = m_clock.elapsed();
    m_timer.start();
}

void SmoothNavigator::tick()
{
    if (!m_camera) {
        stop();
        return;
    }
    const qint64 now = m_clock.elapsed();
    // Clamp so a stalled frame does not jump the map.
    const double dt = qBound(0.001, (now - m_lastTickMs) / 1000.0, 0.05);
    m_lastTickMs = now;

    if (m_zooming) {
        const double current = m_camera->getZoomLevel();
        double step = SmoothNavigation::easeStep(current, m_zoomTarget, dt, SmoothNavigation::kZoomTau);
        if (std::abs(m_zoomTarget - current) < 0.002) {
            step = m_zoomTarget - current;
            m_zooming = false;
        }
        if (step != 0.0)
            m_camera->zoom(static_cast<float>(step), m_zoomAnchor);
    }

    if (m_gliding) {
        m_camera->pan(m_glideVelocity * dt);
        m_glideVelocity = SmoothNavigation::decayVelocity(m_glideVelocity, dt, SmoothNavigation::kGlideTau);
        if (std::hypot(m_glideVelocity.x(), m_glideVelocity.y()) < SmoothNavigation::kStopSpeed)
            m_gliding = false;
    }

    if (!m_zooming && !m_gliding)
        m_timer.stop();
}
