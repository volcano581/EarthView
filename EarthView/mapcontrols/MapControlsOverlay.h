#pragma once
#ifndef MAPCONTROLSOVERLAY_H
#define MAPCONTROLSOVERLAY_H

#include <QObject>
#include <QPointer>
#include <QString>

class MapWidget;
class MapControlsModel;
class QQuickWidget;

/**
 * @brief MapControlsOverlay puts the QML on-map controls over a MapWidget.
 *
 * Two transparent QQuickWidget children of the map, each sized to its content: the side bar
 * anchored bottom-right and the mini-map layers button anchored bottom-left (above the scale
 * bar). Mouse input outside the controls still reaches the map. The layer toggles (hillshade,
 * cities, borders, grid) are applied to the map here; the base map is the host's job (it owns
 * the map sources, see BaseMapSources). Opt-in: a host creates one per MapWidget.
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
    QQuickWidget* sideBarView() const { return m_sideBarView; }
    QQuickWidget* layersView() const { return m_layersView; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QQuickWidget* createView(const QString& qmlFile);
    void updateGeometry();

private:
    QPointer<MapWidget> m_mapWidget;
    MapControlsModel* m_model;
    QQuickWidget* m_sideBarView;
    QQuickWidget* m_layersView;
};

#endif // MAPCONTROLSOVERLAY_H
