#pragma once
#ifndef MAPCONTROLSMODEL_H
#define MAPCONTROLSMODEL_H

#include <QObject>
#include <QPointF>
#include <QString>

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

    /// The measure tool lands in the map-controls PR 3; until then its button is disabled.
    bool isMeasureAvailable() const { return m_measureAvailable; }
    void setMeasureAvailable(bool available);

    bool isSettingsOpen() const { return m_settingsOpen; }
    void setSettingsOpen(bool open);

    /// Compass click: north up (bearing 0) and closes any open panel.
    Q_INVOKABLE void resetNorth();
    Q_INVOKABLE void toggleSettings() { setSettingsOpen(!m_settingsOpen); }

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
};

#endif // MAPCONTROLSMODEL_H
