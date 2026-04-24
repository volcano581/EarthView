#pragma once
#ifndef CAMERA_H
#define CAMERA_H

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QRect>
#include <QMatrix4x4>

/**
 * @brief Camera manages viewport, zoom, pan, and coordinate transformations
 *
 * Responsibilities:
 * - Viewport management
 * - Zoom and pan operations
 * - Screen to Mercator coordinate conversion
 * - OpenGL transformation matrices
 */
class Camera : public QObject
{
    Q_OBJECT

public:
    enum class ProjectionMode {
        Mercator,
        Orthographic
    };

    explicit Camera(QObject* parent = nullptr);

    // Viewport management
    void setViewportSize(int width, int height);

    // Navigation
    void zoom(float delta, const QPointF& screenCenter = QPointF());
    void pan(const QPointF& delta);
    void setCenter(const QPointF& mercatorCenter);
    void setZoomLevel(double zoom);
    void setHorizontalWrapEnabled(bool enabled);
    void setProjectionMode(ProjectionMode mode);

    // Getters
    QPointF getCenterMercator() const { return m_centerMercator; }
    double getZoomLevel() const { return m_zoomLevel; }
    int getViewportWidth() const { return m_viewportWidth; }
    int getViewportHeight() const { return m_viewportHeight; }
    bool isHorizontalWrapEnabled() const { return m_horizontalWrapEnabled; }
    ProjectionMode projectionMode() const { return m_projectionMode; }
    bool isOrthographic() const { return m_projectionMode == ProjectionMode::Orthographic; }

    // Coordinate conversion
    QPointF screenToMercator(const QPointF& screenPos) const;
    QPointF mercatorToScreen(const QPointF& mercatorPos) const;
    bool projectMercatorToScreen(const QPointF& mercatorPos, QPointF* screenPos) const;
    double getOrthographicRadius() const;

    // Tile management
    QRectF getVisibleMercatorExtent() const;
    int getTileZoomLevel() const;
    QRect getTileRange(int zoomLevel) const;

    // OpenGL
    QMatrix4x4 getProjectionMatrix() const { return m_projectionMatrix; }
    QMatrix4x4 getViewMatrix() const { return m_viewMatrix; }
    void applyOpenGLTransform();

    // Resolution (meters per pixel)
    double getResolution() const;

signals:
    void cameraChanged();
    void horizontalWrapChanged(bool enabled);
    void projectionModeChanged(Camera::ProjectionMode mode);

private:
    void updateMatrices();
    void clampCenter();
    double calculateResolution() const;

private:
    QPointF m_centerMercator;
    double m_zoomLevel;
    int m_viewportWidth;
    int m_viewportHeight;
    bool m_horizontalWrapEnabled;
    ProjectionMode m_projectionMode;
    mutable double m_cachedResolution;
    mutable bool m_cacheValid;

    QMatrix4x4 m_projectionMatrix;
    QMatrix4x4 m_viewMatrix;
    QMatrix4x4 m_mvpMatrix;
};

#endif // CAMERA_H
