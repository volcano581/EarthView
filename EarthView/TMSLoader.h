#pragma once
#ifndef TMSLOADER_H
#define TMSLOADER_H

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QHash>
#include <QList>
#include <QMap>
#include <QSet>
#include <QImage>
#include "Constants.h"
#include "TextureManager.h"

class Camera;
class QNetworkReply;

/**
 * @brief TmsLoader downloads and manages TMS tile loading
 *
 * Responsibilities:
 * - Download tiles from TMS server
 * - Manage pending requests
 * - Update visible tile set
 * - Delegate texture creation to TextureManager
 */
class TmsLoader : public QObject
{
    Q_OBJECT

public:
    struct TileInfo {
        GLuint textureId;
        QRectF mercatorBounds;
        QString textureCacheKey;
        QImage image;
        bool isLoading;
        int layerIndex;
        QList<int> fallbackLayerIndices;
    };

    struct TileSourceLayer {
        QString name;
        QString urlTemplate;
        int minZoom = 0;
        int maxZoom = GIS::MAX_TILE_ZOOM;
        int minTileZoom = 0;
        int maxTileZoom = GIS::MAX_TILE_ZOOM;
        bool tmsYOrigin = false;
    };

    explicit TmsLoader(Camera* camera, QObject* parent = nullptr);
    ~TmsLoader();

    // Configuration
    void setTileUrl(const QString& urlTemplate);
    void setTileSourceLayers(const QList<TileSourceLayer>& layers);
    void setLoadingEnabled(bool enabled);
    bool isLoadingEnabled() const { return m_loadingEnabled; }

    // Tile management
    void updateVisibleTiles();
    void clearCache();

    // Accessors
    QMap<QString, TileInfo>& getActiveTiles() { return m_activeTiles; }
    const QMap<QString, TileInfo>& getActiveTiles() const { return m_activeTiles; }
    TextureManager* getTextureManager() { return m_textureManager; }
    const QList<TileSourceLayer>& getTileSourceLayers() const { return m_tileLayers; }

signals:
    void tileLoaded(const QString& key);
    void tileFailed(const QString& key);

private slots:
    void onTileDownloaded(QNetworkReply* reply, int z, int x, int y, int layerIndex);

private:
    void deriveDisplayZoomsFromTileResources();
    void fetchTile(int z, int x, int y, int layerIndex);
    void abortRequestsExcept(const QSet<QString>& keepKeys);
    QList<int> layerIndicesForZoom(int zoomLevel) const;
    int tileZoomForLayer(int zoomLevel, int layerIndex) const;
    QString tileToUrl(int z, int x, int y, int layerIndex) const;
    QRectF tileToMercatorBounds(int z, int x, int y) const;

private:
    Camera* m_camera;
    QNetworkAccessManager* m_networkManager;
    TextureManager* m_textureManager;
    QMap<QString, TileInfo> m_activeTiles;
    QSet<QString> m_pendingRequests;
    QHash<QString, QNetworkReply*> m_pendingReplies;
    QList<TileSourceLayer> m_tileLayers;
    bool m_loadingEnabled;
};

#endif // TMSLOADER_H
