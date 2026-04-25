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
    , m_horizontalWrapEnabled(false)
    , m_projectionMode(ProjectionMode::Mercator)
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
    if (isOrthographic()) {
        const double radius = getOrthographicRadius();
        if (radius > 0.0) {
            QPointF latLon = MercatorProjection::mercatorToLatLon(m_centerMercator.x(), m_centerMercator.y());
            double lat = qBound(-85.0, latLon.x() + delta.y() / radius * 180.0 / M_PI, 85.0);
            double lon = latLon.y() - delta.x() / radius * 180.0 / M_PI;
            m_centerMercator = MercatorProjection::latLonToMercator(lat, lon);
        }

        clampCenter();
        m_cacheValid = false;
        updateMatrices();
        emit cameraChanged();
        return;
    }

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

void Camera::setHorizontalWrapEnabled(bool enabled)
{
    if (m_horizontalWrapEnabled == enabled)
        return;

    m_horizontalWrapEnabled = enabled;
    clampCenter();
    updateMatrices();
    emit horizontalWrapChanged(enabled);
    emit cameraChanged();
}

void Camera::setProjectionMode(ProjectionMode mode)
{
    if (m_projectionMode == mode)
        return;

    m_projectionMode = mode;
    clampCenter();
    updateMatrices();
    emit projectionModeChanged(mode);
    emit cameraChanged();
}

QPointF Camera::screenToMercator(const QPointF& screenPos) const
{
    if (isOrthographic()) {
        QPointF mercatorPos;
        if (MercatorProjection::orthographicScreenToMercator(
            screenPos,
            m_centerMercator,
            m_viewportWidth,
            m_viewportHeight,
            getOrthographicRadius(),
            &mercatorPos)) {
            return mercatorPos;
        }
        return m_centerMercator;
    }

    return MercatorProjection::screenToMercator(
        screenPos,
        m_centerMercator,
        m_viewportWidth,
        m_viewportHeight,
        getResolution());
}

QPointF Camera::mercatorToScreen(const QPointF& mercatorPos) const
{
    if (isOrthographic()) {
        QPointF screenPos;
        MercatorProjection::orthographicMercatorToScreen(
            mercatorPos,
            m_centerMercator,
            m_viewportWidth,
            m_viewportHeight,
            getOrthographicRadius(),
            &screenPos);
        return screenPos;
    }

    return MercatorProjection::mercatorToScreen(
        mercatorPos,
        m_centerMercator,
        m_viewportWidth,
        m_viewportHeight,
        getResolution());
}

bool Camera::projectMercatorToScreen(const QPointF& mercatorPos, QPointF* screenPos) const
{
    if (!screenPos)
        return false;

    if (isOrthographic()) {
        return MercatorProjection::orthographicMercatorToScreen(
            mercatorPos,
            m_centerMercator,
            m_viewportWidth,
            m_viewportHeight,
            getOrthographicRadius(),
            screenPos);
    }

    *screenPos = mercatorToScreen(mercatorPos);
    return true;
}

double Camera::getOrthographicRadius() const
{
    return GIS::TILE_SIZE * std::pow(2.0, m_zoomLevel) / 2.0;
}

