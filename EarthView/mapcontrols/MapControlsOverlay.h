#pragma once
#ifndef MAPCONTROLSOVERLAY_H
#define MAPCONTROLSOVERLAY_H

#include <QObject>
#include <QPointer>

class MapWidget;
class MapControlsModel;
class QQuickWidget;

/**
 * @brief MapControlsOverlay puts the QML on-map controls over a MapWidget.
 *
 * A transparent QQuickWidget child of the map, sized to its content and anchored to the
 * map's bottom-right corner, so mouse input outside the controls still reaches the map.
 * Opt-in: a host creates one per MapWidget; the map widget itself is unchanged.
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
    QQuickWidget* view() const { return m_view; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateGeometry();

private:
    QPointer<MapWidget> m_mapWidget;
    MapControlsModel* m_model;
    QQuickWidget* m_view;
};

#endif // MAPCONTROLSOVERLAY_H
