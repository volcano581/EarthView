#pragma once
#ifndef CITYRENDERER_H
#define CITYRENDERER_H

#include "CityLoader.h"
#include <QColor>
#include <QObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QRectF>
#include <QString>

class Camera;
class QPainter;

class CityRenderer : public QObject, protected QOpenGLFunctions
{
public:
    explicit CityRenderer(Camera* camera, QObject* parent = nullptr);
    ~CityRenderer();

    bool loadDirectory(const QString& rootPath, QString* errorMessage = nullptr);
    void renderMarkers();
    void renderLabels(QPainter& painter);

    bool hasData() const { return !m_loader.layers().isEmpty(); }
    int totalPointCount() const { return m_loader.totalPointCount(); }

private:
    void initializeGpuResources();
    bool pointInExtent(const QPointF& mercator, const QRectF& extent) const;
    int maxLabelsForZoom(double zoomLevel) const;
    int markerSizeForRank(CityLoader::PlaceRank rank) const;
    QColor markerColorForRank(CityLoader::PlaceRank rank) const;
    QColor textColorForRank(CityLoader::PlaceRank rank) const;

private:
    Camera* m_camera;
    CityLoader m_loader;
    QOpenGLShaderProgram m_markerProgram;
    GLuint m_vbo;
    GLuint m_vao;
    bool m_gpuResourcesInitialized;
};

#endif // CITYRENDERER_H
