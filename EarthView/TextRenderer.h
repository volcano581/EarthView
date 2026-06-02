#pragma once
#ifndef TEXTRENDERER_H
#define TEXTRENDERER_H

#include "StreamingBuffer.h"
#include <QColor>
#include <QFont>
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

class TextRenderer : public QObject, protected QOpenGLFunctions
{
public:
    struct Label {
        QString text;
        QRectF rect;
        QFont font;
        QColor textColor = QColor(255, 255, 255);
        QColor backgroundColor = QColor(0, 0, 0, 150);
        QMargins textMargins;
        int radius = 3;
        Qt::Alignment alignment = Qt::AlignCenter;
    };

    explicit TextRenderer(Camera* camera, QObject* parent = nullptr);
    ~TextRenderer();

    void render(const QVector<Label>& labels);

private:
    struct PackedLabel {
        int sourceIndex = -1;
        Label label;
        QRect textureRect;
    };

    void initializeGpuResources();
    QImage buildAtlas(const QVector<Label>& labels, QVector<PackedLabel>* packedLabels) const;
    quint64 hashAtlas(const QVector<Label>& labels) const;
    quint64 hashGeometry(const QVector<Label>& labels) const;
    void uploadAtlas(const QVector<Label>& labels, quint64 atlasHash);
    void uploadGeometry(const QVector<Label>& labels, quint64 geometryHash);
    void setupVertexArray();

private:
    Camera* m_camera;
    QOpenGLShaderProgram m_program;
    StreamingBuffer m_vertexBuffer;
    GLuint m_vao;
    GLuint m_textureId;
    int m_maxTextureSize;
    quint64 m_cachedAtlasHash;
    quint64 m_cachedGeometryHash;
    QVector<PackedLabel> m_cachedPackedLabels;
    QSize m_cachedAtlasSize;
    qsizetype m_cachedVertexCount;
    bool m_gpuResourcesInitialized;
    bool m_atlasCacheValid;
    bool m_geometryCacheValid;
};

#endif // TEXTRENDERER_H
