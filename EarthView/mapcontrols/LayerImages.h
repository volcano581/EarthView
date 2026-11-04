#pragma once
#ifndef LAYERIMAGES_H
#define LAYERIMAGES_H

#include "MapSourceCatalog.h"

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QPointF>
#include <QQuickImageProvider>
#include <QString>
#include <QTimer>
#include <memory>

class Camera;
struct MbTilesVectorTile;

/**
 * @brief The pictures of the Layers panel, Google Maps style: map snippets, not symbols.
 *
 * - "base/<i>": a live thumbnail of map source i at the current location (its own tiles);
 * - "theme/<id>": the OSM map at the current location in that theme;
 * - "layer/<id>": an illustration of a map detail (roads, water, places, ...).
 * Served to QML by MapImageProvider as image://maplayers/<id>; MapThumbnailer fills the live
 * ones in the background.
 */
namespace LayerImages {

/// Illustration of a vector layer (MapFeatures id), @p size pixels square.
QImage layerIcon(const QString& layerId, int size);
/// A drawn map snippet in a theme's colours, for when the live one is not there yet.
QImage themePreview(const QString& themeId, int size);
/// The vector @p tiles (row-major @p columns x @p rows block of decoded tiles) drawn in a theme,
/// @p span tile pixels around @p center (block pixels) into a @p size square.
QImage renderVector(const QVector<MbTilesVectorTile>& tiles, int columns, int zoom, const QString& themeId,
                    const QPointF& center, double span, int size);

} // namespace LayerImages

/// Thread-safe store of the live thumbnails.
class MapImageStore
{
public:
    QImage image(const QString& id) const
    {
        QMutexLocker lock(&m_mutex);
        return m_images.value(id);
    }
    void setImage(const QString& id, const QImage& image)
    {
        QMutexLocker lock(&m_mutex);
        m_images.insert(id, image);
    }

private:
    mutable QMutex m_mutex;
    QHash<QString, QImage> m_images;
};

/// image://maplayers/<id>[?revision]
class MapImageProvider : public QQuickImageProvider
{
public:
    explicit MapImageProvider(std::shared_ptr<MapImageStore> store);
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    std::shared_ptr<MapImageStore> m_store;
};

/**
 * @brief Keeps the live thumbnails of the map sources and themes current.
 *
 * Refreshes when asked (the Layers panel opens) and when the view has settled somewhere new;
 * each refresh reads a 2 x 2 block of tiles per source at the view's centre on the reader
 * threads (the same reader as the map: share, web server, tile server) and draws it there.
 */
class MapThumbnailer : public QObject
{
    Q_OBJECT

public:
    MapThumbnailer(Camera* camera, std::shared_ptr<MapImageStore> store, QObject* parent = nullptr);

    void setSources(const QList<MapSourceCatalog::Source>& sources);
    void setTheme(const QString& themeId);
    /// Refreshes now unless the thumbnails already show this place.
    void refresh(bool force = false);

signals:
    void thumbnailsChanged();

private:
    void scheduleIdleRefresh();
    void storeThumbnail(const QString& id, const QImage& image, quint64 generation);

    Camera* m_camera;
    std::shared_ptr<MapImageStore> m_store;
    QList<MapSourceCatalog::Source> m_sources;
    QString m_theme;
    QTimer m_idleTimer;
    QPointF m_lastCenter;
    double m_lastZoom;
    bool m_hasRefreshed;
    quint64 m_generation;
};

#endif // LAYERIMAGES_H
