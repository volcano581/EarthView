#pragma once
#ifndef MAPCONTROLSOVERLAY_H
#define MAPCONTROLSOVERLAY_H

#include "MapSourceCatalog.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVector>
#include <memory>

class MapWidget;
class MapControlsModel;
class MapImageStore;
class MapThumbnailer;
class MeasureModel;
class SmoothNavigator;
class QPainter;
class QQuickWidget;

/**
 * @brief MapControlsOverlay puts the QML on-map controls over a MapWidget and adds smooth
 * navigation and the measure tool.
 *
 * Two transparent QQuickWidget children of the map, each sized to its content: the side bar
 * at the right edge, vertically centred, and the mini-map layers button at the bottom-right,
 * above the scale bar (which the overlay moves to the bottom-right corner). Mouse input outside
 * the controls still reaches the map. The overlay also
 * - takes over the map's wheel zoom (eased, anchored at the cursor) and lets fast drags glide
 *   (SmoothNavigator);
 * - while measuring, turns map clicks into measure points and draws the line through the map's
 *   overlay painter (right-click removes the last point);
 * - otherwise a click on an OSM (vector MBTiles) feature selects it: it is outlined on the map
 *   and its attributes are shown in a card at the top-left (FeatureCard.qml).
 * Layer toggles are applied to the map here; the base map is the host's job (it owns the map
 * sources, see BaseMapSources). Opt-in: a host creates one per MapWidget. The world cities
 * layer (Data/Cities) is switched off: place names come from the OSM details instead.
 *
 * @code
 * auto* overlay = new MapControlsOverlay(mapWidget); // parented to mapWidget
 * @endcode
 */
class MapControlsOverlay : public QObject
{
    Q_OBJECT

public:
    explicit MapControlsOverlay(MapWidget* mapWidget);
    ~MapControlsOverlay() override;

    MapControlsModel* model() const { return m_model; }
    MeasureModel* measure() const { return m_measure; }
    SmoothNavigator* navigator() const { return m_navigator; }
    QQuickWidget* sideBarView() const { return m_sideBarView; }
    QQuickWidget* layersView() const { return m_layersView; }
    QQuickWidget* featureCardView() const { return m_featureCardView; }
    /// The host's map sources, in the order of its base maps (MapControlsModel::baseMaps):
    /// their live thumbnails in the Layers panel are drawn from them.
    void setMapSources(const QList<MapSourceCatalog::Source>& sources);
    /// Selects the OSM feature under @p screenPos (what a click on the map does); false and
    /// no selection when there is none.
    bool selectFeatureAt(const QPointF& screenPos);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QQuickWidget* createView(const QString& qmlFile);
    void updateGeometry();
    bool handleMapEvent(QEvent* event);
    bool addMeasurePointAt(const QPointF& screenPos);
    void paintOverlay(QPainter& painter);
    void paintSelection(QPainter& painter);
    void paintMeasure(QPainter& painter);
    void updateCursor();

private:
    QPointer<MapWidget> m_mapWidget;
    MapControlsModel* m_model;
    MeasureModel* m_measure;
    SmoothNavigator* m_navigator;
    QQuickWidget* m_sideBarView;
    QQuickWidget* m_layersView;
    QQuickWidget* m_featureCardView;
    std::shared_ptr<MapImageStore> m_imageStore;
    MapThumbnailer* m_thumbnailer;
    QTimer m_thumbnailRevisionTimer;
    QElapsedTimer m_clock;
    QPoint m_pressPos;
    qint64 m_pressTimeMs;
    bool m_leftDown;
    bool m_panelsOpenAtPress;
    bool m_hoverValid;
    QPointF m_hoverPos;
    // Selected feature's geometry (Mercator) for the outline.
    QVector<QVector<QPointF>> m_selectionPaths;
    int m_selectionGeometryType;
};

#endif // MAPCONTROLSOVERLAY_H
