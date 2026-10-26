#pragma once
#ifndef VECTORTILERENDERER_H
#define VECTORTILERENDERER_H

#include "LineBatchRenderer.h"
#include "MapTheme.h"
#include "MbTilesReader.h"
#include "StreamingBuffer.h"
#include "TMSLoader.h"
#include "TextRenderer.h"
#include <QColor>
#include <QFont>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector2D>
#include <QVector4D>
#include <QVector>
#include <memory>

class Camera;
class QOpenGLFramebufferObject;

/**
 * @brief Draws vector MBTiles (OpenMapTiles / Mapbox Streets schema) as a map or as details
 * over another map, in the look of a MapTheme (standard OSM, Positron, Dark).
 *
 * - BaseMap mode draws the whole map: land, areas, roads with casings, names with halos.
 * - Overlay mode draws the details over satellite imagery in a "hybrid" look: no area fills.
 * Every layer of the file can be switched off on its own (setHiddenLayers()).
 *
 * Tiles are read and decoded on MbTilesReader::threadPool(); each tile's geometry and label
 * candidates are built there too and uploaded once into the tile's own buffers. Lines are
 * drawn as instanced quads (vector_line.vert) with round joins and dashes, widths in pixels.
 *
 * The orthographic globe: the same buffers are drawn into an offscreen texture in an
 * azimuthal equidistant projection on the GPU, then mapped onto the globe
 * (globe_composite.frag). Nothing is re-projected on the CPU while the globe turns, and areas
 * crossing the horizon fill correctly (the orthographic projection folds the far side onto the
 * near one; the azimuthal equidistant one does not).
 *
 * pick() finds the feature under a screen point for the details card.
 */
class VectorTileRenderer : public QObject, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    enum class Mode {
        BaseMap,
        Overlay
    };

    /// A feature found by pick(): its attributes and every visible piece of its geometry (a road
    /// or a lake is cut at tile edges; pieces with the same feature id are gathered).
    struct PickedFeature {
        bool valid = false;
        QString layerName;
        int geometryType = 0;  ///< 1 point, 2 line, 3 polygon
        quint64 id = 0;
        QHash<QString, QVariant> tags;
        QVector<QVector<QPointF>> mercatorPaths;
        QPointF mercatorAnchor;
    };

    explicit VectorTileRenderer(Camera* camera, QObject* parent = nullptr, Mode mode = Mode::BaseMap);
    ~VectorTileRenderer();

    void setTileSourceLayers(const QList<TmsLoader::TileSourceLayer>& layers);
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    bool hasVectorLayers() const;
    /// Layers (by id: "water", "transportation", ...) not drawn, not labelled, not picked.
    void setHiddenLayers(const QStringList& layers);
    /// MapTheme id: "standard" (default), "positron", "dark". Tiles already loaded are restyled
    /// on the reader threads; until then they keep the previous look.
    void setTheme(const QString& themeId);
    QString theme() const { return m_theme; }
    /// Layer ids met in the tiles read so far (for files whose metadata does not list them).
    QStringList seenLayers() const;

    /// The widget's framebuffer size in device pixels (the viewport restored after the globe
    /// texture is drawn). MapWidget sets it each frame.
    void setTargetSize(const QSize& deviceSize) { m_targetSize = deviceSize; }
    void render();
    void appendLabels(QVector<TextRenderer::Label>& labels);
    /// Feature under @p screenPos among the drawn layers (names first, then points, lines, the
    /// smallest area). Invalid when there is none.
    PickedFeature pick(const QPointF& screenPos) const;

signals:
    /// A tile arrived, left or was restyled: labels need laying out again.
    void contentChanged();
    /// seenLayers() grew.
    void layersDiscovered();

public:
    // Per-segment instance of a thick line (vector_line.vert). Public for the tile builder.
    struct LineInstance {
        float segment[4];  ///< start, end (Mercator)
        float width[3];    ///< half width (px at the tile's zoom), growth per level, distance at start (tile px)
        float dash[2];     ///< dash, gap (px)
        quint8 color[4];
    };

    struct DrawRange {
        quint64 key = 0;  ///< fills: draw order << 32 | rgba; lines: draw order
        QString layer;    ///< empty: the map background, always drawn
        int first = 0;
        int count = 0;
    };

    /// One tile's geometry in Mercator, built off the UI thread.
    struct TileMesh {
        QVector<LineBatchRenderer::LineVertex> vertices;  ///< fill fans, then hairlines
        QVector<DrawRange> fills;
        QVector<DrawRange> hairlines;
        QVector<LineInstance> lines;
        QVector<DrawRange> lineRanges;
        GLuint vbo = 0;
        GLuint vao = 0;
        GLuint lineVbo = 0;
        GLuint lineVao = 0;
        bool uploaded = false;
    };

    struct LabelCandidate {
        QString text;
        QString layer;
        QString klass;
        int rank = -1;
        QPointF mercator;       ///< anchor
        bool hasLine = false;   ///< along-line label: lineFrom..lineTo is the straight piece it sits on
        QPointF lineFrom;
        QPointF lineTo;
        int layerIndex = -1;    ///< into the decoded tile
        int featureIndex = -1;
        // Laid-out style and text size, per zoom and theme (see m_styleStamp).
        mutable quint64 stamp = ~0ull;
        mutable MapTheme::LabelStyle style;
        mutable QString shownText;
        mutable QSizeF textSize;
    };

