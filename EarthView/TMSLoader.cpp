#include "TMSLoader.h"
#include "TextureManager.h"
#include "Camera.h"
#include "MercatorProjection.h"
#include "Constants.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QImage>
#include <QDebug>
#include <QtMath>
#include <algorithm>
#include <utility>

namespace {
QString renderTileKey(int z, int x, int y, int layerIndex)
{
    return QString("%1_%2_%3_%4").arg(layerIndex).arg(z).arg(x).arg(y);
}

QString textureTileKey(int z, int x, int y, int layerIndex)
{
    return renderTileKey(z, MercatorProjection::wrapTileX(x, z), y, layerIndex);
}
}

TmsLoader::TmsLoader(Camera* camera, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_textureManager(new TextureManager(GIS::TEXTURE_CACHE_SIZE_MB))
    , m_loadingEnabled(true)
{
    TileSourceLayer defaultLayer;
    defaultLayer.name = "OpenStreetMap";
    defaultLayer.urlTemplate = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";
    m_tileLayers.append(defaultLayer);

    connect(m_camera, &Camera::cameraChanged, this, &TmsLoader::updateVisibleTiles);
}

TmsLoader::~TmsLoader()
{
    clearCache();
    delete m_textureManager;
}

void TmsLoader::setTileUrl(const QString& urlTemplate)
{
    TileSourceLayer layer;
    layer.name = "Tile source";
    layer.urlTemplate = urlTemplate;
    setTileSourceLayers({ layer });
}

void TmsLoader::setTileSourceLayers(const QList<TileSourceLayer>& layers)
{
    m_tileLayers = layers;
    deriveDisplayZoomsFromTileResources();
    clearCache();

    if (m_loadingEnabled) {
        updateVisibleTiles();
    }
}

void TmsLoader::setLoadingEnabled(bool enabled)
{
    if (m_loadingEnabled == enabled)
        return;

    m_loadingEnabled = enabled;
    if (!m_loadingEnabled) {
        clearCache();
        return;
    }

    updateVisibleTiles();
}

void TmsLoader::deriveDisplayZoomsFromTileResources()
{
    QList<int> indices;

    for (int i = 0; i < m_tileLayers.size(); ++i) {
        const TileSourceLayer& layer = m_tileLayers.at(i);
        if (layer.maxTileZoom >= layer.minTileZoom) {
            indices.append(i);
        }
    }

    std::sort(indices.begin(), indices.end(), [this](int a, int b) {
        const TileSourceLayer& la = m_tileLayers.at(a);
        const TileSourceLayer& lb = m_tileLayers.at(b);

        if (la.maxTileZoom != lb.maxTileZoom)
            return la.maxTileZoom < lb.maxTileZoom;

        if (la.minTileZoom != lb.minTileZoom)
            return la.minTileZoom < lb.minTileZoom;

        return la.name < lb.name;
    });

    int nextDisplayMinZoom = 0;
    int pos = 0;

    while (pos < indices.size()) {
        const int groupMaxTileZoom = m_tileLayers.at(indices.at(pos)).maxTileZoom;

        QList<int> group;
        while (pos < indices.size()
            && m_tileLayers.at(indices.at(pos)).maxTileZoom == groupMaxTileZoom) {
            group.append(indices.at(pos));
            ++pos;
        }

        const int displayMinZoom = qBound(0, nextDisplayMinZoom, GIS::MAX_TILE_ZOOM);
        const int displayMaxZoom = qBound(displayMinZoom, groupMaxTileZoom, GIS::MAX_TILE_ZOOM);

        for (int layerIndex : group) {
            TileSourceLayer& layer = m_tileLayers[layerIndex];
            layer.minZoom = displayMinZoom;
            layer.maxZoom = displayMaxZoom;
        }

        nextDisplayMinZoom = displayMaxZoom + 1;
    }

    if (!indices.isEmpty()) {
        const int lastMaxTileZoom = m_tileLayers.at(indices.last()).maxTileZoom;

        for (int layerIndex : indices) {
            TileSourceLayer& layer = m_tileLayers[layerIndex];
            if (layer.maxTileZoom == lastMaxTileZoom) {
                layer.maxZoom = GIS::MAX_TILE_ZOOM;
            }
        }
    }

    for (const TileSourceLayer& layer : m_tileLayers) {
        qDebug() << "TMS layer:"
                 << layer.name
                 << "display zoom" << layer.minZoom << "-" << layer.maxZoom
                 << "tile zoom" << layer.minTileZoom << "-" << layer.maxTileZoom;
    }
}

void TmsLoader::updateVisibleTiles()
{
    if (!m_loadingEnabled)
        return;

    const int cameraZoomLevel = m_camera->getTileZoomLevel();
    QList<int> layerIndices = layerIndicesForZoom(cameraZoomLevel);
    if (layerIndices.isEmpty()) {
        m_activeTiles.clear();
        abortRequestsExcept(QSet<QString>());
        return;
    }

    const int primaryLayerIndex = layerIndices.first();
    const int tileZoomLevel = tileZoomForLayer(cameraZoomLevel, primaryLayerIndex);
    QRect tileRange = m_camera->getTileRange(tileZoomLevel);

    QList<int> fallbackLayerIndices;
    for (int i = 1; i < layerIndices.size(); ++i) {
        const int fallbackLayerIndex = layerIndices.at(i);
        if (tileZoomForLayer(cameraZoomLevel, fallbackLayerIndex) == tileZoomLevel) {
            fallbackLayerIndices.append(fallbackLayerIndex);
        }
    }

    const qint64 requestedTileCount = static_cast<qint64>(tileRange.width()) * tileRange.height();
    constexpr qint64 maxTilesPerUpdate = 2048;
    if (tileRange.width() <= 0 || tileRange.height() <= 0 || requestedTileCount > maxTilesPerUpdate) {
        qWarning() << "Skipping tile update with excessive tile range"
                   << tileRange
                   << "at zoom" << tileZoomLevel
                   << "count" << requestedTileCount;
        m_activeTiles.clear();
        abortRequestsExcept(QSet<QString>());
        return;
    }

    QSet<QString> visibleKeys;
    QSet<QString> visibleTextureKeys;

    for (int x = tileRange.x(); x < tileRange.x() + tileRange.width(); ++x) {
        for (int y = tileRange.y(); y < tileRange.y() + tileRange.height(); ++y) {
            const QString renderKey = renderTileKey(tileZoomLevel, x, y, primaryLayerIndex);
            visibleKeys.insert(renderKey);

            auto activeIt = m_activeTiles.find(renderKey);
            if (activeIt != m_activeTiles.end()) {
                visibleTextureKeys.insert(activeIt.value().textureCacheKey);
                continue;
            }

            QString cacheKey = textureTileKey(tileZoomLevel, x, y, primaryLayerIndex);
            visibleTextureKeys.insert(cacheKey);

            TileInfo tile;
            tile.textureId = m_textureManager->getTexture(cacheKey);
            tile.mercatorBounds = tileToMercatorBounds(tileZoomLevel, x, y);
            tile.textureCacheKey = cacheKey;
            tile.image = QImage();
            tile.isLoading = tile.textureId == 0;
            tile.layerIndex = primaryLayerIndex;
            tile.fallbackLayerIndices = fallbackLayerIndices;
            m_activeTiles[renderKey] = tile;

            if (tile.textureId == 0 && !m_pendingRequests.contains(cacheKey)) {
                fetchTile(tileZoomLevel, x, y, primaryLayerIndex);
            }
        }
    }

    // Remove invisible tiles
    QList<QString> toRemove;
    for (auto it = m_activeTiles.begin(); it != m_activeTiles.end(); ++it) {
        if (!visibleKeys.contains(it.key())) {
            toRemove.append(it.key());
        }
    }

    for (const QString& key : toRemove) {
        m_activeTiles.remove(key);
    }

    abortRequestsExcept(visibleTextureKeys);
}

void TmsLoader::clearCache()
{
    abortRequestsExcept(QSet<QString>());
    m_activeTiles.clear();
    m_pendingRequests.clear();
    m_textureManager->clearCache();
}

void TmsLoader::fetchTile(int z, int x, int y, int layerIndex)
{
    if (!m_loadingEnabled)
        return;

    QString key = textureTileKey(z, x, y, layerIndex);

    // Check cache
    if (m_textureManager->hasTexture(key)) {
        return;
    }
    if (m_pendingRequests.contains(key)) {
        return;
    }

    // Download tile
    QString url = tileToUrl(z, x, y, layerIndex);
    if (url.isEmpty())
        return;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "MilitaryGIS/1.0");

    QNetworkReply* reply = m_networkManager->get(request);
    m_pendingRequests.insert(key);
    m_pendingReplies.insert(key, reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, z, x, y, layerIndex]() {
        onTileDownloaded(reply, z, x, y, layerIndex);
        });
}

