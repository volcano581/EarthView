#pragma once
#ifndef DEMLOADER_H
#define DEMLOADER_H

#include <QImage>
#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVector>
#include <QOpenGLFunctions>

class Camera;
class QNetworkReply;

struct DemTile {
    QString key;
    QUrl url;
    QRectF geographicBounds;
    QRectF mercatorBounds;
    QImage image;
    QUrl localUrl;
    QVector<float> elevationSamples;
    int elevationWidth = 0;
    int elevationHeight = 0;
    GLuint textureId = 0;
    float minElevation = 0.0f;
    float maxElevation = 1.0f;
    bool metadataReady = false;
    bool imageReady = false;
    bool metadataLoading = false;
    bool imageLoading = false;
};

class DemLoader : public QObject
{
    Q_OBJECT

public:
    explicit DemLoader(Camera* camera, QObject* parent = nullptr);

    void setBaseUrl(const QString& baseUrl);
    void setIndexBaseUrl(const QString& indexBaseUrl);
    void setLocalMirrorPath(const QString& demRootPath);
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void updateVisibleTiles();
    void clear();

    QMap<QString, DemTile>& activeTiles() { return m_tiles; }
    const QMap<QString, DemTile>& activeTiles() const { return m_tiles; }

signals:
    void catalogUpdated();
    void demTileLoaded(const QString& key);
    void demTileFailed(const QString& key);

private:
    void fetchCatalog();
    void fetchDirectoryCatalog(int generation);
    void requestCatalogUrl(const QUrl& url, bool isIndexCatalog, int generation);
    void finishCatalogRequests(int generation);
    int parseCatalog(const QByteArray& html);
    int parseIndexCatalog(const QByteArray& data, const QUrl& indexUrl);
    bool addTileReference(const QString& reference, const QUrl& catalogUrl, bool forceTileBaseUrl);
    int scanLocalTileDirectory();
    void requestMetadata(const QString& key);
    void requestMetadataWindow(const QString& key, const QByteArray& header, qint64 ifdOffset);
    void requestImage(const QString& key);
    void onCatalogDownloaded(QNetworkReply* reply, const QUrl& catalogUrl, bool isIndexCatalog, int generation);
    void onMetadataDownloaded(QNetworkReply* reply, const QString& key);
    void onImageDownloaded(QNetworkReply* reply, const QString& key);
    QUrl sourceUrlForTile(const DemTile& tile) const;
    bool visibleIntersects(const QRectF& mercatorBounds) const;

private:
    Camera* m_camera;
    QNetworkAccessManager* m_networkManager;
    QUrl m_baseUrl;
    QUrl m_indexBaseUrl;
    QUrl m_localTileBaseUrl;
    QUrl m_localIndexBaseUrl;
    QMap<QString, DemTile> m_tiles;
    QSet<QString> m_pendingMetadata;
    QSet<QString> m_pendingImages;
    int m_pendingCatalogRequests;
    int m_catalogGeneration;
    bool m_enabled;
    bool m_catalogRequested;
    bool m_catalogReady;
    bool m_directoryCatalogRequested;
};

#endif // DEMLOADER_H
