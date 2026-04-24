#include "TMSLoader.h"
#include "TextureManager.h"
#include "Camera.h"
#include "MercatorProjection.h"
#include "Constants.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QImage>
#include <QtMath>

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

    for (int x = tileRange.x(); x < tileRange.x() + tileRange.width(); ++x) {
        for (int y = tileRange.y(); y < tileRange.y() + tileRange.height(); ++y) {
            QString key = QString("%1_%2_%3").arg(zoomLevel).arg(x).arg(y);
            visibleKeys.insert(key);

            if (!m_activeTiles.contains(key) && !m_pendingRequests.contains(key)) {
                fetchTile(zoomLevel, x, y);
            }
        }
    }

    // Remove invisible tiles
    QList<QString> toRemove;
    for (auto it = m_activeTiles.begin(); it != m_activeTiles.end(); ++it) {
        if (!visibleKeys.contains(it.key())) {
            m_textureManager->deleteTexture(it.key());
            toRemove.append(it.key());
        }
    }

    for (const QString& key : toRemove) {
        m_activeTiles.remove(key);
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
    QString key = QString("%1_%2_%3").arg(z).arg(x).arg(y);

    // Check cache
    if (m_textureManager->hasTexture(key)) {
        TileInfo tile;
        tile.textureId = m_textureManager->getTexture(key);
        tile.mercatorBounds = tileToMercatorBounds(z, x, y);
        tile.image = QImage();
        tile.isLoading = false;
        m_activeTiles[key] = tile;
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
    QString key = QString("%1_%2_%3").arg(z).arg(x).arg(y);
    m_pendingRequests.remove(key);

    if (reply->error() == QNetworkReply::NoError) {
        QImage image;
        if (image.loadFromData(reply->readAll())) {
            TileInfo tile;
            tile.textureId = 0;
            tile.mercatorBounds = tileToMercatorBounds(z, x, y);
            tile.image = image;
            tile.isLoading = false;
            m_activeTiles[key] = tile;

            emit tileLoaded(key);
        }
    }
    else {
        emit tileFailed(key);
    }

    reply->deleteLater();
}

QString TmsLoader::tileToUrl(int z, int x, int y) const
{
    QString url = m_urlTemplate;
    url.replace("{z}", QString::number(z));
    url.replace("{x}", QString::number(x));
    url.replace("{y}", QString::number(y));
    return url;
}

QRectF TmsLoader::tileToMercatorBounds(int z, int x, int y) const
{
    return MercatorProjection::tileToMercatorBounds(z, x, y);
}
