#pragma once
#ifndef MAPCONTROLSMODEL_H
#define MAPCONTROLSMODEL_H

#include <QObject>
#include <QPointF>
#include <QString>
#include <QVariantList>

class Camera;

/**
 * @brief MapControlsModel is the state behind the on-map controls (QML side bar).
 *
 * It holds no rendering code and needs only Qt Core, so it is unit-tested without a GL
 * context. When bound to a Camera it keeps the projection in sync in both directions:
 * the Settings panel changes the camera, and a projection change made elsewhere (View
 * menu, host API) updates the panel.
 */
class MapControlsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool orthographic READ isOrthographic WRITE setOrthographic NOTIFY orthographicChanged)
    Q_PROPERTY(double bearingDegrees READ bearingDegrees WRITE setBearingDegrees NOTIFY bearingDegreesChanged)
    Q_PROPERTY(bool globe3DAvailable READ isGlobe3DAvailable WRITE setGlobe3DAvailable NOTIFY globe3DAvailableChanged)
    Q_PROPERTY(bool measureAvailable READ isMeasureAvailable WRITE setMeasureAvailable NOTIFY measureAvailableChanged)
    Q_PROPERTY(bool settingsOpen READ isSettingsOpen WRITE setSettingsOpen NOTIFY settingsOpenChanged)
    Q_PROPERTY(bool layersOpen READ isLayersOpen WRITE setLayersOpen NOTIFY layersOpenChanged)
    Q_PROPERTY(bool measureOpen READ isMeasureOpen WRITE setMeasureOpen NOTIFY measureOpenChanged)
    Q_PROPERTY(QVariantList baseMaps READ baseMaps WRITE setBaseMaps NOTIFY baseMapsChanged)
    Q_PROPERTY(int currentBaseMap READ currentBaseMap WRITE setCurrentBaseMap NOTIFY currentBaseMapChanged)
    Q_PROPERTY(bool hillshadeVisible READ isHillshadeVisible WRITE setHillshadeVisible NOTIFY hillshadeVisibleChanged)
    Q_PROPERTY(bool citiesVisible READ areCitiesVisible WRITE setCitiesVisible NOTIFY citiesVisibleChanged)
    Q_PROPERTY(bool bordersVisible READ areBordersVisible WRITE setBordersVisible NOTIFY bordersVisibleChanged)
    Q_PROPERTY(bool gridVisible READ isGridVisible WRITE setGridVisible NOTIFY gridVisibleChanged)

public:
    explicit MapControlsModel(QObject* parent = nullptr);

    /// Binds the projection to @p camera (nullptr unbinds). The model takes the camera's mode.
    void bindCamera(Camera* camera);
    Camera* camera() const { return m_camera; }

    bool isOrthographic() const { return m_orthographic; }
    void setOrthographic(bool orthographic);

    /// Map bearing, clockwise from north, normalised to [0, 360). The 2D map does not rotate
    /// yet, so this stays 0 until map rotation lands; the compass already follows it.
    double bearingDegrees() const { return m_bearingDegrees; }
    void setBearingDegrees(double degrees);

    /// The 3D globe button is shown but disabled until a real 3D view is decided on.
    bool isGlobe3DAvailable() const { return m_globe3DAvailable; }
    void setGlobe3DAvailable(bool available);

    /// Enables the ruler button (MapControlsOverlay turns it on; off by default).
    bool isMeasureAvailable() const { return m_measureAvailable; }
    void setMeasureAvailable(bool available);

    /// Settings, layers and measure panels: opening one closes the others.
    bool isSettingsOpen() const { return m_settingsOpen; }
    void setSettingsOpen(bool open);
    bool isLayersOpen() const { return m_layersOpen; }
    void setLayersOpen(bool open);
    /// Measure panel open = measuring: map clicks add points. A map click does not close it.
    bool isMeasureOpen() const { return m_measureOpen; }
    void setMeasureOpen(bool open);
    /// Closes Settings and Layers (what a click on the map does); measuring stays on.
    Q_INVOKABLE void closePanels();

    /// Base map tiles, one per map source in the same order as the host's Map Source list:
    /// each entry is a map with "label" and "icon" (see BaseMapSources::entries()).
    QVariantList baseMaps() const { return m_baseMaps; }
    void setBaseMaps(const QVariantList& baseMaps);
    /// Index into baseMaps(). The host owns the map sources: it listens to
    /// currentBaseMapChanged and loads that source. Out-of-range indexes are ignored.
    int currentBaseMap() const { return m_currentBaseMap; }
    void setCurrentBaseMap(int index);

    /// Layer toggles; MapControlsOverlay applies them to its MapWidget.
    bool isHillshadeVisible() const { return m_hillshadeVisible; }
    void setHillshadeVisible(bool visible);
    bool areCitiesVisible() const { return m_citiesVisible; }
    void setCitiesVisible(bool visible);
    bool areBordersVisible() const { return m_bordersVisible; }
    void setBordersVisible(bool visible);
    bool isGridVisible() const { return m_gridVisible; }
    void setGridVisible(bool visible);

    /// Compass click: north up (bearing 0) and closes any open panel.
    Q_INVOKABLE void resetNorth();
    Q_INVOKABLE void toggleSettings() { setSettingsOpen(!m_settingsOpen); }
    Q_INVOKABLE void toggleLayers() { setLayersOpen(!m_layersOpen); }
    Q_INVOKABLE void toggleMeasure() { setMeasureOpen(!m_measureOpen); }

    /// QML items cannot show a tooltip outside the small overlay, so they ask the host widget.
    Q_INVOKABLE void showToolTip(const QString& text, double globalX, double globalY);
    Q_INVOKABLE void hideToolTip();

    static double normalizeBearing(double degrees);

signals:
    void orthographicChanged(bool orthographic);
    void bearingDegreesChanged(double degrees);
    void globe3DAvailableChanged(bool available);
    void measureAvailableChanged(bool available);
    void settingsOpenChanged(bool open);
    void layersOpenChanged(bool open);
    void measureOpenChanged(bool open);
    void baseMapsChanged();
    void currentBaseMapChanged(int index);
    void hillshadeVisibleChanged(bool visible);
    void citiesVisibleChanged(bool visible);
    void bordersVisibleChanged(bool visible);
    void gridVisibleChanged(bool visible);
    void northResetRequested();
    void toolTipRequested(const QString& text, const QPointF& globalPos);
    void toolTipHideRequested();

private:
    Camera* m_camera;
    QMetaObject::Connection m_cameraConnection;
    QMetaObject::Connection m_cameraDestroyedConnection;
    bool m_orthographic;
    double m_bearingDegrees;
    bool m_globe3DAvailable;
    bool m_measureAvailable;
    bool m_settingsOpen;
    bool m_layersOpen;
    bool m_measureOpen;
    QVariantList m_baseMaps;
    int m_currentBaseMap;
    bool m_hillshadeVisible;
    bool m_citiesVisible;
    bool m_bordersVisible;
    bool m_gridVisible;
};

#endif // MAPCONTROLSMODEL_H
