#include "MapControlsModel.h"
#include "Camera.h"

#include <QtMath>
#include <cmath>

MapControlsModel::MapControlsModel(QObject* parent)
    : QObject(parent)
    , m_camera(nullptr)
    , m_orthographic(false)
    , m_bearingDegrees(0.0)
    , m_globe3DAvailable(false)
    , m_measureAvailable(false)
    , m_settingsOpen(false)
{
}

void MapControlsModel::bindCamera(Camera* camera)
{
    if (m_camera == camera)
        return;

    QObject::disconnect(m_cameraConnection);
    QObject::disconnect(m_cameraDestroyedConnection);
    m_camera = camera;
    if (!m_camera)
        return;

    m_cameraDestroyedConnection = connect(m_camera, &QObject::destroyed, this, [this]() {
        QObject::disconnect(m_cameraConnection);
        m_camera = nullptr;
    });

    m_cameraConnection = connect(m_camera, &Camera::projectionModeChanged, this,
        [this](Camera::ProjectionMode mode) {
            const bool orthographic = mode == Camera::ProjectionMode::Orthographic;
            if (m_orthographic == orthographic)
                return;
            m_orthographic = orthographic;
            emit orthographicChanged(m_orthographic);
        });

    const bool orthographic = m_camera->isOrthographic();
    if (m_orthographic != orthographic) {
        m_orthographic = orthographic;
        emit orthographicChanged(m_orthographic);
    }
}

void MapControlsModel::setOrthographic(bool orthographic)
{
    if (m_camera) {
        // The camera's projectionModeChanged signal updates m_orthographic.
        m_camera->setProjectionMode(
            orthographic ? Camera::ProjectionMode::Orthographic : Camera::ProjectionMode::Mercator);
        return;
    }
    if (m_orthographic == orthographic)
        return;
    m_orthographic = orthographic;
    emit orthographicChanged(m_orthographic);
}

void MapControlsModel::setBearingDegrees(double degrees)
{
    const double normalized = normalizeBearing(degrees);
    if (qFuzzyCompare(m_bearingDegrees + 1.0, normalized + 1.0))
        return;
    m_bearingDegrees = normalized;
    emit bearingDegreesChanged(m_bearingDegrees);
}

void MapControlsModel::setGlobe3DAvailable(bool available)
{
    if (m_globe3DAvailable == available)
        return;
    m_globe3DAvailable = available;
    emit globe3DAvailableChanged(m_globe3DAvailable);
}

void MapControlsModel::setMeasureAvailable(bool available)
{
    if (m_measureAvailable == available)
        return;
    m_measureAvailable = available;
    emit measureAvailableChanged(m_measureAvailable);
}

void MapControlsModel::setSettingsOpen(bool open)
{
    if (m_settingsOpen == open)
        return;
    m_settingsOpen = open;
    emit settingsOpenChanged(m_settingsOpen);
}

void MapControlsModel::resetNorth()
{
    setSettingsOpen(false);
    setBearingDegrees(0.0);
    emit northResetRequested();
}

void MapControlsModel::showToolTip(const QString& text, double globalX, double globalY)
{
    emit toolTipRequested(text, QPointF(globalX, globalY));
}

void MapControlsModel::hideToolTip()
{
    emit toolTipHideRequested();
}

double MapControlsModel::normalizeBearing(double degrees)
{
    if (!qIsFinite(degrees))
        return 0.0;
    double normalized = std::fmod(degrees, 360.0);
    if (normalized < 0.0)
        normalized += 360.0;
    if (normalized >= 360.0)
        normalized = 0.0;
    return normalized;
}
