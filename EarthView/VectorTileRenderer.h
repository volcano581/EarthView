#pragma once
#ifndef VECTORTILERENDERER_H
#define VECTORTILERENDERER_H

#include "LineBatchRenderer.h"
#include "MbTilesReader.h"
#include "TMSLoader.h"
#include "TextRenderer.h"
#include <QColor>
#include <QMap>
#include <QObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>

class Camera;

class VectorTileRenderer : public QObject, protected QOpenGLFunctions
{
public:
    explicit VectorTileRenderer(Camera* camera, QObject* parent = nullptr);
    ~VectorTileRenderer();

    void setTileSourceLayers(const QList<TmsLoader::TileSourceLayer>& layers);
    void setEnabled(bool enabled);
    bool hasVectorLayers() const;

    void render();
    void appendLabels(QVector<TextRenderer::Label>& labels);

private:
    struct FillBatch {
        QColor color;
        int drawOrder = 0;
        QVector<LineBatchRenderer::LineVertex> windingVertices;
    };

    struct FillDraw {
        QColor color;
        qsizetype windingFirst = 0;
        qsizetype windingCount = 0;
        qsizetype coverFirst = 0;
        qsizetype coverCount = 0;
    };

    struct BackgroundInstance {
        float rect[4];
        float color[4];
    };

    struct CachedTile {
        int layerIndex = -1;
        int z = 0;
        int x = 0;
        int y = 0;
        QRectF mercatorBounds;
        MbTilesVectorTile tile;
    };

    void initializeGpuResources();
    void clearCache();
    void updateVisibleTiles();
    void rebuildBatches();
    void drawFillBatch();
    void drawInstancedBackground();
    void bindFillProgram(QOpenGLShaderProgram* program, LineBatchRenderer::CoordinateMode mode);
    void setupFillVertexArray();
    void setupBackgroundInstanceArray();

    QList<int> vectorLayerIndicesForZoom(int zoomLevel) const;
    int tileZoomForLayer(int zoomLevel, int layerIndex) const;
    QString tileKey(int layerIndex, int z, int x, int y) const;
    QPointF tilePixelToMercator(const QPointF& point, const QRectF& bounds) const;

private:
    Camera* m_camera;
    QList<TmsLoader::TileSourceLayer> m_layers;
    QMap<QString, CachedTile> m_tileCache;
    QSet<QString> m_visibleKeys;
    QVector<BackgroundInstance> m_backgroundInstances;
    QVector<LineBatchRenderer::LineVertex> m_backgroundFillVertices;
    QVector<FillBatch> m_fillBatches;
    QVector<LineBatchRenderer::LineVertex> m_fillUploadVertices;
    QVector<FillDraw> m_fillDraws;
    QVector<LineBatchRenderer::LineVertex> m_lineVertices;
    LineBatchRenderer* m_lineRenderer;
    QOpenGLShaderProgram m_screenFillProgram;
    QOpenGLShaderProgram m_mercatorFillProgram;
    QOpenGLShaderProgram m_mercatorBackgroundProgram;
    GLuint m_fillVbo;
    GLuint m_fillVao;
    GLuint m_backgroundQuadVbo;
    GLuint m_backgroundInstanceVbo;
    GLuint m_backgroundVao;
    qsizetype m_backgroundFirst;
    qsizetype m_backgroundCount;
    int m_fillViewportWidth;
    int m_fillViewportHeight;
    QPointF m_batchCenterMercator;
    double m_batchResolution;
    LineBatchRenderer::CoordinateMode m_coordinateMode;
    bool m_gpuResourcesInitialized;
    bool m_batchesDirty;
    bool m_fillBufferDirty;
    bool m_backgroundInstanceBufferDirty;
    bool m_hasBatchCameraState;
    bool m_batchWasOrthographic;
    bool m_enabled;
};

#endif // VECTORTILERENDERER_H
