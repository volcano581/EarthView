#pragma once
#ifndef EARTHVIEW3D_GLOBEVIEW3D_H
#define EARTHVIEW3D_GLOBEVIEW3D_H

#include "earthview3d/EntityState3D.h"
#include "earthview3d/MapConfig.h"

#include <QOpenGLWidget>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace earthview3d {

/**
 * @brief osgEarth globe hosted in a QOpenGLWidget: terrain, imagery, sky, entities, cameras.
 *
 * Host-independent (OSGEARTH_INTEGRATION.md §3.1): the host converts its simulation snapshot
 * into EntityState3D records and calls setEntities() from the pre-frame callback (see
 * DOCTRINE_WIRING.md). This header includes no osgEarth/OSG headers (D-011); everything
 * osgEarth lives in the private implementation.
 *
 * The widget sets its own compatibility-profile surface format, so hosts may keep a core
 * profile as their default. GUI thread only.
 *
 * Diagnostics (environment): EARTHVIEW3D_NO_SKY=1 disables the sky,
 * EARTHVIEW3D_LOGDEPTH=1 installs osgEarth's logarithmic depth buffer (off by default, D-010).
 */
class GlobeView3D : public QOpenGLWidget
{
    Q_OBJECT

public:
    /// @param homeLatDeg, homeLonDeg Home viewpoint focal point (e.g. the scenario origin).
    GlobeView3D(const MapConfig& config, double homeLatDeg, double homeLonDeg, QWidget* parent = nullptr);
    ~GlobeView3D() override;

    const MapConfig& mapConfig() const;

    /// Called on the GUI thread right before each frame; the owner pushes entities here.
    void setPreFrameCallback(std::function<void()> callback);

    /// Add/update/remove entity visuals so the scene matches @p entities (diff by id).
    void setEntities(const std::vector<EntityState3D>& entities);
    /// Number of entity visuals currently in the scene.
    int entityCount() const;

    void goHome();
    void selectNextEntity();
    void chaseSelected();     ///< Tethered camera behind the selected entity
    void stealthSelected();   ///< Close, near eye level
    void untether();

    std::uint64_t selectedId() const;
    QString selectedLabel() const;
    QString viewpointDescription() const;
    bool saveSnapshot(const QString& pngPath);

signals:
    void selectionChanged(QString label);
    void loadFailed(QString reason);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace earthview3d

#endif // EARTHVIEW3D_GLOBEVIEW3D_H
