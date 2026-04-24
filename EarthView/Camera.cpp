#include "Camera.h"
#include "Constants.h"
#include "MercatorProjection.h"
#include <QtMath>
#include <cmath>
#include <QOpenGLFunctions>

Camera::Camera(QObject* parent)
    : QObject(parent)
    , m_centerMercator(0.0, 0.0)
    , m_zoomLevel(2.0)
    , m_viewportWidth(800)
    , m_viewportHeight(600)
    , m_cachedResolution(0.0)
    , m_cacheValid(false)
{
    updateMatrices();
}

void Camera::setViewportSize(int width, int height)
{
    m_viewportWidth = width;
    m_viewportHeight = height;
    m_cacheValid = false;
    updateMatrices();
}

void Camera::zoom(float delta, const QPointF& screenCenter)
{
    double newZoom = m_zoomLevel + delta;
    newZoom = qBound(GIS::MIN_ZOOM, newZoom, GIS::MAX_ZOOM);

    if (!screenCenter.isNull() && !qFuzzyCompare(m_zoomLevel, newZoom)) {
        // Get Mercator point BEFORE zoom with OLD resolution
        QPointF mercatorPoint = screenToMercator(screenCenter);

        // UPDATE zoom level FIRST
        m_zoomLevel = newZoom;
        m_cacheValid = false;  // Invalidate cache immediately

        // Get new Mercator point with NEW resolution
        QPointF newMercatorPoint = screenToMercator(screenCenter);

        // Adjust center to keep the screen point fixed
        m_centerMercator += mercatorPoint - newMercatorPoint;
    }
    else {
        m_zoomLevel = newZoom;
        m_cacheValid = false;
    }

    clampCenter();
    updateMatrices();
    emit cameraChanged();
}

void Camera::pan(const QPointF& delta)
{
    double resolution = getResolution();

    // Convert screen pixels to Mercator units
    double mercatorDeltaX = delta.x() * resolution / GIS::EARTH_RADIUS;
    double mercatorDeltaY = delta.y() * resolution / GIS::EARTH_RADIUS;

    // Pan: left/right affects X, up/down affects Y (inverted for screen coordinates)
    m_centerMercator.rx() -= mercatorDeltaX;
    m_centerMercator.ry() += mercatorDeltaY;

    clampCenter();
    m_cacheValid = false;
    updateMatrices();
    emit cameraChanged();
}

void Camera::setCenter(const QPointF& mercatorCenter)
{
    m_centerMercator = mercatorCenter;
    clampCenter();
    m_cacheValid = false;
    updateMatrices();
    emit cameraChanged();
}

void Camera::setZoomLevel(double zoom)
{
    m_zoomLevel = qBound(GIS::MIN_ZOOM, zoom, GIS::MAX_ZOOM);
    clampCenter();
    m_cacheValid = false;
    updateMatrices();
    emit cameraChanged();
}

QPointF Camera::screenToMercator(const QPointF& screenPos) const
{
    double resolution = getResolution();
    double xOffset = (screenPos.x() - m_viewportWidth / 2.0) * resolution;
    double yOffset = (m_viewportHeight / 2.0 - screenPos.y()) * resolution;

    double mercatorX = m_centerMercator.x() + (xOffset / GIS::EARTH_RADIUS);
    double mercatorY = m_centerMercator.y() + (yOffset / GIS::EARTH_RADIUS);

    return QPointF(mercatorX, mercatorY);
}

QPointF Camera::mercatorToScreen(const QPointF& mercatorPos) const
{
    double resolution = getResolution();
    double dx = (mercatorPos.x() - m_centerMercator.x()) * GIS::EARTH_RADIUS;
    double dy = (mercatorPos.y() - m_centerMercator.y()) * GIS::EARTH_RADIUS;

    double screenX = m_viewportWidth / 2.0 + dx / resolution;
    double screenY = m_viewportHeight / 2.0 - dy / resolution;

    return QPointF(screenX, screenY);
}

QRectF Camera::getVisibleMercatorExtent() const
{
    QPointF topLeft = screenToMercator(QPointF(0, 0));
    QPointF bottomRight = screenToMercator(QPointF(m_viewportWidth, m_viewportHeight));
    return QRectF(topLeft, bottomRight);
}

int Camera::getTileZoomLevel() const
{
    return qBound(0, qRound(m_zoomLevel), GIS::MAX_TILE_ZOOM);
}

QRect Camera::getTileRange(int zoomLevel) const
{
    QRectF extent = getVisibleMercatorExtent();
    return MercatorProjection::getTileRange(extent, zoomLevel);
}

void Camera::applyOpenGLTransform()
{
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(m_projectionMatrix.constData());
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m_viewMatrix.constData());
}

double Camera::getResolution() const
{
    if (m_cacheValid)
        return m_cachedResolution;

    m_cachedResolution = calculateResolution();
    m_cacheValid = true;
    return m_cachedResolution;
}

void Camera::updateMatrices()
{
    m_projectionMatrix.setToIdentity();
    m_projectionMatrix.ortho(
        0.0f,
        static_cast<float>(m_viewportWidth),
        static_cast<float>(m_viewportHeight),
        0.0f,
        -1.0f,
        1.0f);

    m_viewMatrix.setToIdentity();
    m_mvpMatrix = m_projectionMatrix * m_viewMatrix;
}

void Camera::clampCenter()
{
    double halfWidthMercator = (m_viewportWidth * getResolution() / 2.0) / GIS::EARTH_RADIUS;
    double halfHeightMercator = (m_viewportHeight * getResolution() / 2.0) / GIS::EARTH_RADIUS;

    double minCenterX = GIS::MIN_MERCATOR_X + halfWidthMercator;
    double maxCenterX = GIS::MAX_MERCATOR_X - halfWidthMercator;
    double minCenterY = GIS::MIN_MERCATOR_Y + halfHeightMercator;
    double maxCenterY = GIS::MAX_MERCATOR_Y - halfHeightMercator;

   
    if (minCenterX <= maxCenterX) {
        m_centerMercator.rx() = qBound(minCenterX, m_centerMercator.x(), maxCenterX);
    } else {
        m_centerMercator.rx() = (GIS::MIN_MERCATOR_X + GIS::MAX_MERCATOR_X) / 2.0;
    }

   
    if (minCenterY <= maxCenterY) {
        m_centerMercator.ry() = qBound(minCenterY, m_centerMercator.y(), maxCenterY);
    } else {
        m_centerMercator.ry() = (GIS::MIN_MERCATOR_Y + GIS::MAX_MERCATOR_Y) / 2.0;
    }
}

double Camera::calculateResolution() const
{
    // Resolution = meters per pixel at current zoom level
    double pixelsAtZoomZero = m_viewportWidth;

    if (pixelsAtZoomZero <= 0)
        return GIS::EARTH_CIRCUMFERENCE;

    return GIS::EARTH_CIRCUMFERENCE / (pixelsAtZoomZero * pow(2.0, m_zoomLevel));
}
