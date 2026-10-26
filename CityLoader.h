#pragma once
#ifndef CITYLOADER_H
#define CITYLOADER_H

#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

class CityLoader
{
public:
    enum class PlaceRank {
        City = 0,
        Town,
        Village,
        Hamlet
    };

    struct CityPoint {
        QString name;
        QPointF mercator;
        double latitude;
        double longitude;
    };

    struct CityLayer {
        QString name;
        PlaceRank rank;
        int minZoom;
        int maxZoom;
        QStringList filePaths;
        QVector<CityPoint> points;
        bool loaded = false;
    };

    bool discover(const QString& rootPath, QString* errorMessage = nullptr);
    void clear();

    QList<int> layerIndicesForZoom(double zoomLevel) const;
    bool ensureLayerLoaded(int layerIndex, QString* errorMessage = nullptr);

    const QList<CityLayer>& layers() const { return m_layers; }
    const CityLayer* layer(int index) const;
    int totalPointCount() const;

private:
    static PlaceRank rankForFileName(const QString& fileName, bool* ok = nullptr);
    static QString rankName(PlaceRank rank);
    static int minZoomForRank(PlaceRank rank);
    static int maxZoomForRank(PlaceRank rank);
    bool loadLayer(CityLayer& layer, QString* errorMessage);

private:
    QList<CityLayer> m_layers;
};

#endif // CITYLOADER_H
