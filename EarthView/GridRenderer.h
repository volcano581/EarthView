#pragma once
#ifndef GRIDRENDERER_H
#define GRIDRENDERER_H

#include <QObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QPointF>

class Camera;
class QPainter;

class GridRenderer : public QObject, protected QOpenGLFunctions
{
public:
    explicit GridRenderer(Camera* camera, QObject* parent = nullptr);
    ~GridRenderer();

    void render();
    void renderLabels(QPainter& painter);

private:
    void initializeGpuResources();
    double gridStepDegrees() const;
    QString latitudeLabel(double latitude) const;
    QString longitudeLabel(double longitude) const;
    bool projectLatLon(double latitude, double longitude, QPointF* screenPos) const;

private:
    Camera* m_camera;
    QOpenGLShaderProgram m_lineProgram;
    GLuint m_vbo;
    GLuint m_vao;
    bool m_gpuResourcesInitialized;
};

#endif // GRIDRENDERER_H
