#pragma once
#ifndef MAPWIDGET_H
#define MAPWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QString>
#include <QTimer>
#include "fpsCounter.h"

class Camera;
class TmsLoader;
class TileRenderer;
class BorderRenderer;
class GridRenderer;

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
    bool loadWorldBorders();
    bool loadBorderShapefile(const QString& filePath, QString* errorMessage = nullptr);
    void setTexturesVisible(bool visible);
    void setBordersVisible(bool visible);
    void setGridVisible(bool visible);

    // Accessors
    Camera* camera() const { return m_camera; }
    bool areTexturesVisible() const { return m_texturesVisible; }
    bool areBordersVisible() const { return m_bordersVisible; }
    bool isGridVisible() const { return m_gridVisible; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void drawFpsOverlay();
    void drawGlobeBackdrop();

private slots:
    void onCameraChanged();

private:
    Camera* m_camera;
    TmsLoader* m_tileLoader;
    TileRenderer* m_tileRenderer;
    BorderRenderer* m_borderRenderer;
    GridRenderer* m_gridRenderer;

    QPoint m_lastMousePos;
    bool m_isPanning;
    bool m_texturesVisible;
    bool m_bordersVisible;
    bool m_gridVisible;
    QString m_pendingBorderFilePath;
    QTimer* m_updateTimer;
    FpsCounter m_fpsCounter;
};

#endif // MAPWIDGET_H
