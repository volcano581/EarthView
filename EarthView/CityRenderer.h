#pragma once
#ifndef CITYRENDERER_H
#define CITYRENDERER_H

#include "CityLoader.h"
#include <QColor>
#include <QObject>
#include <QRectF>
#include <QString>

class Camera;
class QPainter;

class CityRenderer : public QObject
{
public:
    explicit CityRenderer(Camera* camera, QObject* parent = nullptr);

    bool loadDirectory(const QString& rootPath, QString* errorMessage = nullptr);
    void renderLabels(QPainter& painter);

    bool hasData() const { return !m_loader.layers().isEmpty(); }
    int totalPointCount() const { return m_loader.totalPointCount(); }

private:
    bool pointInExtent(const QPointF& mercator, const QRectF& extent) const;
    int maxLabelsForZoom(double zoomLevel) const;
    int markerSizeForRank(CityLoader::PlaceRank rank) const;
    QColor markerColorForRank(CityLoader::PlaceRank rank) const;
    QColor textColorForRank(CityLoader::PlaceRank rank) const;

private:
    Camera* m_camera;
    CityLoader m_loader;
};

#endif // CITYRENDERER_H
