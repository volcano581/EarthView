#include "MapControlsModel.h"
#include "Camera.h"
#include "MapFeatures.h"
#include "MapTheme.h"

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
    , m_layersOpen(false)
    , m_measureOpen(false)
    , m_currentBaseMap(-1)
    , m_hillshadeVisible(true)
    , m_citiesVisible(true)
    , m_bordersVisible(true)
    , m_gridVisible(true)
    , m_currentTheme(QStringLiteral("standard"))
    , m_thumbnailRevision(0)
    , m_panelMaxHeight(10000)
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
    if (open) {
        setLayersOpen(false);
        setMeasureOpen(false);
    }
    if (m_settingsOpen == open)
        return;
    m_settingsOpen = open;
    emit settingsOpenChanged(m_settingsOpen);
}

void MapControlsModel::setLayersOpen(bool open)
{
    if (open) {
        setSettingsOpen(false);
        setMeasureOpen(false);
    }
    if (m_layersOpen == open)
        return;
    m_layersOpen = open;
    emit layersOpenChanged(m_layersOpen);
}

void MapControlsModel::closePanels()
{
    setSettingsOpen(false);
    setLayersOpen(false);
}

void MapControlsModel::setMeasureOpen(bool open)
{
    if (open && !m_measureAvailable)
        return;
    if (open) {
        setSettingsOpen(false);
        setLayersOpen(false);
    }
    if (m_measureOpen == open)
        return;
    m_measureOpen = open;
    emit measureOpenChanged(m_measureOpen);
}

void MapControlsModel::setBaseMaps(const QVariantList& baseMaps)
{
    m_baseMaps = baseMaps;
    emit baseMapsChanged();
    if (m_currentBaseMap >= m_baseMaps.size()) {
        m_currentBaseMap = m_baseMaps.isEmpty() ? -1 : 0;
        emit currentBaseMapChanged(m_currentBaseMap);
    }
}

void MapControlsModel::setCurrentBaseMap(int index)
{
    if (index < 0 || index >= m_baseMaps.size())
        return;
    if (m_currentBaseMap == index)
        return;
    m_currentBaseMap = index;
    emit currentBaseMapChanged(m_currentBaseMap);
}

void MapControlsModel::setHillshadeVisible(bool visible)
{
    if (m_hillshadeVisible == visible)
        return;
    m_hillshadeVisible = visible;
    emit hillshadeVisibleChanged(m_hillshadeVisible);
}

void MapControlsModel::setCitiesVisible(bool visible)
{
    if (m_citiesVisible == visible)
        return;
    m_citiesVisible = visible;
    emit citiesVisibleChanged(m_citiesVisible);
}

void MapControlsModel::setBordersVisible(bool visible)
{
    if (m_bordersVisible == visible)
        return;
    m_bordersVisible = visible;
    emit bordersVisibleChanged(m_bordersVisible);
}

void MapControlsModel::setGridVisible(bool visible)
{
    if (m_gridVisible == visible)
        return;
    m_gridVisible = visible;
    emit gridVisibleChanged(m_gridVisible);
}

void MapControlsModel::setFeatureLayerIds(const QStringList& ids)
{
    QVariantList layers;
    for (const QString& id : MapFeatures::sortedLayers(ids)) {
        // "Points of interest" does not fit under a tile.
        const QString label = id == QLatin1String("poi") || id == QLatin1String("poi_label")
            ? QStringLiteral("POIs") : MapFeatures::layerLabel(id);
        layers.append(QVariantMap{ { "id", id }, { "label", label } });
    }
    if (layers == m_featureLayers)
        return;
    m_featureLayers = layers;
    emit featureLayersChanged();
}

void MapControlsModel::setHiddenFeatureLayers(const QStringList& layers)
{
    QStringList sorted = layers;
    sorted.removeDuplicates();
    sorted.sort();
    if (sorted == m_hiddenFeatureLayers)
        return;
    m_hiddenFeatureLayers = sorted;
    emit hiddenFeatureLayersChanged(m_hiddenFeatureLayers);
}

void MapControlsModel::toggleFeatureLayer(const QString& id)
{
    QStringList layers = m_hiddenFeatureLayers;
    if (!layers.removeAll(id))
        layers.append(id);
    setHiddenFeatureLayers(layers);
}

QVariantList MapControlsModel::themes()
{
    QVariantList list;
    for (const QString& id : MapTheme::ids())
        list.append(QVariantMap{ { "id", id }, { "label", MapTheme::displayName(id) } });
    return list;
}

void MapControlsModel::setCurrentTheme(const QString& id)
{
    if (!MapTheme::ids().contains(id) || id == m_currentTheme)
        return;
    m_currentTheme = id;
    emit currentThemeChanged(m_currentTheme);
}

void MapControlsModel::bumpThumbnailRevision()
{
    ++m_thumbnailRevision;
    emit thumbnailRevisionChanged();
}

void MapControlsModel::setPanelMaxHeight(int height)
{
    height = qMax(120, height);
    if (height == m_panelMaxHeight)
        return;
    m_panelMaxHeight = height;
    emit panelMaxHeightChanged();
}

void MapControlsModel::setSelectedFeature(const QVariantMap& feature)
{
    if (m_selectedFeature == feature)
        return;
    m_selectedFeature = feature;
    emit selectedFeatureChanged();
}

void MapControlsModel::resetNorth()
{
    closePanels();
    setMeasureOpen(false);
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
