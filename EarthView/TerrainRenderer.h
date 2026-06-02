#pragma once
#ifndef TERRAINRENDERER_H
#define TERRAINRENDERER_H

#include <QMap>
#include <QObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QRectF>
#include <QString>

class Camera;
class DemLoader;
struct DemTile;

class TerrainRenderer : public QObject, protected QOpenGLFunctions
{
public:
    explicit TerrainRenderer(Camera* camera, DemLoader* demLoader, QObject* parent = nullptr);
    ~TerrainRenderer();

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    void render();
    void setPitchDegrees(float pitchDegrees);
    void setVerticalExaggeration(float exaggeration);
    float pitchDegrees() const { return m_pitchDegrees; }
    float verticalExaggeration() const { return m_verticalExaggeration; }

private:
    struct TerrainVertex {
        float mercator[2];
        float texCoord[2];
    };

    struct TileGeometry {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLsizei vertexCount = 0;
        QRectF bounds;
    };

    void initializeGpuResources();
    GLuint createElevationTexture(DemTile* tile);
    bool ensureTileGeometry(const QString& key, const QRectF& bounds);
    void destroyTileGeometry(TileGeometry* geometry);
    void appendTileMesh(QVector<TerrainVertex>* vertices, const QRectF& bounds) const;

private:
    Camera* m_camera;
    DemLoader* m_demLoader;
    QOpenGLShaderProgram m_program;
    QMap<QString, TileGeometry> m_tileGeometry;
    bool m_gpuResourcesInitialized;
    bool m_enabled;
    float m_pitchDegrees;
    float m_verticalExaggeration;
    bool m_drawWireframe;
};

#endif // TERRAINRENDERER_H
