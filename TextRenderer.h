#pragma once
#ifndef TEXTRENDERER_H
#define TEXTRENDERER_H

#include "StreamingBuffer.h"
#include <QColor>
#include <QFont>
#include <QHash>
#include <QMargins>
#include <QObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>
#include <Qt>

class Camera;
class QPainter;

/**
 * @brief Draws screen-space labels from one texture atlas.
 *
 * Each distinct label (text and style) is painted once into the atlas and reused for as long
 * as it keeps appearing; a new label costs one small QPainter run and a sub-image upload, so
 * labels coming and going while the map zooms do not repaint the others. When the atlas is
 * full it is started over with the labels of the current frame.
 */
class TextRenderer : public QObject, protected QOpenGLFunctions
{
public:
    enum class Marker { None, Circle, Triangle, Square };

    struct Label {
        QString text;
        QRectF rect;
        QFont font;
        QColor textColor = QColor(255, 255, 255);
        QColor backgroundColor = QColor(0, 0, 0, 150);
        QMargins textMargins;
        int radius = 3;
        Qt::Alignment alignment = Qt::AlignCenter;
        /// Map-style text: an outline of this colour around the glyphs (alpha 0 = none).
        QColor haloColor = QColor(0, 0, 0, 0);
        qreal haloWidth = 0.0;
        /// Degrees, clockwise, around the rect's centre (road names along the road).
        qreal angle = 0.0;
        /// A symbol at the left of the rect (POIs, peaks), the text after it.
        Marker marker = Marker::None;
        QColor markerColor;
        qreal markerSize = 0.0;
    };

    explicit TextRenderer(Camera* camera, QObject* parent = nullptr);
    ~TextRenderer();

    void render(const QVector<Label>& labels);
    /// Paints one label into @p rect (what goes into the atlas); also used for thumbnails.
    static void paintLabel(QPainter& painter, const QRect& rect, const Label& label);

private:
    struct AtlasEntry {
        QRect textureRect;
        quint64 lastUsedFrame = 0;
    };

    void initializeGpuResources();
    void setupVertexArray();
    void resetAtlas();
    bool allocate(const QSize& size, QRect* rect);
    bool addToAtlas(const Label& label, quint64 key, AtlasEntry* entry);
    static quint64 labelKey(const Label& label);

private:
    Camera* m_camera;
    QOpenGLShaderProgram m_program;
    StreamingBuffer m_vertexBuffer;
    GLuint m_vao;
    GLuint m_textureId;
    int m_atlasSize;
    QHash<quint64, AtlasEntry> m_entries;
    // Shelf packer state.
    int m_shelfX;
    int m_shelfY;
    int m_shelfHeight;
    quint64 m_frame;
    quint64 m_cachedGeometryHash;
    qsizetype m_cachedVertexCount;
    bool m_gpuResourcesInitialized;
};

#endif // TEXTRENDERER_H
