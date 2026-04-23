#pragma once
#ifndef MAPWIDGET_H
#define MAPWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QTimer>

class Camera;
class TmsLoader;
class TileRenderer;
class BorderRenderer;

/**
 * @brief MapWidget is the main OpenGL rendering widget
 *
 * Responsibilities:
 * - OpenGL context management
 * - Mouse/keyboard event handling
 * - Orchestrate rendering of tiles and borders
 */
class MapWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit MapWidget(QWidget* parent = nullptr);
    ~MapWidget();

    // Configuration
    void setTileServerUrl(const QString& url);
    void loadWorldBorders();

    // Accessors
    Camera* camera() const { return m_camera; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onCameraChanged();

private:
    Camera* m_camera;
    TmsLoader* m_tileLoader;
    TileRenderer* m_tileRenderer;
    BorderRenderer* m_borderRenderer;

    QPoint m_lastMousePos;
    bool m_isPanning;
    QTimer* m_updateTimer;
};

#endif // MAPWIDGET_H