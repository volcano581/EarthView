#pragma once
#ifndef MAPWIDGET_H
#define MAPWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QList>
#include <QSet>
#include <QOpenGLShaderProgram>
#include <QString>
#include <QTimer>
#include "LineBatchRenderer.h"
#include "TMSLoader.h"
#include "TextRenderer.h"
#include "fpsCounter.h"

class Camera;
class TileRenderer;
class BorderRenderer;
class GridRenderer;
class CityRenderer;
class TextRenderer;
class LineBatchRenderer;
class VectorTileRenderer;
class DemLoader;
class TerrainRenderer;
class QToolButton;

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
    void setTerrainVisible(bool visible);
    void setStealthViewEnabled(bool enabled);

    // Accessors
    Camera* camera() const { return m_camera; }
    bool areTexturesVisible() const { return m_texturesVisible; }
    bool areBordersVisible() const { return m_bordersVisible; }
    bool isGridVisible() const { return m_gridVisible; }
    bool areCitiesVisible() const { return m_citiesVisible; }
    bool isTerrainVisible() const { return m_terrainVisible; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void rebuildMapLabelsIfNeeded();
    void rebuildLineBatchIfNeeded();
    void invalidateMapLabels();
    void invalidateLineBatch();
    void appendFpsOverlay(QVector<TextRenderer::Label>& labels);
    void appendScaleBarOverlay(QVector<TextRenderer::Label>& labels);
    void drawGlobeBackdrop();
    void drawScaleBarOverlay();
    void initializeShapeResources();
    void positionOverlayControls();
    void updateStealthViewMovement();

private slots:
    void onCameraChanged();

private:
    Camera* m_camera;
    TmsLoader* m_tileLoader;
    TileRenderer* m_tileRenderer;
    BorderRenderer* m_borderRenderer;
    GridRenderer* m_gridRenderer;
    CityRenderer* m_cityRenderer;
    VectorTileRenderer* m_vectorTileRenderer;
    DemLoader* m_demLoader;
    TerrainRenderer* m_terrainRenderer;
    LineBatchRenderer* m_lineBatchRenderer;
    TextRenderer* m_textRenderer;
    QToolButton* m_stealthViewButton;
    QVector<LineBatchRenderer::LineVertex> m_cachedLineVertices;
    QVector<TextRenderer::Label> m_cachedMapLabels;
    LineBatchRenderer::CoordinateMode m_lineBatchMode;

    QPoint m_lastMousePos;
    bool m_isPanning;
    bool m_texturesVisible;
    bool m_bordersVisible;
    bool m_gridVisible;
    bool m_citiesVisible;
    bool m_terrainVisible;
    QList<TmsLoader::TileSourceLayer> m_tileSourceLayers;
    QString m_pendingBorderFilePath;
    QString m_pendingCitiesDirectoryPath;
    QTimer* m_updateTimer;
    FpsCounter m_fpsCounter;
    QOpenGLShaderProgram* m_solidProgram;
    GLuint m_shapeVbo;
    GLuint m_shapeVao;
    bool m_shapeResourcesInitialized;
    bool m_lineBatchDirty;
    bool m_mapLabelsDirty;
    // Stealth view controls
    QSet<int> m_keysPressed;
    double m_stealthMoveSpeed;  // Meters per frame
};

#endif // MAPWIDGET_H
