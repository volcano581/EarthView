#pragma once
#ifndef MAPCONTROLSOVERLAY_H
#define MAPCONTROLSOVERLAY_H

#include <QElapsedTimer>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>

class MapWidget;
class MapControlsModel;
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
 *   overlay painter (right-click removes the last point).
 * Layer toggles are applied to the map here; the base map is the host's job (it owns the map
 * sources, see BaseMapSources). Opt-in: a host creates one per MapWidget.
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

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QQuickWidget* createView(const QString& qmlFile);
    void updateGeometry();
    bool handleMapEvent(QEvent* event);
    bool addMeasurePointAt(const QPointF& screenPos);
    void paintMeasure(QPainter& painter);
    void updateCursor();

private:
    QPointer<MapWidget> m_mapWidget;
    MapControlsModel* m_model;
    MeasureModel* m_measure;
    SmoothNavigator* m_navigator;
    QQuickWidget* m_sideBarView;
    QQuickWidget* m_layersView;
    QElapsedTimer m_clock;
    QPoint m_pressPos;
    qint64 m_pressTimeMs;
    bool m_leftDown;
    bool m_hoverValid;
    QPointF m_hoverPos;
};

#endif // MAPCONTROLSOVERLAY_H
