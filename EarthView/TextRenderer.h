#pragma once
#ifndef TEXTRENDERER_H
#define TEXTRENDERER_H

#include <QColor>
#include <QFont>
#include <QMargins>
#include <QObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QRectF>
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
    struct PackedLabel;

    void initializeGpuResources();
    QImage buildAtlas(const QVector<Label>& labels, QVector<PackedLabel>* packedLabels) const;
    quint64 hashLabels(const QVector<Label>& labels) const;
    void uploadBatch(const QVector<Label>& labels, quint64 labelsHash);

private:
    Camera* m_camera;
    QOpenGLShaderProgram m_program;
    GLuint m_vbo;
    GLuint m_vao;
    GLuint m_textureId;
    int m_maxTextureSize;
    quint64 m_cachedLabelHash;
    qsizetype m_cachedVertexCount;
    bool m_gpuResourcesInitialized;
    bool m_cacheValid;
};

#endif // TEXTRENDERER_H
