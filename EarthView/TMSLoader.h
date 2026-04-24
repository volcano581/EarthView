#pragma once
#ifndef TMSLOADER_H
#define TMSLOADER_H

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QMap>
#include <QSet>
#include <QImage>
#include "TextureManager.h"

class Camera;

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
        QImage image;
        bool isLoading;
    };

    explicit TmsLoader(Camera* camera, QObject* parent = nullptr);
    ~TmsLoader();

    // Configuration
    void setTileUrl(const QString& urlTemplate);

    // Tile management
    void updateVisibleTiles();
    void clearCache();

    // Accessors
    QMap<QString, TileInfo>& getActiveTiles() { return m_activeTiles; }
    const QMap<QString, TileInfo>& getActiveTiles() const { return m_activeTiles; }
    TextureManager* getTextureManager() { return m_textureManager; }

signals:
    void tileLoaded(const QString& key);
    void tileFailed(const QString& key);

private slots:
    void onTileDownloaded(QNetworkReply* reply, int z, int x, int y);

private:
    void fetchTile(int z, int x, int y);
    QString tileToUrl(int z, int x, int y) const;
    QRectF tileToMercatorBounds(int z, int x, int y) const;

private:
    Camera* m_camera;
    QNetworkAccessManager* m_networkManager;
    TextureManager* m_textureManager;
    QMap<QString, TileInfo> m_activeTiles;
    QSet<QString> m_pendingRequests;
    QString m_urlTemplate;
};

#endif // TMSLOADER_H