private:
    struct CachedTile {
        int layerIndex = -1;
        int z = 0;
        int x = 0;
        int y = 0;
        QRectF mercatorBounds;
        MbTilesVectorTile tile;
        std::shared_ptr<TileMesh> mesh;
        std::shared_ptr<const QVector<LabelCandidate>> labels;
    };

    struct LabelHit {
        QRectF rect;
        QString tileKey;
        int layerIndex = -1;
        int featureIndex = -1;
    };

    struct Projection {
        int mode = 0;  ///< 0 Mercator screen, 1 azimuthal equidistant texture
        QVector2D viewport;
        QVector2D centerMercator;
        float pixelsPerMercator = 1.0f;
        QVector2D centerLatLon;
        QVector4D aeBounds;
        double aePixelsPerRadian = 1.0;
    };

    static void buildTileContent(CachedTile* cachedTile, Mode mode, const QString& themeId);

    void initializeGpuResources();
    void clearCache();
    void releaseMesh(const std::shared_ptr<TileMesh>& mesh);
    void deleteReleasedMeshes();
    void updateVisibleTiles();
    void fetchVectorTile(int layerIndex, int z, int x, int y);
    void completeVectorTileRead(const QString& key, const CachedTile& tile, const QString& errorMessage,
                                quint64 generation, quint64 themeGeneration);
    void restyleTile(const QString& key);
    void completeRestyle(const QString& key, const CachedTile& tile, quint64 themeGeneration);
    void noteLayers(const MbTilesVectorTile& tile);
    bool isLayerDrawn(const QString& layer) const;
    QList<QString> drawOrderKeys() const;
    void uploadMesh(TileMesh* mesh);
    void setProjectionUniforms(QOpenGLShaderProgram* program, const Projection& projection) const;
    void drawGeometry(const Projection& projection);
    void renderGlobe();
    QFont labelFont(const MapTheme::LabelStyle& style);

    QList<int> vectorLayerIndicesForZoom(int zoomLevel) const;
    int tileZoomForLayer(int zoomLevel, int layerIndex) const;
    QString tileKey(int layerIndex, int z, int x, int y) const;

private:
    Camera* m_camera;
    Mode m_mode;
    QString m_theme;
    quint64 m_themeGeneration;
    QSet<QString> m_hiddenLayers;
    QSet<QString> m_seenLayers;
    QList<TmsLoader::TileSourceLayer> m_layers;
    QMap<QString, CachedTile> m_tileCache;
    QSet<QString> m_visibleKeys;
    QSet<QString> m_pendingTileReads;
    // Tiles of the previous view kept drawn (under the new ones) until the new ones are read,
    // so zooming and panning never flash an empty map.
    QSet<QString> m_retainedKeys;
    std::shared_ptr<MbTilesRequestSet> m_wantedTiles;
    int m_readPriority = 0;
    QVector<GLuint> m_releasedBuffers;
    QVector<GLuint> m_releasedVertexArrays;
    QVector<LabelHit> m_labelHits;
    QHash<quint64, QFont> m_fonts;

    QOpenGLShaderProgram m_geometryProgram;   // vector_geometry.vert + colored_line.frag
    QOpenGLShaderProgram m_screenProgram;     // colored_2d.vert: the stencil cover quads
    QOpenGLShaderProgram m_lineProgram;       // vector_line.vert / .frag
    QOpenGLShaderProgram m_compositeProgram;  // globe_composite.vert / .frag
    GLuint m_quadVbo;                         // the 6 corners of a line quad
    StreamingBuffer m_coverBuffer;            // stencil cover quads
    GLuint m_coverVao;
    StreamingBuffer m_compositeBuffer;
    GLuint m_compositeVao;
    QOpenGLFramebufferObject* m_globeTarget;
    QSize m_targetSize;
    GLint m_maxTextureSize = 4096;
    bool m_gpuResourcesInitialized;
    bool m_enabled;
    quint64 m_requestGeneration;
};

#endif // VECTORTILERENDERER_H
