#include "CityLoader.h"
#include "MercatorProjection.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
QString firstDisplayNamePart(const QString& displayName)
{
    return displayName.section(',', 0, 0).trimmed();
}
}

bool CityLoader::discover(const QString& rootPath, QString* errorMessage)
{
    clear();

    const QDir rootDir(rootPath);
    if (!rootDir.exists()) {
        if (errorMessage) {
            *errorMessage = QString("Cities directory not found: %1").arg(rootPath);
        }
        return false;
    }

    QMap<PlaceRank, CityLayer> layerByRank;
    const QList<PlaceRank> ranks = {
        PlaceRank::City,
        PlaceRank::Town,
        PlaceRank::Village,
        PlaceRank::Hamlet
    };

    for (PlaceRank rank : ranks) {
        CityLayer layer;
        layer.rank = rank;
        layer.name = rankName(rank);
        layer.minZoom = minZoomForRank(rank);
        layer.maxZoom = maxZoomForRank(rank);
        layerByRank.insert(rank, layer);
    }

    const QFileInfoList countryDirs = rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& countryDir : countryDirs) {
        const QFileInfoList ndjsonFiles = QDir(countryDir.absoluteFilePath()).entryInfoList(
            { "*.ndjson" },
            QDir::Files,
            QDir::Name);

        for (const QFileInfo& fileInfo : ndjsonFiles) {
            bool ok = false;
            const PlaceRank rank = rankForFileName(fileInfo.fileName(), &ok);
            if (!ok)
                continue;

            layerByRank[rank].filePaths.append(fileInfo.absoluteFilePath());
        }
    }

    for (PlaceRank rank : ranks) {
        CityLayer layer = layerByRank.value(rank);
        layer.filePaths.sort(Qt::CaseInsensitive);
        if (!layer.filePaths.isEmpty()) {
            m_layers.append(layer);
        }
    }

    if (m_layers.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QString("No city NDJSON files found under %1").arg(rootPath);
        }
        return false;
    }

    if (errorMessage) {
        *errorMessage = QString("Discovered %1 city layers").arg(m_layers.size());
    }
    return true;
}

void CityLoader::clear()
{
    m_layers.clear();
}

QList<int> CityLoader::layerIndicesForZoom(double zoomLevel) const
{
    QList<int> indices;
    for (int i = 0; i < m_layers.size(); ++i) {
        const CityLayer& layer = m_layers.at(i);
        if (zoomLevel >= layer.minZoom && zoomLevel <= layer.maxZoom) {
            indices.append(i);
        }
    }
    return indices;
}

bool CityLoader::ensureLayerLoaded(int layerIndex, QString* errorMessage)
{
    if (layerIndex < 0 || layerIndex >= m_layers.size())
        return false;

    CityLayer& layer = m_layers[layerIndex];
    if (layer.loaded)
        return true;

    return loadLayer(layer, errorMessage);
}

const CityLoader::CityLayer* CityLoader::layer(int index) const
{
    if (index < 0 || index >= m_layers.size())
        return nullptr;

    return &m_layers.at(index);
}

int CityLoader::totalPointCount() const
{
    int total = 0;
    for (const CityLayer& layer : m_layers) {
        total += layer.points.size();
    }
    return total;
}

CityLoader::PlaceRank CityLoader::rankForFileName(const QString& fileName, bool* ok)
{
    const QString lowerName = fileName.toLower();
    if (ok) {
        *ok = true;
    }

    if (lowerName.contains("city"))
        return PlaceRank::City;
    if (lowerName.contains("town"))
        return PlaceRank::Town;
    if (lowerName.contains("village"))
        return PlaceRank::Village;
    if (lowerName.contains("hamlet"))
        return PlaceRank::Hamlet;

    if (ok) {
        *ok = false;
    }
    return PlaceRank::City;
}

QString CityLoader::rankName(PlaceRank rank)
{
    switch (rank) {
    case PlaceRank::City:
        return "Cities";
    case PlaceRank::Town:
        return "Towns";
    case PlaceRank::Village:
        return "Villages";
    case PlaceRank::Hamlet:
        return "Hamlets";
    }

    return "Places";
}

int CityLoader::minZoomForRank(PlaceRank rank)
{
    switch (rank) {
    case PlaceRank::City:
        return 0;
    case PlaceRank::Town:
        return 8;
    case PlaceRank::Village:
        return 11;
    case PlaceRank::Hamlet:
        return 14;
    }

    return 0;
}

int CityLoader::maxZoomForRank(PlaceRank rank)
{
    switch (rank) {
    case PlaceRank::City:
        return 7;
    case PlaceRank::Town:
        return 10;
    case PlaceRank::Village:
        return 13;
    case PlaceRank::Hamlet:
        return 18;
    }

    return 18;
}

bool CityLoader::loadLayer(CityLayer& layer, QString* errorMessage)
{
    layer.points.clear();

    for (const QString& filePath : layer.filePaths) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (errorMessage) {
                *errorMessage = QString("Could not open %1").arg(filePath);
            }
            continue;
        }

        while (!file.atEnd()) {
            const QByteArray line = file.readLine().trimmed();
            if (line.isEmpty())
                continue;

            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject())
                continue;

            const QJsonObject object = document.object();
            const QJsonArray location = object.value("location").toArray();
            if (location.size() < 2)
                continue;

            bool lonOk = false;
            bool latOk = false;
            const double longitude = location.at(0).toDouble(std::numeric_limits<double>::quiet_NaN());
            const double latitude = location.at(1).toDouble(std::numeric_limits<double>::quiet_NaN());
            lonOk = std::isfinite(longitude);
            latOk = std::isfinite(latitude);
            if (!lonOk || !latOk || latitude < -85.0 || latitude > 85.0)
                continue;

            QString name = object.value("name").toString().trimmed();
            if (name.isEmpty()) {
                name = firstDisplayNamePart(object.value("display_name").toString());
            }
            if (name.isEmpty())
                continue;

            CityPoint point;
            point.name = name;
            point.latitude = latitude;
            point.longitude = longitude;
            point.mercator = MercatorProjection::latLonToMercator(latitude, longitude);
            layer.points.append(point);
        }
    }

    layer.points.squeeze();
    layer.loaded = true;
    if (errorMessage) {
        *errorMessage = QString("Loaded %1 %2").arg(layer.points.size()).arg(layer.name.toLower());
    }
    return true;
}
