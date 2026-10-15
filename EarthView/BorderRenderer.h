#pragma once
#ifndef BORDERRENDERER_H
#define BORDERRENDERER_H

#include "LineBatchRenderer.h"
#include <QObject>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QVector>

class Camera;

/**
 * @brief BorderRenderer renders world borders to OpenGL
 *
 * Responsibilities:
 * - Load border data
 * - Render borders as line strips
 */
class BorderRenderer : public QObject, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit BorderRenderer(Camera* camera, QObject* parent = nullptr);
    ~BorderRenderer();

    bool loadShapefile(const QString& filePath, QString* errorMessage = nullptr);
    void clearBorders();
    int borderCount() const { return m_borders.size(); }
    void render();
    void appendMercatorLines(QVector<LineBatchRenderer::LineVertex>& vertices) const;
    void appendScreenLines(QVector<LineBatchRenderer::LineVertex>& vertices) const;

private:
    struct BorderPoint {
        double x;  // Mercator X
        double y;  // Mercator Y
    };

    struct BorderPolygon {
        QVector<BorderPoint> points;
        bool isLand;
    };

private:
    void initializeGpuResources();
    void setupVertexArray(GLuint vao, GLuint vbo);
    void uploadMercatorVertices();
    void renderMercator();
    void renderOrthographic();
    void updateOrthographicCache();
    bool orthographicCacheMatchesCamera() const;

private:
    Camera* m_camera;
    QVector<BorderPolygon> m_borders;
    QOpenGLShaderProgram m_mercatorLineProgram;
    QOpenGLShaderProgram m_screenLineProgram;
    GLuint m_staticVbo;
    GLuint m_staticVao;
    GLuint m_dynamicVbo;
    GLuint m_dynamicVao;
    qsizetype m_staticVertexCount;
    qsizetype m_dynamicVertexCount;
    QPointF m_cachedOrthographicCenter;
    double m_cachedOrthographicZoom;
    QSize m_cachedOrthographicViewport;
    bool m_initialized;
    bool m_staticBufferDirty;
    bool m_orthographicCacheValid;
};

#endif // BORDERRENDERER_H