QRectF Camera::getVisibleMercatorExtent() const
{
    if (isOrthographic()) {
        const int samplesPerAxis = 16;
        const int perimeterSamples = 128;
        const double screenMargin = GIS::TILE_SIZE;
        bool hasSample = false;
        double minX = 0.0;
        double maxX = 0.0;
        double minY = 0.0;
        double maxY = 0.0;
        const double centerLon = m_centerMercator.x();
        const double radius = getOrthographicRadius();
        const QPointF screenCenter(m_viewportWidth / 2.0, m_viewportHeight / 2.0);
        const QRectF sampleBounds(
            -screenMargin,
            -screenMargin,
            m_viewportWidth + 2.0 * screenMargin,
            m_viewportHeight + 2.0 * screenMargin);

        auto includeScreenSample = [&](const QPointF& screenPos) {
            QPointF mercatorPos;
            if (!MercatorProjection::orthographicScreenToMercator(
                screenPos,
                m_centerMercator,
                m_viewportWidth,
                m_viewportHeight,
                radius,
                &mercatorPos)) {
                return;
            }

            double x = mercatorPos.x();
            while (x - centerLon > M_PI) {
                x -= 2.0 * M_PI;
            }
            while (x - centerLon < -M_PI) {
                x += 2.0 * M_PI;
            }

            if (!hasSample) {
                minX = maxX = x;
                minY = maxY = mercatorPos.y();
                hasSample = true;
            }
            else {
                minX = qMin(minX, x);
                maxX = qMax(maxX, x);
                minY = qMin(minY, mercatorPos.y());
                maxY = qMax(maxY, mercatorPos.y());
            }
        };

        for (int ix = 0; ix <= samplesPerAxis; ++ix) {
            for (int iy = 0; iy <= samplesPerAxis; ++iy) {
                includeScreenSample(QPointF(
                    -screenMargin + (m_viewportWidth + 2.0 * screenMargin) * ix / samplesPerAxis,
                    -screenMargin + (m_viewportHeight + 2.0 * screenMargin) * iy / samplesPerAxis));
            }
        }

        const double perimeterRadius = radius * 0.999;
        for (int i = 0; i < perimeterSamples; ++i) {
            const double angle = 2.0 * M_PI * i / perimeterSamples;
            const QPointF perimeterPoint(
                screenCenter.x() + std::cos(angle) * perimeterRadius,
                screenCenter.y() + std::sin(angle) * perimeterRadius);
            if (sampleBounds.contains(perimeterPoint)) {
                includeScreenSample(perimeterPoint);
            }
        }

        includeScreenSample(screenCenter);

        if (hasSample) {
            return QRectF(QPointF(minX, maxY), QPointF(maxX, minY));
        }
    }

    QPointF topLeft = screenToMercator(QPointF(0, 0));
    QPointF bottomRight = screenToMercator(QPointF(m_viewportWidth, m_viewportHeight));
    return QRectF(topLeft, bottomRight);
}

int Camera::getTileZoomLevel() const
{
    return qBound(0, static_cast<int>(std::ceil(m_zoomLevel)), GIS::MAX_TILE_ZOOM);
}

QRect Camera::getTileRange(int zoomLevel) const
{
    QRectF extent = getVisibleMercatorExtent();
    QRect tileRange = MercatorProjection::getTileRange(extent, zoomLevel, m_horizontalWrapEnabled || isOrthographic());
    if (isOrthographic()) {
        const int preloadMargin = 2;
        const int maxTile = (1 << zoomLevel) - 1;
        tileRange.adjust(-preloadMargin, -preloadMargin, preloadMargin, preloadMargin);
        tileRange.setTop(qBound(0, tileRange.top(), maxTile));
        tileRange.setBottom(qBound(0, tileRange.bottom(), maxTile));
    }
    return tileRange;
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

   
    if (m_horizontalWrapEnabled || isOrthographic()) {
        m_centerMercator.rx() = MercatorProjection::wrapMercatorX(m_centerMercator.x());
    }
    else if (minCenterX <= maxCenterX) {
        m_centerMercator.rx() = qBound(minCenterX, m_centerMercator.x(), maxCenterX);
    }
    else {
        m_centerMercator.rx() = (GIS::MIN_MERCATOR_X + GIS::MAX_MERCATOR_X) / 2.0;
    }

   
    if (isOrthographic()) {
        QPointF latLon = MercatorProjection::mercatorToLatLon(m_centerMercator.x(), m_centerMercator.y());
        m_centerMercator = MercatorProjection::latLonToMercator(qBound(-85.0, latLon.x(), 85.0), latLon.y());
    }
    else if (minCenterY <= maxCenterY) {
        m_centerMercator.ry() = qBound(minCenterY, m_centerMercator.y(), maxCenterY);
    } else {
        m_centerMercator.ry() = (GIS::MIN_MERCATOR_Y + GIS::MAX_MERCATOR_Y) / 2.0;
    }
}

double Camera::calculateResolution() const
{
    if (GIS::TILE_SIZE <= 0)
        return GIS::EARTH_CIRCUMFERENCE;

    return GIS::EARTH_CIRCUMFERENCE / (GIS::TILE_SIZE * pow(2.0, m_zoomLevel));
}
