#pragma once
#ifndef EARTHVIEW3D_DEMO_DEMOWINDOW_H
#define EARTHVIEW3D_DEMO_DEMOWINDOW_H

#include "SyntheticFeed.h"

#include <earthview3d/MapConfig.h>

#include <QMainWindow>
#include <QPointer>
#include <QWidget>

class MapWidget;

namespace earthview3d {
class GlobeView3D;
}

namespace earthview3d::demo {

/**
 * @brief Transparent overlay that draws the feed's entities on top of the 2D MapWidget.
 *
 * Uses only MapWidget's public Camera API (projectMercatorToScreen), so 2D code is unchanged.
 */
class EntityOverlay2D : public QWidget
{
    Q_OBJECT

public:
    EntityOverlay2D(MapWidget* map, const SnapshotBuffer* buffer, QWidget* parent);

    /// The snapshot last drawn (for the 2D/3D agreement check).
    const std::vector<EntityState3D>& entities() const { return m_entities; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    MapWidget* m_map;
    const SnapshotBuffer* m_buffer;
    std::uint64_t m_lastTick = 0;
    std::vector<EntityState3D> m_entities;
};

/**
 * @brief Demo host main window: 2D map + "Open 3D View", both fed from one SnapshotBuffer.
 */
class DemoWindow : public QMainWindow
{
    Q_OBJECT

public:
    DemoWindow(const MapConfig& mapConfig, const SyntheticFeed::Settings& feedSettings,
               const SnapshotBuffer* buffer, QWidget* parent = nullptr);

    MapWidget* mapWidget() const { return m_map; }
    /// The open 3D view, or nullptr.
    GlobeView3D* view3D() const;

    /// Open (or raise) the 3D window.
    GlobeView3D* open3DView();
    /// 2D map including the entity overlay, as an image.
    QImage grab2D() const;
    /// Compare where the 2D overlay drew each entity with where the 3D view placed it.
    AgreementReport checkAgreement() const;

private:
    MapConfig m_mapConfig;
    SyntheticFeed::Settings m_feedSettings;
    const SnapshotBuffer* m_buffer;
    QWidget* m_mapContainer = nullptr;
    MapWidget* m_map = nullptr;
    EntityOverlay2D* m_overlay = nullptr;
    QPointer<QMainWindow> m_window3D;
    QPointer<GlobeView3D> m_view3D;
};

} // namespace earthview3d::demo

#endif // EARTHVIEW3D_DEMO_DEMOWINDOW_H
