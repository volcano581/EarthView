#pragma once
#ifndef MBTILESREADER_H
#define MBTILESREADER_H

#include <QHash>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QVariant>
#include <QVector>

struct MbTilesMetadata {
    bool valid = false;
    QString name;
    QString format;
    QString scheme = "tms";
    int minZoom = 0;
    int maxZoom = 0;
};

struct MbTilesVectorFeature {
    int type = 0;
    QHash<QString, QVariant> tags;
    QVector<QVector<QPointF>> paths;
};

struct MbTilesVectorLayer {
    QString name;
    int extent = 4096;
    QVector<MbTilesVectorFeature> features;
};

struct MbTilesVectorTile {
    QVector<MbTilesVectorLayer> layers;
    bool isEmpty() const { return layers.isEmpty(); }
};

class MbTilesReader
{
public:
    static bool isVectorFormat(const QString& format);
    static MbTilesMetadata readMetadata(const QString& filePath, QString* errorMessage = nullptr);
    static MbTilesVectorTile readVectorTile(
        const QString& filePath,
        int z,
        int x,
        int y,
        bool tmsYOrigin,
        QString* errorMessage = nullptr);
    static QImage readTileImage(
        const QString& filePath,
        int z,
        int x,
        int y,
        bool tmsYOrigin,
        const QString& format,
        QString* errorMessage = nullptr);
};

#endif // MBTILESREADER_H