void TmsLoader::onTileDownloaded(QNetworkReply* reply, int z, int x, int y, int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= m_tileLayers.size()) {
        reply->deleteLater();
        return;
    }

    QString key = textureTileKey(z, x, y, layerIndex);
    m_pendingRequests.remove(key);
    m_pendingReplies.remove(key);
    bool loaded = false;

    if (reply->error() == QNetworkReply::NoError) {
        QImage image;
        if (image.loadFromData(reply->readAll())) {
            loaded = true;
            for (auto it = m_activeTiles.begin(); it != m_activeTiles.end(); ++it) {
                TileInfo& tile = it.value();
                if (tile.textureCacheKey != key)
                    continue;

                tile.textureId = 0;
                tile.image = image;
                tile.isLoading = false;
            }

            emit tileLoaded(key);
        }
    }

    if (!loaded) {
        QList<QString> failedKeys;
        QSet<int> retryLayerIndices;
        for (auto it = m_activeTiles.begin(); it != m_activeTiles.end(); ++it) {
            TileInfo& tile = it.value();
            if (tile.textureCacheKey != key)
                continue;

            bool retrying = false;
            while (!tile.fallbackLayerIndices.isEmpty()) {
                const int nextLayerIndex = tile.fallbackLayerIndices.takeFirst();
                const QString nextCacheKey = textureTileKey(z, x, y, nextLayerIndex);
                tile.layerIndex = nextLayerIndex;
                tile.textureCacheKey = nextCacheKey;
                tile.textureId = m_textureManager->getTexture(nextCacheKey);
                tile.image = QImage();
                tile.isLoading = tile.textureId == 0;

                if (tile.textureId == 0) {
                    retryLayerIndices.insert(nextLayerIndex);
                }
                retrying = true;
                break;
            }

            if (!retrying) {
                failedKeys.append(it.key());
            }
        }

        for (const QString& failedKey : failedKeys) {
            m_activeTiles.remove(failedKey);
        }

        for (const int retryLayerIndex : retryLayerIndices) {
            fetchTile(z, x, y, retryLayerIndex);
        }

        emit tileFailed(key);
    }

    reply->deleteLater();
}

