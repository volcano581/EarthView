#include "TMSLoader.h"
#include "TextureManager.h"
#include "Camera.h"
#include "MercatorProjection.h"
#include "Constants.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QImage>
#include <QtMath>

namespace {
QString renderTileKey(int z, int x, int y)
{
    return QString("%1_%2_%3").arg(z).arg(x).arg(y);
}

QString textureTileKey(int z, int x, int y)
{
    return renderTileKey(z, MercatorProjection::wrapTileX(x, z), y);
}
}

TmsLoader::TmsLoader(Camera* camera, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_textureManager(new TextureManager(GIS::TEXTURE_CACHE_SIZE_MB))
    , m_urlTemplate("https://tile.openstreetmap.org/{z}/{x}/{y}.png")
{
    connect(m_camera, &Camera::cameraChanged, this, &TmsLoader::updateVisibleTiles);
}

TmsLoader::~TmsLoader()
{
    clearCache();
    delete m_textureManager;
}

void TmsLoader::setTileUrl(const QString& urlTemplate)
{
    m_urlTemplate = urlTemplate;
}

void TmsLoader::updateVisibleTiles()
{
    int zoomLevel = m_camera->getTileZoomLevel();
    QRect tileRange = m_camera->getTileRange(zoomLevel);

    QSet<QString> visibleKeys;
    QSet<QString> visibleTextureKeys;

    for (int x = tileRange.x(); x < tileRange.x() + tileRange.width(); ++x) {
        for (int y = tileRange.y(); y < tileRange.y() + tileRange.height(); ++y) {
            QString key = renderTileKey(zoomLevel, x, y);
            QString cacheKey = textureTileKey(zoomLevel, x, y);
            visibleKeys.insert(key);
            visibleTextureKeys.insert(cacheKey);

            if (m_activeTiles.contains(key))
                continue;

            TileInfo tile;
            tile.textureId = m_textureManager->getTexture(cacheKey);
            tile.mercatorBounds = tileToMercatorBounds(zoomLevel, x, y);
            tile.textureCacheKey = cacheKey;
            tile.image = QImage();
            tile.isLoading = tile.textureId == 0;
            m_activeTiles[key] = tile;

            if (tile.textureId == 0 && !m_pendingRequests.contains(cacheKey)) {
                fetchTile(zoomLevel, x, y);
            }
        }
    }

    // Remove invisible tiles
    QList<QString> toRemove;
    QSet<QString> textureKeysToRemove;
    for (auto it = m_activeTiles.begin(); it != m_activeTiles.end(); ++it) {
        if (!visibleKeys.contains(it.key())) {
            textureKeysToRemove.insert(it.value().textureCacheKey);
            toRemove.append(it.key());
        }
    }

    for (const QString& key : toRemove) {
        m_activeTiles.remove(key);
    }

    for (const QString& cacheKey : textureKeysToRemove) {
        if (!visibleTextureKeys.contains(cacheKey)) {
            m_textureManager->deleteTexture(cacheKey);
        }
    }
}

void TmsLoader::clearCache()
{
    m_activeTiles.clear();
    m_pendingRequests.clear();
    m_textureManager->clearCache();
}

void TmsLoader::fetchTile(int z, int x, int y)
{
    QString key = textureTileKey(z, x, y);

    // Check cache
    if (m_textureManager->hasTexture(key)) {
        return;
    }

    // Download tile
    QString url = tileToUrl(z, x, y);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "MilitaryGIS/1.0");

    QNetworkReply* reply = m_networkManager->get(request);
    m_pendingRequests.insert(key);

    connect(reply, &QNetworkReply::finished, this, [this, reply, z, x, y]() {
        onTileDownloaded(reply, z, x, y);
        });
}

void TmsLoader::onTileDownloaded(QNetworkReply* reply, int z, int x, int y)
{
    QString key = textureTileKey(z, x, y);
    m_pendingRequests.remove(key);
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
        for (auto it = m_activeTiles.begin(); it != m_activeTiles.end(); ++it) {
            if (it.value().textureCacheKey == key) {
                failedKeys.append(it.key());
            }
        }
        for (const QString& failedKey : failedKeys) {
            m_activeTiles.remove(failedKey);
        }
        emit tileFailed(key);
    }

    reply->deleteLater();
}

QString TmsLoader::tileToUrl(int z, int x, int y) const
{
    QString url = m_urlTemplate;
    url.replace("{z}", QString::number(z));
    url.replace("{x}", QString::number(MercatorProjection::wrapTileX(x, z)));
    url.replace("{y}", QString::number(y));
    return url;
}

QRectF TmsLoader::tileToMercatorBounds(int z, int x, int y) const
{
    return MercatorProjection::tileToMercatorBounds(z, x, y);
}
