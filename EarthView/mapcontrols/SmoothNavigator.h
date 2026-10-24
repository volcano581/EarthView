#pragma once
#ifndef SMOOTHNAVIGATOR_H
#define SMOOTHNAVIGATOR_H

#include "SmoothNavigation.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>

class Camera;
class QOpenGLWidget;

/**
 * @brief Smooth (eased) zoom and inertial pan for a 2D Camera.
 *
 * Wheel notches move a zoom *target*; each frame the camera zoom eases toward it, anchored at
 * the cursor. Releasing a fast drag lets the map glide on and slow down. Uses only Camera's
 * public zoom()/pan(), so Camera itself is unchanged. Owned by MapControlsOverlay, which feeds
 * it the map's wheel and drag events.
 */
class SmoothNavigator : public QObject
{
    Q_OBJECT

public:
    explicit SmoothNavigator(Camera* camera, QObject* parent = nullptr);

    /// Steps the animation once per frame @p widget presents, instead of on a timer that beats
    /// against the display refresh (two steps in one frame, one in the next: judder). The
    /// timer stays as a slow fallback for when no frames come (hidden map). Null: timer only.
    void setFrameSource(QOpenGLWidget* widget);

    /// Adds @p levels to the zoom target, anchored at @p anchor (screen px; null = centre).
    void zoomBy(double levels, const QPointF& anchor = QPointF());
    /// Wheel input: angleDelta in eighths of a degree (120 per notch).
    void wheel(int angleDeltaY, const QPointF& anchor);

    void beginDrag(qint64 timeMs, const QPointF& position);
    void dragTo(qint64 timeMs, const QPointF& position);
    /// Starts a glide when the release was fast enough.
    void endDrag(qint64 timeMs);

    /// Stops any zoom ease or glide where it is.
    void stop();
    bool isAnimating() const { return m_timer.isActive(); }
    double zoomTarget() const { return m_zoomTarget; }

private:
    void tick();
    void onTimer();
    void ensureRunning();

private:
    QPointer<Camera> m_camera;
    QPointer<QOpenGLWidget> m_frameSource;
    QMetaObject::Connection m_frameConnection;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_lastTickMs;
    bool m_zooming;
    double m_zoomTarget;
    QPointF m_zoomAnchor;
    bool m_gliding;
    QPointF m_glideVelocity;
    QVector<SmoothNavigation::DragSample> m_dragSamples;
};

#endif // SMOOTHNAVIGATOR_H
