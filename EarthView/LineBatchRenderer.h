#pragma once
#ifndef LINEBATCHRENDERER_H
#define LINEBATCHRENDERER_H

#include <QColor>
#include <QObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QPointF>
#include <QVector>

class Camera;

class LineBatchRenderer : public QObject, protected QOpenGLFunctions
{
public:
    enum class CoordinateMode {
        Screen,
        Mercator
    };

    struct LineVertex {
        float position[2];
        float color[4];
    };

    explicit LineBatchRenderer(Camera* camera, QObject* parent = nullptr);
    ~LineBatchRenderer();

    void setVertices(const QVector<LineVertex>& vertices);
    void render(CoordinateMode mode, float lineWidth = 1.5f);

    static LineVertex vertex(double x, double y, const QColor& color);
    static void appendSegment(
        QVector<LineVertex>& vertices,
        const QPointF& a,
        const QPointF& b,
        const QColor& color);

private:
    void initializeGpuResources();

private:
    Camera* m_camera;
    QOpenGLShaderProgram m_screenProgram;
    QOpenGLShaderProgram m_mercatorProgram;
    GLuint m_vbo;
    GLuint m_vao;
    qsizetype m_vertexCount;
    bool m_initialized;
};

#endif // LINEBATCHRENDERER_H