void TmsLoader::abortRequestsExcept(const QSet<QString>& keepKeys)
{
    QList<QString> keysToAbort;
    for (auto it = m_pendingReplies.cbegin(); it != m_pendingReplies.cend(); ++it) {
        if (!keepKeys.contains(it.key())) {
            keysToAbort.append(it.key());
        }
    }

    for (const QString& key : keysToAbort) {
        QNetworkReply* reply = m_pendingReplies.take(key);
        m_pendingRequests.remove(key);
        if (!reply)
            continue;

        QObject::disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
}

QList<int> TmsLoader::layerIndicesForZoom(int zoomLevel) const
{
    QList<int> matchingIndices;

    for (int i = 0; i < m_tileLayers.size(); ++i) {
        const TileSourceLayer& layer = m_tileLayers.at(i);
        if (zoomLevel >= layer.minZoom && zoomLevel <= layer.maxZoom) {
            matchingIndices.append(i);
        }
    }

    return matchingIndices;
}

int TmsLoader::tileZoomForLayer(int zoomLevel, int layerIndex) const
{
    if (layerIndex < 0 || layerIndex >= m_tileLayers.size())
        return zoomLevel;

    const TileSourceLayer& layer = m_tileLayers.at(layerIndex);
    int minTileZoom = qBound(0, layer.minTileZoom, GIS::MAX_TILE_ZOOM);
    int maxTileZoom = qBound(0, layer.maxTileZoom, GIS::MAX_TILE_ZOOM);
    if (minTileZoom > maxTileZoom) {
        std::swap(minTileZoom, maxTileZoom);
    }

    return qBound(minTileZoom, zoomLevel, maxTileZoom);
}

QString TmsLoader::tileToUrl(int z, int x, int y, int layerIndex) const
{
    if (layerIndex < 0 || layerIndex >= m_tileLayers.size())
        return QString();

    const TileSourceLayer& layer = m_tileLayers.at(layerIndex);
    QString url = layer.urlTemplate;
    const int tileY = layer.tmsYOrigin ? ((1 << z) - 1 - y) : y;
    url.replace("{z}", QString::number(z));
    url.replace("{x}", QString::number(MercatorProjection::wrapTileX(x, z)));
    url.replace("{y}", QString::number(tileY));
    return url;
}

QRectF TmsLoader::tileToMercatorBounds(int z, int x, int y) const
{
    return MercatorProjection::tileToMercatorBounds(z, x, y);
}
