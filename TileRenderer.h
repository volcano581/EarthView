#pragma once
#ifndef TILERENDERER_H
#define TILERENDERER_H

#include "StreamingBuffer.h"
#include <QObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>

class Camera;
class TmsLoader;

/**
 * @brief TileRenderer renders TMS tiles to OpenGL
 *
 * Responsibilities:
 * - Render cached tiles
 * - Handle blending and texture binding
 * - Manage tile positioning on screen
 */
class TileRenderer : public QObject, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit TileRenderer(Camera* camera, TmsLoader* tileLoader, QObject* parent = nullptr);
    ~TileRenderer();

    void render();

private:
    void initializeGpuResources();
    void setupVertexArray();

private:
    Camera* m_camera;
    TmsLoader* m_tileLoader;
    QOpenGLShaderProgram m_tileProgram;
    // Orthographic globe: one grid over a tile, projected per tile in textured_globe.vert.
    QOpenGLShaderProgram m_globeProgram;
    GLuint m_gridVao;
    GLuint m_gridVbo;
    GLsizei m_gridVertexCount;
    StreamingBuffer m_vertexBuffer;
    GLuint m_vao;
    bool m_gpuResourcesInitialized;
};

#endif // TILERENDERER_H
