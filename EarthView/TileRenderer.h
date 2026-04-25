#pragma once
#ifndef TILERENDERER_H
#define TILERENDERER_H

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

private:
    Camera* m_camera;
    TmsLoader* m_tileLoader;
    QOpenGLShaderProgram m_tileProgram;
    GLuint m_vbo;
    GLuint m_vao;
    bool m_gpuResourcesInitialized;
};

#endif // TILERENDERER_H
