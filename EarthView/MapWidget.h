#pragma once
#ifndef MAPWIDGET_H
#define MAPWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QList>
#include <QOpenGLShaderProgram>
#include <QString>
#include <QTimer>
#include "TMSLoader.h"
#include "fpsCounter.h"

class Camera;
class TileRenderer;
class BorderRenderer;
class GridRenderer;
class CityRenderer;

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
    void setTileSourceLayers(const QList<TmsLoader::TileSourceLayer>& layers);
    bool loadWorldBorders();
    bool loadBorderShapefile(const QString& filePath, QString* errorMessage = nullptr);
    bool loadCities(const QString& directoryPath = QString(), QString* errorMessage = nullptr);
    void setTexturesVisible(bool visible);
    void setBordersVisible(bool visible);
    void setGridVisible(bool visible);
    void setCitiesVisible(bool visible);

    // Accessors
    Camera* camera() const { return m_camera; }
    bool areTexturesVisible() const { return m_texturesVisible; }
    bool areBordersVisible() const { return m_bordersVisible; }
    bool isGridVisible() const { return m_gridVisible; }
    bool areCitiesVisible() const { return m_citiesVisible; }

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
    void initializeShapeResources();

private slots:
    void onCameraChanged();

private:
    Camera* m_camera;
    TmsLoader* m_tileLoader;
    TileRenderer* m_tileRenderer;
    BorderRenderer* m_borderRenderer;
    GridRenderer* m_gridRenderer;
    CityRenderer* m_cityRenderer;

    QPoint m_lastMousePos;
    bool m_isPanning;
    bool m_texturesVisible;
    bool m_bordersVisible;
    bool m_gridVisible;
    bool m_citiesVisible;
    QString m_pendingBorderFilePath;
    QString m_pendingCitiesDirectoryPath;
    QTimer* m_updateTimer;
    FpsCounter m_fpsCounter;
    QOpenGLShaderProgram* m_solidProgram;
    GLuint m_shapeVbo;
    GLuint m_shapeVao;
    bool m_shapeResourcesInitialized;
};

#endif // MAPWIDGET_H
